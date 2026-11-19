#include "capture_controller.h"
#include "grim_acquisition.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QSaveFile>
#include <QTextStream>

#include <cstdlib>
#include <limits>

namespace {

int reportFailure(const QString& reason) {
  QTextStream(stderr) << reason << Qt::endl;
  return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
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
  parser.addOptions({output, delay, cursor});
  parser.process(app);

  const auto path = parser.value(output);
  if (parser.values(output).size() != 1 || path.isEmpty() || !parser.positionalArguments().isEmpty()) {
    return reportFailure(QStringLiteral("Specify exactly one --output PATH and no positional arguments."));
  }
  bool validDelay = false;
  const auto delayMs = parser.value(delay).toInt(&validDelay);
  if (!validDelay || delayMs < 0 || parser.values(delay).size() > 1) {
    return reportFailure(
        QStringLiteral("--delay-ms must be an integer from 0 through %1.").arg(std::numeric_limits<int>::max()));
  }

  screenshot::GrimAcquisition acquisition;
  screenshot::CaptureController controller(
      acquisition, std::chrono::milliseconds(delayMs),
      parser.isSet(cursor) ? screenshot::CursorPolicy::Include : screenshot::CursorPolicy::Exclude);
  QObject::connect(&controller, &screenshot::CaptureController::failed, &app,
                   [&](const QString& reason) { app.exit(reportFailure(reason)); });
  QObject::connect(
      &controller, &screenshot::CaptureController::captured, &app, [&](const screenshot::CaptureFrame& frame) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || !frame.image().save(&file, "PNG") || !file.commit()) {
          app.exit(reportFailure(QStringLiteral("Could not save PNG to %1: %2").arg(path, file.errorString())));
          return;
        }
        app.exit(EXIT_SUCCESS);
      });
  return app.exec();
}
