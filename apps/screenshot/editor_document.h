#pragma once

#include <QImage>
#include <QObject>
#include <QPolygonF>
#include <QVariantMap>

#include <optional>

namespace screenshot {

class EditorDocument final : public QObject {
  Q_OBJECT
  Q_PROPERTY(int revision READ revision NOTIFY changed)
  Q_PROPERTY(int tool READ tool WRITE setTool NOTIFY changed)
  Q_PROPERTY(int propertyTool READ propertyTool NOTIFY changed)
  Q_PROPERTY(QVariantMap properties READ properties NOTIFY changed)
  Q_PROPERTY(QRect crop READ crop NOTIFY changed)
  Q_PROPERTY(QRect view READ view NOTIFY changed)
  Q_PROPERTY(QRectF pendingCrop READ pendingCrop NOTIFY changed)
  Q_PROPERTY(QRectF textBox READ textBox NOTIFY changed)
  Q_PROPERTY(QString draftText READ draftText NOTIFY changed)
  Q_PROPERTY(bool editingText READ editingText NOTIFY changed)
  Q_PROPERTY(QRectF selection READ selection NOTIFY changed)
  Q_PROPERTY(bool cropping READ cropping NOTIFY changed)
  Q_PROPERTY(bool busy READ busy NOTIFY changed)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY changed)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY changed)

 public:
  enum Tool { Select, Pen, Rectangle, Ellipse, Arrow, Text, Number, Blur, ToolCount };
  Q_ENUM(Tool)

  void reset(QImage image);
  QImage render(bool canvas = false) const;
  int revision() const { return revision_; }
  int tool() const { return tool_; }
  int propertyTool() const;
  QVariantMap properties() const;
  QRect crop() const { return state_.crop; }
  QRect view() const { return cropping() ? original_.rect() : crop(); }
  QRectF pendingCrop() const { return pendingCrop_.value_or(QRectF()); }
  QRectF selection() const;
  QRectF textBox() const;
  QString draftText() const;
  bool editingText() const { return textDraft_.has_value(); }
  bool cropping() const { return pendingCrop_.has_value(); }
  bool busy() const { return gesture_.has_value() || cropping() || editingText(); }
  bool canUndo() const { return !busy() && !undo_.isEmpty(); }
  bool canRedo() const { return !busy() && !redo_.isEmpty(); }

  void setTool(int tool);
  Q_INVOKABLE void setPropertyValue(const QString& key, const QVariant& value);
  Q_INVOKABLE void begin(QPointF point, qreal tolerance);
  Q_INVOKABLE void move(QPointF point);
  Q_INVOKABLE void finish(QPointF point);
  Q_INVOKABLE void cancelGesture();
  Q_INVOKABLE void editSelectedText();
  Q_INVOKABLE void commitText(const QString& text);
  Q_INVOKABLE void cancelText();
  Q_INVOKABLE void startCrop();
  Q_INVOKABLE void applyCrop();
  Q_INVOKABLE void cancelCrop();
  Q_INVOKABLE void deleteSelection();
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();

 signals:
  void changed();

 private:
  struct Mark {
    Tool tool;
    QPolygonF points;
    QVariantMap style;
    int number = 0;
    QRectF bounds() const;
    QPainterPath path() const;
    bool hits(QPointF point, qreal tolerance) const;
    void paint(QImage& image) const;
  };

  struct State {
    QList<Mark> marks;
    QRect crop;
  };

  struct Gesture {
    State before;
    QPointF start;
    QRectF originalCrop;
    Qt::Edges edges;
    bool moveCrop = false;
    bool drawing = false;
    qreal tolerance = 0;
  };

  void notify();
  void remember(State previous);
  QPointF bounded(QPointF point) const;

  QImage original_;
  State state_;
  QList<State> undo_;
  QList<State> redo_;
  QVariantMap defaults_[ToolCount];
  std::optional<Gesture> gesture_;
  std::optional<QRectF> pendingCrop_;
  std::optional<Mark> textDraft_;
  Tool tool_ = Select;
  qsizetype selected_ = -1;
  int revision_ = 0;
};

}  // namespace screenshot
