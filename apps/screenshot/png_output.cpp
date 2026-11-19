#include "png_output.h"

#include "png_encoder.h"

#include <QBuffer>
#include <QDateTime>
#include <QDeadlineTimer>
#include <QDir>
#include <QFileDialog>
#include <QProcess>
#include <QStandardPaths>

namespace screenshot {

QString copyPng(const QImage& image) {
  QBuffer buffer;
  buffer.open(QIODevice::WriteOnly);
  if (const auto error = encodePng(buffer, image); !error.isEmpty()) {
    return QStringLiteral("Could not encode clipboard PNG: %1").arg(error);
  }

  QProcess helper;
  helper.setStandardOutputFile(QProcess::nullDevice());
  helper.setStandardErrorFile(QProcess::nullDevice());
  qint64 written = 0;
  QObject::connect(&helper, &QProcess::bytesWritten, &helper, [&](qint64 bytes) { written += bytes; });
  QDeadlineTimer deadline(5000);
  helper.start(QStringLiteral("wl-copy"), {QStringLiteral("--type"), QStringLiteral("image/png")});
  if (!helper.waitForStarted(static_cast<int>(deadline.remainingTime()))) {
    return QStringLiteral("Could not start wl-copy: %1").arg(helper.errorString());
  }
  helper.write(buffer.data());
  helper.closeWriteChannel();
  if (!helper.waitForFinished(static_cast<int>(deadline.remainingTime()))) {
    if (helper.state() != QProcess::NotRunning) {
      helper.kill();
      helper.waitForFinished(1000);
    }
    return QStringLiteral("wl-copy did not finish successfully: %1").arg(helper.errorString());
  }
  if (helper.exitStatus() != QProcess::NormalExit || helper.exitCode() != 0 || written != buffer.size()) {
    return QStringLiteral("wl-copy failed to accept the PNG.");
  }
  return {};
}

QString choosePngPath() {
  auto directory = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
  if (!QDir(directory).exists() || directory.isEmpty()) {
    directory = QDir::homePath();
  }
  QFileDialog dialog;
  dialog.setWindowTitle(QStringLiteral("Save Screenshot"));
  dialog.setDirectory(directory);
  dialog.setNameFilter(QStringLiteral("PNG images (*.png)"));
  dialog.setAcceptMode(QFileDialog::AcceptSave);
  dialog.setFileMode(QFileDialog::AnyFile);
  dialog.setDefaultSuffix(QStringLiteral("png"));
  dialog.selectFile(QDateTime::currentDateTime().toString(QStringLiteral("'Screenshot 'yyyy-MM-dd HH-mm-ss'.png'")));
  return dialog.exec() == QDialog::Accepted ? dialog.selectedFiles().value(0) : QString();
}

}  // namespace screenshot
