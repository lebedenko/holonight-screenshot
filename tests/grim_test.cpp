#include "grim_acquisition.h"

#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace screenshot;

namespace {

constexpr auto layout = R"([
  {"id":0,"name":"left","width":1280,"height":3,"x":-1280,"y":-3,"transform":0,
   "scale":1,"disabled":false,"dpmsStatus":true,"mirrorOf":"none","focused":true},
  {"id":1,"name":"right","width":6,"height":3,"x":0,"y":-2,"transform":1,
   "scale":1.5,"disabled":false,"dpmsStatus":true,"mirrorOf":"none","focused":false}
])";

bool writeFile(const QString& path, const QByteArray& data) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() &&
         file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

}  // namespace

class GrimTest final : public QObject {
  Q_OBJECT

 private slots:
  void init() {
    directory_ = std::make_unique<QTemporaryDir>();
    QVERIFY(directory_->isValid());
    previousPath_ = qgetenv("PATH");
    qputenv("PATH", QFile::encodeName(directory_->path()));
    qputenv("FIXTURE", QFile::encodeName(directory_->path()));
    QVERIFY(writeFile(path("before"), layout));
    QByteArray after(layout);
    after.replace("\"focused\":true", "\"focused\":false");
    QVERIFY(writeFile(path("after"), after));
    QVERIFY(writeFile(path("hyprctl"), R"(#!/bin/sh
[ "$*" = "-j monitors" ] || exit 1
if [ -f "$FIXTURE/queried" ]; then
  : > "$FIXTURE/rechecked"
  exec /bin/cat "$FIXTURE/after"
fi
: > "$FIXTURE/queried"
exec /bin/cat "$FIXTURE/before"
)"));
    QVERIFY(writeFile(path("grim"), R"(#!/bin/sh
printf '%s\n' "$*" >> "$FIXTURE/args"
printf 'stderr must not enter PNG stdout\n' >&2
exec /bin/cat "$FIXTURE/frame.png"
)"));
    QImage image(1923, 7, QImage::Format_RGB32);
    image.setColorSpace(QColorSpace::SRgb);
    image.fill(Qt::red);
    image.setPixelColor(1920, 2, Qt::blue);
    QVERIFY(image.save(path("frame.png"), "PNG"));
  }

  void cleanup() {
    qputenv("PATH", previousPath_);
    qunsetenv("FIXTURE");
    directory_.reset();
  }

  void capturesMosaic_data() {
    QTest::addColumn<bool>("includeCursor");
    QTest::newRow("default") << false;
    QTest::newRow("cursor") << true;
  }

  void capturesMosaic() {
    QFETCH(bool, includeCursor);
    GrimAcquisition acquisition;
    QSignalSpy frames(&acquisition, &DesktopAcquisition::captured);
    QSignalSpy failures(&acquisition, &DesktopAcquisition::failed);
    acquisition.start(includeCursor ? CursorPolicy::Include : CursorPolicy::Exclude);
    QTRY_COMPARE(frames.size(), 1);
    QCOMPARE(failures.size(), 0);
    QFile arguments(path("args"));
    QVERIFY(arguments.open(QIODevice::ReadOnly));
    QCOMPARE(arguments.readAll(), includeCursor ? "-s 1.5 -t png -c -\n" : "-s 1.5 -t png -\n");

    const auto& frame = get<CaptureFrame>(frames.first().first());
    QCOMPARE(frame.colorSpace(), QColorSpace(QColorSpace::SRgb));
    QVERIFY(frame.desktopMapping());
    const auto& mapping = *frame.desktopMapping();
    QCOMPARE(mapping.rasterEdge({0, -3}), QPointF(1920, 0));
    QCOMPARE(mapping.outputs.at(0).rasterBounds, QRect(0, 0, 1920, 5));
    QCOMPARE(mapping.outputs.at(1).logicalBounds, QRectF(0, -2, 2, 4));
    QCOMPARE(mapping.outputs.at(1).transform, OutputTransform::Rotate90);
    QCOMPARE(mapping.outputs.at(1).rasterBounds, QRect(1920, 1, 3, 6));
    QCOMPARE(mapping.rasterEdge({2, 2}), QPointF(1923, 7.5));
    QCOMPARE(frame.image().pixelColor(1919, 2), QColor(Qt::red));
    QCOMPARE(frame.image().pixelColor(1920, 2), QColor(Qt::blue));
  }

  void rejectsFailedCapture_data() {
    QTest::addColumn<QString>("file");
    QTest::addColumn<QByteArray>("contents");
    QTest::addColumn<QString>("diagnostic");
    QTest::newRow("missing grim") << QString("grim") << QByteArray() << QString("grim:");
    QTest::newRow("grim exit") << QString("grim") << QByteArray("#!/bin/sh\necho denied >&2\nexit 4\n")
                               << QString("grim failed (exit 4). denied");
    QTest::newRow("grim crash") << QString("grim") << QByteArray("#!/bin/sh\nkill -9 $$\n") << QString("grim:");
    QTest::newRow("invalid PNG") << QString("frame.png") << QByteArray("garbage") << QString("invalid PNG");
    QTest::newRow("invalid initial layout") << QString("before") << QByteArray("[]") << QString("Invalid");
    QByteArray changed(layout);
    changed.replace("\"x\":0", "\"x\":1");
    QTest::newRow("changed layout") << QString("after") << changed << QString("changed");
    QByteArray mirrored(layout);
    mirrored.replace("\"mirrorOf\":\"none\"", "\"mirrorOf\":\"left\"");
    QTest::newRow("mirror") << QString("before") << mirrored << QString("unsupported");
    QByteArray poweredOff(layout);
    poweredOff.replace("\"dpmsStatus\":true", "\"dpmsStatus\":false");
    QTest::newRow("powered off") << QString("after") << poweredOff << QString("unsupported");
    QTest::newRow("layout timeout") << QString("hyprctl") << QByteArray("#!/bin/sh\nexec /bin/sleep 30\n")
                                    << QString("hyprctl timed out");
    QTest::newRow("grim timeout") << QString("grim") << QByteArray("#!/bin/sh\nexec /bin/sleep 30\n")
                                  << QString("grim timed out");
  }

  void rejectsFailedCapture() {
    QFETCH(QString, file);
    QFETCH(QByteArray, contents);
    QFETCH(QString, diagnostic);
    if (contents.isEmpty()) {
      QVERIFY(QFile::remove(path(file)));
    } else {
      QVERIFY(writeFile(path(file), contents));
    }
    GrimAcquisition acquisition;
    QSignalSpy frames(&acquisition, &DesktopAcquisition::captured);
    QSignalSpy failures(&acquisition, &DesktopAcquisition::failed);
    acquisition.start(CursorPolicy::Exclude);
    QTRY_COMPARE_WITH_TIMEOUT(failures.size(), 1, 17000);
    QCOMPARE(frames.size(), 0);
    QVERIFY(failures.first().first().toString().contains(diagnostic));
  }

  void cancelKillsGrimAndSkipsSecondLayout() {
    QVERIFY(writeFile(path("grim"), "#!/bin/sh\necho $$ > \"$FIXTURE/pid\"\nexec /bin/sleep 30\n"));
    GrimAcquisition acquisition;
    QSignalSpy frames(&acquisition, &DesktopAcquisition::captured);
    QSignalSpy failures(&acquisition, &DesktopAcquisition::failed);
    acquisition.start(CursorPolicy::Exclude);
    QTRY_VERIFY(QFileInfo(path("pid")).size() > 0);
    QFile pid(path("pid"));
    QVERIFY(pid.open(QIODevice::ReadOnly));
    const auto processPath = QStringLiteral("/proc/") + QString::fromLatin1(pid.readAll().trimmed());
    QVERIFY(QFileInfo::exists(processPath));
    acquisition.cancel();
    QTRY_VERIFY(!QFileInfo::exists(processPath));
    QVERIFY(!QFileInfo::exists(path("rechecked")));
    QCOMPARE(frames.size(), 0);
    QCOMPARE(failures.size(), 0);
  }

 private:
  QString path(const QString& name) const { return directory_->filePath(name); }

  std::unique_ptr<QTemporaryDir> directory_;
  QByteArray previousPath_;
};

QTEST_GUILESS_MAIN(GrimTest)
#include "grim_test.moc"
