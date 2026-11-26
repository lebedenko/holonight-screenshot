#include "workflow.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QIcon>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <limits>
#include <wayland-client.h>

namespace {

using screenshot::CaptureTarget;

int reportFailure(const QString& reason) {
  QTextStream(stderr) << reason << Qt::endl;
  return EXIT_FAILURE;
}

std::optional<screenshot::Options> parseOptions(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("holonight-screenshot"));
  app.setApplicationVersion(QStringLiteral(SCREENSHOT_VERSION));
  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("Capture a HoloNight desktop screenshot."));
  parser.addHelpOption();
  parser.addVersionOption();
  const QCommandLineOption output(QStringLiteral("output"), QStringLiteral("Save the PNG to PATH."),
                                  QStringLiteral("PATH"));
  const QCommandLineOption delay(QStringLiteral("delay-ms"), QStringLiteral("Wait N milliseconds before capture."),
                                 QStringLiteral("N"), QStringLiteral("0"));
  const QCommandLineOption cursor(QStringLiteral("include-cursor"), QStringLiteral("Include the pointer."));
  const QCommandLineOption region(QStringLiteral("region"), QStringLiteral("Alias for --target area."));
  const QCommandLineOption target(QStringLiteral("target"),
                                  QStringLiteral("Capture screen, area, window, or active-window."),
                                  QStringLiteral("TARGET"));
  const QCommandLineOption list(QStringLiteral("list-targets"),
                                QStringLiteral("Print supported targets as a JSON array."));
  const QCommandLineOption editor(QStringLiteral("editor"), QStringLiteral("Open the annotation editor."));
  parser.addOptions({output, delay, cursor, region, target, list, editor});
  parser.process(app);

  screenshot::Options options;
  options.path = parser.value(output);
  options.list = parser.isSet(list);
  options.editor = parser.isSet(editor);
  if (parser.values(output).size() > 1 || (parser.isSet(output) && options.path.isEmpty()) ||
      !parser.positionalArguments().isEmpty()) {
    reportFailure(QStringLiteral("Specify --output PATH at most once. Positional arguments are invalid."));
    return std::nullopt;
  }
  if (options.list && (parser.isSet(output) || parser.isSet(delay) || parser.isSet(cursor) || parser.isSet(region) ||
                       parser.isSet(target) || options.editor)) {
    reportFailure(QStringLiteral("--list-targets cannot be combined with capture options."));
    return std::nullopt;
  }
  if (parser.isSet(target)) {
    const auto name = parser.value(target);
    const auto mode = std::ranges::find(screenshot::captureTargets, name, screenshot::targetName);
    if (parser.values(target).size() != 1 || mode == screenshot::captureTargets.end()) {
      reportFailure(QStringLiteral("--target must be one of screen, area, window, or active-window."));
      return std::nullopt;
    }
    options.target = *mode;
  }
  if (parser.isSet(region)) {
    if (options.target && options.target != CaptureTarget::Area) {
      reportFailure(QStringLiteral("--region conflicts with the requested --target."));
      return std::nullopt;
    }
    options.target = CaptureTarget::Area;
  } else if (!options.target &&
             (parser.isSet(output) || parser.isSet(delay) || parser.isSet(cursor) || options.editor)) {
    options.target = CaptureTarget::Screen;
  }
  bool validDelay = false;
  options.delayMs = parser.value(delay).toInt(&validDelay);
  if (!validDelay || options.delayMs < 0 || parser.values(delay).size() > 1) {
    reportFailure(
        QStringLiteral("--delay-ms must be an integer from 0 through %1.").arg(std::numeric_limits<int>::max()));
    return std::nullopt;
  }
  options.cursor = parser.isSet(cursor) ? screenshot::CursorPolicy::Include : screenshot::CursorPolicy::Exclude;
  return options;
}

int run(QCoreApplication& app, const screenshot::Options& options) {
  app.setApplicationName(QStringLiteral("holonight-screenshot"));
  screenshot::Workflow workflow(options);
  QTimer::singleShot(0, &workflow, &screenshot::Workflow::start);
  return app.exec();
}

}  // namespace

int main(int argc, char** argv) {
  auto options = parseOptions(argc, argv);
  if (!options) {
    return EXIT_FAILURE;
  }

  // Qt must not interpret a destination named like a platform option as an option.
  static int appArgc = 1;
  static char* appArgv[] = {argv[0], nullptr};
  const bool headlessCapture =
      !options->editor && (options->target == CaptureTarget::Screen || options->target == CaptureTarget::ActiveWindow);
  if (options->list || headlessCapture) {
    QCoreApplication app(appArgc, appArgv);
    return run(app, *options);
  }

  const auto platform =
      qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM") ? QByteArray("wayland") : qgetenv("QT_QPA_PLATFORM");
  if (platform.startsWith("wayland") && !qEnvironmentVariableIsSet("WAYLAND_SOCKET")) {
    auto* display = wl_display_connect(nullptr);
    if (!display) {
      return reportFailure(QStringLiteral("Could not connect to Wayland."));
    }
    wl_display_disconnect(display);
  }
  qputenv("QT_QPA_PLATFORM", platform);
  QApplication app(appArgc, appArgv);
  app.setWindowIcon(QIcon(QStringLiteral(":/screenshot/holonight-screenshot.svg")));
  const auto desktopId = QStringLiteral("org.holonight.Screenshot");
  if (!QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopId + ".desktop").isEmpty()) {
    app.setDesktopFileName(desktopId);
  }
  app.setQuitOnLastWindowClosed(false);
  return run(app, *options);
}
