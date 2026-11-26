#pragma once

#include <QString>

namespace screenshot {

struct AnnotationDefaults {
  int strokeWidth = 3;
  QString textFontFamily;
  int textFontWeight = 400;
  int textFontSize = 24;
};

struct Config {
  QString saveDirectory;
  bool closeOnRegionCancel = true;
  bool closeAfterCopy = true;
  bool closeAfterSave = true;
  AnnotationDefaults annotations;
};

QString configPath();
QString statePath(const QString& fileName);
QString saveDialogDirectory(const QString& configured);
Config loadConfig();

}  // namespace screenshot
