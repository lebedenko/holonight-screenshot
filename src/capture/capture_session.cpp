#include "capture_session.h"

#include "wayland-ext-foreign-toplevel-list-v1-client-protocol.h"

#include <QGuiApplication>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTimer>
#include <QtGui/qguiapplication_platform.h>

#include <algorithm>
#include <cerrno>
#include <map>
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

bool installed(const QString& command) { return !QStandardPaths::findExecutable(command).isEmpty(); }

class DisplaySync final : public QObject {
 public:
  DisplaySync(wl_display* display, QObject* parent, std::function<void(bool)> done)
      : QObject(parent), done_(std::move(done)), callback_(wl_display_sync(display)) {
    static const wl_callback_listener listener{
        [](void* data, wl_callback*, uint32_t) { static_cast<DisplaySync*>(data)->finish(true); }};
    wl_callback_add_listener(callback_, &listener, this);
    QTimer::singleShot(2000, this, [this] { finish(false); });
  }

  ~DisplaySync() override {
    if (callback_) {
      wl_callback_destroy(callback_);
    }
  }

 private:
  void finish(bool ok) {
    if (!callback_) {
      return;
    }
    wl_callback_destroy(callback_);
    callback_ = nullptr;
    deleteLater();
    std::exchange(done_, {})(ok);
  }

  std::function<void(bool)> done_;
  wl_callback* callback_;
};

}  // namespace

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
    helper.disconnect(&owner);
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

    if (hypr && installed("hyprctl")) {
      capabilities.focus = FocusLookup::Hyprland;
    } else if (!hypr && !labwc && sway && installed("swaymsg")) {
      capabilities.focus = FocusLookup::Sway;
    }

    QString screen;
    QString window;
    if (!connectionError.isEmpty()) {
      screen = window = connectionError;
    } else if (!installed("grim")) {
      screen = window = QStringLiteral("grim is required.");
    } else {
      if (!has("zwlr_screencopy_manager_v1") &&
          !(has(copyManager) && has("ext_output_image_capture_source_manager_v1"))) {
        screen = QStringLiteral("The compositor does not provide output capture.");
      } else if (capabilities.wlrLayout && !has("zwlr_output_manager_v1")) {
        screen = QStringLiteral("The compositor does not provide output layout.");
      } else if (!installed(capabilities.wlrLayout ? "wlr-randr" : "hyprctl")) {
        screen =
            capabilities.wlrLayout ? QStringLiteral("wlr-randr is required.") : QStringLiteral("hyprctl is required.");
      }
      if (!list || !has(toplevelSource) || !has(copyManager)) {
        window = QStringLiteral("The compositor does not provide window capture.");
      } else if (!grimToplevel) {
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

  void discover() {
    if (started) {
      return;
    }
    started = true;
    helper.setProcessChannelMode(QProcess::MergedChannels);
    QObject::connect(&helper, &QProcess::finished, &owner, [this](int, QProcess::ExitStatus status) {
      static const QRegularExpression option(QStringLiteral(R"((?:^|\n)\s*-T(?:\s|,|$))"));
      grimToplevel =
          status == QProcess::NormalExit && option.match(QString::fromLocal8Bit(helper.readAll())).hasMatch();
      helperReady = true;
      publish();
    });
    QObject::connect(&helper, &QProcess::errorOccurred, &owner, [this](QProcess::ProcessError error) {
      if (error == QProcess::FailedToStart) {
        helperReady = true;
        publish();
      }
    });
    helper.start(QStringLiteral("grim"), {QStringLiteral("-h")}, QIODevice::ReadOnly);
    QTimer::singleShot(2000, &owner, [this] {
      if (helper.state() != QProcess::NotRunning) {
        helper.kill();
      }
    });

    display = qtDisplay();
    if (!display) {
      ownsDisplay = true;
      display = wl_display_connect(nullptr);
      if (!display) {
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
  QProcess helper;
  QString connectionError;
  bool ownsDisplay = false;
  bool started = false;
  bool grimToplevel = false;
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
