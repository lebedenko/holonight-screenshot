#include "capture_session.h"

#include "wayland-ext-foreign-toplevel-list-v1-client-protocol.h"
#include "wayland_display_p.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QtGui/qguiapplication_platform.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <map>
#include <memory>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>
#include <wayland-client.h>

namespace screenshot {
namespace {

constexpr auto toplevelSource = "ext_foreign_toplevel_image_capture_source_manager_v1";
constexpr auto copyManager = "ext_image_copy_capture_manager_v1";

wl_display* qtDisplay() {
  auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
  auto* native = app ? app->nativeInterface<QNativeInterface::QWaylandApplication>() : nullptr;
  return native ? native->display() : nullptr;
}

struct HelperCapabilities {
  bool grim = false;
  bool grimToplevel = false;
  bool hyprctl = false;
  bool swaymsg = false;
  bool wlrRandr = false;
};

QByteArray readFile(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool privateDirectory(const QByteArray& path) {
  struct stat info{};
  return lstat(path.constData(), &info) == 0 && S_ISDIR(info.st_mode) && info.st_uid == geteuid() &&
         (info.st_mode & 0777) == 0700;
}

QString helperCachePath(int fd) {
  const auto runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
  if (fd < 0 || !QDir::isAbsolutePath(runtime) || !privateDirectory(QFile::encodeName(QDir::cleanPath(runtime)))) {
    return {};
  }

  sockaddr_un socketAddress{};
  socklen_t addressLength = sizeof(socketAddress);
  ucred peer{};
  socklen_t peerLength = sizeof(peer);
  if (getpeername(fd, reinterpret_cast<sockaddr*>(&socketAddress), &addressLength) != 0 ||
      socketAddress.sun_family != AF_UNIX || socketAddress.sun_path[0] != '/' ||
      getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &peerLength) != 0 || peerLength != sizeof(peer) || peer.pid <= 0) {
    return {};
  }

  struct stat socketInfo{};
  const QByteArray socketPath(socketAddress.sun_path, strnlen(socketAddress.sun_path, sizeof(socketAddress.sun_path)));
  if (stat(socketPath.constData(), &socketInfo) != 0 || !S_ISSOCK(socketInfo.st_mode)) {
    return {};
  }

  const auto boot = readFile("/proc/sys/kernel/random/boot_id").trimmed();
  const auto process = readFile(QStringLiteral("/proc/%1/stat").arg(peer.pid));
  const auto endOfName = process.lastIndexOf(')');
  // After the command name, field 3 (process state) starts the remaining fields.
  const auto fields = endOfName < 0 ? QList<QByteArray>{} : process.mid(endOfName + 2).split(' ');
  const auto startTime = fields.value(19);
  if (boot.isEmpty() || startTime.isEmpty()) {
    return {};
  }

  const auto searchPath = qEnvironmentVariable("PATH");
  QJsonArray key{1,
                 QString::fromLatin1(boot),
                 qEnvironmentVariable("XDG_SESSION_ID"),
                 QFile::decodeName(socketPath),
                 QString::number(socketInfo.st_dev),
                 QString::number(socketInfo.st_ino),
                 QString::number(peer.pid),
                 QString::fromLatin1(startTime),
                 searchPath};
  // Relative entries are interpreted in the launcher's working directory.
  for (const auto& entry : searchPath.split(u':')) {
    if (QDir::isRelativePath(entry)) {
      key.append(QDir::currentPath());
      break;
    }
  }
  const auto hash =
      QCryptographicHash::hash(QJsonDocument(key).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex();
  const auto directory = QDir(runtime).filePath("holonight-screenshot");
  const auto encodedDirectory = QFile::encodeName(directory);
  if ((mkdir(encodedDirectory.constData(), 0700) != 0 && errno != EEXIST) || !privateDirectory(encodedDirectory)) {
    return {};
  }
  return QDir(directory).filePath(QStringLiteral("helpers-%1.json").arg(QString::fromLatin1(hash)));
}

class HelperDiscovery final : public QObject {
 public:
  HelperDiscovery(int displayFd, std::function<void(HelperCapabilities)> done)
      : displayFd_(displayFd), done_(std::move(done)) {}

  ~HelperDiscovery() override {
    if (probe_.state() != QProcess::NotRunning) {
      probe_.disconnect(this);
      probe_.kill();
      probe_.waitForFinished(-1);
    }
  }

  void start() {
    if (QThread::currentThread()->isInterruptionRequested()) {
      return;
    }
    cachePath_ = helperCachePath(displayFd_);
    if (!cachePath_.isEmpty()) {
      lock_ = std::make_unique<QLockFile>(cachePath_ + ".lock");
      if (!lock_->tryLock(2300)) {
        cachePath_.clear();
        lock_.reset();
      } else if (loadCache()) {
        finish();
        return;
      }
    }
    const auto grim = QStandardPaths::findExecutable(QStringLiteral("grim"));
    helpers_.grim = !grim.isEmpty();
    helpers_.hyprctl = installed(QStringLiteral("hyprctl"));
    helpers_.swaymsg = installed(QStringLiteral("swaymsg"));
    helpers_.wlrRandr = installed(QStringLiteral("wlr-randr"));
    if (!helpers_.grim) {
      saveCache();
      finish();
      return;
    }

    probe_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&probe_, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
      static const QRegularExpression option(QStringLiteral(R"((?:^|\n)\s*-T(?:\s|,|$))"));
      helpers_.grimToplevel =
          status == QProcess::NormalExit && option.match(QString::fromLocal8Bit(probe_.readAll())).hasMatch();
      if (status == QProcess::NormalExit && !timedOut_ && exitCode == 0) {
        saveCache();
      }
      finish();
    });
    connect(&probe_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
      if (error == QProcess::FailedToStart) {
        finish();
      }
    });
    QTimer::singleShot(2000, Qt::PreciseTimer, this, [this] {
      timedOut_ = true;
      probe_.kill();
    });
    probe_.start(grim, {QStringLiteral("-h")}, QIODevice::ReadOnly);
  }

