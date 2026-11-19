#pragma once

#include "capture_frame.h"

#include <QColor>
#include <QPolygonF>

class QColorTransform;
class QPainter;

namespace screenshot {

class RegionSelection {
 public:
  enum class Tool { Select, Pen, Arrow, Rectangle };

  bool begin(QPointF point, const CaptureFrame& frame);
  void setTool(Tool tool) { tool_ = tool; }
  void setColor(QColor color) { color_ = color; }
  [[nodiscard]] Tool tool() const { return tool_; }
  [[nodiscard]] QColor color() const { return color_; }
  [[nodiscard]] bool dragging() const { return dragging_ || gesture_.has_value(); }
  [[nodiscard]] bool canUndo() const { return !dragging() && !marks_.isEmpty(); }
  void undo();
  void move(QPointF point);
  void release(QPointF point);
  void cancel();
  [[nodiscard]] QRectF rectangle() const;
  [[nodiscard]] QRect cropRect(const CaptureFrame& frame) const;
  [[nodiscard]] bool canConfirm(const CaptureFrame& frame) const { return !dragging() && !cropRect(frame).isEmpty(); }
  void paintAnnotations(QPainter& painter, const CaptureFrame& frame, bool includeGesture) const;
  [[nodiscard]] QImage confirm(const CaptureFrame& frame) const;

 private:
  struct Mark {
    Tool tool;
    QColor color;
    QPolygonF points;
    [[nodiscard]] bool hasArea() const;
    void paint(QPainter& painter, qreal scale, const QColorTransform& colorTransform) const;
  };

  Tool tool_ = Tool::Select;
  QColor color_ = Qt::red;
  QList<Mark> marks_;
  std::optional<Mark> gesture_;
  QPointF rasterOrigin_;
  qreal captureScale_ = 1;
  QPointF start_;
  QPointF end_;
  bool dragging_ = false;
  bool canceled_ = false;
};

}  // namespace screenshot
