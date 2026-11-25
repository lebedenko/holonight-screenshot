#include "region_selection.h"

#include <algorithm>
#include <cmath>

namespace screenshot {

std::array<RegionSelection::Handle, 8> RegionSelection::handles(const QRectF& rect) {
  const auto center = rect.center();
  return {{{rect.topLeft(), Qt::TopEdge | Qt::LeftEdge},
           {{center.x(), rect.top()}, Qt::TopEdge},
           {rect.topRight(), Qt::TopEdge | Qt::RightEdge},
           {{rect.right(), center.y()}, Qt::RightEdge},
           {rect.bottomRight(), Qt::BottomEdge | Qt::RightEdge},
           {{center.x(), rect.bottom()}, Qt::BottomEdge},
           {rect.bottomLeft(), Qt::BottomEdge | Qt::LeftEdge},
           {{rect.left(), center.y()}, Qt::LeftEdge}}};
}

Qt::Edges RegionSelection::handleAt(QPointF point) const {
  constexpr qreal hitRadius = 10;
  qreal nearest = 2 * hitRadius * hitRadius + 1;
  Qt::Edges edges;
  for (const auto& handle : handles(rectangle())) {
    const auto delta = point - handle.center;
    const auto distance = QPointF::dotProduct(delta, delta);
    if (std::abs(delta.x()) <= hitRadius && std::abs(delta.y()) <= hitRadius && distance < nearest) {
      nearest = distance;
      edges = handle.edges;
    }
  }
  return edges;
}

Qt::CursorShape RegionSelection::cursorAt(QPointF point, const CaptureFrame& frame) const {
  if (canConfirm(frame)) {
    const auto edges = handleAt(point);
    const bool horizontal = edges.testAnyFlags(Qt::LeftEdge | Qt::RightEdge);
    const bool vertical = edges.testAnyFlags(Qt::TopEdge | Qt::BottomEdge);
    if (horizontal && vertical) {
      return edges.testFlag(Qt::TopEdge) == edges.testFlag(Qt::LeftEdge) ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor;
    }
    if (horizontal) {
      return Qt::SizeHorCursor;
    }
    if (vertical) {
      return Qt::SizeVerCursor;
    }
    if (rectangle().contains(point)) {
      return Qt::SizeAllCursor;
    }
  }
  return Qt::CrossCursor;
}

bool RegionSelection::begin(QPointF point, const CaptureFrame& frame, bool editable) {
  if (canceled_ || dragging()) {
    return false;
  }
  if (tool_ == Tool::Select) {
    if (editable) {
      QRectF bounds;
      if (const auto& mapping = frame.desktopMapping()) {
        for (const auto& output : mapping->outputs) {
          bounds = bounds.united(output.logicalBounds);
        }
      }
      if (bounds.isEmpty()) {
        return false;
      }
      const bool valid = canConfirm(frame);
      const auto edges = valid ? handleAt(point) : Qt::Edges{};
      const auto original = valid && (edges || rectangle().contains(point)) ? rectangle() : QRectF{};
      edit_.emplace(Edit{point, original, bounds, edges});
    }
    annotations_ = {};
    if (!edit_ || edit_->original.isEmpty()) {
      start_ = end_ = point;
    }
    dragging_ = true;
    move(point);
  } else {
    if (!rectangle().contains(point) || cropRect(frame).isEmpty()) {
      return false;
    }
    const auto& mapping = *frame.desktopMapping();
    rasterOrigin_ = mapping.rasterEdge({});
    captureScale_ = mapping.captureScale;
    const auto raster = rasterOrigin_ + point * captureScale_;
    return annotations_.begin(static_cast<Annotations::Tool>(tool_), color_, raster);
  }
  return true;
}

void RegionSelection::move(QPointF point) {
  if (dragging_ && edit_) {
    const auto& [press, original, bounds, edges] = *edit_;
    if (original.isEmpty()) {
      const auto clamp = [&bounds](QPointF position) {
        return QPointF(std::clamp(position.x(), bounds.left(), bounds.right()),
                       std::clamp(position.y(), bounds.top(), bounds.bottom()));
      };
      start_ = clamp(press);
      end_ = clamp(point);
      return;
    }

    const auto delta = point - press;
    auto rect = original;
    if (!edges) {
      rect.translate(std::clamp(delta.x(), bounds.left() - rect.left(), bounds.right() - rect.right()),
                     std::clamp(delta.y(), bounds.top() - rect.top(), bounds.bottom() - rect.bottom()));
    }
    if (edges.testFlag(Qt::LeftEdge)) {
      rect.setLeft(std::clamp(original.left() + delta.x(), bounds.left(), original.right()));
    }
    if (edges.testFlag(Qt::RightEdge)) {
      rect.setRight(std::clamp(original.right() + delta.x(), original.left(), bounds.right()));
    }
    if (edges.testFlag(Qt::TopEdge)) {
      rect.setTop(std::clamp(original.top() + delta.y(), bounds.top(), original.bottom()));
    }
    if (edges.testFlag(Qt::BottomEdge)) {
      rect.setBottom(std::clamp(original.bottom() + delta.y(), original.top(), bounds.bottom()));
    }
    start_ = rect.topLeft();
    end_ = rect.bottomRight();
  } else if (dragging_) {
    end_ = point;
  } else {
    annotations_.move(rasterOrigin_ + point * captureScale_);
  }
}

void RegionSelection::release(QPointF point) {
  if (dragging_) {
    move(point);
    dragging_ = false;
    edit_.reset();
  } else {
    annotations_.finish(rasterOrigin_ + point * captureScale_);
  }
}

void RegionSelection::undo() {
  if (canUndo()) {
    annotations_.undo();
  }
}

void RegionSelection::cancel() {
  canceled_ = true;
  dragging_ = false;
  edit_.reset();
  annotations_ = {};
  start_ = end_ = {};
}

QRectF RegionSelection::rectangle() const { return QRectF(start_, end_).normalized(); }

QRect RegionSelection::cropRect(const CaptureFrame& frame) const { return dragging_ ? QRect{} : liveCropRect(frame); }

QRect RegionSelection::liveCropRect(const CaptureFrame& frame) const {
  const auto& mapping = frame.desktopMapping();
  const auto selection = rectangle();
  if (canceled_ || selection.isEmpty() || !mapping || std::ranges::none_of(mapping->outputs, [&](const auto& output) {
        return selection.intersects(output.logicalBounds);
      })) {
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
  annotations_.paint(painter, cropRect(frame), frame.colorSpace(), captureScale_, includeGesture);
}

QImage RegionSelection::confirm(const CaptureFrame& frame) const {
  const auto pixels = cropRect(frame);
  if (dragging() || pixels.isEmpty()) {
    return {};
  }
  return annotations_.render(frame.image().copy(pixels), captureScale_, pixels.topLeft());
}

}  // namespace screenshot
