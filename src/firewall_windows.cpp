#ifdef _WIN32

#include "firewall_windows.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>

#include <netfw.h>
#include <oleauto.h>

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
  ComApartment() : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
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

class InterfaceNames final {
public:
  explicit InterfaceNames(std::wstring_view interfaceName) {
    VariantInit(&value_);
    SAFEARRAY *array = SafeArrayCreateVector(VT_VARIANT, 0, 1);
    if (array == nullptr) {
      result_ = E_OUTOFMEMORY;
      return;
    }

    VARIANT item;
    VariantInit(&item);
    item.vt = VT_BSTR;
    item.bstrVal = SysAllocStringLen(interfaceName.data(), static_cast<UINT>(interfaceName.size()));
    if (item.bstrVal == nullptr) {
      SafeArrayDestroy(array);
      result_ = E_OUTOFMEMORY;
      return;
    }

    LONG index = 0;
    result_ = SafeArrayPutElement(array, &index, &item);
    VariantClear(&item);
    if (FAILED(result_)) {
      SafeArrayDestroy(array);
      return;
    }

    value_.vt = VT_ARRAY | VT_VARIANT;
    value_.parray = array;
  }
  InterfaceNames(const InterfaceNames &) = delete;
  InterfaceNames &operator=(const InterfaceNames &) = delete;

  ~InterfaceNames() { VariantClear(&value_); }

  [[nodiscard]] HRESULT result() const noexcept { return result_; }
  [[nodiscard]] VARIANT get() const noexcept { return value_; }

private:
  VARIANT value_{};
  HRESULT result_ = S_OK;
};

struct FirewallRuleSpec final {
  std::wstring name;
  long protocol = NET_FW_IP_PROTOCOL_ANY;
  std::optional<std::wstring> localPorts;
  std::optional<std::wstring> interfaceName;
};

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

[[nodiscard]] std::optional<std::wstring> toWide(const char *value) {
  if (value == nullptr || value[0] == '\0') {
    return std::nullopt;
  }

  const std::string_view input{value};
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
                                [&] { return rule.put_Profiles(NET_FW_PROFILE2_ALL); },
                                [&] { return rule.put_Enabled(VARIANT_TRUE); });

  if (SUCCEEDED(result) && spec.localPorts) {
    const BString ports{*spec.localPorts};
    result = ports ? rule.put_LocalPorts(ports.get()) : E_OUTOFMEMORY;
  }

  if (SUCCEEDED(result) && spec.interfaceName) {
    const InterfaceNames interfaces{*spec.interfaceName};
    result = interfaces.result();
    if (SUCCEEDED(result)) {
      result = rule.put_Interfaces(interfaces.get());
    }
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

bool ensureTcpFirewallRule(const char *ruleName, int port) {
  const auto name = toWide(ruleName);
  if (!name || port <= 0 || port > 65'535) {
    return false;
  }

  return installRule(FirewallRuleSpec{
      .name = *name,
      .protocol = NET_FW_IP_PROTOCOL_TCP,
      .localPorts = std::to_wstring(port),
  });
}

bool ensureTunFirewallRule(const char *ruleName, const char *interfaceAlias) {
  const auto name = toWide(ruleName);
  const auto interfaceName = toWide(interfaceAlias);
  if (!name || !interfaceName) {
    return false;
  }

  return installRule(FirewallRuleSpec{
      .name = *name,
      .protocol = NET_FW_IP_PROTOCOL_ANY,
      .interfaceName = *interfaceName,
  });
}

#endif
