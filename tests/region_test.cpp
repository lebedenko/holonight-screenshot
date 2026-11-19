#include "region_selection.h"

#include <QTest>

using namespace screenshot;

class RegionTest final : public QObject {
  Q_OBJECT

 private slots:
  void redrawReplacesReleasedSelection() {
    const auto frame = createFrame();
    RegionSelection selection;
    selection.begin({1.8, 1.9});
    selection.move({0.8, 0.6});
    QCOMPARE(selection.rectangle(), QRectF(QPointF(0.8, 0.6), QPointF(1.8, 1.9)));
    QVERIFY(selection.confirm(frame).isNull());
    selection.release({0.8, 0.6});
    QCOMPARE(selection.confirm(frame).size(), QSize(2, 3));

    selection.begin({2, 1});
    QVERIFY(selection.rectangle().isEmpty());
    selection.release({3, 2});
    QCOMPARE(selection.rectangle(), QRectF(2, 1, 1, 1));
    QCOMPARE(selection.confirm(frame).size(), QSize(2, 2));
  }

  void cancellationDiscardsDragAndReleasedSelection() {
    const auto frame = createFrame();
    for (bool released : {false, true}) {
      RegionSelection selection;
      selection.begin({1, 1});
      selection.move({2, 2});
      if (released) {
        selection.release({2, 2});
      }
      selection.cancel();
      QVERIFY(selection.rectangle().isEmpty());
      QVERIFY(selection.confirm(frame).isNull());
    }
  }

  void ignoresEmptyAndGapOnlySelections() {
    const auto frame = createFrame();
    RegionSelection selection;
    QVERIFY(selection.confirm(frame).isNull());
    selection.begin({1, 1});
    selection.release({1, 2});
    QVERIFY(selection.confirm(frame).isNull());
    selection.begin({3, 0});
    selection.release({4, 2});
    QVERIFY(selection.confirm(frame).isNull());
  }

  void clipsToCanvasAndKeepsCapturedGapPixels() {
    const auto frame = createFrame();
    RegionSelection selection;
    selection.begin({-6, -1});
    selection.release({12, 8});
    const auto crop = selection.confirm(frame);
    QCOMPARE(crop, frame.image());
    QCOMPARE(crop.pixelColor(5, 1), QColor(Qt::green));
  }

 private:
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
