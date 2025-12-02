#pragma once

#include <atomic>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <boost/asio.hpp>
#include <steam_api.h>
#include <isteamnetworkingsockets.h>
#include <steamnetworkingtypes.h>

using boost::asio::ip::tcp;

class MultiplexManager {
public:
    MultiplexManager(ISteamNetworkingSockets* steamInterface, HSteamNetConnection steamConn, 
                     boost::asio::io_context& io_context, bool& isHost, int& localPort);
    ~MultiplexManager();

    std::string addClient(std::shared_ptr<tcp::socket> socket);
    bool removeClient(const std::string& id);
    std::shared_ptr<tcp::socket> getClient(const std::string& id);

    void sendTunnelPacket(const std::string& id, const char* data, size_t len, int type);

    void handleTunnelPacket(const char* data, size_t len);

private:
    ISteamNetworkingSockets* steamInterface_;
    HSteamNetConnection steamConn_;
    std::unordered_map<std::string, std::shared_ptr<tcp::socket>> clientMap_;
    std::mutex mapMutex_;
    boost::asio::io_context& io_context_;
    bool& isHost_;
    int& localPort_;
    std::unordered_map<std::string, std::vector<char>> readBuffers_;
    std::unordered_set<std::string> missingClients_;
    std::map<std::string, std::deque<std::vector<char>>> pendingPackets_;
    std::mutex queueMutex_;
    std::unique_ptr<boost::asio::steady_timer> sendTimer_;
    bool flushScheduled_ = false;

    void startAsyncRead(const std::string& id);
    std::vector<char> buildPacket(const std::string &id, const char *data, size_t len, int type) const;
    bool trySendPacket(const std::vector<char> &packet);
    void enqueuePacket(const std::string &id, std::vector<char> packet);
    void flushPendingPackets();
    void scheduleFlush(std::chrono::milliseconds delay = std::chrono::milliseconds(5));
    void resumePausedReads();
    bool isSendSaturated();
    void removeFromOrder(const std::string &id);

    std::atomic<bool> sendBlocked_{false};
    std::atomic<int> backoffMs_{5};
    std::chrono::steady_clock::time_point lastBlocked_;
    std::unordered_set<std::string> pausedReads_;
    std::mutex pausedMutex_;
    std::unordered_set<std::string> sendOrderSet_;
    std::deque<std::string> sendOrder_;
};
