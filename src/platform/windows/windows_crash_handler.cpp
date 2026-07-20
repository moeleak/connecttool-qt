#include "windows_crash_handler.h"

#include <QDir>
#include <QFileInfo>

#include <dbghelp.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>

namespace connecttool::windows {

CrashHandler *CrashHandler::active_ = nullptr;

CrashHandler::CrashHandler(const QString &logPath) {
  const QFileInfo logFile{logPath};
  displayDumpPath_ = QDir(logFile.absolutePath()).filePath(QStringLiteral("connecttool-crash.dmp"));
  logPath_ = QDir::toNativeSeparators(logFile.absoluteFilePath()).toStdWString();
  dumpPath_ = QDir::toNativeSeparators(displayDumpPath_).toStdWString();

  active_ = this;
  previousFilter_ = SetUnhandledExceptionFilter(&CrashHandler::handleException);
  previousTerminateHandler_ = std::set_terminate(&CrashHandler::handleTerminate);
  previousAbortHandler_ = std::signal(SIGABRT, &CrashHandler::handleAbort);
  abortHandlerInstalled_ = previousAbortHandler_ != SIG_ERR;
}

CrashHandler::~CrashHandler() {
  if (active_ == this) {
    if (abortHandlerInstalled_) {
      std::signal(SIGABRT, previousAbortHandler_);
    }
    std::set_terminate(previousTerminateHandler_);
    SetUnhandledExceptionFilter(previousFilter_);
    active_ = nullptr;
  }
}

LONG WINAPI CrashHandler::handleException(EXCEPTION_POINTERS *exception) noexcept {
  CrashHandler *handler = active_;
  if (handler != nullptr) {
    handler->recordFailure(exception, "Unhandled Windows exception");
    if (handler->previousFilter_ != nullptr &&
        handler->previousFilter_ != &CrashHandler::handleException) {
      return handler->previousFilter_(exception);
    }
  }
  return EXCEPTION_EXECUTE_HANDLER;
}

void CrashHandler::handleTerminate() noexcept {
  if (active_ != nullptr) {
    active_->recordFailure(nullptr, "std::terminate");
  }
  TerminateProcess(GetCurrentProcess(), 3);
  std::_Exit(3);
}

void CrashHandler::handleAbort(int) noexcept {
  if (active_ != nullptr) {
    active_->recordFailure(nullptr, "SIGABRT");
  }
  TerminateProcess(GetCurrentProcess(), 3);
  std::_Exit(3);
}

void CrashHandler::recordFailure(EXCEPTION_POINTERS *exception, const char *reason) const noexcept {
  const HANDLE dumpFile = CreateFileW(dumpPath_.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                      CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  bool dumpSaved = false;
  if (dumpFile != INVALID_HANDLE_VALUE) {
    MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{
        .ThreadId = GetCurrentThreadId(),
        .ExceptionPointers = exception,
        .ClientPointers = FALSE,
    };
    dumpSaved = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dumpFile,
                                  MiniDumpNormal, exception != nullptr ? &exceptionInfo : nullptr,
                                  nullptr, nullptr) != FALSE;
    CloseHandle(dumpFile);
  }

  const HANDLE logFile = CreateFileW(logPath_.c_str(), FILE_APPEND_DATA,
                                     FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (logFile == INVALID_HANDLE_VALUE) {
    return;
  }

  const DWORD code = exception != nullptr && exception->ExceptionRecord != nullptr
                         ? exception->ExceptionRecord->ExceptionCode
                         : 0;
  void *address = exception != nullptr && exception->ExceptionRecord != nullptr
                      ? exception->ExceptionRecord->ExceptionAddress
                      : nullptr;
  SYSTEMTIME time{};
  GetSystemTime(&time);

  std::array<char, 512> record{};
  const int length = std::snprintf(
      record.data(), record.size(),
      "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ [FATAL] [thread 0x%lx] native: "
      "%s 0x%08lx at %p; minidump %s.\r\n",
      static_cast<unsigned>(time.wYear), static_cast<unsigned>(time.wMonth),
      static_cast<unsigned>(time.wDay), static_cast<unsigned>(time.wHour),
      static_cast<unsigned>(time.wMinute), static_cast<unsigned>(time.wSecond),
      static_cast<unsigned>(time.wMilliseconds), static_cast<unsigned long>(GetCurrentThreadId()),
      reason, static_cast<unsigned long>(code), address, dumpSaved ? "saved" : "failed");
  if (length > 0) {
    const DWORD bytesToWrite = static_cast<DWORD>(
        std::min<std::size_t>(static_cast<std::size_t>(length), record.size() - 1));
    DWORD bytesWritten = 0;
    WriteFile(logFile, record.data(), bytesToWrite, &bytesWritten, nullptr);
    FlushFileBuffers(logFile);
  }
  CloseHandle(logFile);
}

} // namespace connecttool::windows
