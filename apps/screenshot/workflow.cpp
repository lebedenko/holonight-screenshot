#include "workflow.h"

#include "capture_controller.h"
#include "grim_acquisition.h"
#include "png_output.h"
#include "region_overlay.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QQmlApplicationEngine>
#include <QQuickImageProvider>
#include <QScopedValueRollback>
#include <QScreen>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QWindow>

#include <algorithm>
#include <array>
#include <functional>
#include <utility>

namespace screenshot {
namespace {

constexpr std::array launcherTargets{CaptureTarget::Area, CaptureTarget::Screen, CaptureTarget::Window};

void savePreference(const QString& key, const QVariant& value) {
  QSettings settings(statePath("launcher.ini"), QSettings::IniFormat);
  settings.setValue(key, value);
  settings.sync();
  if (settings.status() != QSettings::NoError) {
    qWarning() << "Could not save launcher preference" << key << "to" << settings.fileName();
  }
}

void exitWithError(const QString& reason) {
  QTextStream(stderr) << reason << Qt::endl;
  QCoreApplication::exit(1);
}

void notifyOutput(QObject* context, const QString& title, const QString& error) {
  auto message = QDBusMessage::createMethodCall(
      QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("/org/freedesktop/Notifications"),
      QStringLiteral("org.freedesktop.Notifications"), QStringLiteral("Notify"));
  message.setArguments({QStringLiteral("HoloNight Screenshot"), uint{0}, QStringLiteral("holonight-screenshot"), title,
                        error.toHtmlEscaped(), QStringList{}, QVariantMap{}, -1});
  auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), context);
  QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context, [](QDBusPendingCallWatcher* finished) {
    const QDBusPendingReply<uint> reply = *finished;
    if (reply.isError()) {
      qWarning() << "Could not deliver screenshot notification:" << reply.error().message();
    }
    finished->deleteLater();
  });
}

QString applicationName(const QString& appId) {
  if (appId.isEmpty() || appId.contains(u'/')) {
    return appId;
  }
  const auto desktopFile = appId.endsWith(".desktop") ? appId : appId + ".desktop";
  const auto path = QStandardPaths::locate(QStandardPaths::GenericDataLocation, "applications/" + desktopFile);
  if (path.isEmpty()) {
    return appId;
  }
  const QSettings desktopEntry(path, QSettings::IniFormat);
  const auto name = desktopEntry.value("Desktop Entry/Name").toString();
  return name.isEmpty() ? appId : name;
}

void suppressHyprlandCloseAnimation(QObject* context, std::function<void(bool)> done) {
  auto* process = new QProcess(context);
  const auto selector = QStringLiteral("pid:%1").arg(QCoreApplication::applicationPid());
  const auto applyLegacy = [process, selector](const QString& property) {
    process->start(QStringLiteral("hyprctl"), {QStringLiteral("setprop"), selector, property, QStringLiteral("1")});
  };
  const auto finish = [process, done = std::move(done)](bool ok) {
    process->deleteLater();
    done(ok);
  };
  auto* deadline = new QTimer(process);
  deadline->setSingleShot(true);
  QObject::connect(deadline, &QTimer::timeout, process, &QProcess::kill);
  QObject::connect(
      process, &QProcess::finished, process,
      [process, deadline, applyLegacy, finish, legacyAttempt = 0](int code, QProcess::ExitStatus status) mutable {
        const auto reply = process->readAllStandardOutput().trimmed();
        if (deadline->isActive() && status == QProcess::NormalExit) {
          if (code == 0 && reply == "ok") {
            finish(true);
            return;
          }
          // Hyprland used two property spellings before switching to Lua dispatchers.
          if (legacyAttempt < 2) {
            applyLegacy(++legacyAttempt == 1 ? QStringLiteral("no_anim") : QStringLiteral("noanim"));
            return;
          }
        }
        finish(false);
      });
  QObject::connect(process, &QProcess::errorOccurred, process, [finish](QProcess::ProcessError error) {
    if (error == QProcess::FailedToStart) {
      finish(false);
    }
  });
  deadline->start(2000);
  process->start(
      QStringLiteral("hyprctl"),
      {QStringLiteral("dispatch"),
       QStringLiteral(R"(hl.dsp.window.set_prop({ window = "%1", prop = "no_anim", value = "1" }))").arg(selector)});
}

class EditorImage final : public QQuickImageProvider {
 public:
  explicit EditorImage(const EditorDocument& document) : QQuickImageProvider(Image), document_(document) {}

  QImage requestImage(const QString&, QSize* size, const QSize&) override {
    auto image = document_.render(true);
    if (size) {
      *size = image.size();
    }
    return image;
  }

