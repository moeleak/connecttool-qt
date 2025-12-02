#ifndef STEAM_MESSAGE_HANDLER_H
#define STEAM_MESSAGE_HANDLER_H

#include "../net/multiplex_manager.h"
#include "../net/tcp_server.h"
#include <boost/asio.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <steamnetworkingtypes.h>
#include <thread>
#include <vector>

class SteamMessageHandler {
public:
  SteamMessageHandler(boost::asio::io_context &io_context,
                      ISteamNetworkingSockets *interface,
                      std::vector<HSteamNetConnection> &connections,
                      std::mutex &connectionsMutex, bool &g_isHost,
                      int &localPort);
  ~SteamMessageHandler();

  void start();
  void stop();

  std::shared_ptr<MultiplexManager>
  getMultiplexManager(HSteamNetConnection conn);

private:
  void startAsyncPoll();

  boost::asio::io_context &io_context_;
  ISteamNetworkingSockets *m_pInterface_;
  std::vector<HSteamNetConnection> &connections_;
  std::mutex &connectionsMutex_;
  bool &g_isHost_;
  int &localPort_;

  std::map<HSteamNetConnection, std::shared_ptr<MultiplexManager>>
      multiplexManagers_;

  std::unique_ptr<boost::asio::steady_timer> timer_;
  bool running_;
  int currentPollInterval_; // 当前轮询间隔（毫秒）
};

#endif // STEAM_MESSAGE_HANDLER_H
