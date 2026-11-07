#include <iostream>
#include <string>
#include <string_view>
#include <vector>
#include <cstring>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#include <cstdio>
#else
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <csignal>
#include <malloc.h>
#endif

#include <QGuiApplication>
#include <QSocketNotifier>
#include <QJsonObject>
#include <QDateTime>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include "core/DaemonServer.hpp"
#include "core/LayerShellWindow.hpp"
#include "core/SingleInstance.hpp"
#include "ui/ShellBridge.hpp"
#include "ui/ThemeManager.hpp"
#include "ui/IconProviders.hpp"
#include "audio/AudioEngine.hpp"
#include <QTimer>
#include "search/AppIndexer.hpp"
#ifdef _WIN32
#include "core/GlobalHotkey_win.hpp"
#endif

namespace {

constexpr const char* VERSION_STRING = "0.3.0";

void printHelp() {
    std::cout << "RavenSpot Desktop Launcher v" << VERSION_STRING << "\n\n"
              << "Usage: ravenspot [OPTIONS] [COMMAND]\n\n"
              << "Commands:\n"
              << "  toggle         Toggle window visibility on active monitor (default)\n"
              << "  show           Display the search window\n"
              << "  hide           Hide the search window\n"
              << "  status         Print daemon status (JSON)\n"
              << "  reload         Reload themes and application configuration\n"
              << "  quit           Gracefully terminate background daemon\n\n"
              << "Options:\n"
              << "  -d, --daemon   Start resident background daemon (starts hidden)\n"
              << "  -h, --help     Show this help message\n"
              << "  -v, --version  Print version information\n";
}

#ifdef _WIN32
// GUI-subsystem exe: print CLI replies ("status", "--help") into the console
// that started us, if any. Double-clicking the exe has no console: no-op.
void attachParentConsole() {
    // stdout already redirected (pipe/file, e.g. CI or "ravenspot status > x"): keep it
    const HANDLE out = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (out && out != INVALID_HANDLE_VALUE && ::GetFileType(out) != FILE_TYPE_UNKNOWN) return;
    if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
        FILE *f = nullptr;
        freopen_s(&f, "CONOUT$", "w", stdout);
        freopen_s(&f, "CONOUT$", "w", stderr);
        std::cout.clear();
        std::cerr.clear();
    }
}

void trimHeap() {}
void setupSignalHandlers(QGuiApplication &) {} // closing is "ravenspot quit" or logoff
#else
void trimHeap() { ::malloc_trim(0); }

int sigFd[2];

void signalHandler(int) {
    char a = 1;
    (void)::write(sigFd[0], &a, sizeof(a));
}

void setupSignalHandlers(QGuiApplication &app) {
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, sigFd) != 0) {
        return;
    }

    auto *notifier = new QSocketNotifier(sigFd[1], QSocketNotifier::Read, &app);
    QObject::connect(notifier, &QSocketNotifier::activated, [&app, notifier]() {
        notifier->setEnabled(false);
        char a;
        (void)::read(sigFd[1], &a, sizeof(a));
        app.quit();
    });

    struct sigaction sa{};
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
}
#endif

} // anonymous namespace

int main(int argc, char *argv[]) {
#ifdef _WIN32
    if (argc > 1) attachParentConsole();
#endif
    std::string command;
    bool daemonMode = false;

    // Fast argument parsing
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printHelp();
            return 0;
        }
        if (arg == "--version" || arg == "-v") {
            std::cout << "ravenspot " << VERSION_STRING << " (C++20 native)\n";
            return 0;
        }
        if (arg == "--daemon" || arg == "-d" || arg == "daemon" || arg == "start-daemon") {
            daemonMode = true;
        } else if (arg == "toggle" || arg == "--toggle") {
            command = "toggle";
        } else if (arg == "show" || arg == "--show") {
            command = "show";
        } else if (arg == "hide" || arg == "--hide") {
            command = "hide";
        } else if (arg == "quit" || arg == "--quit" || arg == "exit") {
            command = "quit";
        } else if (arg == "status" || arg == "--status") {
            command = "status";
        } else if (arg == "reload" || arg == "--reload") {
            command = "reload";
        }
    }

    if (command.empty() && !daemonMode) {
        command = "toggle";
    }

    // =========================================================================
    // FAST CLI CLIENT PROBE (< 1ms, zero Qt GUI initialized)
    // =========================================================================
    if (!command.empty() && !daemonMode) {
        std::string response;
        bool ok = ravenspot::SingleInstance::sendCommand(command, &response, 200);
        if (ok) {
            std::cout << response;
            return 0;
        }

        // Daemon not running
        if (command == "quit") {
            std::cout << "ravenspot: daemon is not running.\n";
            return 0;
        }
        if (command == "status") {
            std::cout << "{\"status\":\"not_running\",\"running\":false,\"visible\":false}\n";
            return 1;
        }
        if (command == "hide") {
            return 0;
        }

        // If 'toggle' or 'show' was invoked and daemon is not running:
        // Launch daemon in foreground mode (startHidden = false)
        daemonMode = true;
    }

    // =========================================================================
    // DAEMON INITIALIZATION PATH
    // =========================================================================
    ravenspot::SingleInstance singleInstance;
    if (!singleInstance.tryAcquireLock()) {
        std::cout << "ravenspot: daemon already running (PID " << singleInstance.existingPid() << ")\n";
        return 0;
    }

