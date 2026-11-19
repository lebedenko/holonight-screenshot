#include "region_selection.h"

#include <QColorTransform>
#include <QPainter>
#include <QTest>

using namespace screenshot;

class RegionTest final : public QObject {
  Q_OBJECT

 private slots:
  void redrawReplacesReleasedSelection() {
    const auto frame = createFrame();
    RegionSelection selection;
    selection.begin({1.8, 1.9}, frame);
    selection.move({0.8, 0.6});
    QCOMPARE(selection.rectangle(), QRectF(QPointF(0.8, 0.6), QPointF(1.8, 1.9)));
    QVERIFY(selection.cropRect(frame).isEmpty());
    QVERIFY(selection.confirm(frame).isNull());
    selection.release({0.8, 0.6});
    QCOMPARE(selection.cropRect(frame).size(), QSize(2, 3));
    QCOMPARE(selection.confirm(frame).size(), QSize(2, 3));

    selection.begin({2, 1}, frame);
    QVERIFY(selection.rectangle().isEmpty());
    selection.release({3, 2});
    QCOMPARE(selection.rectangle(), QRectF(2, 1, 1, 1));
    QCOMPARE(selection.confirm(frame).size(), QSize(2, 2));
  }

  void cancellationDiscardsDragAndReleasedSelection() {
    const auto frame = createFrame();
    for (bool released : {false, true}) {
      RegionSelection selection;
      selection.begin({1, 1}, frame);
      selection.move({2, 2});
      if (released) {
        selection.release({2, 2});
      }
      selection.cancel();
      QVERIFY(selection.rectangle().isEmpty());
      QVERIFY(selection.cropRect(frame).isEmpty());
      QVERIFY(selection.confirm(frame).isNull());
    }
  }

  void ignoresEmptyAndGapOnlySelections() {
    const auto frame = createFrame();
    RegionSelection selection;
    QVERIFY(selection.cropRect(frame).isEmpty());
    QVERIFY(selection.confirm(frame).isNull());
    selection.begin({1, 1}, frame);
    selection.release({1, 2});
    QVERIFY(selection.cropRect(frame).isEmpty());
    QVERIFY(selection.confirm(frame).isNull());
    selection.begin({3, 0}, frame);
    selection.release({4, 2});
    QVERIFY(selection.cropRect(frame).isEmpty());
    QVERIFY(selection.confirm(frame).isNull());
  }

  void clipsToCanvasAndKeepsCapturedGapPixels() {
    const auto frame = createFrame();
    RegionSelection selection;
    selection.begin({-6, -1}, frame);
    selection.release({12, 8});
    const auto crop = selection.confirm(frame);
    QCOMPARE(crop, frame.image());
    QCOMPARE(crop.pixelColor(5, 1), QColor(Qt::green));
  }

  void commitsOnlyValidGesturesAndUndoesInOrder() {
    const auto frame = annotationFrame();
    RegionSelection selection;
    QVERIFY(selection.begin({-8, 2}, frame));
    QVERIFY(!selection.canConfirm(frame));
    selection.release({8, 12});
    const auto original = selection.confirm(frame);

    selection.setTool(RegionSelection::Tool::Pen);
    QVERIFY(!selection.begin({-10, 5}, frame));
    QVERIFY(selection.canConfirm(frame));
    QVERIFY(selection.begin({-4, 5}, frame));
    QVERIFY(!selection.canConfirm(frame));
    QVERIFY(selection.confirm(frame).isNull());
    auto preview = frame.image().copy();
    {
      QPainter painter(&preview);
      selection.paintAnnotations(painter, frame, true);
    }
    selection.release({-4, 5});
    const auto dot = selection.confirm(frame);
    const auto crop = selection.cropRect(frame);
    QCOMPARE(dot, preview.copy(crop));
    QVERIFY(dot != original);

    selection.setTool(RegionSelection::Tool::Arrow);
    QVERIFY(selection.begin({-5, 7}, frame));
    selection.release({-5, 7});
    QCOMPARE(selection.confirm(frame), dot);
    selection.setTool(RegionSelection::Tool::Rectangle);
    QVERIFY(selection.begin({-5, 7}, frame));
    selection.release({5, 7});
    QCOMPARE(selection.confirm(frame), dot);

    selection.setColor(Qt::yellow);
    QVERIFY(selection.begin({-5, 4}, frame));
    selection.release({5, 10});
    const auto rectangle = selection.confirm(frame);
    QVERIFY(rectangle != dot);
    selection.setTool(RegionSelection::Tool::Arrow);
    QVERIFY(selection.begin({-5, 7}, frame));
    selection.release({5, 7});
    QVERIFY(selection.confirm(frame) != rectangle);
    selection.undo();
    QCOMPARE(selection.confirm(frame), rectangle);
    selection.undo();
    QCOMPARE(selection.confirm(frame), dot);
    selection.undo();
    QCOMPARE(selection.confirm(frame), original);
    QVERIFY(!selection.canUndo());
  }

