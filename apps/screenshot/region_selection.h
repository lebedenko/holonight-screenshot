#pragma once

#include "capture_frame.h"

namespace screenshot {

class RegionSelection {
 public:
  void begin(QPointF point);
  void move(QPointF point);
  void release(QPointF point);
  void cancel();
  [[nodiscard]] QRectF rectangle() const;
  [[nodiscard]] QRect cropRect(const CaptureFrame& frame) const;
  [[nodiscard]] QImage confirm(const CaptureFrame& frame) const;

 private:
  QPointF start_;
  QPointF end_;
  bool dragging_ = false;
  bool canceled_ = false;
};

}  // namespace screenshot