 private:
  const EditorDocument& document_;
};

bool presetDelay(int delay) { return delay == 0 || delay == 3000 || delay == 5000 || delay == 10000; }

}  // namespace

Workflow::Workflow(Options options)
    : options_(std::move(options)), config_(loadConfig()), launcher_(!options_.target && !options_.list) {}

Workflow::~Workflow() = default;

bool Workflow::loadUi() {
  if (engine_) {
    return window_ != nullptr;
  }
  engine_ = std::make_unique<QQmlApplicationEngine>();
  engine_->setInitialProperties({{QStringLiteral("workflow"), QVariant::fromValue(static_cast<QObject*>(this))}});
  engine_->addImageProvider(QStringLiteral("capture"), new EditorImage(document_));
  engine_->load(QUrl(QStringLiteral("qrc:/screenshot/Launcher.qml")));
  if (!engine_->rootObjects().isEmpty()) {
    window_ = qobject_cast<QWindow*>(engine_->rootObjects().front());
  }
  if (!window_) {
    exitWithError(QStringLiteral("Could not load the screenshot interface."));
  }
  return window_ != nullptr;
}

void Workflow::start() {
  if (launcher_) {
    loadPreferences();
    if (!loadUi()) {
      return;
    }
    setPage(QStringLiteral("launcher"));
  }
  connect(&session_, &CaptureSession::windowsChanged, this, &Workflow::refreshPicker);
  connect(&session_, &CaptureSession::discovered, this, [this] {
    ready_ = true;
    if (options_.list) {
      QJsonArray supported;
      for (auto mode : captureTargets) {
        if (session_.capabilities().supports(mode)) {
          supported.append(targetName(mode));
        }
      }
      QTextStream(stdout) << QJsonDocument(supported).toJson(QJsonDocument::Compact) << Qt::endl;
      QCoreApplication::exit(0);
      return;
    }
    if (launcher_) {
      restoreTarget();
      return;
    }

    const auto reason = session_.capabilities().reason(*options_.target);
    if (!reason.isEmpty()) {
      failed(reason);
      return;
    }
    startCapture();
  });
  session_.discover();
}

QVariantList Workflow::modes() const {
  QVariantList modes;
  for (auto mode : {CaptureTarget::Area, CaptureTarget::Window, CaptureTarget::Screen}) {
    modes.append(QVariantMap{{"name", targetName(mode)},
                             {"available", ready_ && session_.capabilities().supports(mode)},
                             {"reason", ready_ ? session_.capabilities().reason(mode) : QString()}});
  }
  return modes;
}

QString Workflow::target() const { return options_.target ? targetName(*options_.target) : QString(); }

void Workflow::loadPreferences() {
  const QSettings settings(statePath("launcher.ini"), QSettings::IniFormat);
  savedTarget_ = settings.value("target").toString();
  bool validDelay = false;
  const int delay = settings.value("delayMs").toInt(&validDelay);
  options_.delayMs = validDelay && presetDelay(delay) ? delay : 0;
  options_.cursor =
      settings.value("includeCursor").toString() == "true" ? CursorPolicy::Include : CursorPolicy::Exclude;
  options_.editor = settings.value("openEditor", false).toBool();
  emit preferencesChanged();
}

void Workflow::restoreTarget() {
  options_.target.reset();
  for (auto mode : launcherTargets) {
    if (!session_.capabilities().supports(mode)) {
      continue;
    }
    if (!options_.target || targetName(mode) == savedTarget_) {
      options_.target = mode;
    }
  }
  if (options_.target && target() != savedTarget_) {
    savedTarget_ = target();
    savePreference("target", savedTarget_);
  }
  emit modesChanged();
}

void Workflow::setDelayMs(int delay) {
  if (!launcher_ || delay == options_.delayMs || !presetDelay(delay)) {
    return;
  }
  options_.delayMs = delay;
  savePreference("delayMs", delay);
  emit preferencesChanged();
}

void Workflow::setIncludeCursor(bool include) {
  if (!launcher_ || include == includeCursor()) {
    return;
  }
  options_.cursor = include ? CursorPolicy::Include : CursorPolicy::Exclude;
  savePreference("includeCursor", include);
  emit preferencesChanged();
}

void Workflow::setOpenEditor(bool open) {
  if (launcher_ && open != options_.editor) {
    options_.editor = open;
    savePreference("openEditor", open);
    emit preferencesChanged();
  }
}

void Workflow::selectMode(const QString& name) {
  if (!ready_ || busy_ || page_ != "launcher") {
    return;
  }
  for (auto mode : launcherTargets) {
    if (targetName(mode) == name && options_.target != mode && session_.capabilities().supports(mode)) {
      options_.target = mode;
      savedTarget_ = name;
      savePreference("target", name);
      emit modesChanged();
      break;
    }
  }
}

