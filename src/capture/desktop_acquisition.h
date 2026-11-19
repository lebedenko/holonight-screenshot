#pragma once

#include "capture_frame.h"

#include <QObject>

namespace screenshot {

enum class CursorPolicy { Exclude, Include };

// One operation per instance. Start, cancel and completion run on the controller's thread.
class DesktopAcquisition : public QObject {
  Q_OBJECT
 public:
  virtual void start(CursorPolicy cursor) = 0;
  virtual void cancel() = 0;

 signals:
  void captured(const screenshot::CaptureFrame& frame);
  void failed(const QString& reason);
};

}  // namespace screenshot
