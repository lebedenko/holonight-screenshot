#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <array>
#include <functional>
#include <memory>

namespace screenshot {

enum class CaptureTarget { Screen, Area, Window, ActiveWindow };
enum class FocusLookup { None, Hyprland, Sway };

inline constexpr std::array captureTargets{CaptureTarget::Screen, CaptureTarget::Area, CaptureTarget::Window,
                                           CaptureTarget::ActiveWindow};
QString targetName(CaptureTarget target);

struct CaptureCapabilities {
  std::array<QString, captureTargets.size()> unavailable;
  QString reason(CaptureTarget target) const { return unavailable.at(static_cast<size_t>(target)); }
  bool supports(CaptureTarget target) const { return reason(target).isEmpty(); }
  bool wlrLayout = false;
  FocusLookup focus = FocusLookup::None;
};

struct CaptureWindow {
  QString id;
  QString title;
  QString appId;
};

class CaptureSession final : public QObject {
  Q_OBJECT
 public:
  explicit CaptureSession(QObject* parent = nullptr);
  ~CaptureSession() override;
  void discover();
  const CaptureCapabilities& capabilities() const;
  QList<CaptureWindow> windows() const;
  bool containsWindow(const QString& id) const;

  // Wait for requests already sent on Qt's display, including surface removal.
  static void afterHide(QObject* context, std::function<void(bool)> done);

 signals:
  void discovered();
  void windowsChanged();

 private:
  struct State;
  std::unique_ptr<State> state_;
};

}  // namespace screenshot
