#include "backend.h"
#include "logging.h"
#include "webui.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <cstdio>
#include <memory>
#include <optional>
#if !defined(Q_OS_WIN)
#include <unistd.h>
#else
#include <io.h>
#endif

namespace {

struct FlatConfig {
  QHash<QString, QString> values;
};

FlatConfig parseSimpleYaml(const QString &path, QString *error) {
  FlatConfig cfg;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (error) {
      *error = QStringLiteral("无法读取配置文件: %1").arg(path);
    }
    return cfg;
  }

  struct Frame {
    int indent;
    QString key;
  };
  QVector<Frame> stack;
  int lineNo = 0;
  while (!file.atEnd()) {
    QByteArray rawBytes = file.readLine();
    ++lineNo;
    QString raw = QString::fromUtf8(rawBytes);
    const int commentIdx = raw.indexOf('#');
    if (commentIdx >= 0) {
      raw = raw.left(commentIdx);
    }
    if (raw.trimmed().isEmpty()) {
      continue;
    }

    int indent = 0;
    while (indent < raw.size() && raw[indent] == QLatin1Char(' ')) {
      ++indent;
    }
    QString trimmed = raw.trimmed();
    const int colonIdx = trimmed.indexOf(':');
    if (colonIdx <= 0) {
      continue;
    }
    QString key = trimmed.left(colonIdx).trimmed();
    QString rest = trimmed.mid(colonIdx + 1).trimmed();

    while (!stack.isEmpty() && indent <= stack.last().indent) {
      stack.removeLast();
    }

    if (rest.isEmpty()) {
      stack.push_back({indent, key});
      continue;
    }

    if ((rest.startsWith('"') && rest.endsWith('"')) ||
        (rest.startsWith('\'') && rest.endsWith('\''))) {
      rest = rest.mid(1, rest.size() - 2);
    }

    QString fullKey;
    for (const auto &frame : stack) {
      if (!fullKey.isEmpty()) {
        fullKey += QLatin1Char('.');
      }
      fullKey += frame.key;
    }
    if (!fullKey.isEmpty()) {
      fullKey += QLatin1Char('.');
    }
    fullKey += key;
    cfg.values.insert(fullKey, rest);
  }
  return cfg;
}

QString cfgGet(const FlatConfig &cfg, const QString &key,
               const QString &def = {}) {
  return cfg.values.value(key, def);
}

int cfgGetInt(const FlatConfig &cfg, const QString &key, int def) {
  bool ok = false;
  const int v = cfgGet(cfg, key).toInt(&ok);
  return ok ? v : def;
}

bool cfgGetBool(const FlatConfig &cfg, const QString &key, bool def) {
  const QString v = cfgGet(cfg, key).trimmed().toLower();
  if (v == "true" || v == "1" || v == "yes" || v == "on") {
    return true;
  }
  if (v == "false" || v == "0" || v == "no" || v == "off") {
    return false;
  }
  return def;
}

QByteArray jsonResponse(const QJsonObject &obj) {
  return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray joinHtml(const QString &lobbyId) {
  const QString shareCode = QStringLiteral("￥CTJOIN:%1￥").arg(lobbyId);
  const QString downloadUrl = QStringLiteral(
      "https://github.com/moeleak/connecttool-qt/releases/latest");
  const QString html =
      QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\">"
                     "<title>ConnectTool Join</title></head>"
                     "<body><p>复制分享码后打开 ConnectTool：</p>"
                     "<p style=\"font-family:monospace;font-size:18px;\">%1</p>"
                     "<p>还没安装？<a href=\"%2\" target=\"_blank\">下载 "
                     "ConnectTool</a></p>"
                     "</body></html>")
          .arg(shareCode, downloadUrl);
  return html.toUtf8();
}

} // namespace

int main(int argc, char *argv[]) {
  QCoreApplication::setOrganizationName(QStringLiteral("ConnectTool"));
  QCoreApplication::setApplicationName(QStringLiteral("ConnectTool CLI"));
  QCoreApplication app(argc, argv);

  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("connecttool-cli server"));
  parser.addHelpOption();
  QCommandLineOption configOpt(QStringList() << "c" << "config",
                               QStringLiteral("配置文件路径 (YAML)"),
                               QStringLiteral("config"));
  parser.addOption(configOpt);
  parser.process(app);

  const QString configPath = parser.value(configOpt);
  if (configPath.isEmpty()) {
    parser.showHelp(1);
  }

  QString parseError;
  FlatConfig cfg = parseSimpleYaml(configPath, &parseError);
  if (!parseError.isEmpty()) {
    fprintf(stderr, "%s\n", parseError.toLocal8Bit().constData());
    return 1;
  }

  QFileInfo cfgInfo(configPath);
  const QString cfgDir = cfgInfo.absolutePath();

  auto resolveLogPath = [&](const QString &path) -> QString {
    if (path.isEmpty()) {
      return path;
    }
    QFileInfo info(path);
    if (info.isRelative()) {
      return QDir(cfgDir).filePath(path);
    }
    return path;
  };

  QString steamLogPath = resolveLogPath(
      cfgGet(cfg, "logging.steam_file", cfgDir + QLatin1String("/steam.log")));
  // Network logs are printed to console by default.
  QString netLogPath;

  int netConsoleFd = -1;