 private:
  static bool installed(const QString& helper) { return !QStandardPaths::findExecutable(helper).isEmpty(); }

  bool loadCache() {
    const auto data = QJsonDocument::fromJson(readFile(cachePath_)).array();
    if (data.size() != 5 || !std::ranges::all_of(data, [](const auto& value) { return value.isBool(); }) ||
        (data.at(1).toBool() && !data.at(0).toBool())) {
      return false;
    }
    helpers_ = {data.at(0).toBool(), data.at(1).toBool(), data.at(2).toBool(), data.at(3).toBool(),
                data.at(4).toBool()};
    return true;
  }

  void saveCache() const {
    if (cachePath_.isEmpty()) {
      return;
    }
    QSaveFile file(cachePath_);
    const auto data = QJsonDocument(QJsonArray{helpers_.grim, helpers_.grimToplevel, helpers_.hyprctl, helpers_.swaymsg,
                                               helpers_.wlrRandr})
                          .toJson(QJsonDocument::Compact);
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size()) {
      file.commit();
    }
  }

  void finish() {
    lock_.reset();
    if (!QThread::currentThread()->isInterruptionRequested()) {
      done_(helpers_);
    }
    QThread::currentThread()->quit();
  }

  int displayFd_;
  QString cachePath_;
  std::unique_ptr<QLockFile> lock_;
  bool timedOut_ = false;
  std::function<void(HelperCapabilities)> done_;
  HelperCapabilities helpers_;
  QProcess probe_{this};
};

class DisplaySync final : public QObject {
 public:
  DisplaySync(wl_display* display, QObject* parent, std::function<void(bool)> done)
      : QObject(parent), done_(std::move(done)), callback_(wl_display_sync(display)) {
    static const wl_callback_listener listener{
        [](void* data, wl_callback*, uint32_t) { static_cast<DisplaySync*>(data)->finish(true); }};
    if (callback_) {
      wl_callback_add_listener(callback_, &listener, this);
      QTimer::singleShot(2000, this, [this] { finish(false); });
    } else {
      QTimer::singleShot(0, this, [this] { finish(false); });
    }
  }

  ~DisplaySync() override {
    if (callback_) {
      wl_callback_destroy(callback_);
    }
  }

 private:
  void finish(bool ok) {
    if (!done_) {
      return;
    }
    if (callback_) {
      wl_callback_destroy(callback_);
      callback_ = nullptr;
    }
    deleteLater();
    std::exchange(done_, {})(ok);
  }

  std::function<void(bool)> done_;
  wl_callback* callback_;
};

