#ifdef _WIN32

#include "firewall_windows.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <netfw.h>
#include <oleauto.h>

#include <array>
#include <bit>
#include <charconv>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

template <typename Interface> class ComPtr final {
public:
  ComPtr() = default;
  ComPtr(const ComPtr &) = delete;
  ComPtr &operator=(const ComPtr &) = delete;

  ~ComPtr() { reset(); }

  [[nodiscard]] Interface *get() const noexcept { return value_; }
  [[nodiscard]] Interface **put() noexcept {
    reset();
    return &value_;
  }
  [[nodiscard]] Interface *operator->() const noexcept { return value_; }

private:
  void reset() noexcept {
    if (value_ != nullptr) {
      value_->Release();
      value_ = nullptr;
    }
  }

  Interface *value_ = nullptr;
};

class ComApartment final {
public:
  ComApartment() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ComApartment(const ComApartment &) = delete;
  ComApartment &operator=(const ComApartment &) = delete;

  ~ComApartment() {
    if (SUCCEEDED(result_)) {
      CoUninitialize();
    }
  }

  [[nodiscard]] bool available() const noexcept {
    return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
  }
  [[nodiscard]] HRESULT result() const noexcept { return result_; }

private:
  HRESULT result_;
};

class BString final {
public:
  explicit BString(std::wstring_view value) {
    if (value.size() <= std::numeric_limits<UINT>::max()) {
      value_ = SysAllocStringLen(value.data(), static_cast<UINT>(value.size()));
    }
  }
  BString(const BString &) = delete;
  BString &operator=(const BString &) = delete;

  ~BString() { SysFreeString(value_); }

  [[nodiscard]] BSTR get() const noexcept { return value_; }
  [[nodiscard]] explicit operator bool() const noexcept { return value_ != nullptr; }

private:
  BSTR value_ = nullptr;
};

struct FirewallRuleSpec final {
  std::wstring name;
  long protocol = NET_FW_IP_PROTOCOL_ANY;
  std::optional<std::wstring> localAddresses;
  std::optional<std::wstring> remoteAddresses;
};

[[nodiscard]] std::optional<std::uint32_t> parseIpv4(std::string_view input) {
  std::uint32_t address = 0;
  std::size_t offset = 0;

  for (std::size_t index = 0; index < 4; ++index) {
    const std::size_t separator = input.find('.', offset);
    const bool isLastOctet = index == 3;
    if ((isLastOctet && separator != std::string_view::npos) ||
        (!isLastOctet && separator == std::string_view::npos)) {
      return std::nullopt;
    }

    const std::size_t end = isLastOctet ? input.size() : separator;
    const std::string_view octetText = input.substr(offset, end - offset);
    unsigned int octet = 0;
    const auto [parseEnd, error] =
        std::from_chars(octetText.data(), octetText.data() + octetText.size(), octet);
    if (octetText.empty() || error != std::errc{} ||
        parseEnd != octetText.data() + octetText.size() || octet > 255) {
      return std::nullopt;
    }

    address = (address << 8U) | octet;
    offset = end + 1;
  }
  return address;
}

[[nodiscard]] std::optional<std::string> subnetToken(const TunFirewallScope &scope) {
  const auto address = parseIpv4(scope.localAddress);
  const auto mask = parseIpv4(scope.subnetMask);
  if (!address || !mask) {
    return std::nullopt;
  }

  const unsigned int prefix = std::countl_one(*mask);
  if (prefix != 32 && (*mask << prefix) != 0) {
    return std::nullopt;
  }

  const std::uint32_t network = *address & *mask;
  std::string result;
  result.reserve(18);
  for (unsigned int shift : std::array{24U, 16U, 8U, 0U}) {
    if (!result.empty()) {
      result.push_back('.');
    }
    result += std::to_string((network >> shift) & 0xffU);
  }
  result.push_back('/');
  result += std::to_string(prefix);
  return result;
}

template <typename... Operations> [[nodiscard]] HRESULT firstFailure(Operations &&...operations) {
  HRESULT result = S_OK;
  const auto run = [&result](auto &&operation) {
    if (SUCCEEDED(result)) {
      result = std::invoke(std::forward<decltype(operation)>(operation));
    }
  };
  (run(std::forward<Operations>(operations)), ...);
  return result;
}