#if !defined(Q_OS_WIN)
  netConsoleFd = ::dup(fileno(stdout));
#else
  netConsoleFd = _dup(_fileno(stdout));
#endif

  ConnectToolLogging::initialize(steamLogPath, netLogPath);
  ConnectToolLogging::setConsoleFallbackEnabled(false);

  // If configured paths are not writable, fall back to local files next to
  // config.
  if (!ConnectToolLogging::isInitialized()) {
    steamLogPath = QDir(cfgDir).filePath(QStringLiteral("steam.log"));
    ConnectToolLogging::initialize(steamLogPath, netLogPath);
  }

  if (netConsoleFd >= 0) {
    ConnectToolLogging::setNetConsoleFd(netConsoleFd);
  }

  if (!steamLogPath.isEmpty()) {
    const QByteArray steamLogBytes = steamLogPath.toLocal8Bit();
    std::freopen(steamLogBytes.constData(), "a", stdout);
    std::freopen(steamLogBytes.constData(), "a", stderr);
  }
  ConnectToolLogging::installQtMessageHandler();

  Backend backend;

  const QString modeStr =
      cfgGet(cfg, "connect.mode", "tcp").trimmed().toLower();
  backend.setConnectionMode(modeStr == "tun" ? 1 : 0);
  backend.setLocalPort(
      cfgGetInt(cfg, "connect.local_port", backend.localPort()));
  backend.setLocalBindPort(
      cfgGetInt(cfg, "connect.bind_port", backend.localBindPort()));
  backend.setPublishLobby(cfgGetBool(cfg, "connect.publish", true));

  const QString desiredRoomName = cfgGet(cfg, "connect.room_name").trimmed();
  if (!desiredRoomName.isEmpty()) {
    auto nameApplied = std::make_shared<bool>(false);
    auto applyName = [nameApplied, &backend, desiredRoomName]() {
      if (*nameApplied || !backend.steamReady()) {
        return;
      }
      backend.setRoomName(desiredRoomName);
      *nameApplied = true;
    };
    QObject::connect(&backend, &Backend::stateChanged, &app, applyName);
    QTimer::singleShot(0, &app, applyName);
  }

  // Always auto-host once Steam is ready (no auto_host toggle).
  auto started = std::make_shared<bool>(false);
  auto startIfReady = [started, &backend]() {
    if (*started || !backend.steamReady()) {
      return;
    }
    backend.startHosting();
    *started = true;
  };
  QObject::connect(&backend, &Backend::stateChanged, &app, startIfReady);
  QTimer::singleShot(0, &app, startIfReady);

  const QString listenAddr = cfgGet(cfg, "server.listen", "0.0.0.0");
  const int listenPort = cfgGetInt(cfg, "server.port", 23333);
  const QString adminToken = cfgGet(cfg, "server.admin_token");

  QTcpServer httpServer;
  if (!httpServer.listen(QHostAddress(listenAddr), listenPort)) {
    ConnectToolLogging::logNet(QStringLiteral("HTTP Failed to listen to: %1:%2")
                                   .arg(listenAddr)
                                   .arg(listenPort));
    return 1;
  }
  ConnectToolLogging::logNet(QStringLiteral("HTTP Listened to: %1:%2")
                                 .arg(listenAddr)
                                 .arg(listenPort));

  QObject::connect(&httpServer, &QTcpServer::newConnection, &app, [&]() {
    while (httpServer.hasPendingConnections()) {
      QTcpSocket *socket = httpServer.nextPendingConnection();
      auto buffer = std::make_shared<QByteArray>();

      QObject::connect(
          socket, &QTcpSocket::readyRead, socket,
          [socket, buffer, &backend, adminToken, listenPort]() {
            buffer->append(socket->readAll());
            const int headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0) {
              return;
            }

            const QByteArray headerPart = buffer->left(headerEnd);
            const QList<QByteArray> rawLines = headerPart.split('\n');
            if (rawLines.isEmpty()) {
              socket->disconnectFromHost();
              return;
            }
            QList<QByteArray> lines = rawLines;
            const QByteArray requestLine = lines.takeFirst().trimmed();
            const QList<QByteArray> reqParts = requestLine.split(' ');
            if (reqParts.size() < 2) {
              socket->disconnectFromHost();
              return;
            }

            const QString method = QString::fromLatin1(reqParts[0]).toUpper();
            const QString target = QString::fromLatin1(reqParts[1]);

            QMap<QString, QString> headers;
            for (const QByteArray &l : lines) {
              const QByteArray line = l.trimmed();
              if (line.isEmpty()) {
                continue;
              }
              const int idx = line.indexOf(':');
              if (idx <= 0) {
                continue;
              }
              const QString k =
                  QString::fromLatin1(line.left(idx)).trimmed().toLower();
              const QString v =
                  QString::fromLatin1(line.mid(idx + 1)).trimmed();
              headers.insert(k, v);
            }

            const int contentLen = headers.value("content-length", "0").toInt();
            const int bodyStart = headerEnd + 4;
            if (buffer->size() < bodyStart + contentLen) {
              return;
            }
            const QByteArray body = buffer->mid(bodyStart, contentLen);

            auto send = [socket](int code, const QByteArray &payload,
                                 const QByteArray &ctype) {
              QByteArray reason = "OK";
              if (code == 401) {
                reason = "Unauthorized";
              } else if (code == 404) {
                reason = "Not Found";
              } else if (code == 400) {
                reason = "Bad Request";
              }
              QByteArray resp;
              resp += "HTTP/1.1 " + QByteArray::number(code) + " " + reason +
                      "\r\n";
              resp += "Content-Type: " + ctype + "\r\n";
              resp += "Content-Length: " + QByteArray::number(payload.size()) +
                      "\r\n";
              resp += "Connection: close\r\n\r\n";
              resp += payload;
              socket->write(resp);
              socket->disconnectFromHost();
            };

            const QUrl url(target);
            const QString path = url.path();
            const QUrlQuery query(url);

            const bool isAdmin = path.startsWith("/admin");
            auto checkToken = [&]() -> bool {
              if (adminToken.isEmpty()) {
                return true;
              }
              QString token = query.queryItemValue("token");
              if (token.isEmpty()) {
                const QString auth = headers.value("authorization");
                if (auth.startsWith("Bearer ")) {
                  token = auth.mid(7);
                }
              }
              if (token.isEmpty()) {
                token = headers.value("x-admin-token");
              }
              return token == adminToken;
            };

            if (isAdmin && !checkToken()) {
              send(401, QByteArray("{\"error\":\"unauthorized\"}"),
                   "application/json");
              return;
            }

            if (method == "GET" && (path == "/" || path == "/index.html")) {
              send(200, QByteArray(ConnectToolWebUI::kIndexHtml),
                   "text/html; charset=utf-8");
              return;
            }

            if (isAdmin && method == "GET" &&
                (path == "/admin/ui" || path == "/admin/ui/")) {
              send(200, QByteArray(ConnectToolWebUI::kAdminHtml),
                   "text/html; charset=utf-8");
              return;
            }

            if (method == "GET" && path.startsWith("/join/")) {
              const QString lobbyId = path.mid(QString("/join/").size());
              send(200, joinHtml(lobbyId), "text/html; charset=utf-8");
              return;
            }

            // Backward compatible alias.
            if (method == "GET" && path.startsWith("/invite/")) {
              const QString lobbyId = path.mid(QString("/invite/").size());
              send(200, joinHtml(lobbyId), "text/html; charset=utf-8");
              return;
            }

            if (isAdmin && method == "GET" && path == "/admin/state") {
              QJsonObject obj;
              obj["steamReady"] = backend.steamReady();
              obj["isHost"] = backend.isHost();
              obj["isConnected"] = backend.isConnected();
              obj["lobbyId"] = backend.lobbyId();
              obj["lobbyName"] = backend.lobbyName();
              obj["connectionMode"] = backend.connectionMode();
              obj["localPort"] = backend.localPort();
              obj["localBindPort"] = backend.localBindPort();
              obj["publishLobby"] = backend.publishLobby();
              obj["tunLocalIp"] = backend.tunLocalIp();
              obj["tunDeviceName"] = backend.tunDeviceName();
              obj["tcpClients"] = backend.tcpClients();
              send(200, jsonResponse(obj), "application/json");
              return;
            }

            if (isAdmin && method == "GET" &&
                (path == "/admin/join" || path == "/admin/invite")) {
              const QString lobbyId = backend.lobbyId();
              QString host = headers.value("host");
              if (host.isEmpty()) {
                host = url.host().isEmpty() ? QStringLiteral("localhost")
                                            : url.host();
              }
              const QString joinUrl =
                  lobbyId.isEmpty() ? QString()
                                    : QStringLiteral("http://%1:%2/join/%3")
                                          .arg(host.section(':', 0, 0))
                                          .arg(listenPort)
                                          .arg(lobbyId);
          QJsonObject obj;
          obj["lobbyId"] = lobbyId;
          obj["joinUrl"] = joinUrl;
          obj["inviteUrl"] = joinUrl;
          obj["shareCode"] = lobbyId.isEmpty()
                                 ? QString()
                                 : QStringLiteral("￥CTJOIN:%1￥").arg(lobbyId);
          send(200, jsonResponse(obj), "application/json");
          return;
        }

            if (isAdmin && method == "POST" && path == "/admin/config") {
              QJsonParseError jerr;
              const QJsonDocument doc = QJsonDocument::fromJson(body, &jerr);
              if (jerr.error != QJsonParseError::NoError || !doc.isObject()) {
                send(400, QByteArray("{\"error\":\"invalid json\"}"),
                     "application/json");
                return;
              }
              const QJsonObject o = doc.object();

              const bool wasHost = backend.isHost();
              const bool wasConnected = backend.isConnected();

              const bool hasLocalPort = o.contains("localPort");
              const int desiredLocalPort = hasLocalPort
                                               ? o.value("localPort").toInt()
                                               : backend.localPort();

              const bool hasBindPort = o.contains("localBindPort");
              const int desiredBindPort = hasBindPort
                                              ? o.value("localBindPort").toInt()
                                              : backend.localBindPort();

              const bool hasMode = o.contains("mode");
              const int currentMode = backend.connectionMode();
              int desiredMode = currentMode;
              if (hasMode) {
                const QString m = o.value("mode").toString().toLower();
                desiredMode = (m == "tun") ? 1 : 0;
              }

              const bool hasPublish = o.contains("publish");
              const bool desiredPublish = hasPublish
                                              ? o.value("publish").toBool()
                                              : backend.publishLobby();

              const bool hasRoomName = o.contains("roomName");
              const QString desiredRoomName =
                  hasRoomName ? o.value("roomName").toString() : QString();

              const bool needsDisconnect =
                  (wasHost || wasConnected) &&
                  ((hasMode && desiredMode != currentMode) ||
                   (hasLocalPort && desiredLocalPort != backend.localPort()) ||
                   (hasBindPort && desiredBindPort != backend.localBindPort()));
              const bool restartAfter = needsDisconnect && wasHost;

              auto applyChanges = [&backend, hasLocalPort, desiredLocalPort,
                                   hasBindPort, desiredBindPort, hasMode,
                                   desiredMode, hasPublish, desiredPublish,
                                   hasRoomName, desiredRoomName,
                                   restartAfter]() {
                if (hasLocalPort) {
                  backend.setLocalPort(desiredLocalPort);
                }
                if (hasBindPort) {
                  backend.setLocalBindPort(desiredBindPort);
                }
                if (hasMode) {
                  backend.setConnectionMode(desiredMode);
                }
                if (hasPublish) {
                  backend.setPublishLobby(desiredPublish);
                }
                if (hasRoomName) {
                  backend.setRoomName(desiredRoomName);
                }
                if (restartAfter) {
                  backend.startHosting();
                }
              };

              if (needsDisconnect) {
                backend.disconnect();
                QTimer::singleShot(0, &backend, applyChanges);
              } else {
                applyChanges();
              }
              send(200, QByteArray("{\"ok\":true}"), "application/json");
              return;
            }

            if (isAdmin && method == "POST" && path == "/admin/host/start") {
              backend.startHosting();
              send(200, QByteArray("{\"ok\":true}"), "application/json");
              return;
            }

            if (isAdmin && method == "POST" && path == "/admin/disconnect") {
              backend.disconnect();
              send(200, QByteArray("{\"ok\":true}"), "application/json");
              return;
            }

            send(404, QByteArray("{\"error\":\"not found\"}"),
                 "application/json");
          });

      QObject::connect(socket, &QTcpSocket::disconnected, socket,
                       &QTcpSocket::deleteLater);
    }
  });

  return app.exec();
}
