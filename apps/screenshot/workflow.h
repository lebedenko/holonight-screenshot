#pragma once

#include "annotations.h"
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
  bool editor = false;
};

class Workflow final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString page READ page NOTIFY pageChanged)
  Q_PROPERTY(QVariantList modes READ modes NOTIFY modesChanged)
  Q_PROPERTY(int delayMs READ delayMs WRITE setDelayMs NOTIFY preferencesChanged)
  Q_PROPERTY(bool includeCursor READ includeCursor WRITE setIncludeCursor NOTIFY preferencesChanged)
  Q_PROPERTY(QString target READ target NOTIFY modesChanged)
  Q_PROPERTY(QVariantList windows READ windows NOTIFY windowsChanged)
  Q_PROPERTY(QString selectedWindow READ selectedWindow NOTIFY selectionChanged)
  Q_PROPERTY(QString message READ message NOTIFY messageChanged)
  Q_PROPERTY(bool fixedSavePath READ fixedSavePath NOTIFY previewChanged)
  Q_PROPERTY(bool editing READ editing NOTIFY annotationChanged)
  Q_PROPERTY(bool strokeActive READ strokeActive NOTIFY annotationChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY annotationChanged)
  Q_PROPERTY(int annotationRevision READ annotationRevision NOTIFY annotationChanged)
  Q_PROPERTY(int revision READ revision NOTIFY previewChanged)

 public:
  explicit Workflow(Options options);
  ~Workflow() override;
  void start();
  QString page() const { return page_; }
  QVariantList modes() const;
  QString target() const;
  int delayMs() const { return options_.delayMs; }
  bool includeCursor() const { return options_.cursor == CursorPolicy::Include; }
  void setDelayMs(int delay);
  void setIncludeCursor(bool include);
  QVariantList windows() const;
  QString selectedWindow() const { return selectedWindow_; }
  QString message() const { return message_; }
  int revision() const { return revision_; }

  Q_INVOKABLE void selectMode(const QString& name);
  bool fixedSavePath() const { return options_.editor && !options_.path.isEmpty(); }
  bool editing() const { return editing_; }
  bool strokeActive() const { return annotations_.active(); }
  bool canUndo() const { return annotations_.canUndo(); }
  int annotationRevision() const { return annotationRevision_; }

  Q_INVOKABLE void startCapture();
  Q_INVOKABLE void saveDirectly();
  Q_INVOKABLE void setEditing(bool editing);
  Q_INVOKABLE void beginStroke(int tool, const QColor& color, QPointF point, QRectF bounds);
  Q_INVOKABLE void moveStroke(QPointF point, QRectF bounds);
  Q_INVOKABLE void finishStroke(QPointF point, QRectF bounds);
  Q_INVOKABLE void cancelStroke();
  Q_INVOKABLE void undo();
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
  void preferencesChanged();
  void windowsChanged();
  void selectionChanged();
  void messageChanged();
  void previewChanged();
  void annotationChanged();

 private:
  bool loadUi();
  void loadPreferences();
  void restoreTarget();
  QList<CaptureWindow> pickableWindows() const;
  void refreshPicker();
  void beginCapture(const QString& path);
  void captureAfterHide(const QString& id = {});
  void capture(const QString& id);
  void captured(const CaptureFrame& frame);
  void handleResult(const QImage& image);
  void updateAnnotations();
  void releaseCapture();
  void failed(const QString& reason);
  void canceled();
  void setPage(const QString& page);
  void setMessage(const QString& message);

  Options options_;
  bool launcher_;
  bool ready_ = false;
  bool busy_ = false;
  bool choosingPath_ = false;
  bool editing_ = false;
  QString savedTarget_;
  QString page_;
  QString message_;
  QString selectedWindow_;
  int revision_ = 0;
  CaptureSession session_;
  QImage result_;
  Annotations annotations_;
  int annotationRevision_ = 0;
  std::unique_ptr<QQmlApplicationEngine> engine_;
  QWindow* window_ = nullptr;
  std::unique_ptr<GrimAcquisition> acquisition_;
  std::unique_ptr<CaptureController> controller_;
  std::unique_ptr<RegionOverlay> overlay_;
};

}  // namespace screenshot