class LayerShellMonitor final : public QObject {
 public:
  LayerShellMonitor(wl_display* display, QObject* context, std::function<void(bool)> discovered,
                    std::function<void()> removed)
      : QObject(context), registry_(wl_display_get_registry(display)), removed_(std::move(removed)) {
    static const wl_registry_listener listener{
        .global =
            [](void* data, wl_registry*, uint32_t name, const char* interface, uint32_t) {
              if (QByteArrayView(interface) == "zwlr_layer_shell_v1") {
                static_cast<LayerShellMonitor*>(data)->globals_.insert(name);
              }
            },
        .global_remove =
            [](void* data, wl_registry*, uint32_t name) {
              auto& self = *static_cast<LayerShellMonitor*>(data);
              const bool removed = self.globals_.remove(name);
              if (removed && self.ready_) {
                const auto notify = self.removed_;
                notify();
              }
            }};
    if (!registry_) {
      QTimer::singleShot(0, this, [discovered = std::move(discovered)] { discovered(false); });
      return;
    }
    wl_registry_add_listener(registry_, &listener, this);
    new DisplaySync(display, this, [this, discovered = std::move(discovered)](bool synced) {
      ready_ = synced && !globals_.isEmpty();
      discovered(ready_);
    });
    wl_display_flush(display);
  }

  ~LayerShellMonitor() override {
    if (registry_) {
      wl_registry_destroy(registry_);
    }
  }

 private:
  wl_registry* registry_;
  std::function<void()> removed_;
  QSet<uint32_t> globals_;
  bool ready_ = false;
};

}  // namespace

void watchLayerShell(QObject* context, std::function<void(bool)> discovered, std::function<void()> removed) {
  if (auto* display = qtDisplay()) {
    new LayerShellMonitor(display, context, std::move(discovered), std::move(removed));
  } else {
    QTimer::singleShot(0, context, [discovered = std::move(discovered)] { discovered(false); });
  }
}

QString targetName(CaptureTarget target) {
  switch (target) {
    case CaptureTarget::Screen:
      return QStringLiteral("screen");
    case CaptureTarget::Area:
      return QStringLiteral("area");
    case CaptureTarget::Window:
      return QStringLiteral("window");
    case CaptureTarget::ActiveWindow:
      return QStringLiteral("active-window");
  }
  Q_UNREACHABLE();
}

struct CaptureSession::State {
  explicit State(CaptureSession& owner) : owner(owner) {
    capabilities.unavailable.fill(QStringLiteral("Checking availability…"));
  }
  ~State() {
    helperThread.requestInterruption();
    helperThread.quit();
    helperThread.wait();
    delete read;
    delete write;
    delete syncContext;
    clearWindows();
    if (list) {
      ext_foreign_toplevel_list_v1_destroy(list);
    }
    if (registry) {
      wl_registry_destroy(registry);
    }
    if (ownsDisplay && display) {
      wl_display_disconnect(display);
    }
  }

  struct Window {
    State& state;
    CaptureWindow pending;
    CaptureWindow published;
  };

  void clearWindows() {
    for (const auto& [handle, window] : toplevels) {
      ext_foreign_toplevel_handle_v1_destroy(handle);
    }
    toplevels.clear();
  }

  bool has(const QByteArray& name) const { return std::ranges::find(globals, name) != globals.end(); }

