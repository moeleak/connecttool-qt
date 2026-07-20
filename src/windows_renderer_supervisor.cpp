#include "windows_renderer_supervisor.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
// shellapi.h relies on declarations and macros provided by windows.h.
#include <windows.h>

#include <shellapi.h>

#include <array>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace connecttool::windows {

namespace {
constexpr std::wstring_view childArgumentPrefix = L"--renderer-ready-event=";
constexpr std::wstring_view softwareArgument = L"--software-renderer";
constexpr std::wstring_view smokeTestArgument = L"--qml-smoke-test";
constexpr std::wstring_view screenshotArgumentPrefix = L"--qml-screenshot=";
constexpr std::wstring_view simulateFailureArgument = L"--simulate-renderer-startup-failure";
constexpr DWORD hardwareStartupTimeoutMs = 15'000;
constexpr DWORD softwareStartupTimeoutMs = 30'000;
constexpr DWORD screenshotExitTimeoutMs = 30'000;
constexpr int simulatedFailureExitCode = 86;

class UniqueHandle final {
public:
  explicit UniqueHandle(HANDLE handle = nullptr) noexcept : handle_(handle) {}
  ~UniqueHandle() { reset(); }

  UniqueHandle(const UniqueHandle &) = delete;
  UniqueHandle &operator=(const UniqueHandle &) = delete;

  UniqueHandle(UniqueHandle &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

  UniqueHandle &operator=(UniqueHandle &&other) noexcept {
    if (this != &other) {
      reset(std::exchange(other.handle_, nullptr));
    }
    return *this;
  }

  [[nodiscard]] HANDLE get() const noexcept { return handle_; }
  [[nodiscard]] explicit operator bool() const noexcept {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
  }

  void reset(HANDLE replacement = nullptr) noexcept {
    if (*this) {
      CloseHandle(handle_);
    }
    handle_ = replacement;
  }

private:
  HANDLE handle_ = nullptr;
};

class CommandLineArguments final {
public:
  CommandLineArguments() { values_ = CommandLineToArgvW(GetCommandLineW(), &count_); }

  ~CommandLineArguments() {
    if (values_ != nullptr) {
      LocalFree(values_);
    }
  }

  CommandLineArguments(const CommandLineArguments &) = delete;
  CommandLineArguments &operator=(const CommandLineArguments &) = delete;

