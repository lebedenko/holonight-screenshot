#pragma once

#include "desktop_acquisition.h"

#include <QTimer>

#include <chrono>
#include <memory>

namespace screenshot {

class CaptureController final : public QObject {
  Q_OBJECT
 public:
  CaptureController(DesktopAcquisition& acquisition, std::chrono::milliseconds delay, CursorPolicy cursor,
                    std::unique_ptr<QTimer> timer = std::make_unique<QTimer>());
  ~CaptureController() override;
  void cancel();

 signals:
  void captured(const screenshot::CaptureFrame& frame);
  void canceled();
  void failed(const QString& reason);

 private:
  enum class State { Waiting, Acquiring, Finished };
  DesktopAcquisition& acquisition_;
  std::unique_ptr<QTimer> timer_;
  State state_ = State::Waiting;
};

}  // namespace screenshot
