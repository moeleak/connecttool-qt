#include "network/vpn/sha256.h"

#include <array>
#include <bit>
#include <vector>

namespace connecttool::network::detail {
namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants{
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
    0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
    0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
    0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
    0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
    0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
    0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
    0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

[[nodiscard]] constexpr std::uint32_t loadBigEndian(const std::byte *bytes) {
  return (std::to_integer<std::uint32_t>(bytes[0]) << 24U) |
         (std::to_integer<std::uint32_t>(bytes[1]) << 16U) |
         (std::to_integer<std::uint32_t>(bytes[2]) << 8U) |
         std::to_integer<std::uint32_t>(bytes[3]);
}

} // namespace

std::array<std::uint8_t, 32> sha256(std::span<const std::byte> input) {
  std::vector<std::byte> padded(input.begin(), input.end());
  padded.push_back(std::byte{0x80});
  while ((padded.size() % 64U) != 56U) {
    padded.push_back(std::byte{0});
  }
  const std::uint64_t bitLength = static_cast<std::uint64_t>(input.size()) * 8U;
  for (int shift = 56; shift >= 0; shift -= 8) {
    padded.push_back(static_cast<std::byte>((bitLength >> shift) & 0xffU));
  }

  std::array<std::uint32_t, 8> hash{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U,
                                    0xa54ff53aU, 0x510e527fU, 0x9b05688cU,
                                    0x1f83d9abU, 0x5be0cd19U};
  for (std::size_t block = 0; block < padded.size(); block += 64U) {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16; ++i) {
      words[i] = loadBigEndian(padded.data() + block + i * 4U);
    }
    for (std::size_t i = 16; i < words.size(); ++i) {
      const auto s0 = std::rotr(words[i - 15], 7) ^ std::rotr(words[i - 15], 18) ^
                      (words[i - 15] >> 3U);
      const auto s1 = std::rotr(words[i - 2], 17) ^ std::rotr(words[i - 2], 19) ^
                      (words[i - 2] >> 10U);
      words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    auto [a, b, c, d, e, f, g, h] = hash;
    for (std::size_t i = 0; i < words.size(); ++i) {
      const auto sum1 = std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
      const auto choose = (e & f) ^ (~e & g);
      const auto temp1 = h + sum1 + choose + kRoundConstants[i] + words[i];
      const auto sum0 = std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
      const auto majority = (a & b) ^ (a & c) ^ (b & c);
      const auto temp2 = sum0 + majority;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }
    hash[0] += a;
    hash[1] += b;
    hash[2] += c;
    hash[3] += d;
    hash[4] += e;
    hash[5] += f;
    hash[6] += g;
    hash[7] += h;
  }

  std::array<std::uint8_t, 32> digest{};
  for (std::size_t i = 0; i < hash.size(); ++i) {
    digest[i * 4U] = static_cast<std::uint8_t>(hash[i] >> 24U);
    digest[i * 4U + 1U] = static_cast<std::uint8_t>(hash[i] >> 16U);
    digest[i * 4U + 2U] = static_cast<std::uint8_t>(hash[i] >> 8U);
    digest[i * 4U + 3U] = static_cast<std::uint8_t>(hash[i]);
  }
  return digest;
}

} // namespace connecttool::network::detail
