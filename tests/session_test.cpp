#include "capture_controller.h"
#include "capture_session.h"
#include "grim_acquisition.h"
#include "toplevel-server.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSignalSpy>
#include <QSocketNotifier>
#include <QTemporaryDir>
#include <QtTest>

#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wayland-server-core.h>

using namespace screenshot;

namespace {

constexpr auto png = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC";

struct Registry {
  wl_display* display = wl_display_create();
  wl_event_loop* loop = wl_display_get_event_loop(display);
  QSocketNotifier events{wl_event_loop_get_fd(loop), QSocketNotifier::Read};
  QList<wl_resource*> handles;

  Registry() {
    QObject::connect(&events, &QSocketNotifier::activated, &events, [this] {
      wl_event_loop_dispatch(loop, 0);
      wl_display_flush_clients(display);
    });
  }

  ~Registry() {
    events.setEnabled(false);
    wl_display_destroy_clients(display);
    wl_display_destroy(display);
  }

  int connectClient() {
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds) < 0) {
      return -1;
    }
    if (!wl_client_create(display, fds[0])) {
      close(fds[0]);
      close(fds[1]);
      return -1;
    }
    return fds[1];
  }

  bool connectInProcess() {
    const int fd = connectClient();
    if (fd < 0) {
      return false;
    }
    qputenv("WAYLAND_SOCKET", QByteArray::number(fd));
    return true;
  }

  bool startCli(QProcess& cli, const QStringList& arguments) {
    const int fd = connectClient();
    if (fd < 0) {
      return false;
    }
    constexpr int childFd = 198;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("WAYLAND_SOCKET", QString::number(childFd));
    cli.setProcessEnvironment(environment);
    cli.setChildProcessModifier([fd] {
      if (dup2(fd, childFd) < 0) {
        _exit(127);
      }
    });
    cli.start(QStringLiteral(SCREENSHOT_APP), arguments);
    close(fd);
    return true;
  }

  void advertise(const wl_interface& interface) {
    wl_global_create(display, &interface, 1, nullptr, [](wl_client*, void*, uint32_t, uint32_t) {});
  }

  void advertiseWindows(bool populate) {
    wl_global_create(
        display, &ext_foreign_toplevel_list_v1_interface, 1, populate ? this : nullptr,
        [](wl_client* client, void* data, uint32_t, uint32_t id) {
          auto* list = wl_resource_create(client, &ext_foreign_toplevel_list_v1_interface, 1, id);
          static const struct ext_foreign_toplevel_list_v1_interface listRequests{
              .stop = [](wl_client*, wl_resource* list) { ext_foreign_toplevel_list_v1_send_finished(list); },
              .destroy = [](wl_client*, wl_resource* list) { wl_resource_destroy(list); }};
          wl_resource_set_implementation(list, &listRequests, nullptr, nullptr);
          if (auto* self = static_cast<Registry*>(data)) {
            self->addWindow(client, list, "first");
            self->addWindow(client, list, "second");
          }
        });
  }

  void addWindow(wl_client* client, wl_resource* list, const char* id) {
    auto* handle = wl_resource_create(client, &ext_foreign_toplevel_handle_v1_interface, 1, 0);
    static const struct ext_foreign_toplevel_handle_v1_interface handleRequests{
        .destroy = [](wl_client*, wl_resource* handle) { wl_resource_destroy(handle); }};
    wl_resource_set_implementation(handle, &handleRequests, this, [](wl_resource* handle) {
      static_cast<Registry*>(wl_resource_get_user_data(handle))->handles.removeOne(handle);
    });
    handles.append(handle);
    ext_foreign_toplevel_list_v1_send_toplevel(list, handle);
    ext_foreign_toplevel_handle_v1_send_identifier(handle, id);
    ext_foreign_toplevel_handle_v1_send_title(handle, "Duplicate title");
    ext_foreign_toplevel_handle_v1_send_app_id(handle, "test.app");
    ext_foreign_toplevel_handle_v1_send_done(handle);
  }

  void closeFirst() {
    ext_foreign_toplevel_handle_v1_send_closed(handles.front());
    wl_display_flush_clients(display);
  }
};

constexpr wl_interface screenProtocol{"zwlr_screencopy_manager_v1", 1, 0, nullptr, 0, nullptr};
constexpr wl_interface layerProtocol{"zwlr_layer_shell_v1", 1, 0, nullptr, 0, nullptr};
constexpr wl_interface layoutProtocol{"zwlr_output_manager_v1", 1, 0, nullptr, 0, nullptr};
constexpr wl_interface imageProtocol{"ext_image_copy_capture_manager_v1", 1, 0, nullptr, 0, nullptr};
constexpr wl_interface sourceProtocol{
    "ext_foreign_toplevel_image_capture_source_manager_v1", 1, 0, nullptr, 0, nullptr};

