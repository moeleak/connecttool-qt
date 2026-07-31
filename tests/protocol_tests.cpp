#include "ConnectTool/models/lobbies_model.h"
#include "application/services/update_controller.h"
#include "domain/strong_id.h"
#include "network/protocol/lobby_control.h"
#include "network/protocol/multiplex_protocol.h"
#include "network/protocol/wire_codec.h"
#include "network/vpn/lan_discovery.h"
#include "network/vpn/node_identity.h"
#include "network/vpn/vpn_protocol.h"

#include <QCryptographicHash>
#include <QTest>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace connecttool;

class ProtocolTests final : public QObject {
  Q_OBJECT

private slots:
  void strongIdsAreNotInterchangeable();
  void vpnEnvelopeMatchesLegacyBytes();
  void vpnEnvelopeRejectsMalformedInput();
  void typedPayloadRoundTripsWithoutAlignmentAssumptions();
  void multiplexDataFrameMatchesLegacyBytes();
  void multiplexRejectsMalformedFrames();
  void nodeIdentityMatchesLegacyQtHash();
  void lobbyControlFramesStayOutOfChat();
  void minecraftDiscoveryGetsUnicastFallback();
  void minecraftDiscoveryFallbackRejectsOtherTraffic();
  void lobbiesSortReachablePeersByPing();
  void lobbyFilterKeepsCountInSync();
  void versionsAreComparedSemantically();
};

void ProtocolTests::strongIdsAreNotInterchangeable() {
  static_assert(!std::is_convertible_v<domain::PeerId, domain::LobbyId>);
  static_assert(!std::is_convertible_v<std::uint64_t, domain::PeerId>);

  const domain::PeerId empty;
  const domain::PeerId valid{480};
  QVERIFY(!empty.valid());
  QVERIFY(valid.valid());
  QCOMPARE(valid.value(), std::uint64_t{480});
}