  void redrawClearsCommittedAnnotations() {
    const auto frame = annotationFrame();
    RegionSelection selection;
    QVERIFY(selection.begin({-8, 2}, frame));
    selection.release({8, 12});
    selection.setTool(RegionSelection::Tool::Pen);
    QVERIFY(selection.begin({0, 7}, frame));
    selection.release({0, 7});
    QVERIFY(selection.canUndo());

    selection.setTool(RegionSelection::Tool::Select);
    QVERIFY(selection.begin({-7, 3}, frame));
    selection.release({7, 11});
    QVERIFY(!selection.canUndo());
    QCOMPARE(selection.confirm(frame), frame.image().copy(selection.cropRect(frame)));
  }

  void paintsAcrossMixedScaleSeamAndClipsToRoundedCrop() {
    const auto frame = annotationFrame();
    RegionSelection selection;
    QVERIFY(selection.begin({-6.25, 2.25}, frame));
    selection.release({6.25, 11.75});
    selection.setTool(RegionSelection::Tool::Pen);
    QVERIFY(selection.begin({-6.25, 7}, frame));
    selection.move({0, 7});
    selection.release({9, 7});

    const auto crop = selection.cropRect(frame);
    QCOMPARE(crop, QRect(5, 3, 20, 15));
    const auto exported = selection.confirm(frame);
    QCOMPARE(exported.colorSpace(), frame.colorSpace());
    const auto red = QColorSpace(QColorSpace::SRgb).transformationToColorSpace(frame.colorSpace()).map(QColor(Qt::red));
    auto rendered = frame.image().copy();
    {
      QPainter painter(&rendered);
      selection.paintAnnotations(painter, frame, true);
    }
    QCOMPARE(rendered.copy(crop), exported);
    for (int y = 0; y < rendered.height(); ++y) {
      for (int x = 0; x < rendered.width(); ++x) {
        if (!crop.contains(x, y) || y < 8 || y > 12) {
          QCOMPARE(rendered.pixel(x, y), frame.image().pixel(x, y));
        }
      }
    }
    // The output changes at raster x = 15 even though the outputs have different native scales.
    for (int x : {5, 14, 15, 24}) {
      QCOMPARE(rendered.pixel(x, 10), red.rgb());
    }
  }

 private:
  static CaptureFrame annotationFrame() {
    QImage image(30, 21, QImage::Format_RGB32);
    image.fill(Qt::blue);
    image.setColorSpace(QColorSpace::DisplayP3);
    QPainter painter(&image);
    painter.fillRect(QRect(0, 0, 15, 21), Qt::green);
    painter.end();
    return CaptureFrame(
        image,
        DesktopMapping{{
                           {QStringLiteral("left"), {-10, 0, 10, 14}, OutputTransform::Normal, 1, {0, 0, 15, 21}},
                           {QStringLiteral("right"), {0, 0, 10, 14}, OutputTransform::Normal, 1.5, {15, 0, 15, 21}},
                       },
                       1.5});
  }

  static CaptureFrame createFrame() {
    QImage image(10, 4, QImage::Format_RGB32);
    image.fill(Qt::red);
    image.setPixelColor(5, 1, Qt::green);
    return CaptureFrame(
        image, DesktopMapping{{
                                  {QStringLiteral("left"), {0, 0, 3, 3}, OutputTransform::Normal, 1, {0, 0, 5, 4}},
                                  {QStringLiteral("right"), {4, 0, 3, 3}, OutputTransform::Normal, 1.5, {6, 0, 4, 4}},
                              },
                              1.5});
  }
};

QTEST_MAIN(RegionTest)
#include "region_test.moc"
