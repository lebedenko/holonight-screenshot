#include "region_selection.h"

#include <QColorTransform>
#include <QPainter>
#include <QTest>

using namespace screenshot;

class RegionTest final : public QObject {
  Q_OBJECT

 private slots:
  void resizedPreviewExportsCommittedMarksAtFullResolution() {
    QImage source(1200, 600, QImage::Format_RGB32);
    source.setColorSpace(QColorSpace::DisplayP3);
    source.fill(Qt::black);
    Annotations marks;
    const QRectF initialBounds(20, 60, 600, 300);
    const QRectF resizedBounds(10, 100, 300, 150);
    const auto start = Annotations::imagePoint({70, 110}, initialBounds, source.size());
    const auto end = Annotations::imagePoint({110, 125}, resizedBounds, source.size());
    QCOMPARE(start, QPointF(100, 100));
    QCOMPARE(end, QPointF(400, 100));

    QVERIFY(marks.begin(Annotations::Tool::Pen, Qt::red, start));
    marks.move(end);
    QVERIFY(marks.active());
    QVERIFY(!marks.canUndo());
    QCOMPARE(marks.render(source), source);
    marks.finish(end);

    const auto rendered = marks.render(source);
    QCOMPARE(rendered.size(), source.size());
    QCOMPARE(rendered.colorSpace(), source.colorSpace());
    auto srgb = rendered.convertedToColorSpace(QColorSpace::SRgb);
    QVERIFY(srgb.pixelColor(250, 100).red() > 240);
    QCOMPARE(srgb.pixelColor(250, 110), QColor(Qt::black));
    QVERIFY(marks.canUndo());
    marks.undo();
    QCOMPARE(marks.render(source), source);
  }

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

  void resizesCornerAndEdgeWithOppositeEdgesFixed() {
    const auto frame = editFrame();
    RegionSelection selection;
    QVERIFY(selection.begin({30, 30}, frame, true));
    selection.release({90, 90});

    QCOMPARE(selection.cursorAt({24, 25}, frame), Qt::SizeFDiagCursor);
    QVERIFY(selection.begin({24, 25}, frame, true));
    selection.move({-6, 10});
    QCOMPARE(selection.rectangle(), QRectF(0, 15, 90, 75));
    QVERIFY(!selection.canConfirm(frame));
    QVERIFY(selection.confirm(frame).isNull());
    selection.release({-6, 10});

    QCOMPARE(selection.cursorAt({90, 52.5}, frame), Qt::SizeHorCursor);
    QVERIFY(selection.begin({90, 52.5}, frame, true));
    selection.release({250, 65});
    QCOMPARE(selection.rectangle(), QRectF(0, 15, 220, 75));
    QVERIFY(selection.canConfirm(frame));
  }

  void movesAcrossOutputsAndClampsWithoutResizing() {
    const auto frame = editFrame();
    RegionSelection selection;
    QVERIFY(selection.begin({20, 20}, frame, true));
    selection.release({80, 70});

    QCOMPARE(selection.cursorAt({50, 45}, frame), Qt::SizeAllCursor);
    QVERIFY(selection.begin({50, 45}, frame, true));
    selection.move({120, 55});
    QCOMPARE(selection.rectangle(), QRectF(90, 30, 60, 50));
    QVERIFY(!selection.canConfirm(frame));
    selection.release({250, 200});
    QCOMPARE(selection.rectangle(), QRectF(160, 70, 60, 50));
    QVERIFY(selection.canConfirm(frame));
  }

  void replacesFromOutsideAndRejectsGapAndEmptyCrops() {
    const auto frame = editFrame();
    RegionSelection selection;
    QVERIFY(selection.begin({20, 20}, frame, true));
    selection.release({80, 70});
    QCOMPARE(selection.cursorAt({103, 10}, frame), Qt::CrossCursor);
    QVERIFY(selection.begin({103, 10}, frame, true));
    QVERIFY(selection.rectangle().isEmpty());
    selection.release({117, 70});
    QCOMPARE(selection.rectangle(), QRectF(103, 10, 14, 60));
    QVERIFY(selection.liveCropRect(frame).isEmpty());
    QVERIFY(!selection.canConfirm(frame));

    QVERIFY(selection.begin({130, 10}, frame, true));
    selection.release({130, 10});
    QVERIFY(!selection.canConfirm(frame));
  }

  void liveCropMatchesFractionalExportAfterClipping() {
    const auto frame = createFrame();
    RegionSelection selection;
    QVERIFY(selection.begin({0.8, 0.6}, frame, true));
    selection.move({8, 5});
    QCOMPARE(selection.rectangle(), QRectF(QPointF(0.8, 0.6), QPointF(7, 3)));
    const auto liveSize = selection.liveCropRect(frame).size();
    QCOMPARE(liveSize, QSize(9, 4));
    QVERIFY(!selection.canConfirm(frame));
    selection.release({8, 5});
    QCOMPARE(selection.confirm(frame).size(), liveSize);
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
  static CaptureFrame editFrame() {
    QImage image(330, 180, QImage::Format_RGB32);
    image.fill(Qt::black);
    return CaptureFrame(
        image, DesktopMapping{
                   {
                       {QStringLiteral("left"), {0, 0, 100, 120}, OutputTransform::Normal, 1, {0, 0, 150, 180}},
                       {QStringLiteral("right"), {120, 0, 100, 120}, OutputTransform::Normal, 1.5, {180, 0, 150, 180}},
                   },
                   1.5});
  }

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
