#include "region_selection.h"

#include <QColorTransform>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace screenshot {

bool RegionSelection::begin(QPointF point, const CaptureFrame& frame) {
  if (canceled_ || dragging()) {
    return false;
  }
  if (tool_ == Tool::Select) {
    marks_.clear();
    start_ = end_ = point;
    dragging_ = true;
  } else {
    if (!rectangle().contains(point) || cropRect(frame).isEmpty()) {
      return false;
    }
    const auto& mapping = *frame.desktopMapping();
    rasterOrigin_ = mapping.rasterEdge({});
    captureScale_ = mapping.captureScale;
    const auto raster = rasterOrigin_ + point * captureScale_;
    gesture_.emplace(Mark{tool_, color_, tool_ == Tool::Pen ? QPolygonF{raster} : QPolygonF{raster, raster}});
  }
  return true;
}

void RegionSelection::move(QPointF point) {
  if (dragging_) {
    end_ = point;
  } else if (gesture_) {
    auto& points = gesture_->points;
    const auto raster = rasterOrigin_ + point * captureScale_;
    if (gesture_->tool == Tool::Pen) {
      if (points.back() != raster) {
        points.append(raster);
      }
    } else {
      points.back() = raster;
    }
  }
}

void RegionSelection::release(QPointF point) {
  move(point);
  dragging_ = false;
  if (gesture_ && gesture_->hasArea()) {
    marks_.append(std::move(*gesture_));
  }
  gesture_.reset();
}

void RegionSelection::undo() {
  if (canUndo()) {
    marks_.removeLast();
  }
}

bool RegionSelection::Mark::hasArea() const {
  const auto delta = points.back() - points.front();
  switch (tool) {
    case Tool::Pen:
      return true;
    case Tool::Arrow:
      return !delta.isNull();
    case Tool::Rectangle:
      return delta.x() != 0 && delta.y() != 0;
    case Tool::Select:
      return false;
  }
  return false;
}

void RegionSelection::Mark::paint(QPainter& painter, qreal scale, const QColorTransform& colorTransform) const {
  if (!hasArea()) {
    return;
  }
  painter.setPen(QPen(colorTransform.map(color), 3 * scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  switch (tool) {
    case Tool::Pen:
      if (points.size() == 1) {
        painter.drawPoint(points.front());
      } else {
        painter.drawPolyline(points);
      }
      break;
    case Tool::Arrow: {
      const auto tip = points.back();
      const auto delta = tip - points.front();
      const auto length = std::hypot(delta.x(), delta.y());
      const auto direction = delta / length;
      const auto head = std::min(12 * scale, length * 0.45);
      const auto base = tip - direction * head;
      const QPointF wing(-direction.y() * head * 0.55, direction.x() * head * 0.55);
      painter.drawLine(points.front(), tip);
      painter.drawPolyline(QPolygonF{base + wing, tip, base - wing});
      break;
    }
    case Tool::Rectangle:
      painter.drawRect(QRectF(points.front(), points.back()).normalized());
      break;
    case Tool::Select:
      break;
  }
}

void RegionSelection::cancel() {
  canceled_ = true;
  dragging_ = false;
  gesture_.reset();
  marks_.clear();
  start_ = end_ = {};
}

QRectF RegionSelection::rectangle() const { return QRectF(start_, end_).normalized(); }

QRect RegionSelection::cropRect(const CaptureFrame& frame) const {
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
  return clipped.toRect();
}

void RegionSelection::paintAnnotations(QPainter& painter, const CaptureFrame& frame, bool includeGesture) const {
  if (marks_.isEmpty() && (!includeGesture || !gesture_)) {
    return;
  }
  const auto colorSpace = frame.colorSpace();
  const auto colorTransform =
      colorSpace.isValid() ? QColorSpace(QColorSpace::SRgb).transformationToColorSpace(colorSpace) : QColorTransform();
  painter.save();
  painter.setClipRect(cropRect(frame), Qt::IntersectClip);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setBrush(Qt::NoBrush);
  for (const auto& mark : marks_) {
    mark.paint(painter, captureScale_, colorTransform);
  }
  if (includeGesture && gesture_) {
    gesture_->paint(painter, captureScale_, colorTransform);
  }
  painter.restore();
}

QImage RegionSelection::confirm(const CaptureFrame& frame) const {
  const auto pixels = cropRect(frame);
  if (dragging() || pixels.isEmpty()) {
    return {};
  }
  auto crop = frame.image().copy(pixels);
  if (!crop.isNull() && !marks_.isEmpty()) {
    if (crop.depth() <= 8 || crop.format() == QImage::Format_CMYK8888) {
      crop.convertTo(QImage::Format_ARGB32);
    }
    QPainter painter(&crop);
    painter.translate(-pixels.topLeft());
    paintAnnotations(painter, frame, false);
  }
  return crop;
}

}  // namespace screenshot