void ProtocolTests::vpnEnvelopeMatchesLegacyBytes() {
  const std::array payload{std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
  const auto encoded = wire::encodeEnvelope(wire::VpnMessageType::Heartbeat, payload);
  QVERIFY(encoded.has_value());

  const std::vector expected{std::byte{14},   std::byte{0},    std::byte{3},
                             std::byte{0x11}, std::byte{0x22}, std::byte{0x33}};
  QCOMPARE(*encoded, expected);

  const auto decoded = wire::decodeEnvelope(*encoded);
  QVERIFY(decoded.has_value());
  QCOMPARE(decoded->type, wire::VpnMessageType::Heartbeat);
  QCOMPARE(decoded->payload.size(), payload.size());
  QVERIFY(std::ranges::equal(decoded->payload, payload));
}

void ProtocolTests::vpnEnvelopeRejectsMalformedInput() {
  const std::array shortHeader{std::byte{1}, std::byte{0}};
  const auto headerResult = wire::decodeEnvelope(shortHeader);
  QVERIFY(!headerResult.has_value());
  QCOMPARE(headerResult.error(), wire::DecodeError::TruncatedHeader);

  const std::array shortPayload{std::byte{1}, std::byte{0}, std::byte{4}, std::byte{0}};
  const auto payloadResult = wire::decodeEnvelope(shortPayload);
  QVERIFY(!payloadResult.has_value());
  QCOMPARE(payloadResult.error(), wire::DecodeError::TruncatedPayload);

  const std::array unknown{std::byte{99}, std::byte{0}, std::byte{0}};
  const auto typeResult = wire::decodeEnvelope(unknown);
  QVERIFY(!typeResult.has_value());
  QCOMPARE(typeResult.error(), wire::DecodeError::InvalidMessageType);
}

void ProtocolTests::typedPayloadRoundTripsWithoutAlignmentAssumptions() {
  wire::HeartbeatPayload payload{};
  payload.ipAddress = 0x01020304U;
  payload.nodeId[0] = 0xabU;
  payload.timestampMs = 123456789;

  std::vector<std::byte> unaligned(sizeof(payload) + 1);
  std::ranges::copy(wire::asBytes(payload), unaligned.begin() + 1);
  const auto decoded = wire::decodePayload<wire::HeartbeatPayload>(std::span{unaligned}.subspan(1));
  QVERIFY(decoded.has_value());
  QCOMPARE(decoded->ipAddress, payload.ipAddress);
  QCOMPARE(decoded->nodeId, payload.nodeId);
  QCOMPARE(decoded->timestampMs, payload.timestampMs);
}

void ProtocolTests::multiplexDataFrameMatchesLegacyBytes() {
  const auto connectionId = multiplex::ConnectionId::parse("aB12z9");
  QVERIFY(connectionId.has_value());
  const std::array payload{std::byte{0xde}, std::byte{0xad}};
  const auto encoded = multiplex::encodeFrame(*connectionId, multiplex::PacketType::Data, payload);

  const std::vector expected{std::byte{'a'}, std::byte{'B'}, std::byte{'1'}, std::byte{'2'},
                             std::byte{'z'}, std::byte{'9'}, std::byte{0},   std::byte{0},
                             std::byte{0},   std::byte{0},   std::byte{0},   std::byte{0xde},
                             std::byte{0xad}};
  QCOMPARE(encoded, expected);

  const auto decoded = multiplex::decodeFrame(encoded);
  QVERIFY(decoded.has_value());
  QCOMPARE(decoded->connectionId.view(), std::string_view{"aB12z9"});
  QCOMPARE(decoded->type, multiplex::PacketType::Data);
  QVERIFY(std::ranges::equal(decoded->payload, payload));
}

void ProtocolTests::multiplexRejectsMalformedFrames() {
  const auto invalidId = multiplex::ConnectionId::parse("bad id");
  QVERIFY(!invalidId.has_value());

  std::array<std::byte, multiplex::kHeaderSize> missingTerminator{};
  missingTerminator[6] = std::byte{'x'};
  const auto terminatorResult = multiplex::decodeFrame(missingTerminator);
  QVERIFY(!terminatorResult.has_value());
  QCOMPARE(terminatorResult.error(), multiplex::CodecError::MissingIdTerminator);
}

void ProtocolTests::nodeIdentityMatchesLegacyQtHash() {
  const std::uint64_t peerValue = 76561198000000000ULL;
  QByteArray input(reinterpret_cast<const char *>(&peerValue), sizeof(peerValue));
  input.append(APP_SECRET_SALT);
  const QByteArray legacyHash = QCryptographicHash::hash(input, QCryptographicHash::Sha256);

  const auto nodeId = NodeIdentity::generate(domain::PeerId{peerValue});
  QCOMPARE(QByteArray(reinterpret_cast<const char *>(nodeId.data()), nodeId.size()),
           legacyHash.first(NODE_ID_SIZE));
}

namespace {

std::uint16_t readBigEndianU16(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) |
                                    static_cast<std::uint16_t>(bytes[offset + 1]));
}

void writeBigEndianU16(std::span<std::uint8_t> bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
  bytes[offset + 1] = static_cast<std::uint8_t>(value & 0xFFU);
}

std::uint16_t finishTestChecksum(std::uint32_t sum) {
  while ((sum >> 16U) != 0) {
    sum = (sum & 0xFFFFU) + (sum >> 16U);
  }
  return static_cast<std::uint16_t>(~sum);
}

void addTestChecksumBytes(std::uint32_t &sum, std::span<const std::uint8_t> bytes) {
  std::size_t offset = 0;
  for (; offset + 1 < bytes.size(); offset += 2) {
    sum += readBigEndianU16(bytes, offset);
  }
  if (offset < bytes.size()) {
    sum += static_cast<std::uint16_t>(bytes[offset]) << 8U;
  }
}

std::uint16_t checksum(std::span<const std::uint8_t> bytes) {
  std::uint32_t sum = 0;
  addTestChecksumBytes(sum, bytes);
  return finishTestChecksum(sum);
}

std::uint16_t udpPacketChecksum(std::span<const std::uint8_t> packet) {
  const std::size_t headerLength = (packet[0] & 0x0FU) * 4U;
  const std::uint16_t udpLength = readBigEndianU16(packet, headerLength + 4);
  std::uint32_t sum = 0;
  addTestChecksumBytes(sum, packet.subspan(12, 8));
  sum += 17;
  sum += udpLength;
  addTestChecksumBytes(sum, packet.subspan(headerLength, udpLength));
  return finishTestChecksum(sum);
}

