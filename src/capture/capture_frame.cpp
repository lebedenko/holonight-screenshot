#include "capture_frame.h"

#include <utility>

namespace screenshot {

QPointF DesktopMapping::rasterEdge(QPointF desktopEdge) const {
  QRectF bounds;
  for (const auto& output : outputs) {
    bounds = bounds.united(output.logicalBounds);
  }
  return (desktopEdge - bounds.topLeft()) * captureScale;
}

CaptureFrame::CaptureFrame(const QImage& uprightImage, std::optional<DesktopMapping> mapping)
    : image_(uprightImage.copy()), mapping_(std::move(mapping)) {}

}  // namespace screenshot
