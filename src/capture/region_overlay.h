#pragma once

#include "region_selection.h"

#include <QPointer>
#include <QTimer>

#include <holonight/wayland/layersurfacehost.h>
#include <memory>
#include <vector>

class QQuickItem;
class QQuickView;
class QScreen;

namespace screenshot {

class GrimAcquisition;

class RegionOverlay final : public QObject {
  Q_OBJECT
 public:
  enum class Mode { Cli, Share, PickColor, SelectArea, Desktop, Choice };
  RegionOverlay(CaptureFrame frame, GrimAcquisition& acquisition, Mode mode = Mode::Cli);
  RegionOverlay(CaptureFrame frame, GrimAcquisition& acquisition, Mode mode, QString appName);
  void cancel();
  void open();

  enum class Action { Copy, Save, Share, PickColor };
  Q_ENUM(Action)

 signals:
  void selected(Action action, const QImage& crop);
  void colorPicked(const QColor& color);
  void canceled();
  void failed(const QString& reason);

 private:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void configured();
  void redraw();
  void undo();
  void setDesktop(bool desktop);
  void confirm(Action action);
  void finish(QImage crop = {}, QString error = {}, QColor color = {});

  struct Surface {
    QScreen* screen;
    QPointer<QQuickItem> preview;
    OutputGeometry output;
    std::unique_ptr<Holonight::Wayland::LayerSurfaceHost> host;
  };

  Mode mode_;
  QString appName_;
  bool desktop_;
  QPointF pickedPoint_;
  std::optional<CaptureFrame> frame_;
  GrimAcquisition& acquisition_;
  RegionSelection selection_;
  std::vector<Surface> surfaces_;
  QPointer<QQuickView> dragView_;
  QTimer timeout_;
  QTimer layoutTimer_;
  std::optional<Action> pendingAction_;
  enum class LayoutCheck { Idle, Polling, Confirming };
  LayoutCheck layoutCheck_ = LayoutCheck::Idle;
  bool ready_ = false;
  bool finished_ = false;
};

}  // namespace screenshot
