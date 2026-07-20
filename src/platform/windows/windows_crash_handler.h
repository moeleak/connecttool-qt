#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <QString>

#include <csignal>
#include <exception>
#include <string>

namespace connecttool::windows {

// Records otherwise silent native crashes before Windows terminates the
// process. The handler deliberately uses only Win32 APIs after an exception.
class CrashHandler final {
public:
  explicit CrashHandler(const QString &logPath);
  ~CrashHandler();

  CrashHandler(const CrashHandler &) = delete;
  CrashHandler &operator=(const CrashHandler &) = delete;

  [[nodiscard]] const QString &dumpPath() const noexcept { return displayDumpPath_; }

private:
  static LONG WINAPI handleException(EXCEPTION_POINTERS *exception) noexcept;
  static void handleTerminate() noexcept;
  static void handleAbort(int signal) noexcept;
  void recordFailure(EXCEPTION_POINTERS *exception, const char *reason) const noexcept;

  static CrashHandler *active_;

  QString displayDumpPath_;
  std::wstring logPath_;
  std::wstring dumpPath_;
  LPTOP_LEVEL_EXCEPTION_FILTER previousFilter_ = nullptr;
  std::terminate_handler previousTerminateHandler_ = nullptr;
  using SignalHandler = void (*)(int);
  SignalHandler previousAbortHandler_ = SIG_DFL;
  bool abortHandlerInstalled_ = false;
};

} // namespace connecttool::windows
