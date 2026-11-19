#include "png_encoder.h"

#include <QImageWriter>
#include <QSaveFile>

namespace screenshot {
QString encodePng(QIODevice& device, const QImage& image) {
  QImageWriter writer(&device, "PNG");
  return writer.write(image) ? QString() : writer.errorString();
}

QString savePng(const QString& path, const QImage& image) {
  QSaveFile file(path);
  if (file.open(QIODevice::WriteOnly)) {
    const auto error = encodePng(file, image);
    if (!error.isEmpty()) {
      return QStringLiteral("Could not save PNG to %1: %2").arg(path, error);
    }
    if (file.commit()) {
      return {};
    }
  }
  return QStringLiteral("Could not save PNG to %1: %2").arg(path, file.errorString());
}

}  // namespace screenshot
