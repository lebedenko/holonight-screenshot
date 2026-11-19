#include "capture_frame.h"

#include <algorithm>
#include <cmath>
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

QColor CaptureFrame::sampleSrgb(QPointF point) const {
  if (!mapping_ || !std::isfinite(point.x()) || !std::isfinite(point.y()) ||
      std::ranges::none_of(mapping_->outputs, [point](const auto& output) {
        const auto& bounds = output.logicalBounds;
        return point.x() >= bounds.left() && point.x() < bounds.right() && point.y() >= bounds.top() &&
               point.y() < bounds.bottom();
      })) {
    return {};
  }
  const auto edge = mapping_->rasterEdge(point);
  if (!QRectF(image_.rect()).contains(edge) || edge.x() >= image_.width() || edge.y() >= image_.height()) {
    return {};
  }
  const auto color = image_.pixelColor(static_cast<int>(edge.x()), static_cast<int>(edge.y()));
  return colorSpace().isValid() ? colorSpace().transformationToColorSpace(QColorSpace::SRgb).map(color) : color;
}

}  // namespace screenshot
