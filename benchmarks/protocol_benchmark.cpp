#include "net/multiplex_protocol.h"
#include "net/wire_codec.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <span>
#include <string_view>

namespace {

using Clock = std::chrono::steady_clock;

template <typename Operation>
void measure(std::string_view name, std::size_t iterations, Operation &&operation) {
  std::uint64_t checksum = 0;
  const auto started = Clock::now();
  for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
    checksum += operation(iteration);
  }
  const auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
  const double operationsPerSecond = static_cast<double>(iterations) / elapsed;

  std::cout << std::left << std::setw(22) << name << std::right << std::fixed
            << std::setprecision(2) << operationsPerSecond << " ops/s  (" << elapsed * 1000.0
            << " ms, checksum " << checksum << ")\n";
}

} // namespace

int main() {
  using namespace connecttool;

  // Long enough to average out CPU frequency ramp-up while remaining quick
  // enough for routine before/after checks on developer machines.
  constexpr std::size_t iterations = 5'000'000;
  std::array<std::byte, 1'200> payload{};
  const auto connectionId = multiplex::ConnectionId::parse("Ab12Z9");
  if (!connectionId) {
    return 1;
  }

  const auto vpnFrame = wire::encodeEnvelope(wire::VpnMessageType::IpPacket, payload);
  if (!vpnFrame) {
    return 1;
  }
  const auto multiplexFrame =
      multiplex::encodeFrame(*connectionId, multiplex::PacketType::Data, payload);
  auto mutableVpnFrame = *vpnFrame;
  auto mutableMultiplexFrame = multiplexFrame;

  std::cout << "ConnectTool protocol benchmark (1,200-byte payload, " << iterations
            << " iterations)\n";

  measure("VPN encode", iterations, [&](std::size_t iteration) {
    payload[iteration % payload.size()] = static_cast<std::byte>(iteration & 0xffU);
    const auto encoded = wire::encodeEnvelope(wire::VpnMessageType::IpPacket, payload);
    return encoded ? encoded->size() + std::to_integer<std::uint8_t>(encoded->back())
                   : std::size_t{};
  });

  measure("VPN decode", iterations, [&](std::size_t iteration) {
    const auto payloadIndex = iteration % payload.size();
    mutableVpnFrame[sizeof(wire::VpnMessageHeader) + payloadIndex] =
        static_cast<std::byte>(iteration & 0xffU);
    const auto decoded = wire::decodeEnvelope(mutableVpnFrame);
    return decoded ? decoded->payload.size() +
                         std::to_integer<std::uint8_t>(decoded->payload[payloadIndex])
                   : std::size_t{};
  });

  measure("Multiplex encode", iterations, [&](std::size_t iteration) {
    payload[iteration % payload.size()] = static_cast<std::byte>((iteration * 3U) & 0xffU);
    const auto encoded =
        multiplex::encodeFrame(*connectionId, multiplex::PacketType::Data, payload);
    return encoded.size() + std::to_integer<std::uint8_t>(encoded.back());
  });

  measure("Multiplex decode", iterations, [&](std::size_t iteration) {
    const auto payloadIndex = iteration % payload.size();
    mutableMultiplexFrame[multiplex::kHeaderSize + payloadIndex] =
        static_cast<std::byte>((iteration * 7U) & 0xffU);
    const auto decoded = multiplex::decodeFrame(mutableMultiplexFrame);
    return decoded ? decoded->payload.size() +
                         std::to_integer<std::uint8_t>(decoded->payload[payloadIndex])
                   : std::size_t{};
  });
}
