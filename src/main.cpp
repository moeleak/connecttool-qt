#include "ConnectTool/controllers/application_controller.h"
#include "application/runtime/application_diagnostics.h"
#include "application/runtime/application_runtime.h"
#if defined(Q_OS_WIN)
#include "platform/windows/windows_accessibility_guard.h"
#include "platform/windows/windows_crash_handler.h"
#include "platform/windows/windows_renderer_supervisor.h"
#endif

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QOperatingSystemVersion>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTimer>
#include <QtQml/QQmlExtensionPlugin>

#include <algorithm>
#include <span>
#include <string_view>

Q_IMPORT_QML_PLUGIN(Qcm_MaterialPlugin)
Q_IMPORT_QML_PLUGIN(ConnectToolPlugin)

namespace {
[[nodiscard]] bool configureGraphicsBackend(std::span<char *> arguments) {
  const bool softwareBackendRequested =
      std::ranges::any_of(arguments, [](const char *argument) {
        return argument != nullptr && std::string_view{argument} == "--software-renderer";
      });
  if (softwareBackendRequested) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    return true;
  }
  return false;
}

QString optionValue(const QStringList &arguments, const QString &prefix) {
  const auto matchingArgument = std::ranges::find_if(
      arguments, [&prefix](const QString &argument) { return argument.startsWith(prefix); });
  return matchingArgument == arguments.end() ? QString{} : matchingArgument->sliced(prefix.size());
}

void installBundledFonts(QGuiApplication &app) {
  const QStringList fontResources = {
      QStringLiteral(":/fonts/NotoSansCJKsc-Regular.otf"),
  };

  QString preferredFamily;
  for (const QString &resourcePath : fontResources) {
    const int fontId = QFontDatabase::addApplicationFont(resourcePath);
    if (fontId < 0) {
      qWarning() << "Failed to load bundled font" << resourcePath;
      continue;
    }

    const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
    if (preferredFamily.isEmpty() && !families.isEmpty()) {
      preferredFamily = families.first();
    }
  }

  if (preferredFamily.isEmpty()) {
    qWarning() << "No bundled font families were loaded; using platform default.";
    return;
  }

  // QmlMaterial falls back to the generic Qt family on a few internal text
  // paths. Resolve it to the bundled CJK family up front instead of letting
  // the macOS font backend populate every alias on first use.
  QFont::insertSubstitution(QStringLiteral("Sans Serif"), preferredFamily);
  QFont::insertSubstitution(QStringLiteral("sans-serif"), preferredFamily);

  QFont font(preferredFamily);
  font.setStyleStrategy(QFont::PreferAntialias);
  app.setFont(font);
}
} // namespace