#ifndef _WIN32
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
#endif
    // Windows: Qt's default Direct3D 11 (translucent windows via DirectComposition,
    // WARP fallback without a GPU). QSG_RHI_BACKEND=opengl switches if needed.

    QGuiApplication app(argc, argv);

    // Qt SVG "Invalid path data; path truncated." uyarısı hangi dosyadan geldiğini
    // söylemiyor: son istenen ikon kimliğini ekleyerek logla (bir sonraki
    // tekrarında bozuk ikon kesinleşsin).
    static QtMessageHandler previousHandler = nullptr;
    previousHandler = qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &ctx, const QString &msg) {
        if (msg.startsWith(QLatin1String("Invalid path data"))) {
            const QString annotated = msg + QStringLiteral(" [last icon request: ") + ravenspot::lastRequestedIcon() + QLatin1Char(']');
            if (previousHandler) previousHandler(type, ctx, annotated);
            return;
        }
        if (previousHandler) previousHandler(type, ctx, msg);
    });
    QGuiApplication::setApplicationName(QStringLiteral("ravenspot"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.3.0"));
    QGuiApplication::setOrganizationName(QStringLiteral("CachyOS"));
    QGuiApplication::setOrganizationDomain(QStringLiteral("cachyos.org"));
    QGuiApplication::setDesktopFileName(QStringLiteral("ravenspot"));

    setupSignalHandlers(app);
    ravenspot::setupIconThemeEnvironment();

    bool startHidden = (daemonMode && command != "toggle" && command != "show");

    auto themeManager = std::make_unique<ravenspot::ThemeManager>(&app);
    auto audioEngine = std::make_unique<ravenspot::AudioEngine>(&app);
    auto appIndexer = std::make_unique<ravenspot::AppIndexer>(&app);
    appIndexer->initialize();
    trimHeap();

    auto shellBridge = std::make_unique<ravenspot::ShellBridge>(startHidden, &app);

    shellBridge->setThemeManager(themeManager.get());
    shellBridge->setAudioEngine(audioEngine.get());
    // Olay döngüsü başladıktan kısa süre sonra sesleri ısıt (yapıcıda değil: açılışı geciktirmesin)
    QTimer::singleShot(250, audioEngine.get(), &ravenspot::AudioEngine::warmUp);
    shellBridge->setAppIndexer(appIndexer.get());

    auto *appIconProvider = new ravenspot::AppIconProvider();
    auto *themeIconProvider = new ravenspot::ThemeIconProvider();

    QObject::connect(themeManager.get(), &ravenspot::ThemeManager::themeChanged, [appIconProvider, themeIconProvider]() {
        appIconProvider->clearCache();
        themeIconProvider->clearCache();
    });

    QQmlApplicationEngine engine;
#ifndef _WIN32
    engine.addImportPath(QStringLiteral("/usr/lib/qt6/qml"));
#endif
    engine.rootContext()->setContextProperty(QStringLiteral("Shell"), shellBridge.get());
    engine.addImageProvider(QStringLiteral("appicon"), appIconProvider);
    engine.addImageProvider(QStringLiteral("themeicon"), themeIconProvider);

    // Locate Menu.qml
    QString qmlPath;
    const QByteArray envQml = qgetenv("RAVENSPOT_QML_PATH");
    if (!envQml.isEmpty() && QFile::exists(QString::fromUtf8(envQml))) {
        qmlPath = QString::fromUtf8(envQml);
    } else if (QFile::exists(QDir::currentPath() + QStringLiteral("/Menu.qml"))) {
        qmlPath = QDir::currentPath() + QStringLiteral("/Menu.qml");
    } else if (QFile::exists(QCoreApplication::applicationDirPath() + QStringLiteral("/Menu.qml"))) {
        qmlPath = QCoreApplication::applicationDirPath() + QStringLiteral("/Menu.qml");
    } else if (QFile::exists(QStringLiteral("/home/dokuro/Masaüstü/ravenspot/Menu.qml"))) {
        qmlPath = QStringLiteral("/home/dokuro/Masaüstü/ravenspot/Menu.qml");
    } else if (QFile::exists(QStringLiteral("/usr/share/ravenspot/Menu.qml"))) {
        qmlPath = QStringLiteral("/usr/share/ravenspot/Menu.qml");
    }

    if (qmlPath.isEmpty()) {
        std::cerr << "ravenspot: Menu.qml could not be found.\n";
        singleInstance.releaseLock();
        return 1;
    }

#ifdef _WIN32
    // KDE-only QML modules imported by Menu.qml get small stand-ins on Windows
    for (const QString &base : {QFileInfo(qmlPath).absolutePath(), QCoreApplication::applicationDirPath()}) {
        const QString compat = base + QStringLiteral("/compat/qml");
        if (QDir(compat).exists()) {
            engine.addImportPath(compat);
            break;
        }
    }
#endif

    // Veri dosyaları (emoji.json) Menu.qml'in yanındaki assets/ dizininde
    shellBridge->setAssetsDir(QFileInfo(qmlPath).absolutePath() + QStringLiteral("/assets"));
    engine.load(QUrl::fromLocalFile(qmlPath));
    if (engine.rootObjects().isEmpty()) {
        std::cerr << "ravenspot: failed to load QML root object from " << qmlPath.toStdString() << "\n";
        singleInstance.releaseLock();
        return 1;
    }

    auto *rootWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    auto layerWindow = std::make_unique<ravenspot::LayerShellWindow>(rootWindow, &app);
    shellBridge->setLayerShellWindow(layerWindow.get());

    ravenspot::DaemonServer server;

    server.setStatusProvider([layerWin = layerWindow.get(), thMgr = themeManager.get()]() {
        QJsonObject obj;
        obj[QStringLiteral("status")] = QStringLiteral("ok");
        obj[QStringLiteral("visible")] = layerWin ? layerWin->isVisible() : false;
        obj[QStringLiteral("theme")] = thMgr ? thMgr->activeThemeKey() : QStringLiteral("raven_cyber");
        obj[QStringLiteral("pid")] = static_cast<qint64>(QCoreApplication::applicationPid());
        return obj;
    });

    QObject::connect(&server, &ravenspot::DaemonServer::toggleRequested, shellBridge.get(), &ravenspot::ShellBridge::toggleMenu);
    QObject::connect(&server, &ravenspot::DaemonServer::showRequested, shellBridge.get(), &ravenspot::ShellBridge::showMenu);
    QObject::connect(&server, &ravenspot::DaemonServer::hideRequested, shellBridge.get(), &ravenspot::ShellBridge::hideMenu);
    QObject::connect(&server, &ravenspot::DaemonServer::reloadRequested, shellBridge.get(), &ravenspot::ShellBridge::requestReload);
    QObject::connect(&server, &ravenspot::DaemonServer::quitRequested, &app, &QGuiApplication::quit);

    trimHeap();

    if (!server.start()) {
        std::cerr << "ravenspot: failed to start IPC daemon server\n";
        singleInstance.releaseLock();
        return 1;
    }

#ifdef _WIN32
    // Windows has no desktop-level shortcut that runs "ravenspot --toggle" like
    // KDE does, so the daemon owns a global hotkey. RAVENSPOT_HOTKEY overrides it.
    ravenspot::GlobalHotkey hotkey;
    QObject::connect(&hotkey, &ravenspot::GlobalHotkey::activated, shellBridge.get(), &ravenspot::ShellBridge::toggleMenu);
    const QString hotkeySpec = qEnvironmentVariable("RAVENSPOT_HOTKEY", QStringLiteral("alt+space"));
    if (!hotkey.registerHotkey(hotkeySpec)) {
        hotkey.registerHotkey(QStringLiteral("ctrl+alt+space"));
    }
#endif

    if (!startHidden) {
        shellBridge->showMenu();
    }

    int exitCode = app.exec();

    server.stop();
    singleInstance.releaseLock();
    return exitCode;
}
