#include "grim_acquisition.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace screenshot {
namespace {

constexpr int layoutTimeoutMs = 2000;
constexpr int captureTimeoutMs = 15000;
// Keep the same precision for grim's argument and the raster calculations.
constexpr int scalePrecision = std::numeric_limits<double>::max_digits10;

bool usesWlrOutputManagement() {
  const auto desktops = qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(':');
  return desktops.contains(QStringLiteral("sway"), Qt::CaseInsensitive) ||
         desktops.contains(QStringLiteral("labwc"), Qt::CaseInsensitive);
}

QJsonObject normalizeWlrOutput(const QJsonObject& output) {
  const auto enabled = output.value("enabled");
  if (!enabled.isBool()) {
    return {};
  }
  if (!enabled.toBool()) {
    return {{"disabled", true}};
  }
  static const QStringList transforms{QStringLiteral("normal"),      QStringLiteral("90"),
                                      QStringLiteral("180"),         QStringLiteral("270"),
                                      QStringLiteral("flipped"),     QStringLiteral("flipped-90"),
                                      QStringLiteral("flipped-180"), QStringLiteral("flipped-270")};
  QJsonObject currentMode;
  for (const auto& entry : output.value("modes").toArray()) {
    const auto mode = entry.toObject();
    const auto current = mode.value("current");
    if (!current.isBool() || (current.toBool() && !currentMode.isEmpty())) {
      return {};
    }
    if (current.toBool()) {
      currentMode = mode;
    }
  }
  const auto position = output.value("position").toObject();
  return {{"disabled", false},
          {"name", output.value("name")},
          {"width", currentMode.value("width")},
          {"height", currentMode.value("height")},
          {"x", position.value("x")},
          {"y", position.value("y")},
          {"transform", static_cast<int>(transforms.indexOf(output.value("transform").toString()))},
          // wlr-randr prints six decimals from a Wayland 24.8 fixed-point scale.
          {"scale", std::round(output.value("scale").toDouble() * 256) / 256},
          {"identity", QJsonArray{output.value("make"), output.value("model"), output.value("serial"),
                                  currentMode.value("refresh")}}};
}

struct Layout {
  QJsonArray identity;
  DesktopMapping mapping{{}, 0};
  QSize canvas;
};

bool integerInRange(double value, int minimum, int maximum) {
  return value >= minimum && value <= maximum && std::trunc(value) == value;
}

std::optional<Layout> parseLayout(const QByteArray& json, bool wlr) {
  const auto monitors = QJsonDocument::fromJson(json).array();
  QSet<QString> names;
  QSet<int> ids;
  QList<QJsonObject> active;
  for (const auto& monitor : monitors) {
    const auto object = wlr ? normalizeWlrOutput(monitor.toObject()) : monitor.toObject();
    const auto disabled = object.value("disabled");
    if (!disabled.isBool()) {
      return std::nullopt;
    }
    if (!disabled.toBool()) {
      active.append(object);
    }
  }
  std::ranges::sort(active, {}, [](const QJsonObject& object) { return object.value("name").toString(); });

  Layout layout;
  QRectF bounds;
  constexpr int max = std::numeric_limits<int>::max();
  constexpr int min = std::numeric_limits<int>::min();
  for (const auto& object : active) {
    const auto name = object.value("name").toString();
    const auto id = object.value("id").toDouble(-1);
    const auto width = object.value("width").toDouble(-1);
    const auto height = object.value("height").toDouble(-1);
    const auto x = object.value("x").toDouble(NAN);
    const auto y = object.value("y").toDouble(NAN);
    const auto transform = object.value("transform").toDouble(-1);
    const auto scale = object.value("scale").toDouble();
    if (name.isEmpty() || names.contains(name) || !integerInRange(width, 1, max) || !integerInRange(height, 1, max) ||
        !integerInRange(x, min, max) || !integerInRange(y, min, max) || !integerInRange(transform, 0, 7) ||
        !std::isfinite(scale) || scale <= 0 ||
        (!wlr &&
         (!integerInRange(id, 0, max) || ids.contains(static_cast<int>(id)) ||
          object.value("mirrorOf").toString() != QStringLiteral("none") || !object.value("dpmsStatus").toBool()))) {
      return std::nullopt;
    }
    names.insert(name);
    if (!wlr) {
      ids.insert(static_cast<int>(id));
    }
    const auto rotation = static_cast<int>(transform);
    const bool swapped = rotation % 2 != 0;
    // wlroots uses the integer effective resolution for the output layout box.
    const auto logicalDimension = [wlr, scale](double pixels) {
      return wlr ? std::trunc(pixels / scale) : std::round(pixels / scale);
    };
    const auto logicalWidth = logicalDimension(swapped ? height : width);
    const auto logicalHeight = logicalDimension(swapped ? width : height);
    if (!integerInRange(logicalWidth, 1, max) || !integerInRange(logicalHeight, 1, max)) {
      return std::nullopt;
    }
    const QRectF logical(x, y, logicalWidth, logicalHeight);
    if (wlr && std::ranges::any_of(layout.mapping.outputs, [&logical](const auto& output) {
          return logical.intersects(output.logicalBounds);
        })) {
      return std::nullopt;
    }
    bounds = bounds.united(logical);
    layout.mapping.outputs.append({name, logical, static_cast<OutputTransform>(rotation), scale, {}});
    layout.mapping.captureScale = std::max(layout.mapping.captureScale, scale);
    layout.identity.append(QJsonArray{name, id, width, height, x, y, transform, scale, object.value("identity")});
  }

  const auto scale = layout.mapping.captureScale;
  const auto width = std::trunc(bounds.width() * scale);
  const auto height = std::trunc(bounds.height() * scale);
  if (!integerInRange(width, 1, max) || !integerInRange(height, 1, max)) {
    return std::nullopt;
  }
  layout.canvas = QSize(static_cast<int>(width), static_cast<int>(height));
  for (auto& output : layout.mapping.outputs) {
    const auto start = (output.logicalBounds.topLeft() - bounds.topLeft()) * scale;
    const auto end = (output.logicalBounds.bottomRight() - bounds.topLeft()) * scale;
    // grim composites outwards from the output edges into a truncated canvas.
    const auto left = static_cast<int>(std::floor(start.x()));
    const auto top = static_cast<int>(std::floor(start.y()));
    const auto right = static_cast<int>(std::min(std::ceil(end.x()), width));
    const auto bottom = static_cast<int>(std::min(std::ceil(end.y()), height));
    output.rasterBounds = QRect(left, top, right - left, bottom - top);
  }
  return layout;
}

}  // namespace