bool writeHelper(const QDir& dir, const QString& name, const QByteArray& script) {
  QFile file(dir.filePath(name));
  return file.open(QIODevice::WriteOnly) && file.write("#!/bin/sh\n" + script) > 0 &&
         file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

QByteArray grimScript(bool toplevel) {
  QByteArray script = "if [ \"$1\" = -h ]; then\n";
  script += toplevel ? "  echo '  -T <identifier>  Capture a window.'\n" : "  echo '  -s <scale>'\n";
  script += "  exit 0\nfi\n";
  script += "if [ \"$3\" = -T ] && [ \"$4\" != second ]; then echo 'wrong capture ID' >&2; exit 1; fi\n";
  return script + "echo '" + png + "' | /usr/bin/base64 -d\n";
}

constexpr auto layout =
    R"(echo '[{"name":"test","enabled":true,"position":{"x":0,"y":0},"transform":"normal","scale":1,"modes":[{"width":1,"height":1,"refresh":60,"current":true}]}]')";

class SessionTest final : public QObject {
  Q_OBJECT
 private slots:
  void init() {
    QVERIFY(helpers.isValid());
    qputenv("PATH", helpers.path().toUtf8());
    qputenv("XDG_CURRENT_DESKTOP", "labwc");
    qunsetenv("WAYLAND_SOCKET");
    qunsetenv("HYPRLAND_INSTANCE_SIGNATURE");
    qunsetenv("SWAYSOCK");
    QDir helperDir(helpers.path());
    for (const auto& path : helperDir.entryList(QDir::Files)) {
      QVERIFY(helperDir.remove(path));
    }
  }

  void capabilities_data() {
    QTest::addColumn<QString>("missing");
    QTest::addColumn<QString>("desktop");
    QTest::addColumn<QStringList>("expected");
    QTest::newRow("labwc") << QString() << QString("labwc") << QStringList{"screen", "area", "window"};
    QTest::newRow("sway empty list") << QString() << QString("sway")
                                     << QStringList{"screen", "area", "window", "active-window"};
    QTest::newRow("hyprland") << QString() << QString("Hyprland")
                              << QStringList{"screen", "area", "window", "active-window"};
    QTest::newRow("no selection overlay") << QString("layer") << QString("labwc") << QStringList{"screen", "window"};
    QTest::newRow("no output capture") << QString("screen") << QString("labwc") << QStringList{"window"};
    QTest::newRow("no layout helper") << QString("layout") << QString("labwc") << QStringList{"window"};
    QTest::newRow("no list protocol") << QString("list") << QString("labwc") << QStringList{"screen", "area"};
    QTest::newRow("no image source") << QString("source") << QString("labwc") << QStringList{"screen", "area"};
    QTest::newRow("no image manager") << QString("image") << QString("labwc") << QStringList{"screen", "area"};
    QTest::newRow("old grim") << QString("grim-T") << QString("labwc") << QStringList{"screen", "area"};
    QTest::newRow("no grim") << QString("grim") << QString("labwc") << QStringList{};
    QTest::newRow("no focus helper") << QString("focus") << QString("sway") << QStringList{"screen", "area", "window"};
  }

  void capabilities() {
    QFETCH(QString, missing);
    QFETCH(QString, desktop);
    QFETCH(QStringList, expected);
    qputenv("XDG_CURRENT_DESKTOP", desktop.toUtf8());
    Registry registry;
    if (missing != "layer") registry.advertise(layerProtocol);
    if (missing != "screen") registry.advertise(screenProtocol);
    if (missing != "source") registry.advertise(sourceProtocol);
    if (missing != "image") registry.advertise(imageProtocol);
    if (missing != "list") registry.advertiseWindows(false);
    registry.advertise(layoutProtocol);

    if (missing != "grim") QVERIFY(writeHelper(QDir(helpers.path()), "grim", grimScript(missing != "grim-T")));
    if (missing != "layout") QVERIFY(writeHelper(QDir(helpers.path()), "wlr-randr", layout));
    if (missing != "focus") {
      QVERIFY(writeHelper(QDir(helpers.path()), "swaymsg", "echo '{}'"));
      QVERIFY(writeHelper(QDir(helpers.path()), "hyprctl", "echo '{}'"));
    }

    QVERIFY(registry.connectInProcess());
    CaptureSession session;
    QSignalSpy discovered(&session, &CaptureSession::discovered);
    session.discover();
    QTRY_COMPARE(discovered.size(), 1);
    QVERIFY(session.windows().isEmpty());
    QStringList names;
    for (auto target : captureTargets) {
      if (session.capabilities().supports(target)) names.append(targetName(target));
    }
    QCOMPARE(names, expected);

    QProcess cli;
    QVERIFY(registry.startCli(cli, {"--list-targets"}));
    QTRY_COMPARE(cli.state(), QProcess::NotRunning);
    QCOMPARE(cli.exitCode(), 0);
    QJsonArray supported;
    for (const auto& name : expected) supported.append(name);
    QCOMPARE(cli.readAllStandardOutput().trimmed(), QJsonDocument(supported).toJson(QJsonDocument::Compact));
  }

  void slowProbe() {
    Registry registry;
    registry.advertiseWindows(true);
    registry.advertise(sourceProtocol);
    registry.advertise(imageProtocol);
    const auto release = helpers.filePath("release");
    const auto script = QByteArray("while [ ! -e '") + QFile::encodeName(release) +
                        "' ]; do /usr/bin/sleep 0.02; done\necho '  -T <identifier>'";
    QVERIFY(writeHelper(QDir(helpers.path()), "grim", script));
    QVERIFY(registry.connectInProcess());

    CaptureSession session;
    QSignalSpy discovered(&session, &CaptureSession::discovered);
    session.discover();
    session.discover();
    QTRY_COMPARE(session.windows().size(), 2);
    QVERIFY(discovered.isEmpty());
    QFile gate(release);
    QVERIFY(gate.open(QIODevice::WriteOnly));
    gate.close();
    QTRY_COMPARE(discovered.size(), 1);
    QVERIFY(session.capabilities().supports(CaptureTarget::Window));
    session.discover();
    QTest::qWait(50);
    QCOMPARE(discovered.size(), 1);
  }

  void probeDeadline() {
    Registry registry;
    registry.advertiseWindows(false);
    registry.advertise(sourceProtocol);
    registry.advertise(imageProtocol);
    QVERIFY(writeHelper(QDir(helpers.path()), "grim", "exec /usr/bin/sleep 30"));
    QVERIFY(registry.connectInProcess());

    CaptureSession session;
    QSignalSpy discovered(&session, &CaptureSession::discovered);
    session.discover();
    QTRY_COMPARE_WITH_TIMEOUT(discovered.size(), 1, 3500);
    QCOMPARE(session.capabilities().reason(CaptureTarget::Window), QString("grim with -T support is required."));
  }

  void shutdownDuringProbe() {
    Registry registry;
    const auto pidPath = helpers.filePath("probe.pid");
    const auto script = QByteArray("echo $$ > '") + QFile::encodeName(pidPath) + "'\nexec /usr/bin/sleep 30";
    QVERIFY(writeHelper(QDir(helpers.path()), "grim", script));
    QVERIFY(registry.connectInProcess());

    auto session = std::make_unique<CaptureSession>();
    QSignalSpy discovered(session.get(), &CaptureSession::discovered);
    session->discover();
    QTRY_VERIFY(QFileInfo(pidPath).size() > 0);
    QFile pidFile(pidPath);
    QVERIFY(pidFile.open(QIODevice::ReadOnly));
    const auto pid = pidFile.readAll().trimmed().toInt();
    QVERIFY(pid > 0);
    QCOMPARE(kill(pid, 0), 0);
    QElapsedTimer shutdown;
    shutdown.start();
    session.reset();
    QVERIFY(shutdown.elapsed() < 1000);
    QCOMPARE(kill(pid, 0), -1);
    QCOMPARE(errno, ESRCH);
    QCoreApplication::processEvents();
    QVERIFY(discovered.isEmpty());
  }

  void windowCapture() {
    Registry registry;
    registry.advertiseWindows(true);
    registry.advertise(sourceProtocol);
    registry.advertise(imageProtocol);
    QVERIFY(writeHelper(QDir(helpers.path()), "grim", grimScript(true)));

    QVERIFY(registry.connectInProcess());
    CaptureSession session;
    QSignalSpy discovered(&session, &CaptureSession::discovered);
    session.discover();
    QTRY_COMPARE(discovered.size(), 1);
    const auto rows = session.windows();
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.front().title, rows.back().title);
    QCOMPARE(rows.front().appId, QString("test.app"));
    QCOMPARE(rows.front().id, QString("first"));
    QCOMPARE(rows.back().id, QString("second"));

    using namespace std::chrono_literals;
    GrimAcquisition closed(session, CaptureTarget::Window, rows.front().id);
    CaptureController delayed(closed, 250ms, CursorPolicy::Exclude);
    QSignalSpy failed(&delayed, &CaptureController::failed);
    registry.closeFirst();
    QTRY_COMPARE(session.windows().size(), 1);
    QTRY_COMPARE(failed.size(), 1);
    QCOMPARE(failed.front().front().toString(), QString("The selected window closed before capture."));

    GrimAcquisition open(session, CaptureTarget::Window, rows.back().id);
    std::optional<CaptureFrame> result;
    connect(&open, &DesktopAcquisition::captured, this, [&](const CaptureFrame& frame) { result = frame; });
    open.start(CursorPolicy::Include);
    QTRY_VERIFY(result.has_value());
    QCOMPARE(result->image().size(), QSize(1, 1));
    QVERIFY(!result->desktopMapping());
  }

  void focusAtExpiry_data() {
    QTest::addColumn<QString>("desktop");
    QTest::newRow("hyprland") << QString("Hyprland");
    QTest::newRow("sway") << QString("sway");
  }

  void focusAtExpiry() {
    QFETCH(QString, desktop);
    qputenv("XDG_CURRENT_DESKTOP", desktop.toUtf8());
    Registry registry;
    registry.advertiseWindows(false);
    registry.advertise(sourceProtocol);
    registry.advertise(imageProtocol);
    QVERIFY(writeHelper(QDir(helpers.path()), "grim", grimScript(true)));
    const auto helper = desktop == "Hyprland" ? QString("hyprctl") : QString("swaymsg");
    QVERIFY(writeHelper(QDir(helpers.path()), helper, "echo '{}'"));
    QVERIFY(registry.connectInProcess());
    CaptureSession session;
    QSignalSpy discovered(&session, &CaptureSession::discovered);
    session.discover();
    QTRY_COMPARE(discovered.size(), 1);
    QVERIFY(session.capabilities().supports(CaptureTarget::ActiveWindow));

    GrimAcquisition empty(session, CaptureTarget::ActiveWindow);
    QSignalSpy failed(&empty, &DesktopAcquisition::failed);
    empty.start(CursorPolicy::Exclude);
    QTRY_COMPARE(failed.size(), 1);
    QVERIFY(failed.front().front().toString().contains("no focused window"));
    QVERIFY(session.capabilities().supports(CaptureTarget::ActiveWindow));

    using namespace std::chrono_literals;
    GrimAcquisition focused(session, CaptureTarget::ActiveWindow);
    CaptureController delayed(focused, 50ms, CursorPolicy::Exclude);
    bool captured = false;
    connect(&delayed, &CaptureController::captured, this, [&] { captured = true; });
    QVERIFY(writeHelper(
        QDir(helpers.path()), helper,
        desktop == "Hyprland"
            ? R"(echo '{"stableId":"second"}')"
            : R"(echo '{"nodes":[{"floating_nodes":[{"focused":true,"foreign_toplevel_identifier":"second"}]}]}')"));
    QTRY_VERIFY(captured);
  }

  void screenOutput() {
    Registry registry;
    registry.advertise(screenProtocol);
    registry.advertise(layoutProtocol);
    QVERIFY(writeHelper(QDir(helpers.path()), "wlr-randr", layout));
    QVERIFY(writeHelper(QDir(helpers.path()), "grim", grimScript(true)));
    const auto destination = helpers.filePath("capture.png");
    QProcess cli;
    QVERIFY(registry.startCli(cli, {"--output", destination, "--delay-ms", "0"}));
    QTRY_COMPARE(cli.state(), QProcess::NotRunning);
    QCOMPARE(cli.exitCode(), 0);
    QVERIFY(!QImage(destination).isNull());

    QFile file(destination);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto original = file.readAll();
    file.close();
    QVERIFY(writeHelper(QDir(helpers.path()), "grim", "echo invalid-png"));
    QVERIFY(registry.startCli(cli, {"--target", "screen", "--output", destination}));
    QTRY_COMPARE(cli.state(), QProcess::NotRunning);
    QCOMPARE(cli.exitCode(), 1);
    QVERIFY(cli.readAllStandardError().contains("invalid PNG"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);
  }

 private:
  QTemporaryDir helpers;
};

}  // namespace

QTEST_GUILESS_MAIN(SessionTest)
#include "session_test.moc"
