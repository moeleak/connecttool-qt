#pragma once

#include <atomic>
#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace connecttool::network {

using boost::asio::ip::tcp;

class TcpTunnelServer {
public:
  using ClientClosedCallback = std::function<void()>;
  using ClientRegistrar = std::function<std::string(std::shared_ptr<tcp::socket>,
                                                    ClientClosedCallback)>;

  TcpTunnelServer(int port, ClientRegistrar registrar);
  ~TcpTunnelServer();

  bool start();
  void stop();
  int getClientCount() const;
  void setClientCountCallback(std::function<void(int)> callback);

private:
  struct ClientRegistry;

  void start_accept();

  int port_;
  std::atomic<bool> running_;
  boost::asio::io_context io_context_;
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_;
  tcp::acceptor acceptor_;
  std::shared_ptr<ClientRegistry> clients_;
  std::jthread serverThread_;
  ClientRegistrar registrar_;
};

} // namespace connecttool::network