GrimAcquisition::GrimAcquisition() : wlr_(usesWlrOutputManagement()) {
  process_.setProcessChannelMode(QProcess::SeparateChannels);
  timeout_.setSingleShot(true);
  timeout_.setTimerType(Qt::PreciseTimer);
  connect(&timeout_, &QTimer::timeout, this, [this] { fail(QStringLiteral("%1 timed out.").arg(process_.program())); });
  connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
    if (stage_ == Stage::Done && error == QProcess::FailedToStart) {
      publishFailure();
    } else {
      fail(QStringLiteral("%1: %2").arg(process_.program(), process_.errorString()));
    }
  });
  connect(&process_, &QProcess::finished, this, &GrimAcquisition::finished);
}

void GrimAcquisition::start(CursorPolicy cursor) {
  if (stage_ != Stage::Ready) {
    return;
  }
  cursor_ = cursor;
  stage_ = Stage::Before;
  queryLayout();
}

void GrimAcquisition::queryLayout() {
  if (wlr_) {
    run(QStringLiteral("wlr-randr"), {QStringLiteral("--json")}, layoutTimeoutMs);
  } else {
    run(QStringLiteral("hyprctl"), {QStringLiteral("-j"), QStringLiteral("monitors")}, layoutTimeoutMs);
  }
}

void GrimAcquisition::verifyLayout() {
  if (stage_ == Stage::Done && !layout_.isEmpty() && process_.state() == QProcess::NotRunning) {
    stage_ = Stage::Verifying;
    queryLayout();
  }
}

void GrimAcquisition::cancel() {
  pendingFailure_.clear();
  stop();
}

void GrimAcquisition::stop() {
  stage_ = Stage::Done;
  timeout_.stop();
  png_.clear();
  if (process_.state() != QProcess::NotRunning) {
    process_.kill();
  }
  process_.readAllStandardOutput();
  process_.readAllStandardError();
}

void GrimAcquisition::run(const QString& program, const QStringList& arguments, int timeoutMs) {
  timeout_.start(timeoutMs);
  process_.start(program, arguments, QIODevice::ReadOnly);
}

void GrimAcquisition::fail(const QString& reason) {
  if (stage_ != Stage::Done) {
    layoutFailure_ = stage_ == Stage::Verifying;
    pendingFailure_ = reason;
    stop();
    if (process_.state() == QProcess::NotRunning) {
      publishFailure();
    }
  }
}

void GrimAcquisition::publishFailure() {
  const auto reason = std::exchange(pendingFailure_, {});
  if (!reason.isEmpty()) {
    if (layoutFailure_) {
      emit layoutVerificationFailed(reason);
    } else {
      emit failed(reason);
    }
  }
}

void GrimAcquisition::finished(int exitCode, QProcess::ExitStatus status) {
  auto data = process_.readAllStandardOutput();
  const auto diagnostic = QString::fromLocal8Bit(process_.readAllStandardError()).trimmed();
  if (stage_ == Stage::Done) {
    publishFailure();
    return;
  }
  timeout_.stop();
  if (status != QProcess::NormalExit || exitCode != 0) {
    fail(QStringLiteral("%1 failed (exit %2). %3").arg(process_.program()).arg(exitCode).arg(diagnostic).trimmed());
    return;
  }
  if (stage_ == Stage::Capture) {
    png_ = std::move(data);
    stage_ = Stage::After;
    queryLayout();
    return;
  }

  auto layout = parseLayout(data, wlr_);
  if (!layout) {
    fail(QStringLiteral("Invalid or unsupported monitor layout."));
    return;
  }
  if (stage_ == Stage::Before) {
    layout_ = std::move(layout->identity);
    mapping_ = std::move(layout->mapping);
    canvas_ = layout->canvas;
    QStringList arguments{QStringLiteral("-s"), QString::number(mapping_.captureScale, 'g', scalePrecision),
                          QStringLiteral("-t"), QStringLiteral("png")};
    if (cursor_ == CursorPolicy::Include) {
      arguments.append(QStringLiteral("-c"));
    }
    arguments.append(QStringLiteral("-"));
    stage_ = Stage::Capture;
    run(QStringLiteral("grim"), arguments, captureTimeoutMs);
    return;
  }

  if (layout->identity != layout_) {
    fail(stage_ == Stage::Verifying ? QStringLiteral("Monitor layout changed during region selection.")
                                    : QStringLiteral("Monitor layout changed during capture."));
    return;
  }
  if (stage_ == Stage::Verifying) {
    stage_ = Stage::Done;
    emit layoutVerified();
    return;
  }
  const auto image = QImage::fromData(png_, "PNG");
  if (image.isNull() || image.size() != canvas_) {
    fail(QStringLiteral("grim returned an invalid PNG or unexpected canvas size."));
    return;
  }
  cancel();
  emit captured(CaptureFrame(image, std::move(mapping_)));
}

}  // namespace screenshot
