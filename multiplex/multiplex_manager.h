#pragma once

#include <unordered_map>
#include <memory>
#include <mutex>
#include <vector>
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

    uint32_t addClient(std::shared_ptr<tcp::socket> socket);
    void removeClient(uint32_t id);
    std::shared_ptr<tcp::socket> getClient(uint32_t id);

    void sendTunnelPacket(uint32_t id, const char* data, size_t len, int type);

    void handleTunnelPacket(const char* data, size_t len);

private:
    ISteamNetworkingSockets* steamInterface_;
    HSteamNetConnection steamConn_;
    std::unordered_map<uint32_t, std::shared_ptr<tcp::socket>> clientMap_;
    std::mutex mapMutex_;
    uint32_t nextId_;
    boost::asio::io_context& io_context_;
    bool& isHost_;
    int& localPort_;
};