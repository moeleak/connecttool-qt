#ifndef STEAM_MESSAGE_HANDLER_H
#define STEAM_MESSAGE_HANDLER_H

#include "../net/multiplex_manager.h"
#include "../net/tcp_server.h"
#include <atomic>
#include <boost/asio.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <steamnetworkingtypes.h>
#include <thread>
#include <vector>

class SteamMessageHandler {
public:
  struct Dependencies {
    boost::asio::io_context &ioContext;
    ISteamNetworkingSockets &networking;
    std::vector<HSteamNetConnection> &connections;
    std::mutex &connectionsMutex;
    bool &isHost;
    int &localPort;
  };

  explicit SteamMessageHandler(Dependencies dependencies);
  ~SteamMessageHandler();

  void start();
  void stop();

  std::shared_ptr<MultiplexManager> getMultiplexManager(HSteamNetConnection conn);
  void clearMultiplexManagers();

private:
  void startAsyncPoll();

  boost::asio::io_context &io_context_;
  ISteamNetworkingSockets *m_pInterface_;
  std::vector<HSteamNetConnection> &connections_;
  std::mutex &connectionsMutex_;
  bool &g_isHost_;
  int &localPort_;

  std::map<HSteamNetConnection, std::shared_ptr<MultiplexManager>> multiplexManagers_;
  std::mutex multiplexMutex_;

  std::unique_ptr<boost::asio::steady_timer> timer_;
  std::atomic<bool> running_;
  int currentPollInterval_; // 当前轮询间隔（毫秒）
};

#endif // STEAM_MESSAGE_HANDLER_H
