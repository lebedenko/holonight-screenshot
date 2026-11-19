#include "png_output.h"

#include <QApplication>
#include <QColorSpace>
#include <QFileDialog>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace screenshot;

class PngOutputTest final : public QObject {
  Q_OBJECT

 private slots:
  void init() { QVERIFY(qputenv("PATH", directory_.path().toLocal8Bit())); }
  void cleanup() { qputenv("PATH", originalPath_); }

  void helperFailures_data() {
    QTest::addColumn<QByteArray>("script");
    QTest::newRow("missing") << QByteArray();
    QTest::newRow("failed") << QByteArray("echo helper-error >&2\nexit 1\n");
    QTest::newRow("timeout") << QByteArray("exec /usr/bin/sleep 30\n");
  }

  void helperFailures() {
    QFETCH(QByteArray, script);
    if (!script.isEmpty()) {
      QVERIFY(installHelper(script));
    }
    QVERIFY(copyPng(image()).contains(QStringLiteral("wl-copy")));
    if (!script.isEmpty()) {
      QVERIFY(QFile::remove(directory_.filePath("wl-copy")));
    }
  }

  void copyAndSavePreservePixelsAndColorSpace() {
    const auto expected = image();
    const auto destination = directory_.filePath("saved.png");
    const auto clipboard = directory_.filePath("clipboard.png");
    QVERIFY(qputenv("SCREENSHOT_TEST_CLIPBOARD", clipboard.toLocal8Bit()));
    QVERIFY(
        installHelper("[ \"$*\" = '--type image/png' ] || exit 1\n"
                      "exec /usr/bin/cat > \"$SCREENSHOT_TEST_CLIPBOARD\"\n"));

    QCOMPARE(savePng(destination, expected), QString());
    QCOMPARE(copyPng(expected), QString());
    for (const auto& path : {destination, clipboard}) {
      const QImage decoded(path);
      QCOMPARE(decoded.convertToFormat(expected.format()), expected);
      QCOMPARE(decoded.colorSpace(), expected.colorSpace());
    }
  }

  void failedWriteAndDialogCancellationPreserveDestination() {
    const auto destination = directory_.filePath("existing.png");
    QFile existing(destination);
    QVERIFY(existing.open(QIODevice::WriteOnly));
    QCOMPARE(existing.write("existing data"), 13);
    existing.close();

    QVERIFY(!savePng(destination, {}).isEmpty());
    QVERIFY(!savePng(directory_.path(), image()).isEmpty());
    QTimer::singleShot(0, [] {
      auto* dialog = QApplication::activeModalWidget();
      if (dialog) {
        dialog->close();
      }
    });
    QCOMPARE(choosePngPath(), QString());

    QVERIFY(existing.open(QIODevice::ReadOnly));
    QCOMPARE(existing.readAll(), QByteArray("existing data"));
  }

 private:
  bool installHelper(const QByteArray& script) {
    QFile helper(directory_.filePath("wl-copy"));
    const auto contents = "#!/bin/sh\n" + script;
    return helper.open(QIODevice::WriteOnly) && helper.write(contents) == contents.size() &&
           helper.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
  }

  static QImage image() {
    QImage image(2, 1, QImage::Format_RGB32);
    image.setColorSpace(QColorSpace::DisplayP3);
    image.setPixelColor(0, 0, Qt::red);
    image.setPixelColor(1, 0, Qt::blue);
    return image;
  }

  QTemporaryDir directory_;
  const QByteArray originalPath_ = qgetenv("PATH");
};

QTEST_MAIN(PngOutputTest)
#include "png_output_test.moc"
