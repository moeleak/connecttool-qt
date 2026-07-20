#pragma once

#include "network/tcp/reliable_channel.h"

#include <atomic>
#include <boost/asio.hpp>
#include <chrono>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace connecttool::network {

using boost::asio::ip::tcp;

class MultiplexSession : public std::enable_shared_from_this<MultiplexSession> {
public:
  struct Dependencies {
    std::shared_ptr<ReliableChannel> channel;
    boost::asio::io_context &ioContext;
    bool &isHost;
    int &localPort;
  };

  explicit MultiplexSession(Dependencies dependencies);
  ~MultiplexSession();

  using ClientClosedCallback = std::function<void()>;
  std::string addClient(std::shared_ptr<tcp::socket> socket, ClientClosedCallback onClosed = {});
  bool removeClient(const std::string &id);
  std::shared_ptr<tcp::socket> getClient(const std::string &id);

  void sendTunnelPacket(const std::string &id, const char *data, size_t len, int type);

  void handleTunnelPacket(const char *data, size_t len);

private:
  struct ClientState;

  std::shared_ptr<ReliableChannel> channel_;
  std::unordered_map<std::string, std::shared_ptr<ClientState>> clients_;
  std::mutex mapMutex_;
  boost::asio::io_context &io_context_;
  bool &isHost_;
  int &localPort_;
  std::unordered_set<std::string> missingClients_;
  std::map<std::string, std::deque<std::vector<char>>> pendingPackets_;
  std::mutex queueMutex_;
  std::unique_ptr<boost::asio::steady_timer> sendTimer_;
  bool flushScheduled_ = false;

  void startAsyncRead(const std::string &id);
  void enqueueClientWrite(const std::string &id, const char *data, std::size_t length);
  void startClientWrite(const std::string &id, const std::shared_ptr<ClientState> &client);
  std::shared_ptr<ClientState> getClientState(const std::string &id);
  std::vector<char> buildPacket(const std::string &id, const char *data, size_t len,
                                int type) const;
  bool trySendPacket(const std::vector<char> &packet);
  void enqueuePacket(const std::string &id, std::vector<char> packet);
  void flushPendingPackets();
  void scheduleFlush(std::chrono::milliseconds delay = std::chrono::milliseconds(5));
  void resumePausedReads();
  bool isSendSaturated();
  void removeFromOrder(const std::string &id);

  std::atomic<bool> sendBlocked_{false};
  std::atomic<int> backoffMs_{5};
  std::atomic<std::chrono::steady_clock::duration::rep> lastBlockedTicks_{0};
  std::unordered_set<std::string> pausedReads_;
  std::mutex pausedMutex_;
  std::unordered_set<std::string> sendOrderSet_;
  std::deque<std::string> sendOrder_;
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> recentConnectFail_;
};

} // namespace connecttool::network
