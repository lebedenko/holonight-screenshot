#include "workflow.h"

#include "capture_controller.h"
#include "grim_acquisition.h"
#include "png_output.h"
#include "region_overlay.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QQuickImageProvider>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QWindow>

#include <algorithm>
#include <utility>

namespace screenshot {
namespace {

constexpr std::array launcherTargets{CaptureTarget::Area, CaptureTarget::Screen, CaptureTarget::Window};

void exitWithError(const QString& reason) {
  QTextStream(stderr) << reason << Qt::endl;
  QCoreApplication::exit(1);
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

class PreviewImage final : public QQuickImageProvider {
 public:
  explicit PreviewImage(const QImage& image) : QQuickImageProvider(Image), image_(image) {}

  QImage requestImage(const QString&, QSize* size, const QSize& requestedSize) override {
    if (size) {
      *size = image_.size();
    }
    if (requestedSize.isValid() && requestedSize != image_.size()) {
      return image_.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    return image_;
  }

 private:
  const QImage& image_;
};

}  // namespace

Workflow::Workflow(Options options) : options_(std::move(options)), launcher_(!options_.target && !options_.list) {}

Workflow::~Workflow() = default;

bool Workflow::loadUi() {
  if (engine_) {
    return window_ != nullptr;
  }
  engine_ = std::make_unique<QQmlApplicationEngine>();
  engine_->setInitialProperties({{QStringLiteral("workflow"), QVariant::fromValue(static_cast<QObject*>(this))}});
  engine_->addImageProvider(QStringLiteral("capture"), new PreviewImage(result_));
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
      for (auto mode : launcherTargets) {
        if (session_.capabilities().supports(mode)) {
          options_.target = mode;
          break;
        }
      }
      emit modesChanged();
      return;
    }

    const auto reason = session_.capabilities().reason(*options_.target);
    if (!reason.isEmpty()) {
      failed(reason);
      return;
    }
    startCapture(options_.delayMs, options_.cursor == CursorPolicy::Include);
  });
  session_.discover();
}

QVariantList Workflow::modes() const {
  QVariantList modes;
  for (auto mode : {CaptureTarget::Area, CaptureTarget::Window, CaptureTarget::Screen}) {
    modes.append(QVariantMap{
        {"name", targetName(mode)},
        {"reason", ready_ ? session_.capabilities().reason(mode) : QStringLiteral("Checking availability…")}});
  }
  return modes;
}

QString Workflow::target() const { return options_.target ? targetName(*options_.target) : QString(); }

void Workflow::selectMode(const QString& name) {
  if (!ready_ || busy_ || page_ != "launcher") {
    return;
  }
  for (auto mode : launcherTargets) {
    if (targetName(mode) == name && session_.capabilities().supports(mode)) {
      options_.target = mode;
      emit modesChanged();
      break;
    }
  }
}

void Workflow::startCapture(int delayMs, bool cursor) {
  if (!ready_ || busy_ || !options_.target || !session_.capabilities().supports(*options_.target)) {
    return;
  }
  busy_ = true;
  options_.delayMs = delayMs;
  options_.cursor = cursor ? CursorPolicy::Include : CursorPolicy::Exclude;
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
  const auto selected = id;
  setPage({});
  CaptureSession::afterHide(this, [this, selected](bool synced) {
    if (synced) {
      capture(selected);
    } else {
      failed(QStringLiteral("The compositor did not acknowledge hiding the capture window."));
    }
  });
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
    if (launcher_ || options_.path.isEmpty()) {
      preview(frame.image());
    } else if (const auto error = savePng(options_.path, frame.image()); !error.isEmpty()) {
      failed(error);
    } else {
      QCoreApplication::exit(0);
    }
    return;
  }

  const auto mode = launcher_ ? RegionOverlay::Mode::SelectionOnly : RegionOverlay::Mode::Cli;
  overlay_ = std::make_unique<RegionOverlay>(frame, *acquisition_, mode);
  connect(overlay_.get(), &RegionOverlay::failed, this, &Workflow::failed, Qt::QueuedConnection);
  connect(overlay_.get(), &RegionOverlay::canceled, this, &Workflow::canceled, Qt::QueuedConnection);
  connect(
      overlay_.get(), &RegionOverlay::selected, this,
      [this](RegionOverlay::Action action, const QImage& crop) {
        if (launcher_) {
          preview(crop);
          return;
        }
        QString error;
        if (action == RegionOverlay::Action::Copy) {
          error = copyPng(crop);
        } else {
          const auto path = options_.path.isEmpty() ? choosePngPath() : options_.path;
          if (path.isEmpty()) {
            canceled();
            return;
          }
          error = savePng(path, crop);
        }
        if (!error.isEmpty()) {
          failed(error);
        } else {
          QCoreApplication::exit(0);
        }
      },
      Qt::QueuedConnection);
  overlay_->open();
}

void Workflow::preview(const QImage& image) {
  result_ = image;
  releaseCapture();
  if (loadUi()) {
    ++revision_;
    emit previewChanged();
    setPage(QStringLiteral("preview"));
  }
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
  setPage(QStringLiteral("launcher"));
}

void Workflow::closeWindow() {
  if (page_ == "picker") {
    QMetaObject::invokeMethod(this, &Workflow::cancelPicker, Qt::QueuedConnection);
  } else {
    QCoreApplication::exit(0);
  }
}

void Workflow::newCapture() {
  launcher_ = true;
  options_.path.clear();
  result_ = {};
  setMessage({});
  setPage(QStringLiteral("launcher"));
}

void Workflow::copy() {
  const auto error = copyPng(result_);
  setMessage(error.isEmpty() ? QStringLiteral("Copied to clipboard.") : error);
}

void Workflow::save() {
  const auto path = choosePngPath();
  if (!path.isEmpty()) {
    const auto error = savePng(path, result_);
    setMessage(error.isEmpty() ? QStringLiteral("Screenshot saved.") : error);
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
