#include "region_selection.h"

#include <algorithm>
#include <cmath>

namespace screenshot {

void RegionSelection::begin(QPointF point) {
  if (!canceled_) {
    start_ = end_ = point;
    dragging_ = true;
  }
}

void RegionSelection::move(QPointF point) {
  if (dragging_) {
    end_ = point;
  }
}

void RegionSelection::release(QPointF point) {
  move(point);
  dragging_ = false;
}

void RegionSelection::cancel() {
  canceled_ = true;
  dragging_ = false;
  start_ = end_ = {};
}

QRectF RegionSelection::rectangle() const { return QRectF(start_, end_).normalized(); }

QImage RegionSelection::confirm(const CaptureFrame& frame) const {
  const auto& mapping = frame.desktopMapping();
  const auto selection = rectangle();
  if (dragging_ || canceled_ || selection.isEmpty() || !mapping ||
      std::ranges::none_of(mapping->outputs,
                           [&](const auto& output) { return selection.intersects(output.logicalBounds); })) {
    return {};
  }
  const auto start = mapping->rasterEdge(selection.topLeft());
  const auto end = mapping->rasterEdge(selection.bottomRight());
  // Intersect before converting to integers. A pointer grab can go beyond the desktop.
  const QRectF pixels(QPointF(std::floor(start.x()), std::floor(start.y())),
                      QPointF(std::ceil(end.x()), std::ceil(end.y())));
  const auto clipped = pixels.intersected(QRectF(frame.image().rect()));
  return clipped.isEmpty() ? QImage() : frame.image().copy(clipped.toRect());
}

}  // namespace screenshot