std::vector<std::uint8_t> minecraftAdvertisement() {
  constexpr std::string_view payload = "[MOTD]Test[/MOTD][AD]54321[/AD]";
  std::vector<std::uint8_t> packet(20 + 8 + payload.size(), 0);
  std::span<std::uint8_t> bytes{packet};
  bytes[0] = 0x45;
  writeBigEndianU16(bytes, 2, static_cast<std::uint16_t>(packet.size()));
  writeBigEndianU16(bytes, 4, 0x1234);
  bytes[8] = 1;
  bytes[9] = 17;
  // 10.23.45.67 -> 224.0.2.60
  const std::array<std::uint8_t, 8> addresses{10, 23, 45, 67, 224, 0, 2, 60};
  std::ranges::copy(addresses, packet.begin() + 12);
  writeBigEndianU16(bytes, 20, 50000);
  writeBigEndianU16(bytes, 22, network::kMinecraftLanDiscoveryPort);
  writeBigEndianU16(bytes, 24, static_cast<std::uint16_t>(8 + payload.size()));
  std::ranges::copy(payload, packet.begin() + 28);

  writeBigEndianU16(bytes, 10, checksum(std::span<const std::uint8_t>{packet}.first(20)));
  std::uint16_t udpChecksum = udpPacketChecksum(packet);
  if (udpChecksum == 0) {
    udpChecksum = 0xFFFFU;
  }
  writeBigEndianU16(bytes, 26, udpChecksum);
  return packet;
}

} // namespace

void ProtocolTests::lobbyControlFramesStayOutOfChat() {
  QCOMPARE(lobby::classifyPayload("hello"), lobby::PayloadKind::UserChat);
  QCOMPARE(lobby::classifyPayload("PING|7656119:42:relay"), lobby::PayloadKind::Ping);
  QCOMPARE(lobby::classifyPayload(R"(PROFILE|{"displayName":"Alice","steamId":"7656119"})"),
           lobby::PayloadKind::LegacyProfile);
  QCOMPARE(lobby::classifyPayload("PROFILE is a normal word"), lobby::PayloadKind::UserChat);
}

void ProtocolTests::minecraftDiscoveryGetsUnicastFallback() {
  const auto multicast = minecraftAdvertisement();
  constexpr std::uint32_t localAddress = 0x0A010203U;  // 10.1.2.3
  constexpr std::uint32_t remoteAddress = 0x0A090807U; // 10.9.8.7
  const auto fallback =
      network::makeMinecraftLanDiscoveryUnicast(multicast, localAddress, remoteAddress);
  QVERIFY(fallback.has_value());
  QCOMPARE(fallback->size(), multicast.size());

  const std::array<std::uint8_t, 4> expectedSource{10, 9, 8, 7};
  QVERIFY(
      std::ranges::equal(std::span<const std::uint8_t>{*fallback}.subspan(12, 4), expectedSource));
  const std::array<std::uint8_t, 4> expectedDestination{10, 1, 2, 3};
  QVERIFY(std::ranges::equal(std::span<const std::uint8_t>{*fallback}.subspan(16, 4),
                             expectedDestination));
  QCOMPARE(readBigEndianU16(*fallback, 22), network::kMinecraftLanDiscoveryPort);
  QCOMPARE(checksum(std::span<const std::uint8_t>{*fallback}.first(20)), std::uint16_t{0});
  QCOMPARE(udpPacketChecksum(*fallback), std::uint16_t{0});
  // The source packet remains the original multicast datagram.
  const std::array<std::uint8_t, 4> multicastDestination{224, 0, 2, 60};
  QVERIFY(std::ranges::equal(std::span<const std::uint8_t>{multicast}.subspan(16, 4),
                             multicastDestination));
  const std::array<std::uint8_t, 4> originalSource{10, 23, 45, 67};
  QVERIFY(
      std::ranges::equal(std::span<const std::uint8_t>{multicast}.subspan(12, 4), originalSource));
}

