#pragma once

#include "desktop_acquisition.h"

#include <QJsonArray>
#include <QProcess>
#include <QTimer>

namespace screenshot {

class GrimAcquisition final : public DesktopAcquisition {
 public:
  GrimAcquisition();
  void start(CursorPolicy cursor) override;
  void cancel() override;

 private:
  enum class Stage { Ready, Before, Capture, After, Done };
  void run(const QString& program, const QStringList& arguments, int timeoutMs);
  void finished(int exitCode, QProcess::ExitStatus status);
  void fail(const QString& reason);
  void stop();
  void publishFailure();

  QProcess process_;
  QTimer timeout_;
  Stage stage_ = Stage::Ready;
  CursorPolicy cursor_ = CursorPolicy::Exclude;
  QJsonArray layout_;
  DesktopMapping mapping_;
  QSize canvas_;
  QByteArray png_;
  QString pendingFailure_;
};

}  // namespace screenshot
