#include "capture_controller.h"
#include "grim_acquisition.h"
#include "region_overlay.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QSaveFile>
#include <QTextStream>

#include <cstdlib>
#include <limits>
#include <wayland-client.h>

namespace {

int reportFailure(const QString& reason) {
  QTextStream(stderr) << reason << Qt::endl;
  return EXIT_FAILURE;
}

int savePng(const QString& path, const QImage& image) {
  QSaveFile file(path);
  if (!file.open(QIODevice::WriteOnly) || !image.save(&file, "PNG") || !file.commit()) {
    return reportFailure(QStringLiteral("Could not save PNG to %1: %2").arg(path, file.errorString()));
  }
  return EXIT_SUCCESS;
}

struct Options {
  QString path;
  int delayMs;
  screenshot::CursorPolicy cursor;
  bool region;
};

std::optional<Options> parseOptions(int argc, char** argv) {
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
  const QCommandLineOption region(QStringLiteral("region"), QStringLiteral("Select a region of the frozen desktop."));
  parser.addOptions({output, delay, cursor, region});
  parser.process(app);

  const auto path = parser.value(output);
  if (parser.values(output).size() != 1 || path.isEmpty() || !parser.positionalArguments().isEmpty()) {
    reportFailure(QStringLiteral("Specify exactly one --output PATH and no positional arguments."));
    return std::nullopt;
  }
  bool validDelay = false;
  const auto delayMs = parser.value(delay).toInt(&validDelay);
  if (!validDelay || delayMs < 0 || parser.values(delay).size() > 1) {
    reportFailure(
        QStringLiteral("--delay-ms must be an integer from 0 through %1.").arg(std::numeric_limits<int>::max()));
    return std::nullopt;
  }

  return Options{path, delayMs,
                 parser.isSet(cursor) ? screenshot::CursorPolicy::Include : screenshot::CursorPolicy::Exclude,
                 parser.isSet(region)};
}

int capture(QCoreApplication& app, const Options& options) {
  screenshot::GrimAcquisition acquisition;
  std::unique_ptr<screenshot::RegionOverlay> overlay;
  screenshot::CaptureController controller(acquisition, std::chrono::milliseconds(options.delayMs), options.cursor);
  const auto fail = [&](const QString& reason) { app.exit(reportFailure(reason)); };
  QObject::connect(&controller, &screenshot::CaptureController::failed, &app, fail);
  QObject::connect(
      &controller, &screenshot::CaptureController::captured, &app, [&](const screenshot::CaptureFrame& frame) {
        if (!options.region) {
          app.exit(savePng(options.path, frame.image()));
          return;
        }
        overlay = std::make_unique<screenshot::RegionOverlay>(frame, acquisition);
        QObject::connect(overlay.get(), &screenshot::RegionOverlay::failed, &app, fail, Qt::QueuedConnection);
        QObject::connect(
            overlay.get(), &screenshot::RegionOverlay::canceled, &app, [&] { app.exit(2); }, Qt::QueuedConnection);
        QObject::connect(
            overlay.get(), &screenshot::RegionOverlay::selected, &app,
            [&](const QImage& crop) { app.exit(savePng(options.path, crop)); }, Qt::QueuedConnection);
        overlay->open();
      });
  return app.exec();
}

}  // namespace

int main(int argc, char** argv) {
  const auto options = parseOptions(argc, argv);
  if (!options) {
    return EXIT_FAILURE;
  }

  // Application options have already been consumed. In particular, a destination
  // named like a Qt platform option must only be interpreted as a path.
  static int appArgc = 1;
  static char* appArgv[] = {argv[0], nullptr};
  if (!options->region) {
    QCoreApplication app(appArgc, appArgv);
    return capture(app, *options);
  }

  auto platform = qgetenv("QT_QPA_PLATFORM");
  if (platform.isEmpty()) {
    platform = "wayland";
  }
  // QGuiApplication aborts when it cannot connect to the platform display.
  // Check the Wayland connection so a missing session is a normal CLI failure.
  if (platform.startsWith("wayland")) {
    // A supplied WAYLAND_SOCKET is a single-use connection owned by Qt.
    if (!qEnvironmentVariableIsSet("WAYLAND_SOCKET")) {
      auto* display = wl_display_connect(nullptr);
      if (!display) {
        return reportFailure(QStringLiteral("Could not connect to Wayland for region selection."));
      }
      wl_display_disconnect(display);
    }
  }
  qputenv("QT_QPA_PLATFORM", platform);
  // The layer-surface host assigns the Wayland role itself. Honor its window
  // flag so Qt does not create an xdg-toplevel role for the same surface.
  qputenv("QT_WAYLAND_USE_BYPASSWINDOWMANAGERHINT", "1");
  // Keep the Wayland display alive through destruction of the layer-shell singleton.
  static QGuiApplication app(appArgc, appArgv);
  app.setQuitOnLastWindowClosed(false);
  return capture(app, *options);
}
