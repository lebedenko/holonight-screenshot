#include "editor_document.h"

#include <QColorSpace>
#include <QColorTransform>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QTextOption>

#include <algorithm>
#include <cmath>

namespace screenshot {
namespace {

QFont textFont(const QVariantMap& style) {
  QFont font;
  // Annotation size is measured in source pixels and survives display DPI changes.
  font.setPixelSize(style.value("size").toInt());
  font.setBold(style.value("bold").toBool());
  font.setItalic(style.value("italic").toBool());
  return font;
}

QPointF translation(QRectF moving, QRectF limit, QPointF delta) {
  return {
      std::clamp(delta.x(), std::min(0.0, limit.left() - moving.left()), std::max(0.0, limit.right() - moving.right())),
      std::clamp(delta.y(), std::min(0.0, limit.top() - moving.top()),
                 std::max(0.0, limit.bottom() - moving.bottom()))};
}

}  // namespace

QRectF EditorDocument::Mark::bounds() const {
  const auto origin = points.front();
  if (tool == Number) {
    const auto size = style.value("size").toInt();
    return {origin.x() - size / 2.0, origin.y() - size / 2.0, qreal(size), qreal(size)};
  }
  return points.boundingRect();
}

QPainterPath EditorDocument::Mark::path() const {
  QPainterPath shape;
  switch (tool) {
    case Pen:
      shape.addPolygon(points);
      break;
    case Arrow: {
      const auto tip = points.back();
      const auto delta = tip - points.front();
      const auto length = std::hypot(delta.x(), delta.y());
      if (length > 0) {
        const auto direction = delta / length;
        const auto head = std::min(4.0 * style.value("width").toInt(), length * 0.45);
        const auto base = tip - direction * head;
        const QPointF wing(-direction.y() * head * 0.55, direction.x() * head * 0.55);
        shape.moveTo(points.front());
        shape.lineTo(tip);
        shape.moveTo(base + wing);
        shape.lineTo(tip);
        shape.lineTo(base - wing);
      }
      break;
    }
    case Rectangle: {
      const auto radius = style.value("radius").toInt();
      shape.addRoundedRect(bounds(), radius, radius);
      break;
    }
    case Ellipse:
      shape.addEllipse(bounds());
      break;
    default:
      shape.addRect(bounds());
      break;
  }
  return shape;
}

bool EditorDocument::Mark::hits(QPointF point, qreal tolerance) const {
  const auto shape = path();
  if (tool != Pen && tool != Arrow && shape.contains(point)) {
    return true;
  }
  const auto width = style.value("width").toInt() / 2.0 + tolerance;
  if (points.size() == 1 && tool == Pen) {
    return QLineF(point, points.front()).length() <= width;
  }
  QPainterPathStroker stroke;
  stroke.setWidth(2 * width);
  stroke.setCapStyle(Qt::RoundCap);
  return stroke.createStroke(shape).contains(point);
}

void EditorDocument::Mark::paint(QImage& image) const {
  if (tool == Blur) {
    const auto area = bounds().toAlignedRect().intersected(image.rect());
    if (area.isEmpty()) {
      return;
    }
    const auto strength = style.value("strength").toInt();
    // Downsample then interpolate the composited pixels within the mark.
    const auto reduced = QSize(std::max(1, area.width() / strength), std::max(1, area.height() / strength));
    const auto blurred = image.copy(area)
                             .scaled(reduced, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .scaled(area.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&image);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(area.topLeft(), blurred);
    return;
  }

  const auto colorSpace = image.colorSpace();
  const auto transform =
      colorSpace.isValid() ? QColorSpace(QColorSpace::SRgb).transformationToColorSpace(colorSpace) : QColorTransform();
  const auto ink = transform.map(style.value("color").value<QColor>());
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(ink, style.value("width").toInt(), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  if (tool == Text) {
    painter.setFont(textFont(style));
    painter.setClipRect(bounds());
    QTextOption options;
    options.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    painter.drawText(bounds(), style.value("text").toString(), options);
  } else if (tool == Number) {
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    if (style.value("square").toBool()) {
      painter.drawRoundedRect(bounds(), 3, 3);
    } else {
      painter.drawEllipse(bounds());
    }
    painter.setPen(transform.map(QColor(Qt::white)));
    auto font = textFont(style);
    font.setPixelSize(std::max(1, font.pixelSize() / 2));
    painter.setFont(font);
    painter.drawText(bounds(), Qt::AlignCenter, QString::number(number));
  } else if (tool == Pen && points.size() == 1) {
    painter.drawPoint(points.front());
  } else {
    painter.setBrush(transform.map(style.value("fill").value<QColor>()));
    painter.drawPath(path());
  }
}

void EditorDocument::reset(QImage image) {
  original_ = std::move(image);
  state_ = {{}, original_.rect()};
  undo_.clear();
  redo_.clear();
  gesture_.reset();
  pendingCrop_.reset();
  textDraft_.reset();
  selected_ = -1;
  tool_ = Select;
  for (auto& style : defaults_) {
    style = {{"color", QColor("#8b6dff")},
             {"fill", QColor(Qt::transparent)},
             {"width", 3},
             {"radius", 8},
             {"text", QString()},
             {"size", 24},
             {"bold", false},
             {"italic", false},
             {"square", false},
             {"strength", 12}};
  }
  notify();
}

QImage EditorDocument::render(bool canvas) const {
  const auto& state = !canvas && gesture_ ? gesture_->before : state_;
  auto image = original_;
  if (!state.marks.isEmpty()) {
    const auto format = image.depth() > 32 ? QImage::Format_RGBA64_Premultiplied : QImage::Format_ARGB32_Premultiplied;
    image.convertTo(format);
    for (const auto& mark : state.marks) {
      mark.paint(image);
    }
  }
  const auto area = canvas ? view() : state.crop;
  if (area != image.rect()) {
    image = image.copy(area);
  }
  if (canvas && image.colorSpace().isValid() && image.colorSpace() != QColorSpace::SRgb) {
    image.convertToColorSpace(QColorSpace::SRgb);
  }
  return image;
}

int EditorDocument::propertyTool() const { return selected_ < 0 ? tool_ : state_.marks[selected_].tool; }
QVariantMap EditorDocument::properties() const {
  return selected_ < 0 ? defaults_[tool_] : state_.marks[selected_].style;
}

QRectF EditorDocument::selection() const {
  if (selected_ < 0 || cropping() || editingText()) {
    return {};
  }
  const auto& mark = state_.marks[selected_];
  const auto margin = std::max(2, mark.style.value("width").toInt()) / 2.0;
  return mark.path().controlPointRect().united(mark.bounds()).adjusted(-margin, -margin, margin, margin);
}

QRectF EditorDocument::textBox() const {
  if (textDraft_) {
    return textDraft_->bounds();
  }
  if (gesture_ && gesture_->drawing && tool_ == Text) {
    return state_.marks.last().bounds();
  }
  return {};
}

QString EditorDocument::draftText() const {
  return textDraft_ ? textDraft_->style.value("text").toString() : QString();
}

void EditorDocument::setTool(int tool) {
  if (!busy() && tool >= Select && tool < ToolCount && tool != tool_) {
    tool_ = static_cast<Tool>(tool);
    selected_ = -1;
    notify();
  }
}

void EditorDocument::setPropertyValue(const QString& key, const QVariant& value) {
  if (busy() || !defaults_[tool_].contains(key)) {
    return;
  }
  QVariant normalized = value;
  if (key == "color" || key == "fill") {
    const auto color = value.value<QColor>();
    if (!color.isValid()) {
      return;
    }
    normalized = color;
  } else if (key == "width" || key == "size" || key == "strength" || key == "radius") {
    normalized = std::clamp(value.toInt(), key == "radius" ? 0 : 1, 256);
  }
  if (properties().value(key) == normalized) {
    return;
  }
  if (selected_ >= 0) {
    remember(state_);
    state_.marks[selected_].style.insert(key, normalized);
  } else {
    defaults_[tool_].insert(key, normalized);
  }
  notify();
}

QPointF EditorDocument::bounded(QPointF point) const {
  const QRectF area(view());
  return {std::clamp(point.x(), area.left(), area.right()), std::clamp(point.y(), area.top(), area.bottom())};
}

void EditorDocument::begin(QPointF point, qreal tolerance) {
  if (gesture_ || editingText() || !QRectF(view()).contains(point)) {
    return;
  }
  gesture_.emplace(
      Gesture{.before = state_, .start = point, .originalCrop = pendingCrop(), .edges = {}, .tolerance = tolerance});
  if (cropping()) {
    const auto rect = pendingCrop();
    if (rect.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(point)) {
      if (std::abs(point.x() - rect.left()) <= tolerance) {
        gesture_->edges |= Qt::LeftEdge;
      } else if (std::abs(point.x() - rect.right()) <= tolerance) {
        gesture_->edges |= Qt::RightEdge;
      }
      if (std::abs(point.y() - rect.top()) <= tolerance) {
        gesture_->edges |= Qt::TopEdge;
      } else if (std::abs(point.y() - rect.bottom()) <= tolerance) {
        gesture_->edges |= Qt::BottomEdge;
      }
      gesture_->moveCrop = !gesture_->edges && rect.contains(point);
    }
    if (!gesture_->edges && !gesture_->moveCrop) {
      pendingCrop_ = QRectF(point, point);
    }
  } else if (tool_ == Select) {
    selected_ = state_.marks.size() - 1;
    while (selected_ >= 0 && !state_.marks[selected_].hits(point, tolerance)) {
      --selected_;
    }
    if (selected_ < 0) {
      gesture_.reset();
    }
  } else {
    int number = 0;
    if (tool_ == Number) {
      for (const auto& mark : state_.marks) {
        number = std::max(number, mark.number);
      }
      ++number;
    }
    gesture_->drawing = true;
    state_.marks.append(Mark{tool_, QPolygonF{point}, defaults_[tool_], number});
  }
  notify();
}

void EditorDocument::move(QPointF point) {
  if (!gesture_) {
    return;
  }
  point = bounded(point);
  const auto delta = point - gesture_->start;
  if (cropping()) {
    auto rect = gesture_->originalCrop;
    if (gesture_->moveCrop) {
      rect.translate(translation(rect, QRectF(view()), delta));
    } else if (gesture_->edges) {
      if (gesture_->edges.testFlag(Qt::LeftEdge)) {
        rect.setLeft(std::min(rect.left() + delta.x(), rect.right() - 1));
      }
      if (gesture_->edges.testFlag(Qt::RightEdge)) {
        rect.setRight(std::max(rect.right() + delta.x(), rect.left() + 1));
      }
      if (gesture_->edges.testFlag(Qt::TopEdge)) {
        rect.setTop(std::min(rect.top() + delta.y(), rect.bottom() - 1));
      }
      if (gesture_->edges.testFlag(Qt::BottomEdge)) {
        rect.setBottom(std::max(rect.bottom() + delta.y(), rect.top() + 1));
      }
      rect = rect.intersected(QRectF(view()));
    } else {
      rect = QRectF(gesture_->start, point).normalized();
    }
    pendingCrop_ = rect;
  } else if (gesture_->drawing) {
    auto& mark = state_.marks.last();
    if (mark.tool == Pen && point != mark.points.back()) {
      mark.points.append(point);
    } else if (mark.tool == Rectangle || mark.tool == Ellipse || mark.tool == Arrow || mark.tool == Blur ||
               mark.tool == Text) {
      mark.points = QPolygonF{gesture_->start, point};
    }
  } else {
    const auto& initial = gesture_->before.marks[selected_];
    state_.marks[selected_].points = initial.points.translated(translation(initial.bounds(), QRectF(view()), delta));
  }
  notify();
}

void EditorDocument::finish(QPointF point) {
  if (!gesture_) {
    return;
  }
  move(point);
  if (cropping()) {
    if (pendingCrop().isEmpty()) {
      pendingCrop_ = gesture_->originalCrop;
    }
  } else if (gesture_->drawing) {
    const auto& mark = state_.marks.last();
    if (mark.tool == Text) {
      textDraft_ = state_.marks.takeLast();
      auto box = textDraft_->bounds();
      const QRectF area(view());
      if (QLineF(gesture_->start, bounded(point)).length() <= gesture_->tolerance) {
        const QFontMetricsF metrics(textFont(textDraft_->style));
        const auto width = std::min(area.width(), metrics.averageCharWidth() * 20);
        const auto height = std::min(area.height(), std::ceil(metrics.height() + metrics.lineSpacing()));
        box = {std::min(gesture_->start.x(), area.right() - width),
               std::min(gesture_->start.y(), area.bottom() - height), width, height};
      } else {
        box.setWidth(std::max(1.0, box.width()));
        box.setHeight(std::max(1.0, box.height()));
        box.moveLeft(std::min(box.left(), area.right() - box.width()));
        box.moveTop(std::min(box.top(), area.bottom() - box.height()));
      }
      textDraft_->points = QPolygonF{box.topLeft(), box.bottomRight()};
    } else if ((mark.tool == Rectangle || mark.tool == Ellipse || mark.tool == Blur) && mark.bounds().isEmpty()) {
      state_.marks.removeLast();
    } else if (mark.tool == Arrow && mark.points.front() == mark.points.back()) {
      state_.marks.removeLast();
    } else {
      remember(std::move(gesture_->before));
    }
  } else if (state_.marks[selected_].points != gesture_->before.marks[selected_].points) {
    remember(std::move(gesture_->before));
  }
  gesture_.reset();
  notify();
}

void EditorDocument::cancelGesture() {
  if (gesture_) {
    state_ = std::move(gesture_->before);
    if (cropping()) {
      pendingCrop_ = gesture_->originalCrop;
    }
    gesture_.reset();
    notify();
  }
}

void EditorDocument::editSelectedText() {
  if (!busy() && selected_ >= 0 && state_.marks[selected_].tool == Text) {
    textDraft_ = state_.marks[selected_];
    notify();
  }
}

void EditorDocument::commitText(const QString& text) {
  if (!textDraft_) {
    return;
  }
  if (!text.trimmed().isEmpty() && text != draftText()) {
    remember(state_);
    textDraft_->style.insert("text", text);
    if (selected_ >= 0) {
      state_.marks[selected_] = std::move(*textDraft_);
    } else {
      state_.marks.append(std::move(*textDraft_));
    }
  }
  cancelText();
}

void EditorDocument::cancelText() {
  if (textDraft_) {
    textDraft_.reset();
    notify();
  }
}

void EditorDocument::startCrop() {
  if (!busy()) {
    pendingCrop_ = state_.crop == original_.rect() ? QRectF() : QRectF(state_.crop);
    selected_ = -1;
    notify();
  }
}

void EditorDocument::applyCrop() {
  if (!cropping() || gesture_ || pendingCrop().isEmpty()) {
    return;
  }
  const auto next = pendingCrop().toAlignedRect().intersected(original_.rect());
  if (next != state_.crop) {
    remember(state_);
    state_.crop = next;
  }
  pendingCrop_.reset();
  notify();
}

void EditorDocument::cancelCrop() {
  cancelGesture();
  pendingCrop_.reset();
  notify();
}

void EditorDocument::deleteSelection() {
  if (!busy() && selected_ >= 0) {
    remember(state_);
    state_.marks.removeAt(selected_);
    selected_ = -1;
    notify();
  }
}

void EditorDocument::undo() {
  if (canUndo()) {
    redo_.append(state_);
    state_ = undo_.takeLast();
    selected_ = -1;
    notify();
  }
}

void EditorDocument::redo() {
  if (canRedo()) {
    undo_.append(state_);
    state_ = redo_.takeLast();
    selected_ = -1;
    notify();
  }
}

void EditorDocument::remember(State previous) {
  undo_.append(std::move(previous));
  redo_.clear();
}

void EditorDocument::notify() {
  ++revision_;
  emit changed();
}

}  // namespace screenshot
