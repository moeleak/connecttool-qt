#include "backend.h"
#include "chat_model.h"
#include "lobbies_model.h"
#include "members_model.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QTimer>

#include <optional>

namespace {

std::optional<QString> parseShareCodeLobbyId(const QString &text) {
  const QString trimmed = text.trimmed();
  if (trimmed.isEmpty()) {
    return std::nullopt;
  }

  static const QRegularExpression wrappedRe(
      QStringLiteral(R"(￥CTJOIN:([0-9]{5,20})￥)"));
  auto match = wrappedRe.match(trimmed);
  if (match.hasMatch()) {
    return match.captured(1);
  }

  static const QRegularExpression plainRe(
      QStringLiteral(R"(CTJOIN:([0-9]{5,20}))"));
  match = plainRe.match(trimmed);
  if (match.hasMatch()) {
    return match.captured(1);
  }

  return std::nullopt;
}

} // namespace

int main(int argc, char *argv[]) {
  QCoreApplication::setOrganizationName(QStringLiteral("ConnectTool"));
  QCoreApplication::setApplicationName(QStringLiteral("ConnectTool"));

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

  QString lastShareLobbyId;
  auto tryJoinFromClipboard = [&backend, &lastShareLobbyId]() {
    if (backend.isHost() || backend.isConnected()) {
      return;
    }
    const QString clipText =
        QGuiApplication::clipboard()
            ? QGuiApplication::clipboard()->text()
            : QString();
    if (auto id = parseShareCodeLobbyId(clipText)) {
      if (*id == lastShareLobbyId) {
        return;
      }
      lastShareLobbyId = *id;
      backend.joinLobby(*id);
    }
  };

  // Detect share code from clipboard on launch (Taobao-style).
  QTimer::singleShot(0, &backend, tryJoinFromClipboard);
  if (QGuiApplication::clipboard()) {
    QObject::connect(QGuiApplication::clipboard(), &QClipboard::dataChanged,
                     &app, tryJoinFromClipboard);
  }

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
