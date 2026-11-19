#pragma once

#include <QColor>
#include <QColorSpace>
#include <QImage>
#include <QList>
#include <QRectF>
#include <QString>

#include <optional>

namespace screenshot {

enum class OutputTransform { Normal, Rotate90, Rotate180, Rotate270, Flipped, Flipped90, Flipped180, Flipped270 };

struct OutputGeometry {
  QString name;
  QRectF logicalBounds;
  OutputTransform transform;
  qreal scale;
  QRect rasterBounds;
};

struct DesktopMapping {
  QList<OutputGeometry> outputs;
  qreal captureScale;
  [[nodiscard]] QPointF rasterEdge(QPointF desktopEdge) const;
};

class CaptureFrame {
 public:
  explicit CaptureFrame(const QImage& uprightImage, std::optional<DesktopMapping> mapping = std::nullopt);
  [[nodiscard]] const QImage& image() const { return image_; }
  [[nodiscard]] QColorSpace colorSpace() const { return image_.colorSpace(); }
  [[nodiscard]] const std::optional<DesktopMapping>& desktopMapping() const { return mapping_; }

  [[nodiscard]] QColor sampleSrgb(QPointF desktopPoint) const;

 private:
  QImage image_;
  std::optional<DesktopMapping> mapping_;
};

}  // namespace screenshot
