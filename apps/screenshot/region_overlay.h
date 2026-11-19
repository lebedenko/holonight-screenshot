#pragma once

#include "region_selection.h"

#include <QPointer>
#include <QTimer>

#include <holonight/wayland/layersurfacehost.h>
#include <memory>
#include <vector>

class QQuickView;
class QScreen;

namespace screenshot {

class GrimAcquisition;

class RegionOverlay final : public QObject {
  Q_OBJECT
 public:
  RegionOverlay(CaptureFrame frame, GrimAcquisition& acquisition);
  void open();

 signals:
  void selected(const QImage& crop);
  void canceled();
  void failed(const QString& reason);

 private:
  bool eventFilter(QObject* watched, QEvent* event) override;
  void configured();
  void redraw();
  void finish(QImage crop = {}, QString error = {});

  struct Surface {
    QScreen* screen;
    OutputGeometry output;
    std::unique_ptr<Holonight::Wayland::LayerSurfaceHost> host;
  };

  CaptureFrame frame_;
  GrimAcquisition& acquisition_;
  RegionSelection selection_;
  std::vector<Surface> surfaces_;
  QPointer<QQuickView> dragView_;
  QTimer timeout_;
  QTimer layoutTimer_;
  QImage pendingCrop_;
  enum class LayoutCheck { Idle, Polling, Confirming };
  LayoutCheck layoutCheck_ = LayoutCheck::Idle;
  bool ready_ = false;
  bool finished_ = false;
};

}  // namespace screenshot
