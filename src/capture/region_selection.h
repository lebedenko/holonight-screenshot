#pragma once

#include "annotations.h"
#include "capture_frame.h"

#include <array>

namespace screenshot {

class RegionSelection {
 public:
  enum class Tool { Select, Pen, Arrow, Rectangle };

  struct Handle {
    QPointF center;
    Qt::Edges edges;
  };

  bool begin(QPointF point, const CaptureFrame& frame, bool editable = false);
  [[nodiscard]] static std::array<Handle, 8> handles(const QRectF& rectangle);
  [[nodiscard]] Qt::CursorShape cursorAt(QPointF point, const CaptureFrame& frame) const;
  [[nodiscard]] QRect liveCropRect(const CaptureFrame& frame) const;
  void setTool(Tool tool) { tool_ = tool; }
  void setColor(QColor color) { color_ = color; }
  [[nodiscard]] Tool tool() const { return tool_; }
  [[nodiscard]] QColor color() const { return color_; }
  [[nodiscard]] bool dragging() const { return dragging_ || annotations_.active(); }
  [[nodiscard]] bool canUndo() const { return !dragging() && annotations_.canUndo(); }
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
  [[nodiscard]] Qt::Edges handleAt(QPointF point) const;

  struct Edit {
    QPointF press;
    QRectF original;
    QRectF bounds;
    Qt::Edges edges;
  };

  Tool tool_ = Tool::Select;
  QColor color_ = Qt::red;
  Annotations annotations_;
  QPointF rasterOrigin_;
  qreal captureScale_ = 1;
  std::optional<Edit> edit_;
  QPointF start_;
  QPointF end_;
  bool dragging_ = false;
  bool canceled_ = false;
};

}  // namespace screenshot
