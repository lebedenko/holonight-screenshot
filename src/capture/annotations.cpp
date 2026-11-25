#include "annotations.h"

#include <QColorSpace>
#include <QColorTransform>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace screenshot {

bool Annotations::begin(Tool tool, QColor color, QPointF point) {
  if (active() || tool == Tool::Select) {
    return false;
  }
  gesture_.emplace(Mark{tool, color, tool == Tool::Pen ? QPolygonF{point} : QPolygonF{point, point}});
  return true;
}

void Annotations::move(QPointF point) {
  if (!gesture_) {
    return;
  }
  auto& points = gesture_->points;
  if (gesture_->tool == Tool::Pen) {
    if (points.back() != point) {
      points.append(point);
    }
  } else {
    points.back() = point;
  }
}

void Annotations::finish(QPointF point) {
  move(point);
  if (gesture_ && gesture_->hasArea()) {
    marks_.append(std::move(*gesture_));
  }
  cancelStroke();
}

void Annotations::undo() {
  if (canUndo()) {
    marks_.removeLast();
  }
}

QPointF Annotations::imagePoint(QPointF point, QRectF bounds, QSize size) {
  const auto offset = point - bounds.topLeft();
  return {offset.x() * size.width() / bounds.width(), offset.y() * size.height() / bounds.height()};
}

bool Annotations::Mark::hasArea() const {
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

void Annotations::Mark::paint(QPainter& painter, qreal scale, const QColorTransform& colorTransform) const {
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

void Annotations::paint(QPainter& painter, QRectF clip, const QColorSpace& colorSpace, qreal scale,
                        bool includeGesture) const {
  if (marks_.isEmpty() && (!includeGesture || !gesture_)) {
    return;
  }
  const auto colorTransform =
      colorSpace.isValid() ? QColorSpace(QColorSpace::SRgb).transformationToColorSpace(colorSpace) : QColorTransform();
  painter.save();
  painter.setClipRect(clip, Qt::IntersectClip);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setBrush(Qt::NoBrush);
  for (const auto& mark : marks_) {
    mark.paint(painter, scale, colorTransform);
  }
  if (includeGesture && gesture_) {
    gesture_->paint(painter, scale, colorTransform);
  }
  painter.restore();
}

QImage Annotations::render(QImage image, qreal scale, QPoint origin) const {
  if (image.isNull() || marks_.isEmpty()) {
    return image;
  }
  if (image.depth() <= 8 || image.format() == QImage::Format_CMYK8888) {
    image.convertTo(QImage::Format_ARGB32);
  }
  QPainter painter(&image);
  painter.translate(-origin);
  paint(painter, image.rect().translated(origin), image.colorSpace(), scale, false);
  painter.end();
  return image;
}

}  // namespace screenshot
