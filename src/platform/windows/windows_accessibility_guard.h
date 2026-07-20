#pragma once

#include <QAbstractNativeEventFilter>
#include <QWindow>

#include <atomic>
#include <cstdint>

// QmlMaterial's controls currently expose a Qt Quick accessibility path that
// can be invalidated while Windows UI Automation handles WM_GETOBJECT. Keep the
// bridge disabled until the underlying Qt/QmlMaterial lifetime issue is fixed.
class WindowsAccessibilityGuard final : public QAbstractNativeEventFilter {
public:
  bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

  [[nodiscard]] bool postProbe(QWindow &window) const;
  [[nodiscard]] std::uint64_t blockedEventCount() const noexcept;

private:
  std::atomic_uint64_t blockedEvents_ = 0;
};
