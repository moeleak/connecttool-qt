#include "backend.h"
#include "chat_model.h"
#include "lobbies_model.h"
#include "members_model.h"

#include <QCoreApplication>
#include <QDebug>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QOperatingSystemVersion>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QDir>
#include <QQuickStyle>
#include <QQuickWindow>

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
  const QStringList nixPluginPaths =
      QString::fromLocal8Bit(CONNECTTOOL_NIX_PLUGIN_PATH)
          .split(QLatin1Char(':'), Qt::SkipEmptyParts);
  for (const QString &path : nixPluginPaths) {
    if (QDir(path).exists()) {
      app.addLibraryPath(path);
    }
  }
#endif

  app.setWindowIcon(QIcon(QStringLiteral(":/qt/qml/ConnectTool/AppIcon.png")));
  QQuickStyle::setStyle(QStringLiteral("Material"));

  qmlRegisterUncreatableType<FriendsModel>("ConnectTool", 1, 0, "FriendsModel",
                                           "Provided by backend");
  qmlRegisterUncreatableType<MembersModel>("ConnectTool", 1, 0, "MembersModel",
                                           "Provided by backend");
  qmlRegisterUncreatableType<LobbiesModel>("ConnectTool", 1, 0, "LobbiesModel",
                                           "Provided by backend");
  qmlRegisterUncreatableType<ChatModel>("ConnectTool", 1, 0, "ChatModel",
                                        "Provided by backend");

  Backend backend;

  QQmlApplicationEngine engine;
#ifdef CONNECTTOOL_NIX_QML_IMPORT_PATH
  const QStringList nixQmlImportPaths =
      QString::fromLocal8Bit(CONNECTTOOL_NIX_QML_IMPORT_PATH)
          .split(QLatin1Char(':'), Qt::SkipEmptyParts);
  for (const QString &path : nixQmlImportPaths) {
    if (QDir(path).exists()) {
      engine.addImportPath(path);
    }
  }
#endif

  engine.rootContext()->setContextProperty(QStringLiteral("backend"), &backend);

  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

  engine.loadFromModule("ConnectTool", "Main");

  if (!engine.rootObjects().isEmpty()) {
    if (auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first())) {
      backend.initializeSound(window);
    }
  }

  return app.exec();
}
