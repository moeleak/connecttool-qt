#pragma once

#include <optional>

namespace connecttool::windows {

// The first process supervises startup only. It launches this executable with
// the normal renderer and returns an exit code once that child renders a frame.
// A child process returns std::nullopt and continues into the Qt application.
[[nodiscard]] std::optional<int> superviseRendererStartup();

// Signals the supervising process after the first frame has reached the swap
// chain. This is intentionally a no-op for explicitly launched, unsupervised
// instances.
void signalRendererReady() noexcept;

} // namespace connecttool::windows