[[nodiscard]] std::optional<std::wstring> toWide(std::string_view input) {
  if (input.empty()) {
    return std::nullopt;
  }

  if (input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
    return std::nullopt;
  }

  const auto convert = [input](UINT codePage, DWORD flags) -> std::optional<std::wstring> {
    const int inputLength = static_cast<int>(input.size());
    const int size = MultiByteToWideChar(codePage, flags, input.data(), inputLength, nullptr, 0);
    if (size <= 0) {
      return std::nullopt;
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(codePage, flags, input.data(), inputLength, result.data(), size) == 0) {
      return std::nullopt;
    }
    return result;
  };

  if (auto utf8 = convert(CP_UTF8, MB_ERR_INVALID_CHARS)) {
    return utf8;
  }
  return convert(CP_ACP, 0);
}

[[nodiscard]] bool reportFailure(std::string_view operation, HRESULT result) {
  std::cerr << "Windows Firewall API: " << operation << " failed (HRESULT 0x" << std::hex
            << static_cast<unsigned long>(result) << std::dec << ")" << std::endl;
  return false;
}

[[nodiscard]] HRESULT configureRule(INetFwRule &rule, const FirewallRuleSpec &spec) {
  const BString name{spec.name};
  if (!name) {
    return E_OUTOFMEMORY;
  }

  HRESULT result = firstFailure([&] { return rule.put_Name(name.get()); },
                                [&] { return rule.put_Protocol(spec.protocol); },
                                [&] { return rule.put_Direction(NET_FW_RULE_DIR_IN); },
                                [&] { return rule.put_Action(NET_FW_ACTION_ALLOW); },
                                [&] { return rule.put_Profiles(NET_FW_PROFILE2_ALL); });

  if (SUCCEEDED(result) && spec.localAddresses) {
    const BString addresses{*spec.localAddresses};
    result = addresses ? rule.put_LocalAddresses(addresses.get()) : E_OUTOFMEMORY;
  }

  if (SUCCEEDED(result) && spec.remoteAddresses) {
    const BString addresses{*spec.remoteAddresses};
    result = addresses ? rule.put_RemoteAddresses(addresses.get()) : E_OUTOFMEMORY;
  }

  if (SUCCEEDED(result)) {
    result = rule.put_Enabled(VARIANT_TRUE);
  }
  return result;
}

[[nodiscard]] bool installRule(const FirewallRuleSpec &spec) {
  const ComApartment apartment;
  if (!apartment.available()) {
    return reportFailure("COM initialization", apartment.result());
  }

  ComPtr<INetFwPolicy2> policy;
  HRESULT result =
      CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER,
                       __uuidof(INetFwPolicy2), reinterpret_cast<void **>(policy.put()));
  if (FAILED(result)) {
    return reportFailure("policy creation", result);
  }

  ComPtr<INetFwRules> rules;
  result = policy->get_Rules(rules.put());
  if (FAILED(result)) {
    return reportFailure("rule collection lookup", result);
  }

  const BString name{spec.name};
  if (!name) {
    return reportFailure("rule name allocation", E_OUTOFMEMORY);
  }
  result = rules->Remove(name.get());
  if (FAILED(result)) {
    return reportFailure("old rule removal", result);
  }

  ComPtr<INetFwRule> rule;
  result = CoCreateInstance(__uuidof(NetFwRule), nullptr, CLSCTX_INPROC_SERVER,
                            __uuidof(INetFwRule), reinterpret_cast<void **>(rule.put()));
  if (FAILED(result)) {
    return reportFailure("rule creation", result);
  }

  result = configureRule(*rule.get(), spec);
  if (FAILED(result)) {
    return reportFailure("rule configuration", result);
  }

  result = rules->Add(rule.get());
  return SUCCEEDED(result) || reportFailure("rule installation", result);
}

} // namespace

bool ensureTunFirewallRule(std::string_view ruleName, const TunFirewallScope &scope) {
  const auto name = toWide(ruleName);
  const auto localAddress = toWide(scope.localAddress);
  const auto subnet = subnetToken(scope);
  const auto remoteAddresses = subnet ? toWide(*subnet) : std::nullopt;
  if (!name || !localAddress || !remoteAddresses) {
    return false;
  }

  return installRule(FirewallRuleSpec{
      .name = *name,
      .protocol = NET_FW_IP_PROTOCOL_ANY,
      .localAddresses = *localAddress,
      .remoteAddresses = *remoteAddresses,
  });
}

#endif
