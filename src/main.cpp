#include "backend.h"
#include "chat_model.h"
#include "lobbies_model.h"
#include "members_model.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QOperatingSystemVersion>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>

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
