#include "logging.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTextStream>
#include <QStringConverter>
#include <QtGlobal>
#include <atomic>
#include <iostream>
#if !defined(Q_OS_WIN)
#include <unistd.h>
#endif

namespace ConnectToolLogging {
namespace {
QMutex g_mutex;
QFile g_steamFile;
QFile g_netFile;
bool g_initialized = false;
std::atomic<bool> g_consoleFallbackEnabled{true};
std::atomic<int> g_netConsoleFd{-1};
QtMessageHandler g_prevHandler = nullptr;

void writeLine(QFile &file, const QString &line) {
  if (!file.isOpen()) {
    return;
  }
  QTextStream out(&file);
  out.setEncoding(QStringConverter::Encoding::Utf8);
  out << line << "\n";
  out.flush();
}

QString stampLine(const QString &line) {
  const QString ts =
      QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
  return QStringLiteral("[%1] %2").arg(ts, line);
}

void qtHandler(QtMsgType type, const QMessageLogContext &ctx,
               const QString &msg) {
  Q_UNUSED(ctx);
  if (msg.contains(QStringLiteral("[SteamNet]")) ||
      msg.contains(QStringLiteral("[SteamAPI]")) ||
      msg.contains(QStringLiteral("[SteamVPN]"))) {
    logSteam(msg);
    return;
  }
  if (msg.contains(QStringLiteral("[Multiplex]")) ||
      msg.contains(QStringLiteral("TCP server")) ||
      msg.contains(QStringLiteral("Heartbeat manager")) ||
      msg.contains(QStringLiteral("IP negotiation"))) {
    logNet(msg);
    return;
  }

  if (!g_consoleFallbackEnabled.load(std::memory_order_relaxed)) {
    return;
  }

  if (g_prevHandler) {
    g_prevHandler(type, ctx, msg);
    return;
  }
  // Default fallback: stderr only.
  fprintf(stderr, "%s\n", msg.toLocal8Bit().constData());
}

} // namespace

void initialize(const QString &steamLogPath, const QString &networkLogPath) {
  QMutexLocker locker(&g_mutex);
  if (!steamLogPath.isEmpty()) {
    QFileInfo steamInfo(steamLogPath);
    QDir steamDir = steamInfo.dir();
    if (!steamDir.exists()) {
      steamDir.mkpath(QStringLiteral("."));
    }
    g_steamFile.setFileName(steamLogPath);
    if (!g_steamFile.open(QIODevice::WriteOnly | QIODevice::Append |
                          QIODevice::Text)) {
      g_steamFile.close();
    }
  }
  if (!networkLogPath.isEmpty()) {
    QFileInfo netInfo(networkLogPath);
    QDir netDir = netInfo.dir();
    if (!netDir.exists()) {
      netDir.mkpath(QStringLiteral("."));
    }
    g_netFile.setFileName(networkLogPath);
    if (!g_netFile.open(QIODevice::WriteOnly | QIODevice::Append |
                        QIODevice::Text)) {
      g_netFile.close();
    }
  }
  g_initialized = g_steamFile.isOpen() || g_netFile.isOpen();
}

bool isInitialized() {
  QMutexLocker locker(&g_mutex);
  return g_initialized;
}

void setConsoleFallbackEnabled(bool enabled) {
  QMutexLocker locker(&g_mutex);
  g_consoleFallbackEnabled.store(enabled, std::memory_order_relaxed);
}

void setNetConsoleFd(int fd) {
  g_netConsoleFd.store(fd, std::memory_order_relaxed);
}

void logSteam(const QString &message) {
  QMutexLocker locker(&g_mutex);
  if (g_steamFile.isOpen()) {
    writeLine(g_steamFile, stampLine(message));
    return;
  }
  if (!g_initialized &&
      g_consoleFallbackEnabled.load(std::memory_order_relaxed)) {
    std::cout << message.toStdString() << std::endl;
  }
}

void logSteam(const std::string &message) {
  logSteam(QString::fromUtf8(message.c_str()));
}

void logNet(const QString &message) {
  QMutexLocker locker(&g_mutex);
  if (g_netFile.isOpen()) {
    writeLine(g_netFile, stampLine(message));
    return;
  }
  const int fd = g_netConsoleFd.load(std::memory_order_relaxed);
  if (fd >= 0) {
#if defined(Q_OS_WIN)
    // On Windows, fall back to std::cout when no file is set.
    std::cout << message.toStdString() << std::endl;
#else
    QByteArray bytes = stampLine(message).toUtf8();
    bytes.append('\n');
    ::write(fd, bytes.constData(), static_cast<size_t>(bytes.size()));
#endif
    return;
  }
  if (g_consoleFallbackEnabled.load(std::memory_order_relaxed)) {
    std::cout << stampLine(message).toStdString() << std::endl;
  }
}

void logNet(const std::string &message) {
  logNet(QString::fromUtf8(message.c_str()));
}

void installQtMessageHandler() {
  QMutexLocker locker(&g_mutex);
  if (g_prevHandler == nullptr) {
    g_prevHandler = qInstallMessageHandler(qtHandler);
  }
}

} // namespace ConnectToolLogging
