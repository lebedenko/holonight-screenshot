#pragma once

#include "capture_session.h"
#include "desktop_acquisition.h"

#include <QImage>
#include <QObject>
#include <QVariantList>

#include <memory>
#include <optional>

class QQmlApplicationEngine;
class QWindow;

namespace screenshot {

class CaptureController;
class GrimAcquisition;
class RegionOverlay;

struct Options {
  QString path;
  int delayMs = 0;
  CursorPolicy cursor = CursorPolicy::Exclude;
  std::optional<CaptureTarget> target;
  bool list = false;
};

class Workflow final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString page READ page NOTIFY pageChanged)
  Q_PROPERTY(QVariantList modes READ modes NOTIFY modesChanged)
  Q_PROPERTY(QString target READ target NOTIFY modesChanged)
  Q_PROPERTY(QVariantList windows READ windows NOTIFY windowsChanged)
  Q_PROPERTY(QString selectedWindow READ selectedWindow NOTIFY selectionChanged)
  Q_PROPERTY(QString message READ message NOTIFY messageChanged)
  Q_PROPERTY(int revision READ revision NOTIFY previewChanged)

 public:
  explicit Workflow(Options options);
  ~Workflow() override;
  void start();
  QString page() const { return page_; }
  QVariantList modes() const;
  QString target() const;
  QVariantList windows() const;
  QString selectedWindow() const { return selectedWindow_; }
  QString message() const { return message_; }
  int revision() const { return revision_; }

  Q_INVOKABLE void selectMode(const QString& name);
  Q_INVOKABLE void startCapture(int delayMs, bool cursor);
  Q_INVOKABLE void selectWindow(const QString& id);
  Q_INVOKABLE void moveSelection(int offset);
  Q_INVOKABLE void confirmWindow();
  Q_INVOKABLE void cancelPicker();
  Q_INVOKABLE void closeWindow();
  Q_INVOKABLE void newCapture();
  Q_INVOKABLE void copy();
  Q_INVOKABLE void save();

 signals:
  void pageChanged();
  void modesChanged();
  void windowsChanged();
  void selectionChanged();
  void messageChanged();
  void previewChanged();

 private:
  bool loadUi();
  QList<CaptureWindow> pickableWindows() const;
  void refreshPicker();
  void captureAfterHide(const QString& id = {});
  void capture(const QString& id);
  void captured(const CaptureFrame& frame);
  void preview(const QImage& image);
  void releaseCapture();
  void failed(const QString& reason);
  void canceled();
  void setPage(const QString& page);
  void setMessage(const QString& message);

  Options options_;
  bool launcher_;
  bool ready_ = false;
  bool busy_ = false;
  QString page_;
  QString message_;
  QString selectedWindow_;
  int revision_ = 0;
  CaptureSession session_;
  QImage result_;
  std::unique_ptr<QQmlApplicationEngine> engine_;
  QWindow* window_ = nullptr;
  std::unique_ptr<GrimAcquisition> acquisition_;
  std::unique_ptr<CaptureController> controller_;
  std::unique_ptr<RegionOverlay> overlay_;
};

}  // namespace screenshot