int main(int argc, char *argv[]) {
#if defined(Q_OS_WIN)
  if (const auto supervisorExit =
          connecttool::windows::superviseRendererStartup()) {
    return *supervisorExit;
  }
#endif

  QCoreApplication::setOrganizationName(QStringLiteral("ConnectTool"));
  QCoreApplication::setApplicationName(QStringLiteral("ConnectTool"));

  const bool softwareRenderer = configureGraphicsBackend(
      std::span<char *>{argv, static_cast<std::size_t>(argc)});

#if defined(Q_OS_WIN)
  if (const auto osVersion = QOperatingSystemVersion::current().version();
      osVersion < QOperatingSystemVersion::Windows10_1809.version() &&
      qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    // Avoid missing text on Windows Server 2016 and older Windows builds.
    qputenv("QT_QPA_PLATFORM", "windows:nodirectwrite");
  }
#endif

  QGuiApplication app(argc, argv);
  const QStringList arguments = QCoreApplication::arguments();
  ApplicationDiagnostics diagnostics(optionValue(arguments, QStringLiteral("--log-file=")));
#if defined(Q_OS_WIN)
  connecttool::windows::CrashHandler crashHandler(diagnostics.logPath());
#endif
  qInfo().noquote() << "ConnectTool" << CONNECTTOOL_VERSION << "starting with Qt" << QT_VERSION_STR
                    << "log:" << diagnostics.logPath();
#if defined(Q_OS_WIN)
  qInfo().noquote() << "[Startup] Native crash dump:" << crashHandler.dumpPath();
#endif
  qInfo().noquote()
      << "[Startup] Renderer preference:"
      << (softwareRenderer ? QStringLiteral("software")
                           : QStringLiteral("automatic"));

#if defined(Q_OS_WIN)
  WindowsAccessibilityGuard accessibilityGuard;
  app.installNativeEventFilter(&accessibilityGuard);
#endif

  installBundledFonts(app);
  qInfo() << "[Startup] Application fonts loaded.";
#ifdef CONNECTTOOL_NIX_PLUGIN_PATH
  const QStringList nixPluginPaths = QString::fromLocal8Bit(CONNECTTOOL_NIX_PLUGIN_PATH)
                                         .split(QLatin1Char(':'), Qt::SkipEmptyParts);
  for (const QString &path : nixPluginPaths) {
    if (QDir(path).exists()) {
      app.addLibraryPath(path);
    }
  }
#endif

  app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/ConnectTool/AppIcon.png")));
  qInfo() << "[Startup] Constructing application services.";
  ApplicationRuntime runtime;
  ApplicationController application(runtime);
  ApplicationControllerRegistration::bind(application);
  qInfo() << "[Startup] Application services constructed.";

  QQmlApplicationEngine engine;
  engine.addImportPath(QStringLiteral("qrc:/"));
#ifdef CONNECTTOOL_NIX_QML_IMPORT_PATH
  const QStringList nixQmlImportPaths = QString::fromLocal8Bit(CONNECTTOOL_NIX_QML_IMPORT_PATH)
                                            .split(QLatin1Char(':'), Qt::SkipEmptyParts);
  for (const QString &path : nixQmlImportPaths) {
    if (QDir(path).exists()) {
      engine.addImportPath(path);
    }
  }
#endif

  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() {
        qCritical() << "The QML root object could not be created.";
        QCoreApplication::exit(EXIT_FAILURE);
      },
      Qt::QueuedConnection);

  qInfo() << "[Startup] Loading the QML interface.";
  engine.loadFromModule("ConnectTool", "Main");
  qInfo() << "[Startup] QML load returned with" << engine.rootObjects().size() << "root object(s).";

  bool screenshotRequested = false;
  if (!engine.rootObjects().isEmpty()) {
    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
      window->show();
#if defined(Q_OS_WIN)
      window->raise();
      window->requestActivate();
#endif
      runtime.initializeSound(window);

      QObject::connect(
          window, &QQuickWindow::frameSwapped, &runtime,
          [&runtime]() {
#if defined(Q_OS_WIN)
            connecttool::windows::signalRendererReady();
#endif
            runtime.startServices();
          },
          Qt::SingleShotConnection);

      QObject::connect(window, &QQuickWindow::sceneGraphError, window,
                       [](QQuickWindow::SceneGraphError error, const QString &message) {
                         qCritical().noquote() << "Qt Quick scene graph error"
                                               << static_cast<int>(error) << ':' << message;
                       });
      QObject::connect(
          window, &QQuickWindow::sceneGraphInitialized, window,
          [window]() {
            qInfo() << "Qt Quick scene graph initialized with graphics API"
                    << static_cast<int>(window->rendererInterface()->graphicsApi());
          },
          Qt::DirectConnection);

      const QString screenshotPath = optionValue(arguments, QStringLiteral("--qml-screenshot="));
      if (!screenshotPath.isEmpty()) {
        screenshotRequested = true;
        bool pageIsValid = false;
        const int requestedPage =
            optionValue(arguments, QStringLiteral("--qml-page=")).toInt(&pageIsValid);
        if (pageIsValid) {
          window->setProperty("pageIndex", qBound(0, requestedPage, 3));
        }

#if defined(Q_OS_WIN)
        const bool accessibilityProbeRequested =
            arguments.contains(QStringLiteral("--windows-uia-probe"));
        if (accessibilityProbeRequested) {
          QTimer::singleShot(200, window, [&accessibilityGuard, window]() {
            if (!accessibilityGuard.postProbe(*window)) {
              qWarning() << "Failed to post the Windows UI Automation probe.";
            }
          });
        }
        const auto platformProbePassed = [&accessibilityGuard, accessibilityProbeRequested]() {
          return !accessibilityProbeRequested || accessibilityGuard.blockedEventCount() > 0;
        };
#else
        constexpr bool accessibilityProbeRequested = false;
        const auto platformProbePassed = []() { return true; };
#endif

        const int captureDelayMs = accessibilityProbeRequested ? 3000 : 1200;
        QTimer::singleShot(
            captureDelayMs, window, [window, screenshotPath, &app, platformProbePassed]() {
              const QImage image = window->grabWindow();
              const bool screenshotSaved = !image.isNull() && image.save(screenshotPath);
              const bool probePassed = platformProbePassed();
              if (!screenshotSaved) {
                qWarning() << "Failed to save QML screenshot to" << screenshotPath;
              }
              if (!probePassed) {
                qWarning() << "The Windows UI Automation probe was not intercepted.";
              }
              app.exit(screenshotSaved && probePassed ? EXIT_SUCCESS : EXIT_FAILURE);
            });
      }
    }
  }

  if (QCoreApplication::arguments().contains(QStringLiteral("--qml-smoke-test")) &&
      !screenshotRequested) {
    return engine.rootObjects().isEmpty() ? EXIT_FAILURE : EXIT_SUCCESS;
  }

  const int result = app.exec();
  qInfo() << "ConnectTool exiting with code" << result;
  return result;
}
