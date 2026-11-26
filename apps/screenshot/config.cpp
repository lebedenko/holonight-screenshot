#include "config.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <toml++/toml.hpp>

namespace screenshot {
namespace {

QString appPath(const char* variable, const QString& fallback, const QString& fileName) {
  auto directory = qEnvironmentVariable(variable);
  if (!QDir::isAbsolutePath(directory)) {
    directory = QDir::homePath() + fallback;
  }
  return QDir(directory).filePath("holonight-screenshot/" + fileName);
}

template <typename T, typename Validate>
void readValue(const toml::table& config, std::string_view section, std::string_view key, T& destination,
               Validate validate) {
  const auto value = config[section][key];
  if (!value) {
    return;
  }
  if (const auto parsed = value.value_exact<T>(); parsed && validate(*parsed)) {
    destination = *parsed;
    return;
  }
  qWarning() << "Invalid screenshot setting:" << section << key;
}

}  // namespace

QString configPath() { return appPath("XDG_CONFIG_HOME", "/.config", "config.toml"); }

QString statePath(const QString& fileName) { return appPath("XDG_STATE_HOME", "/.local/state", fileName); }

QString saveDialogDirectory(const QString& configured) {
  const auto pictures = QDir::home().filePath("Pictures");
  for (const auto& path : {configured, pictures}) {
    const QFileInfo folder(path);
    if (folder.isDir() && folder.isReadable() && folder.isWritable() && folder.isExecutable()) {
      return path;
    }
  }
  return QDir::homePath();
}

Config loadConfig() {
  Config result;
  QFile file(configPath());
  if (!file.exists()) {
    return result;
  }
  if (!file.open(QIODevice::ReadOnly)) {
    qWarning() << "Could not read screenshot config" << file.fileName() << file.errorString();
    return result;
  }

  toml::table config;
  try {
    const auto contents = file.readAll();
    if (file.error() != QFile::NoError) {
      qWarning() << "Could not read screenshot config" << file.fileName() << file.errorString();
      return result;
    }
    config = toml::parse(std::string_view(contents.constData(), contents.size()));
  } catch (const toml::parse_error& error) {
    qWarning() << "Could not parse screenshot config" << file.fileName() << error.description();
    return result;
  }

  for (const auto* section : {"general", "editor"}) {
    if (config[section] && !config[section].is_table()) {
      qWarning() << "Invalid screenshot config section:" << section;
    }
  }
  std::string directory;
  readValue(config, "general", "save_directory", directory, [](const std::string& value) {
    return value.starts_with("~/") || QDir::isAbsolutePath(QString::fromStdString(value));
  });
  result.saveDirectory = directory.starts_with("~/")
                             ? QDir::home().filePath(QString::fromStdString(directory.substr(2)))
                             : QString::fromStdString(directory);
  const auto boolean = [](bool) { return true; };
  readValue(config, "general", "close_on_region_cancel", result.closeOnRegionCancel, boolean);
  readValue(config, "general", "close_after_copy", result.closeAfterCopy, boolean);
  readValue(config, "general", "close_after_save", result.closeAfterSave, boolean);

  const auto pixels = [](int64_t value) { return value >= 1 && value <= 256; };
  int64_t stroke = result.annotations.strokeWidth;
  int64_t size = result.annotations.textFontSize;
  int64_t weight = result.annotations.textFontWeight;
  readValue(config, "editor", "stroke_width", stroke, pixels);
  readValue(config, "editor", "text_font_size", size, pixels);
  readValue(config, "editor", "text_font_weight", weight, [](int64_t value) { return value >= 1 && value <= 1000; });
  std::string family;
  readValue(config, "editor", "text_font_family", family,
            [](const std::string& value) { return !QString::fromStdString(value).trimmed().isEmpty(); });
  result.annotations = {static_cast<int>(stroke), QString::fromStdString(family), static_cast<int>(weight),
                        static_cast<int>(size)};
  return result;
}

}  // namespace screenshot