  [[nodiscard]] bool contains(std::wstring_view expected) const noexcept {
    for (int index = 1; index < count_; ++index) {
      if (values_[index] != nullptr && std::wstring_view{values_[index]} == expected) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] bool containsPrefix(std::wstring_view prefix) const noexcept {
    return valueWithPrefix(prefix).has_value();
  }

  [[nodiscard]] std::optional<std::wstring> valueWithPrefix(std::wstring_view prefix) const {
    for (int index = 1; index < count_; ++index) {
      if (values_[index] == nullptr) {
        continue;
      }
      const std::wstring_view argument{values_[index]};
      if (argument.starts_with(prefix)) {
        return std::wstring{argument.substr(prefix.size())};
      }
    }
    return std::nullopt;
  }

private:
  int count_ = 0;
  LPWSTR *values_ = nullptr;
};

enum class AttemptState { ready, exited, timedOut, launchFailed };

struct AttemptOutcome final {
  AttemptState state = AttemptState::launchFailed;
  DWORD exitCode = EXIT_FAILURE;
};

[[nodiscard]] std::wstring applicationPath() {
  std::array<wchar_t, 32'768> buffer{};
  const DWORD length =
      GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  if (length == 0 || length >= buffer.size()) {
    return {};
  }
  return std::wstring{buffer.data(), length};
}

[[nodiscard]] std::wstring makeEventName() {
  return L"Local\\ConnectTool.RendererReady." + std::to_wstring(GetCurrentProcessId()) + L'.' +
         std::to_wstring(GetTickCount64());
}

[[nodiscard]] std::optional<UniqueHandle> launchChild(std::wstring_view eventName,
                                                      bool softwareRenderer) {
  const std::wstring executable = applicationPath();
  if (executable.empty()) {
    return std::nullopt;
  }

  std::wstring commandLine = GetCommandLineW();
  commandLine += L' ';
  commandLine += childArgumentPrefix;
  commandLine += eventName;
  if (softwareRenderer) {
    commandLine += L' ';
    commandLine += softwareArgument;
  }

  std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
  mutableCommandLine.push_back(L'\0');

  STARTUPINFOW startupInfo{};
  startupInfo.cb = sizeof(startupInfo);
  PROCESS_INFORMATION processInfo{};
  const BOOL created =
      CreateProcessW(executable.c_str(), mutableCommandLine.data(), nullptr, nullptr, FALSE, 0,
                     nullptr, nullptr, &startupInfo, &processInfo);
  if (!created) {
    return std::nullopt;
  }

  CloseHandle(processInfo.hThread);
  return UniqueHandle{processInfo.hProcess};
}

[[nodiscard]] AttemptOutcome runAttempt(bool softwareRenderer, bool waitForChildExit) {
  const std::wstring eventName = makeEventName();
  UniqueHandle readyEvent{CreateEventW(nullptr, TRUE, FALSE, eventName.c_str())};
  if (!readyEvent) {
    return {};
  }

  auto process = launchChild(eventName, softwareRenderer);
  if (!process) {
    return {};
  }

  const std::array handles{readyEvent.get(), process->get()};
  const DWORD timeout = softwareRenderer ? softwareStartupTimeoutMs : hardwareStartupTimeoutMs;
  const DWORD waitResult =
      WaitForMultipleObjects(static_cast<DWORD>(handles.size()), handles.data(), FALSE, timeout);

  if (waitResult == WAIT_OBJECT_0) {
    if (!waitForChildExit) {
      return {.state = AttemptState::ready, .exitCode = EXIT_SUCCESS};
    }
    if (WaitForSingleObject(process->get(), screenshotExitTimeoutMs) == WAIT_OBJECT_0) {
      DWORD exitCode = EXIT_FAILURE;
      GetExitCodeProcess(process->get(), &exitCode);
      return {.state = exitCode == EXIT_SUCCESS ? AttemptState::ready : AttemptState::exited,
              .exitCode = exitCode};
    }
  } else if (waitResult == WAIT_OBJECT_0 + 1) {
    DWORD exitCode = EXIT_FAILURE;
    GetExitCodeProcess(process->get(), &exitCode);
    return {.state = AttemptState::exited, .exitCode = exitCode};
  }

  TerminateProcess(process->get(), EXIT_FAILURE);
  WaitForSingleObject(process->get(), 2'000);
  return {.state = AttemptState::timedOut, .exitCode = EXIT_FAILURE};
}

[[nodiscard]] int failureExitCode(const AttemptOutcome &outcome) noexcept {
  return outcome.state == AttemptState::exited ? static_cast<int>(outcome.exitCode) : EXIT_FAILURE;
}
} // namespace

std::optional<int> superviseRendererStartup() {
  const CommandLineArguments arguments;
  const bool childProcess = arguments.containsPrefix(childArgumentPrefix);
  const bool softwareRenderer = arguments.contains(softwareArgument);

  if (childProcess) {
    if (!softwareRenderer && arguments.contains(simulateFailureArgument)) {
      return simulatedFailureExitCode;
    }
    return std::nullopt;
  }

  // Explicit software mode is useful for diagnostics and must not recurse.
  if (softwareRenderer || arguments.contains(smokeTestArgument)) {
    return std::nullopt;
  }

  const bool waitForChildExit = arguments.containsPrefix(screenshotArgumentPrefix);
  const AttemptOutcome hardware = runAttempt(false, waitForChildExit);
  if (hardware.state == AttemptState::ready) {
    return EXIT_SUCCESS;
  }

  const AttemptOutcome software = runAttempt(true, waitForChildExit);
  if (software.state == AttemptState::ready) {
    return EXIT_SUCCESS;
  }

  if (!waitForChildExit) {
    MessageBoxW(nullptr,
                L"ConnectTool \u56fe\u5f62\u754c\u9762\u542f\u52a8\u5931\u8d25\u3002\n\u8bf7\u5c06 "
                L"logs\\connecttool.log \u53d1\u7ed9\u5f00\u53d1\u8005\u3002",
                L"ConnectTool", MB_OK | MB_ICONERROR);
  }
  return failureExitCode(software);
}

void signalRendererReady() noexcept {
  const CommandLineArguments arguments;
  const auto eventName = arguments.valueWithPrefix(childArgumentPrefix);
  if (!eventName || eventName->empty()) {
    return;
  }

  UniqueHandle readyEvent{OpenEventW(EVENT_MODIFY_STATE, FALSE, eventName->c_str())};
  if (readyEvent) {
    SetEvent(readyEvent.get());
  }
}

} // namespace connecttool::windows
