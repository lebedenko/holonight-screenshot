#include "capture_controller.h"

#include <QSignalSpy>
#include <QTest>
#include <QTimerEvent>

using namespace screenshot;
using namespace std::chrono_literals;

namespace {
QImage grid() {
  QImage image(2, 2, QImage::Format_RGB32);
  image.setColorSpace(QColorSpace::SRgb);
  image.setPixelColor(0, 0, Qt::red);
  image.setPixelColor(1, 0, Qt::green);
  image.setPixelColor(0, 1, Qt::blue);
  image.setPixelColor(1, 1, Qt::white);
  return image;
}

class FixtureAcquisition final : public DesktopAcquisition {
 public:
  QList<CursorPolicy> starts;
  int stops = 0;
  void start(CursorPolicy cursor) override { starts.append(cursor); }
  void cancel() override {
    ++stops;
    emit failed("helper stopped");
  }
  void complete() { emit captured(CaptureFrame(grid())); }
};

struct Request {
  FixtureAcquisition provider;
  std::unique_ptr<QTimer> clock = std::make_unique<QTimer>();
  QTimer* timer = clock.get();
  CaptureController controller;
  QSignalSpy frames{&controller, &CaptureController::captured};
  QSignalSpy cancellations{&controller, &CaptureController::canceled};
  QSignalSpy failures{&controller, &CaptureController::failed};

  explicit Request(std::chrono::milliseconds delay = 3s, CursorPolicy cursor = CursorPolicy::Exclude)
      : controller(provider, delay, cursor, std::move(clock)) {}

  // Deliver the armed Qt timer event directly, independent of elapsed wall time.
  void expire() {
    QTimerEvent event(timer->id());
    QCoreApplication::sendEvent(timer, &event);
  }
  qsizetype outcomes() const { return frames.size() + failures.size() + cancellations.size(); }
};
}  // namespace

class CaptureTest final : public QObject {
  Q_OBJECT
 private slots:
  void samplesMixedScaleOutputsInSrgb() {
    DesktopMapping mapping{
        {
            {QStringLiteral("left"), QRectF(-2, 0, 2, 2), OutputTransform::Normal, 1, QRect(0, 0, 4, 4)},
            {QStringLiteral("right"), QRectF(0, 0, 2, 2), OutputTransform::Normal, 2, QRect(4, 0, 4, 4)},
        },
        2};
    QImage image(8, 4, QImage::Format_RGB32);
    image.fill(Qt::black);
    image.setPixelColor(3, 1, Qt::red);
    image.setPixelColor(4, 1, Qt::green);
    const CaptureFrame untagged(image, mapping);
    QCOMPARE(untagged.sampleSrgb(QPointF(-0.25, 0.75)), QColor(Qt::red));
    QCOMPARE(untagged.sampleSrgb(QPointF(0, 0.75)), QColor(Qt::green));
    QVERIFY(!untagged.sampleSrgb(QPointF(2, 0.75)).isValid());

    image.setColorSpace(QColorSpace::SRgbLinear);
    image.setPixelColor(4, 1, QColor(128, 64, 0));
    const CaptureFrame tagged(image, mapping);
    const auto color = tagged.sampleSrgb(QPointF(0, 0.75));
    QVERIFY(qAbs(color.red() - 188) <= 1);
    QVERIFY(qAbs(color.green() - 137) <= 1);
    QCOMPARE(color.blue(), 0);
  }

  void timerStartsOneAcquisition_data() {
    QTest::addColumn<int>("milliseconds");
    QTest::newRow("immediate") << 0;
    QTest::newRow("delayed") << 3000;
  }

