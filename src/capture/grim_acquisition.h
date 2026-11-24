#pragma once

#include "capture_session.h"
#include "desktop_acquisition.h"

#include <QJsonArray>
#include <QProcess>
#include <QTimer>

namespace screenshot {

class GrimAcquisition final : public DesktopAcquisition {
  Q_OBJECT
 public:
  GrimAcquisition();
  GrimAcquisition(const CaptureSession& session, CaptureTarget target, QString windowId = {});
  void start(CursorPolicy cursor) override;
  void cancel() override;
  void verifyLayout();

 signals:
  void layoutVerified();
  void layoutVerificationFailed(const QString& reason);

 private:
  enum class Stage { Ready, Before, Capture, Focus, Window, After, Done, Verifying };
  explicit GrimAcquisition(bool wlr);
  void queryLayout();
  void captureWindow();
  void run(const QString& program, const QStringList& arguments, int timeoutMs);
  void finished(int exitCode, QProcess::ExitStatus status);
  void fail(const QString& reason);
  void stop();
  void publishFailure();

  const bool wlr_;
  const CaptureSession* session_ = nullptr;
  CaptureTarget target_ = CaptureTarget::Screen;
  QString windowId_;
  QProcess process_;
  QTimer timeout_;
  Stage stage_ = Stage::Ready;
  CursorPolicy cursor_ = CursorPolicy::Exclude;
  QJsonArray layout_;
  DesktopMapping mapping_;
  QSize canvas_;
  QByteArray png_;
  QString pendingFailure_;
  bool layoutFailure_ = false;
};

}  // namespace screenshot