void Workflow::startCapture() {
  if (!ready_ || busy_ || choosingPath_ || !options_.target || !session_.capabilities().supports(*options_.target)) {
    return;
  }
  busy_ = true;
  setMessage({});
  if (*options_.target == CaptureTarget::Window) {
    if (loadUi()) {
      setPage(QStringLiteral("picker"));
      refreshPicker();
    }
  } else if (launcher_) {
    captureAfterHide();
  } else {
    capture({});
  }
}

QList<CaptureWindow> Workflow::pickableWindows() const {
  auto windows = session_.windows();
  auto appId = QGuiApplication::desktopFileName();
  if (appId.isEmpty()) {
    appId = QFileInfo(QCoreApplication::applicationFilePath()).baseName();
  }
  windows.removeIf([&](const CaptureWindow& window) { return window.appId == appId; });
  return windows;
}

QVariantList Workflow::windows() const {
  QVariantList rows;
  for (const auto& window : pickableWindows()) {
    rows.append(QVariantMap{{"id", window.id}, {"title", window.title}, {"appName", applicationName(window.appId)}});
  }
  return rows;
}

void Workflow::refreshPicker() {
  if (page_ != "picker") {
    return;
  }
  const auto rows = pickableWindows();
  if (std::ranges::find(rows, selectedWindow_, &CaptureWindow::id) == rows.end()) {
    selectedWindow_ = rows.isEmpty() ? QString() : rows.front().id;
  }
  emit windowsChanged();
  emit selectionChanged();
}

void Workflow::selectWindow(const QString& id) {
  if (page_ == "picker" && session_.containsWindow(id)) {
    selectedWindow_ = id;
    emit selectionChanged();
  }
}

void Workflow::moveSelection(int offset) {
  const auto rows = pickableWindows();
  const auto current = std::ranges::find(rows, selectedWindow_, &CaptureWindow::id);
  if (current != rows.end()) {
    const auto index = std::clamp(current - rows.begin() + offset, qsizetype(0), rows.size() - 1);
    selectWindow(rows[index].id);
  }
}

void Workflow::confirmWindow() {
  if (page_ == "picker" && session_.containsWindow(selectedWindow_)) {
    captureAfterHide(selectedWindow_);
  }
}

void Workflow::cancelPicker() {
  if (page_ == "picker") {
    canceled();
  }
}

void Workflow::captureAfterHide(const QString& id) {
  const auto hide = [this, id] {
    setPage({});
    CaptureSession::afterHide(this, [this, id](bool synced) {
      if (synced) {
        capture(id);
      } else {
        failed(QStringLiteral("The compositor did not acknowledge hiding the capture window."));
      }
    });
  };
  // The desktop grab includes Hyprland's closing snapshot even after the surface has unmapped.
  // A window-ID capture reads the selected surface directly.
  if (id.isEmpty() && session_.capabilities().focus == FocusLookup::Hyprland) {
    suppressHyprlandCloseAnimation(this, [this, hide](bool suppressed) {
      if (suppressed) {
        hide();
      } else {
        failed(QStringLiteral("Could not disable the screenshot window's closing animation in Hyprland."));
      }
    });
  } else {
    hide();
  }
}

void Workflow::capture(const QString& id) {
  acquisition_ = std::make_unique<GrimAcquisition>(session_, *options_.target, id);
  controller_ =
      std::make_unique<CaptureController>(*acquisition_, std::chrono::milliseconds(options_.delayMs), options_.cursor);
  connect(controller_.get(), &CaptureController::failed, this, &Workflow::failed, Qt::QueuedConnection);
  connect(controller_.get(), &CaptureController::captured, this, &Workflow::captured, Qt::QueuedConnection);
}

void Workflow::captured(const CaptureFrame& frame) {
  if (*options_.target != CaptureTarget::Area) {
    handleResult(frame.image());
    return;
  }

  overlay_ = std::make_unique<RegionOverlay>(frame, *acquisition_, RegionOverlay::Mode::SelectionOnly);
  connect(overlay_.get(), &RegionOverlay::failed, this, &Workflow::failed, Qt::QueuedConnection);
  connect(overlay_.get(), &RegionOverlay::canceled, this, &Workflow::canceled, Qt::QueuedConnection);
  connect(
      overlay_.get(), &RegionOverlay::selected, this,
      [this](RegionOverlay::Action, const QImage& crop) { handleResult(crop); }, Qt::QueuedConnection);
  overlay_->open();
}

