#include "multiplex_manager.h"
#include <iostream>
#include <cstring>

MultiplexManager::MultiplexManager(ISteamNetworkingSockets* steamInterface, HSteamNetConnection steamConn,
                                   boost::asio::io_context& io_context, bool& isHost, int& localPort)
    : steamInterface_(steamInterface), steamConn_(steamConn), nextId_(1),
      io_context_(io_context), isHost_(isHost), localPort_(localPort) {}

MultiplexManager::~MultiplexManager() {
    // Close all sockets
    std::lock_guard<std::mutex> lock(mapMutex_);
    for (auto& pair : clientMap_) {
        pair.second->close();
    }
    clientMap_.clear();
}

uint32_t MultiplexManager::addClient(std::shared_ptr<tcp::socket> socket) {
    std::lock_guard<std::mutex> lock(mapMutex_);
    uint32_t id = nextId_++;
    clientMap_[id] = socket;
    return id;
}

void MultiplexManager::removeClient(uint32_t id) {
    std::lock_guard<std::mutex> lock(mapMutex_);
    auto it = clientMap_.find(id);
    if (it != clientMap_.end()) {
        it->second->close();
        clientMap_.erase(it);
    }
}

std::shared_ptr<tcp::socket> MultiplexManager::getClient(uint32_t id) {
    std::lock_guard<std::mutex> lock(mapMutex_);
    auto it = clientMap_.find(id);
    if (it != clientMap_.end()) {
        return it->second;
    }
    return nullptr;
}

void MultiplexManager::sendTunnelPacket(uint32_t id, const char* data, size_t len, int type) {
    // Packet format: uint32_t id, uint32_t type, then data if type==0
    size_t packetSize = sizeof(uint32_t) * 2 + (type == 0 ? len : 0);
    std::vector<char> packet(packetSize);
    uint32_t* pId = reinterpret_cast<uint32_t*>(&packet[0]);
    uint32_t* pType = reinterpret_cast<uint32_t*>(&packet[sizeof(uint32_t)]);
    *pId = id;
    *pType = type;
    if (type == 0 && data) {
        std::memcpy(&packet[sizeof(uint32_t) * 2], data, len);
    }
    steamInterface_->SendMessageToConnection(steamConn_, packet.data(), packet.size(), k_nSteamNetworkingSend_Reliable, nullptr);
}

void MultiplexManager::handleTunnelPacket(const char* data, size_t len) {
    if (len < sizeof(uint32_t) * 2) {
        std::cerr << "Invalid tunnel packet size" << std::endl;
        return;
    }
    uint32_t id = *reinterpret_cast<const uint32_t*>(data);
    uint32_t type = *reinterpret_cast<const uint32_t*>(data + sizeof(uint32_t));
    if (type == 0) {
        // Data packet
        size_t dataLen = len - sizeof(uint32_t) * 2;
        const char* packetData = data + sizeof(uint32_t) * 2;
        auto socket = getClient(id);
        if (!socket && isHost_ && localPort_ > 0) {
            // 如果是主持且没有对应的 TCP Client，创建一个连接到本地端口
            std::cout << "Creating new TCP client for id " << id << " connecting to localhost:" << localPort_ << std::endl;
            try {
                auto newSocket = std::make_shared<tcp::socket>(io_context_);
                tcp::resolver resolver(io_context_);
                auto endpoints = resolver.resolve("127.0.0.1", std::to_string(localPort_));
                boost::asio::connect(*newSocket, endpoints);
                
                std::lock_guard<std::mutex> lock(mapMutex_);
                clientMap_[id] = newSocket;
                socket = newSocket;
                std::cout << "Successfully created TCP client for id " << id << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Failed to create TCP client for id " << id << ": " << e.what() << std::endl;
                return;
            }
        }
        if (socket) {
            boost::asio::async_write(*socket, boost::asio::buffer(packetData, dataLen), [](const boost::system::error_code&, std::size_t) {});
        } else {
            std::cerr << "No client found for id " << id << std::endl;
        }
    } else if (type == 1) {
        // Disconnect packet
        removeClient(id);
        std::cout << "Client " << id << " disconnected" << std::endl;
    } else {
        std::cerr << "Unknown packet type " << type << std::endl;
    }
}