#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace tun {

class TunInterface {
public:
  virtual ~TunInterface() = default;

  virtual bool open(const std::string &deviceName = "", int mtu = 1500) = 0;
  virtual void close() = 0;
  virtual bool is_open() const = 0;

  virtual int read(uint8_t *buffer, size_t size) = 0;
  virtual int write(const uint8_t *buffer, size_t size) = 0;

  virtual std::string get_device_name() const = 0;
  virtual bool set_ip(const std::string &ip, const std::string &netmask) = 0;
  // Optionally install a route for the virtual subnet; return true if added or
  // unsupported.
  virtual bool add_route(const std::string &network,
                         const std::string &netmask) = 0;
  virtual bool set_mtu(int mtu) = 0;
  virtual bool set_up(bool up) = 0;
  virtual bool set_non_blocking(bool nonBlocking) = 0;
  virtual std::string get_last_error() const = 0;
  virtual void *get_read_wait_event() const { return nullptr; }
};

std::unique_ptr<TunInterface> create_tun();

} // namespace tun
