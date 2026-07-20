#include "application_diagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>

#include <cstdio>
#include <utility>

ApplicationDiagnostics *ApplicationDiagnostics::active_ = nullptr;

namespace {
constexpr qint64 maxLogSize = 2 * 1024 * 1024;

QString appDataLogPath() {
  const QString dataDirectory =
      QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
  return QDir(dataDirectory).filePath(QStringLiteral("logs/connecttool.log"));
}

QStringList defaultLogPaths() {
  QStringList paths;
#if defined(Q_OS_WIN)
  // Release builds are distributed as a portable ZIP. Keeping the primary log
  // beside the executable makes it discoverable even when Windows resolves
  // AppLocalDataLocation differently for an organization/application pair.
  paths.push_back(QDir(QCoreApplication::applicationDirPath())
                      .filePath(QStringLiteral("logs/connecttool.log")));
#endif
  paths.push_back(appDataLogPath());
  paths.removeDuplicates();
  return paths;
}

bool openLogFile(QFile &file, const QString &path) {
  const QFileInfo logInfo(path);
  if (!QDir().mkpath(logInfo.absolutePath())) {
    return false;
  }

  if (logInfo.exists() && logInfo.size() > maxLogSize) {
    const QString previousPath = path + QStringLiteral(".previous");
    QFile::remove(previousPath);
    QFile::rename(path, previousPath);
  }

  file.setFileName(path);
  return file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

const char *severityName(QtMsgType type) {
  switch (type) {
  case QtDebugMsg:
    return "DEBUG";
  case QtInfoMsg:
    return "INFO";
  case QtWarningMsg:
    return "WARN";
  case QtCriticalMsg:
    return "ERROR";
  case QtFatalMsg:
    return "FATAL";
  }
  return "UNKNOWN";
}
} // namespace

ApplicationDiagnostics::ApplicationDiagnostics(QString requestedLogPath) {
  const QStringList candidates = requestedLogPath.isEmpty()
                                     ? defaultLogPaths()
                                     : QStringList{QDir::cleanPath(std::move(requestedLogPath))};
  for (const QString &candidate : candidates) {
    if (openLogFile(logFile_, candidate)) {
      logPath_ = candidate;
      break;
    }
  }
  if (logPath_.isEmpty()) {
    logPath_ = candidates.value(0);
    return;
  }

  active_ = this;
  previousHandler_ = qInstallMessageHandler(&ApplicationDiagnostics::messageHandler);
  installed_ = true;
}

ApplicationDiagnostics::~ApplicationDiagnostics() {
  if (!installed_) {
    return;
  }
  qInstallMessageHandler(previousHandler_);
  active_ = nullptr;
  logFile_.close();
}

void ApplicationDiagnostics::messageHandler(QtMsgType type, const QMessageLogContext &context,
                                            const QString &message) {
  const QByteArray formatted =
      QStringLiteral("%1 [%2] [thread 0x%3] %4%5%6\n")
          .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs),
               QString::fromLatin1(severityName(type)),
               QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()), 16),
               context.category && context.category[0] != '\0'
                   ? QString::fromLatin1(context.category)
                   : QStringLiteral("default"),
               QStringLiteral(": "), message)
          .toUtf8();

  if (active_ != nullptr) {
    QMutexLocker lock(&active_->mutex_);
    active_->logFile_.write(formatted);
    active_->logFile_.flush();
  }

  if (active_ != nullptr && active_->previousHandler_ != nullptr) {
    active_->previousHandler_(type, context, message);
    return;
  }

  std::fwrite(formatted.constData(), 1, static_cast<size_t>(formatted.size()), stderr);
  std::fflush(stderr);
}