void ProtocolTests::minecraftDiscoveryFallbackRejectsOtherTraffic() {
  auto packet = minecraftAdvertisement();
  QVERIFY(!network::makeMinecraftLanDiscoveryUnicast(packet, 0, 0x0A000002U).has_value());
  QVERIFY(!network::makeMinecraftLanDiscoveryUnicast(packet, 0x0A000001U, 0).has_value());
  QVERIFY(!network::makeMinecraftLanDiscoveryUnicast(packet, 0x0A000001U, 0x0A000001U).has_value());

  packet[22] = 0x13;
  packet[23] = 0x88; // UDP/5000
  QVERIFY(!network::makeMinecraftLanDiscoveryUnicast(packet, 0x0A000001U, 0x0A000002U).has_value());

  packet = minecraftAdvertisement();
  packet[6] = 0x20; // more fragments
  QVERIFY(!network::makeMinecraftLanDiscoveryUnicast(packet, 0x0A000001U, 0x0A000002U).has_value());

  QVERIFY(!network::makeMinecraftLanDiscoveryUnicast(
               std::span<const std::uint8_t>{packet}.first(19), 0x0A000001U, 0x0A000002U)
               .has_value());

  packet = minecraftAdvertisement();
  writeBigEndianU16(packet, 26, 0); // IPv4 permits a missing UDP checksum.
  const auto zeroChecksumFallback =
      network::makeMinecraftLanDiscoveryUnicast(packet, 0x0A000001U, 0x0A000002U);
  QVERIFY(zeroChecksumFallback.has_value());
  QCOMPARE(readBigEndianU16(*zeroChecksumFallback, 26), std::uint16_t{0});
}

void ProtocolTests::lobbiesSortReachablePeersByPing() {
  LobbiesModel model;
  model.setLobbies({
      {.lobbyId = QStringLiteral("unknown"),
       .name = QStringLiteral("Unknown"),
       .memberCount = 8,
       .ping = -1},
      {.lobbyId = QStringLiteral("slow"),
       .name = QStringLiteral("Slow"),
       .memberCount = 2,
       .ping = 120},
      {.lobbyId = QStringLiteral("fast"),
       .name = QStringLiteral("Fast"),
       .memberCount = 1,
       .ping = 40},
  });

  model.setSortMode(LobbiesModel::SortByPing);

  QCOMPARE(model.data(model.index(0), LobbiesModel::LobbyIdRole).toString(),
           QStringLiteral("fast"));
  QCOMPARE(model.data(model.index(1), LobbiesModel::LobbyIdRole).toString(),
           QStringLiteral("slow"));
  QCOMPARE(model.data(model.index(2), LobbiesModel::LobbyIdRole).toString(),
           QStringLiteral("unknown"));
}

void ProtocolTests::lobbyFilterKeepsCountInSync() {
  LobbiesModel model;
  model.setLobbies({
      {.lobbyId = QStringLiteral("1"),
       .name = QStringLiteral("Alpha"),
       .hostName = QStringLiteral("Alice")},
      {.lobbyId = QStringLiteral("2"),
       .name = QStringLiteral("Beta"),
       .hostName = QStringLiteral("Bob")},
  });
  QCOMPARE(model.count(), 2);

  model.setFilter(QStringLiteral("bob"));

  QCOMPARE(model.count(), 1);
  QCOMPARE(model.rowCount(), 1);
  QCOMPARE(model.data(model.index(0), LobbiesModel::NameRole).toString(), QStringLiteral("Beta"));
}

void ProtocolTests::versionsAreComparedSemantically() {
  QCOMPARE(UpdateController::normalizeVersion(QStringLiteral(" v1.5.15 ")),
           QStringLiteral("1.5.15"));
  QVERIFY(UpdateController::isVersionNewer(QStringLiteral("1.6.0"), QStringLiteral("1.5.15")));
  QVERIFY(!UpdateController::isVersionNewer(QStringLiteral("1.5.2"), QStringLiteral("1.5.15")));
  QVERIFY(!UpdateController::isVersionNewer(QStringLiteral("1.5.15"), QStringLiteral("1.5.15")));
}

QTEST_APPLESS_MAIN(ProtocolTests)

#include "protocol_tests.moc"
