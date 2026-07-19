#include "application_controller.h"
#include "backend.h"

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
#include <QTimer>
#include <QtQml/QQmlExtensionPlugin>

Q_IMPORT_QML_PLUGIN(Qcm_MaterialPlugin)

namespace {
void installBundledFonts(QGuiApplication &app) {
  const QStringList fontResources = {
      QStringLiteral(":/fonts/NotoSansCJKsc-Regular.otf"),
      QStringLiteral(":/fonts/NotoSansCJKsc-Bold.otf"),
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
  QCoreApplication::setOrganizationName(QStringLiteral("ConnectTool"));
  QCoreApplication::setApplicationName(QStringLiteral("ConnectTool"));

#if defined(Q_OS_WIN)
  if (const auto osVersion = QOperatingSystemVersion::current().version();
      osVersion < QOperatingSystemVersion::Windows10_1809.version() &&
      qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) {
    // Avoid missing text on Windows Server 2016 and older Windows builds.
    qputenv("QT_QPA_PLATFORM", "windows:nodirectwrite");
  }
#endif

  QGuiApplication app(argc, argv);
  installBundledFonts(app);
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
  Backend backend;
  ApplicationController application(backend);
  ApplicationControllerRegistration::bind(application);

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
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

  engine.loadFromModule("ConnectTool", "Main");

  bool screenshotRequested = false;
  if (!engine.rootObjects().isEmpty()) {
    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
      backend.initializeSound(window);

      const QStringList arguments = QCoreApplication::arguments();
      const auto optionValue = [&arguments](const QString &prefix) {
        for (const QString &argument : arguments) {
          if (argument.startsWith(prefix)) {
            return argument.sliced(prefix.size());
          }
        }
        return QString{};
      };

      const QString screenshotPath =
          optionValue(QStringLiteral("--qml-screenshot="));
      if (!screenshotPath.isEmpty()) {
        screenshotRequested = true;
        bool pageIsValid = false;
        const int requestedPage =
            optionValue(QStringLiteral("--qml-page=")).toInt(&pageIsValid);
        if (pageIsValid) {
          window->setProperty("pageIndex", qBound(0, requestedPage, 3));
        }

        QTimer::singleShot(1200, window, [window, screenshotPath, &app]() {
          const QImage image = window->grabWindow();
          const bool saved = !image.isNull() && image.save(screenshotPath);
          if (!saved) {
            qWarning() << "Failed to save QML screenshot to" << screenshotPath;
          }
          app.exit(saved ? EXIT_SUCCESS : EXIT_FAILURE);
        });
      }
    }
  }

  if (QCoreApplication::arguments().contains(QStringLiteral("--qml-smoke-test")) &&
      !screenshotRequested) {
    return engine.rootObjects().isEmpty() ? EXIT_FAILURE : EXIT_SUCCESS;
  }

  return app.exec();
}
