#pragma once

#include <QColor>
#include <QImage>
#include <QPolygonF>

#include <optional>

class QColorSpace;
class QColorTransform;
class QPainter;

namespace screenshot {

class Annotations {
 public:
  enum class Tool { Select, Pen, Arrow, Rectangle };

  bool begin(Tool tool, QColor color, QPointF point);
  void move(QPointF point);
  void finish(QPointF point);
  void cancelStroke() { gesture_.reset(); }
  void undo();
  [[nodiscard]] bool active() const { return gesture_.has_value(); }
  [[nodiscard]] bool canUndo() const { return !active() && !marks_.isEmpty(); }
  [[nodiscard]] bool empty() const { return marks_.isEmpty() && !active(); }
  [[nodiscard]] static QPointF imagePoint(QPointF point, QRectF bounds, QSize size);
  void paint(QPainter& painter, QRectF clip, const QColorSpace& colorSpace, qreal scale, bool includeGesture) const;
  [[nodiscard]] QImage render(QImage image, qreal scale = 1, QPoint origin = {}) const;

 private:
  struct Mark {
    Tool tool;
    QColor color;
    QPolygonF points;
    [[nodiscard]] bool hasArea() const;
    void paint(QPainter& painter, qreal scale, const QColorTransform& colorTransform) const;
  };

  QList<Mark> marks_;
  std::optional<Mark> gesture_;
};

}  // namespace screenshot