void Workflow::handleResult(const QImage& image) {
  if (options_.editor) {
    releaseCapture();
    document_.reset(image, config_.annotations);
    if (loadUi()) {
      setPage(QStringLiteral("editor"));
    }
    return;
  }

  const auto error = options_.path.isEmpty() ? copyPng(image) : savePng(options_.path, image);
  if (!error.isEmpty()) {
    failed(error);
    return;
  }
  releaseCapture();
  if (launcher_) {
    notifyOutput(this, tr("Copied to clipboard."), {});
    if (!config_.closeAfterCopy) {
      setPage(QStringLiteral("launcher"));
      return;
    }
  }
  QCoreApplication::exit(0);
}

void Workflow::releaseCapture() {
  overlay_.reset();
  controller_.reset();
  acquisition_.reset();
  selectedWindow_.clear();
  emit selectionChanged();
  busy_ = false;
}

void Workflow::failed(const QString& reason) {
  if (!launcher_) {
    exitWithError(reason);
    return;
  }
  releaseCapture();
  setMessage(reason);
  setPage(QStringLiteral("launcher"));
}

void Workflow::canceled() {
  if (!launcher_) {
    QCoreApplication::exit(2);
    return;
  }
  releaseCapture();
  if (page_ != "picker" && config_.closeOnRegionCancel) {
    QCoreApplication::exit(0);
    return;
  }
  setPage(QStringLiteral("launcher"));
}

void Workflow::closeWindow() {
  if (page_ == "picker") {
    QMetaObject::invokeMethod(this, &Workflow::cancelPicker, Qt::QueuedConnection);
  } else if (page_ == "editor" && choosingPath_) {
    return;
  } else if (page_ == "editor" && launcher_) {
    setPage(QStringLiteral("launcher"));
    document_.reset({});
  } else {
    QCoreApplication::exit(0);
  }
}

void Workflow::showEditorWindow(QWindow* window) {
  QSettings settings(statePath("editor.ini"), QSettings::IniFormat);
  QSize size(settings.value("width").toInt(), settings.value("height").toInt());
  if (size.width() < window->minimumWidth() || size.height() < window->minimumHeight()) {
    size = QSize(1600, 900);
  }
  if (const auto* screen = window->screen()) {
    size = size.boundedTo(screen->availableGeometry().size());
  }
  window->resize(size.expandedTo(window->minimumSize()));
  if (settings.value("maximized", false).toBool()) {
    window->setWindowState(Qt::WindowMaximized);
  }
  window->setVisible(true);

  // Coalesce width and height changes and check the state after compositor-driven resizes.
  auto* geometrySave = new QTimer(window);
  geometrySave->setSingleShot(true);
  connect(window, &QWindow::widthChanged, geometrySave, qOverload<>(&QTimer::start));
  connect(window, &QWindow::heightChanged, geometrySave, qOverload<>(&QTimer::start));
  connect(window, &QWindow::windowStateChanged, geometrySave, qOverload<>(&QTimer::start));
  connect(geometrySave, &QTimer::timeout, window, [window] {
    QSettings state(statePath("editor.ini"), QSettings::IniFormat);
    if (window->windowState() == Qt::WindowNoState) {
      state.setValue("width", window->width());
      state.setValue("height", window->height());
      state.setValue("maximized", false);
    } else if (window->windowState() == Qt::WindowMaximized) {
      state.setValue("maximized", true);
    }
    state.sync();
    if (state.status() != QSettings::NoError) {
      qWarning() << "Could not save editor geometry to" << state.fileName();
    }
  });
}

void Workflow::copy() {
  if (page_ != "editor" || document_.busy() || choosingPath_) {
    return;
  }
  const auto error = copyPng(document_.render());
  notifyOutput(this, error.isEmpty() ? tr("Copied to clipboard.") : tr("Could not copy screenshot"), error);
  if (error.isEmpty() && config_.closeAfterCopy) {
    QCoreApplication::exit(0);
  }
}

void Workflow::save() {
  if (page_ != "editor" || document_.busy() || choosingPath_) {
    return;
  }
  const QScopedValueRollback choosing(choosingPath_, true);
  const auto path = options_.path.isEmpty() ? choosePngPath(saveDialogDirectory(config_.saveDirectory)) : options_.path;
  if (!path.isEmpty()) {
    const auto error = savePng(path, document_.render());
    notifyOutput(this, error.isEmpty() ? tr("Screenshot saved.") : tr("Could not save screenshot"), error);
    if (error.isEmpty() && config_.closeAfterSave) {
      QCoreApplication::exit(0);
    }
  }
}

void Workflow::setPage(const QString& page) {
  page_ = page;
  emit pageChanged();
}

void Workflow::setMessage(const QString& message) {
  message_ = message;
  emit messageChanged();
}

}  // namespace screenshot
