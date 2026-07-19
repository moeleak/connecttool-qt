#pragma once

#include <QFile>
#include <QMutex>
#include <QString>
#include <QtLogging>

class ApplicationDiagnostics final {
public:
  explicit ApplicationDiagnostics(QString requestedLogPath = {});
  ~ApplicationDiagnostics();

  ApplicationDiagnostics(const ApplicationDiagnostics &) = delete;
  ApplicationDiagnostics &operator=(const ApplicationDiagnostics &) = delete;

  [[nodiscard]] const QString &logPath() const noexcept { return logPath_; }

private:
  static void messageHandler(QtMsgType type, const QMessageLogContext &context,
                             const QString &message);

  static ApplicationDiagnostics *active_;

  QString logPath_;
  QFile logFile_;
  QMutex mutex_;
  QtMessageHandler previousHandler_ = nullptr;
  bool installed_ = false;
};