  void timerStartsOneAcquisition() {
    QFETCH(int, milliseconds);
    Request request(std::chrono::milliseconds{milliseconds}, CursorPolicy::Include);
    QVERIFY(request.provider.starts.empty());
    QVERIFY(request.timer->isActive());
    QCOMPARE(request.timer->interval(), milliseconds);
    request.expire();
    QVERIFY(!request.timer->isActive());
    QCOMPARE(request.provider.starts, QList{CursorPolicy::Include});
    QCOMPARE(request.outcomes(), 0);
    request.provider.complete();
    request.provider.complete();
    request.controller.cancel();
    QCOMPARE(request.outcomes(), 1);
    QCOMPARE(request.frames.size(), 1);
    const auto& frame = get<CaptureFrame>(request.frames.first().first());
    QCOMPARE(frame.image().pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(frame.image().pixelColor(1, 0), QColor(Qt::green));
    QCOMPARE(frame.image().pixelColor(0, 1), QColor(Qt::blue));
    QCOMPARE(frame.image().pixelColor(1, 1), QColor(Qt::white));
    QCOMPARE(frame.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QVERIFY(!frame.desktopMapping());
  }

  void cancelDuringDelay() {
    Request request;
    request.controller.cancel();
    request.controller.cancel();
    QVERIFY(!request.timer->isActive());
    QVERIFY(request.provider.starts.empty());
    QCOMPARE(request.provider.stops, 0);
    QCOMPARE(request.cancellations.size(), 1);
    QCOMPARE(request.outcomes(), 1);
  }

  void cancelDuringAcquisitionDiscardsLateFrame() {
    Request request;
    request.expire();
    QCOMPARE(request.provider.starts, QList{CursorPolicy::Exclude});
    request.controller.cancel();
    request.provider.complete();
    request.controller.cancel();
    QCOMPARE(request.provider.stops, 1);
    QCOMPARE(request.cancellations.size(), 1);
    QCOMPARE(request.outcomes(), 1);
  }

  void failureRetiresRequest() {
    Request request;
    request.expire();
    emit request.provider.failed("decode failed");
    request.provider.complete();
    request.controller.cancel();
    QCOMPARE(request.failures.size(), 1);
    QCOMPARE(request.failures.first().first().toString(), "decode failed");
    QCOMPARE(request.outcomes(), 1);
    QCOMPARE(request.provider.stops, 0);
  }

  void frameOwnsItsPixels() {
    auto image = grid();
    auto* pixel = reinterpret_cast<QRgb*>(image.bits());
    CaptureFrame frame(image);
    *pixel = qRgb(0, 0, 0);
    QCOMPARE(frame.image().pixelColor(0, 0), QColor(Qt::red));
  }

  void mixedScaleSeamUsesTheRecordedMosaic() {
    const OutputGeometry left{"left", {-1280, -2, 1280, 2}, OutputTransform::Normal, 1, {0, 0, 1920, 3}};
    const OutputGeometry right{"right", {0, -2, 2, 2}, OutputTransform::Rotate90, 1.5, {1920, 0, 3, 3}};
    QImage mosaic(1923, 3, QImage::Format_RGB32);
    mosaic.fill(Qt::red);
    mosaic.setPixelColor(1920, 0, Qt::blue);
    mosaic.setColorSpace(QColorSpace::SRgb);
    CaptureFrame frame(mosaic, DesktopMapping{{left, right}, 1.5});
    QVERIFY(frame.desktopMapping());
    const auto& mapping = *frame.desktopMapping();
    QCOMPARE(mapping.rasterEdge({-1280, -2}), QPointF(0, 0));
    auto seam = mapping.rasterEdge({0, -2});
    QCOMPARE(seam, QPointF(1920, 0));
    QCOMPARE(mapping.outputs.at(1).rasterBounds.topLeft(), seam.toPoint());
    QCOMPARE(mapping.outputs.at(1).transform, OutputTransform::Rotate90);
    QCOMPARE(frame.image().pixelColor(seam.toPoint() - QPoint(1, 0)), QColor(Qt::red));
    QCOMPARE(frame.image().pixelColor(seam.toPoint()), QColor(Qt::blue));
    QCOMPARE(mapping.rasterEdge({2, 0}), QPointF(frame.image().width(), frame.image().height()));
  }
};

QTEST_GUILESS_MAIN(CaptureTest)
#include "capture_test.moc"
