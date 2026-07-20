#include "windows_accessibility_guard.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <qt_windows.h>

bool WindowsAccessibilityGuard::nativeEventFilter(const QByteArray &eventType, void *message,
                                                  qintptr *result) {
  if (eventType != QByteArrayLiteral("windows_generic_MSG") || message == nullptr) {
    return false;
  }

  const auto *nativeMessage = static_cast<const MSG *>(message);
  if (nativeMessage->message != WM_GETOBJECT) {
    return false;
  }

  blockedEvents_.fetch_add(1, std::memory_order_relaxed);
  if (result != nullptr) {
    *result = 0;
  }
  return true;
}

bool WindowsAccessibilityGuard::postProbe(QWindow &window) const {
  const auto handle = reinterpret_cast<HWND>(window.winId());
  return handle != nullptr &&
         PostMessageW(handle, WM_GETOBJECT, 0, static_cast<LPARAM>(OBJID_CLIENT)) != FALSE;
}

std::uint64_t WindowsAccessibilityGuard::blockedEventCount() const noexcept {
  return blockedEvents_.load(std::memory_order_relaxed);
}
