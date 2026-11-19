#include "capture_controller.h"

#include <utility>

namespace screenshot {

CaptureController::CaptureController(DesktopAcquisition& acquisition, std::chrono::milliseconds delay,
                                     CursorPolicy cursor, std::unique_ptr<QTimer> timer)
    : acquisition_(acquisition), timer_(std::move(timer)) {
  connect(&acquisition_, &DesktopAcquisition::captured, this, [this](const CaptureFrame& frame) {
    if (state_ == State::Acquiring) {
      state_ = State::Finished;
      emit captured(frame);
    }
  });
  connect(&acquisition_, &DesktopAcquisition::failed, this, [this](const QString& reason) {
    if (state_ == State::Acquiring) {
      state_ = State::Finished;
      emit failed(reason);
    }
  });
  connect(timer_.get(), &QTimer::timeout, this, [this, cursor] {
    if (state_ == State::Waiting) {
      state_ = State::Acquiring;
      acquisition_.start(cursor);
    }
  });
  timer_->setSingleShot(true);
  timer_->setTimerType(Qt::PreciseTimer);
  timer_->start(delay);
}

CaptureController::~CaptureController() { cancel(); }

void CaptureController::cancel() {
  const auto previous = std::exchange(state_, State::Finished);
  if (previous == State::Finished) {
    return;
  }
  timer_->stop();
  if (previous == State::Acquiring) {
    acquisition_.cancel();
  }
  emit canceled();
}

}  // namespace screenshot
