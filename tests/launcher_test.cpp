#include <QAccessible>
#include <QGuiApplication>
#include <QPalette>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>
#include <QVariant>

#include <memory>

class FakeWorkflow final : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString page MEMBER page NOTIFY pageChanged)
  Q_PROPERTY(QString target MEMBER target NOTIFY modesChanged)
  Q_PROPERTY(QVariantList modes READ modes NOTIFY modesChanged)
  Q_PROPERTY(QString message MEMBER message CONSTANT)
  Q_PROPERTY(QVariantList windows MEMBER windows CONSTANT)
  Q_PROPERTY(QString selectedWindow MEMBER selectedWindow CONSTANT)

 public:
  QString page = QStringLiteral("launcher");
  QString target;
  QString message;
  QVariantList windows;
  QString selectedWindow;
  int delay = -1;
  bool cursor = false;

  QVariantList modes() const {
    QVariantList result;
    for (const auto& name : {"area", "window", "screen"}) {
      const bool available = supported.contains(name);
      result.append(QVariantMap{{"name", name},
                                {"available", available},
                                {"reason", !ready || available ? QString() : unavailableReason(name)}});
    }
    return result;
  }

  static QString unavailableReason(const QString& name) {
    return QStringLiteral("Capture unavailable for %1").arg(name);
  }

  void publish(const QStringList& available, const QString& selection) {
    supported = available;
    target = selection;
    ready = true;
    emit modesChanged();
  }

  Q_INVOKABLE void selectMode(const QString& name) {
    target = name;
    emit modesChanged();
  }

  Q_INVOKABLE void startCapture(int delayMs, bool includeCursor) {
    delay = delayMs;
    cursor = includeCursor;
    setPage({});
  }

  void setPage(const QString& value) {
    page = value;
    emit pageChanged();
  }

 signals:
  void pageChanged();
  void modesChanged();
  void selectionChanged();

 private:
  QStringList supported;
  bool ready = false;
};

namespace {
QQuickItem* findItem(QQuickItem* item, const QString& name) {
  if (item->objectName() == name) {
    return item;
  }
  for (auto* child : item->childItems()) {
    if (auto* match = findItem(child, name)) {
      return match;
    }
  }
  return nullptr;
}
}  // namespace

class LauncherTest final : public QObject {
  Q_OBJECT

 private slots:
  void startupGeometry_data() {
    QTest::addColumn<QStringList>("available");
    QTest::addColumn<QString>("selected");
    QTest::newRow("all") << QStringList{"area", "window", "screen"} << QString("area");
    QTest::newRow("partial") << QStringList{"window", "screen"} << QString("screen");
    QTest::newRow("window only") << QStringList{"window"} << QString("window");
    QTest::newRow("none") << QStringList{} << QString();
  }

  void startupGeometry() {
    QFETCH(QStringList, available);
    QFETCH(QString, selected);
    FakeWorkflow workflow;
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/screenshot/Launcher.qml")));
    std::unique_ptr<QObject> launcher(
        component.createWithInitialProperties({{"workflow", QVariant::fromValue(&workflow)}}));
    QVERIFY2(launcher, qPrintable(component.errorString()));
    auto* window = qobject_cast<QQuickWindow*>(launcher.get());
    QVERIFY(window);
    QSignalSpy frames(window, &QQuickWindow::afterAnimating);
    window->requestUpdate();
    QTRY_VERIFY(!frames.isEmpty());

    const auto find = [window](const QString& name) { return findItem(window->contentItem(), name); };
    auto* capture = find("capture");
    QVERIFY(capture && !capture->isEnabled());
    const auto size = window->size();
    const auto minimumHeight = window->minimumHeight();
    const auto capturePosition = capture->mapToScene(QPointF());
    QVERIFY(minimumHeight > 0);
    QVERIFY(capturePosition.y() > 0);
    for (const auto& name : {"area", "window", "screen"}) {
      auto* button = find(QStringLiteral("target-") + name);
      QVERIFY(button && !button->isEnabled());
      auto* accessible = QAccessible::queryAccessibleInterface(button);
      QVERIFY(accessible);
      QVERIFY(accessible->text(QAccessible::Description).isEmpty());
      QVERIFY(!button->property("checked").toBool());
    }
    for (int index = 0; index < 4; ++index) {
      auto* delay = find(QStringLiteral("delay-%1").arg(index));
      QVERIFY(delay && delay->isEnabled());
    }
    auto* cursor = find("include-cursor");
    QVERIFY(cursor && cursor->isEnabled());

    QSignalSpy heights(window, &QQuickWindow::heightChanged);
    QSignalSpy widths(window, &QQuickWindow::widthChanged);
    QSignalSpy minimumHeights(window, &QQuickWindow::minimumHeightChanged);
    QSignalSpy captureMoves(capture, &QQuickItem::yChanged);
    frames.clear();
    workflow.publish(available, selected);
    window->requestUpdate();
    QTRY_VERIFY(!frames.isEmpty());
    QCOMPARE(window->size(), size);
    QCOMPARE(window->minimumHeight(), minimumHeight);
    QCOMPARE(capture->mapToScene(QPointF()), capturePosition);
    QVERIFY(heights.isEmpty() && widths.isEmpty() && minimumHeights.isEmpty() && captureMoves.isEmpty());
    QCOMPARE(capture->isEnabled(), !selected.isEmpty());
    for (const auto& name : {"area", "window", "screen"}) {
      auto* button = find(QStringLiteral("target-") + name);
      const bool supported = available.contains(name);
      QCOMPARE(button->isEnabled(), supported);
      QCOMPARE(button->property("checked").toBool(), selected == name);
      const auto reason = supported ? QString() : FakeWorkflow::unavailableReason(name);
      QCOMPARE(QAccessible::queryAccessibleInterface(button)->text(QAccessible::Description), reason);
      auto* tooltip = button->parentItem()->findChild<QObject*>("availability-tooltip");
      QVERIFY(tooltip);
      QCOMPARE(tooltip->property("text").toString(), reason);
    }
    if (!available.contains("window")) {
      auto* button = find("target-window");
      QTest::mouseMove(window, capture->mapToScene(capture->boundingRect().center()).toPoint());
      QTest::mouseMove(window, button->mapToScene(button->boundingRect().center()).toPoint());
      auto* tooltip = button->parentItem()->findChild<QObject*>("availability-tooltip");
      QTRY_VERIFY(tooltip->property("opened").toBool());
    }
  }

