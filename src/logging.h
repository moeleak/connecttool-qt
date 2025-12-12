#pragma once

#include <QString>
#include <string>

namespace ConnectToolLogging {

void initialize(const QString &steamLogPath, const QString &networkLogPath);
bool isInitialized();

void setConsoleFallbackEnabled(bool enabled);
// Optional: route network logs to a preserved console fd.
void setNetConsoleFd(int fd);

void logSteam(const QString &message);
void logSteam(const std::string &message);
inline void logSteam(const char *message) {
  logSteam(std::string(message ? message : ""));
}
void logNet(const QString &message);
void logNet(const std::string &message);
inline void logNet(const char *message) {
  logNet(std::string(message ? message : ""));
}

// Routes Qt qDebug/qWarning output. Call after initialize() when running headless.
void installQtMessageHandler();

} // namespace ConnectToolLogging
