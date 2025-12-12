#include "tcp_server.h"
#include "../steam/steam_networking_manager.h"
#include <algorithm>
#include "logging.h"
#include <sstream>

TCPServer::TCPServer(int port, SteamNetworkingManager* manager) : port_(port), running_(false), acceptor_(io_context_), work_(boost::asio::make_work_guard(io_context_)), manager_(manager) {}

TCPServer::~TCPServer() { stop(); }

bool TCPServer::start() {
    try {
        tcp::endpoint endpoint(tcp::v4(), port_);
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();

        running_ = true;
        serverThread_ = std::thread([this]() { 
            ConnectToolLogging::logNet("Server thread started");
            io_context_.run(); 
            ConnectToolLogging::logNet("Server thread stopped");
        });
        start_accept();
        {
            std::ostringstream oss;
            oss << "TCP server started on port " << port_;
            ConnectToolLogging::logNet(oss.str());
        }
        return true;
    } catch (const std::exception& e) {
        std::ostringstream oss;
        oss << "Failed to start TCP server: " << e.what();
        ConnectToolLogging::logNet(oss.str());
        return false;
    }
}

void TCPServer::stop() {
    running_ = false;
    io_context_.stop();
    if (serverThread_.joinable()) {
        serverThread_.join();
    }
    acceptor_.close();
}

void TCPServer::sendToAll(const std::string& message, std::shared_ptr<tcp::socket> excludeSocket) {
    sendToAll(message.c_str(), message.size(), excludeSocket);
}

void TCPServer::sendToAll(const char* data, size_t size, std::shared_ptr<tcp::socket> excludeSocket) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    for (auto& client : clients_) {
        if (client != excludeSocket) {
            boost::asio::async_write(*client, boost::asio::buffer(data, size), [](const boost::system::error_code&, std::size_t) {});
        }
    }
}

int TCPServer::getClientCount() {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    return clients_.size();
}

void TCPServer::setClientCountCallback(std::function<void(int)> callback) {
    clientCountCallback_ = std::move(callback);
}

void TCPServer::notifyClientCount(int count) {
    if (clientCountCallback_) {
        clientCountCallback_(count);
    }
}

void TCPServer::start_accept() {
    auto socket = std::make_shared<tcp::socket>(io_context_);
    acceptor_.async_accept(*socket, [this, socket](const boost::system::error_code& error) {
        if (!error) {
            ConnectToolLogging::logNet("New client connected");
            // Low latency between local TCP and Steam tunnel
            boost::system::error_code ec;
            socket->set_option(tcp::no_delay(true), ec);
            auto multiplexManager = manager_->getMessageHandler()->getMultiplexManager(manager_->getConnection());
            std::string id = multiplexManager->addClient(socket);
            int currentCount = 0;
            {
                std::lock_guard<std::mutex> lock(clientsMutex_);
                clients_.push_back(socket);
                currentCount = static_cast<int>(clients_.size());
            }
            notifyClientCount(currentCount);
            start_read(socket, id);
        }
        if (running_) {
            start_accept();
        }
    });
}

void TCPServer::start_read(std::shared_ptr<tcp::socket> socket, std::string id) {
    auto buffer = std::make_shared<std::vector<char>>(1048576);
    socket->async_read_some(boost::asio::buffer(*buffer), [this, socket, buffer, id](const boost::system::error_code& error, std::size_t bytes_transferred) {
        if (!error) {
            if (manager_->isConnected()) {
                auto multiplexManager = manager_->getMessageHandler()->getMultiplexManager(manager_->getConnection());
                multiplexManager->sendTunnelPacket(id, buffer->data(), bytes_transferred, 0);
            } else {
                ConnectToolLogging::logNet("Not connected to Steam, skipping forward");
            }
            sendToAll(buffer->data(), bytes_transferred, socket);
            start_read(socket, id);
        } else {
            std::ostringstream oss;
            oss << "TCP client " << id << " disconnected or error: " << error.message();
            ConnectToolLogging::logNet(oss.str());
            // Send disconnect packet
            if (manager_->isConnected()) {
                auto multiplexManager = manager_->getMessageHandler()->getMultiplexManager(manager_->getConnection());
                multiplexManager->sendTunnelPacket(id, nullptr, 0, 1);
                // Remove client
                multiplexManager->removeClient(id);
            }
            int currentCount = 0;
            {
                std::lock_guard<std::mutex> lock(clientsMutex_);
                clients_.erase(std::remove(clients_.begin(), clients_.end(), socket), clients_.end());
                currentCount = static_cast<int>(clients_.size());
            }
            notifyClientCount(currentCount);
        }
    });
}
