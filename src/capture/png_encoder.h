#pragma once

#include <QImage>

class QIODevice;

namespace screenshot {

QString encodePng(QIODevice& device, const QImage& image);
QString savePng(const QString& path, const QImage& image);

}  // namespace screenshot
