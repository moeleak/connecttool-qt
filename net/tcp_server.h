#pragma once

#include "multiplex_manager.h"
#include <atomic>
#include <boost/asio.hpp>
#include <functional>
#include <isteamnetworkingsockets.h>
#include <isteamnetworkingutils.h>
#include <memory>
#include <steamnetworkingtypes.h>
#include <string>
#include <thread>

class SteamNetworkingManager;

using boost::asio::ip::tcp;

// TCP Server class
class TCPServer {
public:
  TCPServer(int port, SteamNetworkingManager *manager);
  ~TCPServer();

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
  SteamNetworkingManager *manager_;
};