  void interactionAndLivePalette() {
    FakeWorkflow workflow;
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl(QStringLiteral("qrc:/screenshot/Launcher.qml")));
    std::unique_ptr<QObject> launcher(
        component.createWithInitialProperties({{"workflow", QVariant::fromValue(&workflow)}}));
    QVERIFY2(launcher, qPrintable(component.errorString()));
    auto* window = qobject_cast<QQuickWindow*>(launcher.get());
    QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowActive(window));

    const auto find = [window](const QString& name) { return findItem(window->contentItem(), name); };
    const auto checked = [](const QQuickItem* button) { return button->property("checked").toBool(); };
    const auto click = [window](QQuickItem* item) {
      QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                        item->mapToScene(item->boundingRect().center()).toPoint());
    };
    auto* capture = find("capture");
    auto* region = find("target-area");
    auto* unavailable = find("target-window");
    auto* screen = find("target-screen");
    auto* none = find("delay-0");
    auto* fiveSeconds = find("delay-2");
    auto* cursor = find("include-cursor");
    QVERIFY(capture && region && unavailable && screen && none && fiveSeconds && cursor);
    QVERIFY(!capture->isEnabled());
    QVERIFY(checked(none));
    QVERIFY(!unavailable->isEnabled());
    click(unavailable);
    QVERIFY(workflow.target.isEmpty());
    click(fiveSeconds);
    QVERIFY(checked(fiveSeconds));
    click(cursor);
    QVERIFY(checked(cursor));
    workflow.publish({"area", "screen"}, "area");
    click(region);
    QCOMPARE(workflow.target, QStringLiteral("area"));
    QVERIFY(checked(find("target-area")));

    screen = find("target-screen");
    QVERIFY(screen);
    screen->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_Space);
    QCOMPARE(workflow.target, QStringLiteral("screen"));
    QVERIFY(checked(find("target-screen")));
    QVERIFY(capture->isEnabled());
    QVERIFY(checked(fiveSeconds));
    click(fiveSeconds);
    QVERIFY(checked(fiveSeconds));
    QVERIFY(checked(cursor));

    const QPalette originalPalette = QGuiApplication::palette();
    QPalette changedPalette = originalPalette;
    changedPalette.setColor(QPalette::Highlight, QColor("#563d93"));
    changedPalette.setColor(QPalette::HighlightedText, QColor("#fff1d0"));
    QGuiApplication::setPalette(changedPalette);
    QTRY_COMPARE(capture->property("foreground").value<QColor>(), QColor("#fff1d0"));
    auto* captureBackground = qvariant_cast<QQuickItem*>(capture->property("background"));
    QVERIFY(captureBackground);
    QTRY_COMPARE(captureBackground->property("color").value<QColor>(), QColor("#563d93"));
    QVERIFY(checked(find("target-screen")));
    QVERIFY(checked(fiveSeconds));
    QGuiApplication::setPalette(originalPalette);

    capture->forceActiveFocus(Qt::TabFocusReason);
    QTest::keyClick(window, Qt::Key_Space);
    QCOMPARE(workflow.delay, 5000);
    QVERIFY(workflow.cursor);
    QTRY_VERIFY(!window->isVisible());
    workflow.setPage(QStringLiteral("launcher"));
    QTRY_VERIFY(window->isVisible());
    QVERIFY(checked(fiveSeconds));
  }
};

QTEST_MAIN(LauncherTest)
#include "launcher_test.moc"
