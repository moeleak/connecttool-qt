#include "multiplex_manager.h"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>

namespace {
constexpr std::size_t kTunnelChunkBytes = 60 * 1024;

// Simple, local ID generator to avoid pulling in the full nanoid dependency
std::string generateId(std::size_t length = 6)
{
    static constexpr char chars[] =
        "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> dist(0, sizeof(chars) - 2);

    std::string id;
    id.reserve(length);
    for (std::size_t i = 0; i < length; ++i)
    {
        id.push_back(chars[dist(rng)]);
    }
    return id;
}
} // namespace

MultiplexManager::MultiplexManager(ISteamNetworkingSockets *steamInterface, HSteamNetConnection steamConn,
                                   boost::asio::io_context &io_context, bool &isHost, int &localPort)
    : steamInterface_(steamInterface), steamConn_(steamConn),
      io_context_(io_context), isHost_(isHost), localPort_(localPort) {}

MultiplexManager::~MultiplexManager()
{
    // Close all sockets
    std::lock_guard<std::mutex> lock(mapMutex_);
    for (auto &pair : clientMap_)
    {
        pair.second->close();
    }
    clientMap_.clear();
}

std::string MultiplexManager::addClient(std::shared_ptr<tcp::socket> socket)
{
    std::string id;
    {
        std::lock_guard<std::mutex> lock(mapMutex_);
        id = generateId(6);
        clientMap_[id] = socket;
        readBuffers_[id].resize(1048576);
    }
    startAsyncRead(id);
    std::cout << "Added client with id " << id << std::endl;
    return id;
}

void MultiplexManager::removeClient(const std::string &id)
{
    std::lock_guard<std::mutex> lock(mapMutex_);
    auto it = clientMap_.find(id);
    if (it != clientMap_.end())
    {
        it->second->close();
        clientMap_.erase(it);
    }
    readBuffers_.erase(id);

    std::cout << "Removed client with id " << id << std::endl;
}

std::shared_ptr<tcp::socket> MultiplexManager::getClient(const std::string &id)
{
    std::lock_guard<std::mutex> lock(mapMutex_);
    auto it = clientMap_.find(id);
    if (it != clientMap_.end())
    {
        return it->second;
    }
    return nullptr;
}

namespace
{

void sendPacketInternal(ISteamNetworkingSockets *iface,
                        HSteamNetConnection conn,
                        const std::string &id,
                        const char *data,
                        size_t len,
                        int type)
{
    const size_t idLen = id.size() + 1;
    const size_t payloadLen = (type == 0 ? len : 0);
    const size_t packetSize = idLen + sizeof(uint32_t) + payloadLen;
    std::vector<char> packet(packetSize);
    std::memcpy(&packet[0], id.c_str(), idLen);
    auto *pType = reinterpret_cast<uint32_t *>(&packet[idLen]);
    *pType = type;
    if (payloadLen > 0 && data)
    {
        std::memcpy(&packet[idLen + sizeof(uint32_t)], data, payloadLen);
    }
    iface->SendMessageToConnection(conn, packet.data(), packet.size(), k_nSteamNetworkingSend_Reliable, nullptr);
}

} // namespace

void MultiplexManager::sendTunnelPacket(const std::string &id, const char *data, size_t len, int type)
{
    if (type == 0 && data && len > kTunnelChunkBytes)
    {
        size_t offset = 0;
        while (offset < len)
        {
            const size_t chunk = std::min(kTunnelChunkBytes, len - offset);
            sendPacketInternal(steamInterface_, steamConn_, id, data + offset, chunk, 0);
            offset += chunk;
        }
        return;
    }

    sendPacketInternal(steamInterface_, steamConn_, id, data, len, type);
}

void MultiplexManager::handleTunnelPacket(const char *data, size_t len)
{
    size_t idLen = 7; // 6 + null
    if (len < idLen + sizeof(uint32_t))
    {
        std::cerr << "Invalid tunnel packet size" << std::endl;
        return;
    }
    std::string id(data, 6);
    uint32_t type = *reinterpret_cast<const uint32_t *>(data + idLen);
    if (type == 0)
    {
        // Data packet
        size_t dataLen = len - idLen - sizeof(uint32_t);
        const char *packetData = data + idLen + sizeof(uint32_t);
        auto socket = getClient(id);
        if (!socket && isHost_ && localPort_ > 0)
        {
            // 如果是主持且没有对应的 TCP Client，创建一个连接到本地端口
            std::cout << "Creating new TCP client for id " << id << " connecting to localhost:" << localPort_ << std::endl;
            try
            {
                auto newSocket = std::make_shared<tcp::socket>(io_context_);
                tcp::resolver resolver(io_context_);
                auto endpoints = resolver.resolve("127.0.0.1", std::to_string(localPort_));
                boost::asio::connect(*newSocket, endpoints);

                std::string tempId = id;
                {
                    std::lock_guard<std::mutex> lock(mapMutex_);
                    clientMap_[id] = newSocket;
                    readBuffers_[id].resize(1048576);
                    socket = newSocket;
                }
                std::cout << "Successfully created TCP client for id " << id << std::endl;
                startAsyncRead(tempId);
            }
            catch (const std::exception &e)
            {
                std::cerr << "Failed to create TCP client for id " << id << ": " << e.what() << std::endl;
                return;
            }
        }
        if (socket)
        {
            boost::asio::async_write(*socket, boost::asio::buffer(packetData, dataLen), [](const boost::system::error_code &, std::size_t) {});
        }
        else
        {
            std::cerr << "No client found for id " << id << std::endl;
        }
    }
    else if (type == 1)
    {
        // Disconnect packet
        removeClient(id);
        std::cout << "Client " << id << " disconnected" << std::endl;
    }
    else
    {
        std::cerr << "Unknown packet type " << type << std::endl;
    }
}

void MultiplexManager::startAsyncRead(const std::string &id)
{
    auto socket = getClient(id);
    if (!socket)
    {
        std::cout << "Error: Socket is null for id " << id << std::endl;
        return;
    }
    socket->async_read_some(boost::asio::buffer(readBuffers_[id]),
    [this, id](const boost::system::error_code &ec, std::size_t bytes_transferred)
    {
        if (!ec)
        {
            if (bytes_transferred > 0)
            {
                sendTunnelPacket(id, readBuffers_[id].data(), bytes_transferred, 0);
            }
            startAsyncRead(id);
        }
        else
        {
            std::cout << "Error reading from TCP client " << id << ": " << ec.message() << std::endl;
            removeClient(id);
        }
    });
}
