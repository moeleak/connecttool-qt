#include "domain/strong_id.h"
#include "ConnectTool/models/lobbies_model.h"
#include "network/protocol/multiplex_protocol.h"
#include "network/protocol/wire_codec.h"
#include "network/vpn/node_identity.h"
#include "network/vpn/vpn_protocol.h"
#include "application/services/update_controller.h"

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
