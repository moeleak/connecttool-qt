#include "tcp_server.h"
#include "../steam/steam_networking_manager.h"
#include <iostream>
#include <algorithm>

TCPServer::TCPServer(int port, SteamNetworkingManager* manager) : port_(port), running_(false), acceptor_(io_context_), work_(boost::asio::make_work_guard(io_context_)), hasAcceptedConnection_(false), manager_(manager), forwarding_(false) {}

TCPServer::~TCPServer() { stop(); }

bool TCPServer::start() {
    try {
        hasAcceptedConnection_ = false;
        tcp::endpoint endpoint(tcp::v4(), port_);
        acceptor_.open(endpoint.protocol());
        acceptor_.set_option(tcp::acceptor::reuse_address(true));
        acceptor_.bind(endpoint);
        acceptor_.listen();

        running_ = true;
        serverThread_ = std::thread([this]() { 
            std::cout << "Server thread started" << std::endl;
            io_context_.run(); 
            std::cout << "Server thread stopped" << std::endl;
        });
        start_accept();
        std::cout << "TCP server started on port " << port_ << std::endl;
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to start TCP server: " << e.what() << std::endl;
        return false;
    }
}

void TCPServer::stop() {
    running_ = false;
    hasAcceptedConnection_ = false;
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

void TCPServer::start_accept() {
    auto socket = std::make_shared<tcp::socket>(io_context_);
    acceptor_.async_accept(*socket, [this, socket](const boost::system::error_code& error) {
        if (!error) {
            std::cout << "New client connected" << std::endl;
            {
                std::lock_guard<std::mutex> lock(clientsMutex_);
                clients_.push_back(socket);
            }
            hasAcceptedConnection_ = true;
            start_read(socket);
        }
        if (running_ && !hasAcceptedConnection_) {
            start_accept();
        }
    });
}

void TCPServer::start_read(std::shared_ptr<tcp::socket> socket) {
    auto buffer = std::make_shared<std::vector<char>>(1024);
    socket->async_read_some(boost::asio::buffer(*buffer), [this, socket, buffer](const boost::system::error_code& error, std::size_t bytes_transferred) {
        if (!error) {
            std::cout << "Received " << bytes_transferred << " bytes from TCP client" << std::endl;
            if (!forwarding_) {
                forwarding_ = true;
                if (manager_->isConnected()) {
                    std::cout << "Forwarding TCP message to Steam connection" << std::endl;
                    manager_->getInterface()->SendMessageToConnection(manager_->getConnection(), buffer->data(), bytes_transferred, k_nSteamNetworkingSend_Reliable, nullptr);
                } else {
                    std::cout << "Not connected to Steam, skipping forward" << std::endl;
                }
                forwarding_ = false;
            }
            sendToAll(buffer->data(), bytes_transferred, socket);
            start_read(socket);
        } else {
            std::cout << "TCP client disconnected or error: " << error.message() << std::endl;
            // Remove client
            std::lock_guard<std::mutex> lock(clientsMutex_);
            clients_.erase(std::remove(clients_.begin(), clients_.end(), socket), clients_.end());
            // Reset to allow new connection
            hasAcceptedConnection_ = false;
            if (running_) {
                start_accept();
            }
        }
    });
}