  void publish() {
    if (!registryReady || !helperReady || published) {
      return;
    }
    published = true;
    const auto desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(':');
    const auto desktopIs = [&](const QString& name) { return desktops.contains(name, Qt::CaseInsensitive); };
    const bool labwc = desktopIs("labwc");
    const bool swayDesktop = desktopIs("sway");
    const bool hypr = !labwc && (desktopIs("Hyprland") ||
                                 (!swayDesktop && !qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")));
    const bool sway = swayDesktop || !qEnvironmentVariableIsEmpty("SWAYSOCK");
    capabilities.wlrLayout = !hypr && (has("zwlr_output_manager_v1") || sway || labwc);

    if (hypr && helpers.hyprctl) {
      capabilities.focus = FocusLookup::Hyprland;
    } else if (!hypr && !labwc && sway && helpers.swaymsg) {
      capabilities.focus = FocusLookup::Sway;
    }

    QString screen;
    QString window;
    if (!connectionError.isEmpty()) {
      screen = window = connectionError;
    } else if (!helpers.grim) {
      screen = window = QStringLiteral("grim is required.");
    } else {
      if (!has("zwlr_screencopy_manager_v1") &&
          !(has(copyManager) && has("ext_output_image_capture_source_manager_v1"))) {
        screen = QStringLiteral("The compositor does not provide output capture.");
      } else if (capabilities.wlrLayout && !has("zwlr_output_manager_v1")) {
        screen = QStringLiteral("The compositor does not provide output layout.");
      } else if (!(capabilities.wlrLayout ? helpers.wlrRandr : helpers.hyprctl)) {
        screen =
            capabilities.wlrLayout ? QStringLiteral("wlr-randr is required.") : QStringLiteral("hyprctl is required.");
      }
      if (!list || !has(toplevelSource) || !has(copyManager)) {
        window = QStringLiteral("The compositor does not provide window capture.");
      } else if (!helpers.grimToplevel) {
        window = QStringLiteral("grim with -T support is required.");
      }
    }

    const auto area = screen.isEmpty() && !has("zwlr_layer_shell_v1")
                          ? QStringLiteral("The compositor does not provide the region overlay protocol.")
                          : screen;
    const auto active = window.isEmpty() && capabilities.focus == FocusLookup::None
                            ? QStringLiteral("Active window lookup is unavailable in this session.")
                            : window;
    capabilities.unavailable = {screen, area, window, active};
    emit owner.discovered();
  }

  void flush() {
    if (wl_display_flush(display) >= 0) {
      if (write) {
        write->setEnabled(false);
      }
    } else if (write && errno == EAGAIN) {
      write->setEnabled(true);
    }
  }

  void connectionFailed(const QString& error) {
    if (read) {
      read->setEnabled(false);
      write->setEnabled(false);
    }
    connectionError = error;
    registryReady = true;
    clearWindows();
    emit owner.windowsChanged();
    publish();
  }

  void bind(uint32_t name) {
    list = static_cast<ext_foreign_toplevel_list_v1*>(
        wl_registry_bind(registry, name, &ext_foreign_toplevel_list_v1_interface, 1));
    static const ext_foreign_toplevel_list_v1_listener listener{
        .toplevel = [](void* data, ext_foreign_toplevel_list_v1*,
                       ext_foreign_toplevel_handle_v1* handle) { static_cast<State*>(data)->addWindow(handle); },
        .finished =
            [](void* data, ext_foreign_toplevel_list_v1*) {
              auto& self = *static_cast<State*>(data);
              self.clearWindows();
              ext_foreign_toplevel_list_v1_destroy(std::exchange(self.list, nullptr));
              emit self.owner.windowsChanged();
            }};
    ext_foreign_toplevel_list_v1_add_listener(list, &listener, this);
  }

  void addWindow(ext_foreign_toplevel_handle_v1* handle) {
    auto& window = toplevels.emplace(handle, Window{*this, {}, {}}).first->second;
    static const ext_foreign_toplevel_handle_v1_listener listener{
        .closed =
            [](void* data, ext_foreign_toplevel_handle_v1* handle) {
              auto& state = static_cast<Window*>(data)->state;
              ext_foreign_toplevel_handle_v1_destroy(handle);
              state.toplevels.erase(handle);
              emit state.owner.windowsChanged();
            },
        .done =
            [](void* data, ext_foreign_toplevel_handle_v1*) {
              auto& window = *static_cast<Window*>(data);
              window.published = window.pending;
              emit window.state.owner.windowsChanged();
            },
        .title = [](void* data, ext_foreign_toplevel_handle_v1*,
                    const char* title) { static_cast<Window*>(data)->pending.title = QString::fromUtf8(title); },
        .app_id = [](void* data, ext_foreign_toplevel_handle_v1*,
                     const char* id) { static_cast<Window*>(data)->pending.appId = QString::fromUtf8(id); },
        .identifier = [](void* data, ext_foreign_toplevel_handle_v1*,
                         const char* id) { static_cast<Window*>(data)->pending.id = QString::fromUtf8(id); }};
    ext_foreign_toplevel_handle_v1_add_listener(handle, &listener, &window);
  }

  void discoverHelpers() {
    auto* worker = new HelperDiscovery(display ? wl_display_get_fd(display) : -1, [this](HelperCapabilities result) {
      QMetaObject::invokeMethod(
          &owner,
          [this, result] {
            helpers = result;
            helperReady = true;
            publish();
          },
          Qt::QueuedConnection);
    });
    worker->moveToThread(&helperThread);
    QObject::connect(&helperThread, &QThread::started, worker, &HelperDiscovery::start, Qt::QueuedConnection);
    QObject::connect(&helperThread, &QThread::finished, worker, &QObject::deleteLater);
    helperThread.start();
  }

  void discover() {
    if (started) {
      return;
    }
    started = true;
    display = qtDisplay();
    if (!display) {
      ownsDisplay = true;
      display = wl_display_connect(nullptr);
      if (!display) {
        discoverHelpers();
        connectionFailed(QStringLiteral("Could not connect to Wayland."));
        return;
      }
      const int fd = wl_display_get_fd(display);
      read = new QSocketNotifier(fd, QSocketNotifier::Read, &owner);
      write = new QSocketNotifier(fd, QSocketNotifier::Write, &owner);
      write->setEnabled(false);
      QObject::connect(read, &QSocketNotifier::activated, &owner, [this] {
        if (wl_display_dispatch(display) < 0) {
          connectionFailed(QStringLiteral("Wayland connection was lost."));
        } else {
          flush();
        }
      });
      QObject::connect(write, &QSocketNotifier::activated, &owner, [this] { flush(); });
    }

    discoverHelpers();
    registry = wl_display_get_registry(display);
    static const wl_registry_listener listener{
        .global =
            [](void* data, wl_registry*, uint32_t name, const char* interface, uint32_t) {
              auto& self = *static_cast<State*>(data);
              self.globals.insert(name, QByteArray(interface));
              if (!self.list && self.globals.value(name) == ext_foreign_toplevel_list_v1_interface.name) {
                self.bind(name);
              }
            },
        .global_remove = [](void* data, wl_registry*,
                            uint32_t name) { static_cast<State*>(data)->globals.remove(name); }};
    wl_registry_add_listener(registry, &listener, this);
    syncContext = new QObject(&owner);
    new DisplaySync(display, syncContext, [this](bool ok) {
      if (!ok) {
        connectionFailed(QStringLiteral("Wayland capability discovery timed out."));
        return;
      }
      // The registry listener binds the window list. Wait for its initial handles too.
      new DisplaySync(display, syncContext, [this](bool listed) {
        if (!listed) {
          connectionFailed(QStringLiteral("Wayland window discovery timed out."));
          return;
        }
        registryReady = true;
        publish();
      });
      flush();
    });
    flush();
  }

  CaptureSession& owner;
  CaptureCapabilities capabilities;
  QHash<uint32_t, QByteArray> globals;
  std::map<ext_foreign_toplevel_handle_v1*, Window> toplevels;
  wl_display* display = nullptr;
  wl_registry* registry = nullptr;
  ext_foreign_toplevel_list_v1* list = nullptr;
  QSocketNotifier* read = nullptr;
  QSocketNotifier* write = nullptr;
  QObject* syncContext = nullptr;
  QThread helperThread;
  HelperCapabilities helpers;
  QString connectionError;
  bool ownsDisplay = false;
  bool started = false;
  bool registryReady = false;
  bool helperReady = false;
  bool published = false;
};

CaptureSession::CaptureSession(QObject* parent) : QObject(parent), state_(std::make_unique<State>(*this)) {}
CaptureSession::~CaptureSession() = default;

void CaptureSession::discover() {
  QTimer::singleShot(0, this, [this] { state_->discover(); });
}

const CaptureCapabilities& CaptureSession::capabilities() const { return state_->capabilities; }

QList<CaptureWindow> CaptureSession::windows() const {
  QList<CaptureWindow> windows;
  for (const auto& [handle, window] : state_->toplevels) {
    if (!window.published.id.isEmpty()) {
      windows.append(window.published);
    }
  }
  std::ranges::sort(windows, {}, &CaptureWindow::id);
  return windows;
}

bool CaptureSession::containsWindow(const QString& id) const {
  return !id.isEmpty() &&
         std::ranges::any_of(state_->toplevels, [&](const auto& entry) { return entry.second.published.id == id; });
}

void CaptureSession::afterHide(QObject* context, std::function<void(bool)> done) {
  if (auto* display = qtDisplay()) {
    new DisplaySync(display, context, std::move(done));
    wl_display_flush(display);
  } else {
    QTimer::singleShot(0, context, [done = std::move(done)] { done(true); });
  }
}

}  // namespace screenshot
