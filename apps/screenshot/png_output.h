#pragma once

#include <QImage>

namespace screenshot {

[[nodiscard]] QString savePng(const QString& path, const QImage& image);
[[nodiscard]] QString copyPng(const QImage& image);
[[nodiscard]] QString choosePngPath(const QString& directory);

}  // namespace screenshot
