#include "editor_document.h"

#include <QColorSpace>
#include <QTest>

using screenshot::EditorDocument;

namespace {

void draw(EditorDocument& document, EditorDocument::Tool tool, QPointF start, QPointF end, QColor color = Qt::red) {
  document.setTool(tool);
  document.setPropertyValue("color", color);
  document.begin(start, 1);
  document.finish(end);
}

class EditorTest final : public QObject {
  Q_OBJECT

 private slots:
  void selectionPropertiesAndHistory() {
    QImage original(80, 60, QImage::Format_ARGB32_Premultiplied);
    original.fill(Qt::black);
    EditorDocument document;
    document.reset(original);
    draw(document, EditorDocument::Rectangle, {5, 5}, {50, 45});
    draw(document, EditorDocument::Ellipse, {10, 10}, {40, 35}, Qt::yellow);

    document.setTool(EditorDocument::Select);
    document.begin({22, 20}, 1);
    document.finish({22, 20});
    QCOMPARE(document.propertyTool(), EditorDocument::Ellipse);
    document.setPropertyValue("fill", QColor(Qt::green));
    QCOMPARE(document.render().pixelColor(22, 20), QColor(Qt::green));

    document.begin({22, 20}, 1);
    document.finish({52, 30});
    QCOMPARE(document.render().pixelColor(22, 20), QColor(Qt::black));
    QCOMPARE(document.render().pixelColor(52, 30), QColor(Qt::green));
    document.deleteSelection();
    QCOMPARE(document.render().pixelColor(52, 30), QColor(Qt::black));
    document.undo();
    QCOMPARE(document.render().pixelColor(52, 30), QColor(Qt::green));
    document.undo();
    QCOMPARE(document.render().pixelColor(22, 20), QColor(Qt::green));
    document.undo();
    QCOMPARE(document.render().pixelColor(22, 20), QColor(Qt::black));
    document.redo();
    QCOMPARE(document.render().pixelColor(22, 20), QColor(Qt::green));
  }

  void cropBoundsAndRendering() {
    QImage original(50, 40, QImage::Format_RGB32);
    original.setColorSpace(QColorSpace::DisplayP3);
    original.fill(Qt::black);
    EditorDocument document;
    document.reset(original);
    draw(document, EditorDocument::Rectangle, {14, 12}, {36, 32});
    const auto full = document.render();
    document.startCrop();
    document.begin({10, 8}, 1);
    document.finish({60, 70});
    QCOMPARE(document.pendingCrop(), QRectF(10, 8, 40, 32));
    document.begin({10, 8}, 1);
    document.finish({12, 10});
    QCOMPARE(document.pendingCrop(), QRectF(12, 10, 38, 30));
    document.begin({20, 20}, 1);
    document.finish({18, 17});
    QCOMPARE(document.pendingCrop(), QRectF(10, 7, 38, 30));
    QVERIFY(document.busy());
    QCOMPARE(document.render(), full);
    document.applyCrop();
    QCOMPARE(document.render(), full.copy(10, 7, 38, 30));
    QCOMPARE(document.render().colorSpace(), original.colorSpace());
    QCOMPARE(document.render(true), document.render().convertedToColorSpace(QColorSpace::SRgb));
    document.undo();
    QCOMPARE(document.render(), full);
    document.redo();
    QCOMPARE(document.crop(), QRect(10, 7, 38, 30));
    document.startCrop();
    document.begin({1, 1}, 1);
    document.finish({6, 6});
    document.cancelCrop();
    QCOMPARE(document.crop(), QRect(10, 7, 38, 30));
  }

  void blurUsesPrecedingMarks() {
    QImage original(40, 40, QImage::Format_ARGB32_Premultiplied);
    original.fill(Qt::black);
    EditorDocument document;
    document.reset(original);
    draw(document, EditorDocument::Pen, {8, 5}, {8, 34});
    draw(document, EditorDocument::Blur, {2, 2}, {38, 38});
    draw(document, EditorDocument::Pen, {24, 5}, {24, 34}, Qt::green);
    const auto result = document.render();
    QVERIFY(result.pixelColor(8, 20).red() < 255);
    QVERIFY(result.pixelColor(12, 20).red() > 0);
    QCOMPARE(result.pixelColor(24, 20), QColor(Qt::green));
    QCOMPARE(document.render(true), result);
  }

  void textBoxesCommitMultilineAndKeepHistory() {
    QImage original(400, 200, QImage::Format_RGB32);
    original.fill(Qt::black);
    EditorDocument document;
    document.reset(original);
    document.setTool(EditorDocument::Text);
    document.begin({20, 20}, 6);
    document.finish({20, 20});
    QVERIFY(document.editingText());
    QVERIFY(document.draftText().isEmpty());
    const auto box = document.textBox();
    QCOMPARE(box.topLeft(), QPointF(20, 20));
    QCOMPARE(document.render(), original);
    document.commitText(QStringLiteral("First\nSecond"));
    QVERIFY(!document.busy());
    const auto committed = document.render();
    QVERIFY(committed.copy(QRect(20, 20, box.width(), box.height() / 2)) !=
            original.copy(QRect(20, 20, box.width(), box.height() / 2)));
    const QRect secondLine(20, 20 + box.height() / 2, box.width(), box.height() / 2);
    QVERIFY(committed.copy(secondLine) != original.copy(secondLine));
    document.undo();
    QCOMPARE(document.render(), original);
    document.redo();
    QCOMPARE(document.render(), committed);

    document.begin({400, 180}, 6);
    document.finish({310, 90});
    QCOMPARE(document.textBox(), QRectF(310, 90, 90, 90));
    document.cancelText();
    QCOMPARE(document.render(), committed);

    document.setTool(EditorDocument::Select);
    document.begin({30, 30}, 6);
    document.finish({30, 30});
    document.editSelectedText();
    QCOMPARE(document.draftText(), QStringLiteral("First\nSecond"));
    document.commitText(QStringLiteral("Revised"));
    QVERIFY(document.render() != committed);
    document.undo();
    QCOMPARE(document.render(), committed);
  }

  void badgesReuseDeletedHighestNumber() {
    QImage original(80, 40, QImage::Format_ARGB32_Premultiplied);
    original.fill(Qt::black);
    EditorDocument document;
    document.reset(original);
    draw(document, EditorDocument::Number, {16, 20}, {16, 20});
    draw(document, EditorDocument::Number, {48, 20}, {48, 20});
    const auto originalBadges = document.render();
    document.setTool(EditorDocument::Select);
    document.begin({48, 20}, 1);
    document.finish({48, 20});
    document.deleteSelection();
    draw(document, EditorDocument::Number, {48, 20}, {48, 20});
    QCOMPARE(document.render(), originalBadges);
    QVERIFY(originalBadges.copy(4, 8, 24, 24) != originalBadges.copy(36, 8, 24, 24));
  }
};

}  // namespace

QTEST_MAIN(EditorTest)
#include "editor_test.moc"
