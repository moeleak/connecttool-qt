#include "tcp_server.h"
#include "../steam/steam_networking_manager.h"
#include <algorithm>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <vector>

struct TCPServer::ClientRegistry {
  void add(std::shared_ptr<tcp::socket> socket) {
    int count = 0;
    {
      std::lock_guard lock(clientsMutex);
      sockets.push_back(std::move(socket));
      count = static_cast<int>(sockets.size());
    }
    publish(count);
  }

  void remove(const std::weak_ptr<tcp::socket> &socket) {
    const auto closed = socket.lock();
    int count = 0;
    {
      std::lock_guard lock(clientsMutex);
      std::erase(sockets, closed);
      count = static_cast<int>(sockets.size());
    }
    publish(count);
  }

  void clear() {
    std::vector<std::shared_ptr<tcp::socket>> closing;
    {
      std::lock_guard lock(clientsMutex);
      closing.swap(sockets);
    }
    for (const auto &socket : closing) {
      boost::system::error_code ignored;
      socket->shutdown(tcp::socket::shutdown_both, ignored);
      socket->close(ignored);
    }
    publish(0);
  }

  [[nodiscard]] int count() const {
    std::lock_guard lock(clientsMutex);
    return static_cast<int>(sockets.size());
  }

  void setCallback(std::function<void(int)> next) {
    std::lock_guard lock(callbackMutex);
    callback = std::move(next);
  }

private:
  void publish(int count) {
    // Clearing the callback is also a synchronization barrier during
    // teardown: no callback can retain the owning Backend afterwards.
    std::lock_guard lock(callbackMutex);
    if (callback) {
      callback(count);
    }
  }

  mutable std::mutex clientsMutex;
  std::vector<std::shared_ptr<tcp::socket>> sockets;
  std::mutex callbackMutex;
  std::function<void(int)> callback;
};

TCPServer::TCPServer(int port, SteamNetworkingManager *manager)
    : port_(port), running_(false), work_(boost::asio::make_work_guard(io_context_)),
      acceptor_(io_context_), clients_(std::make_shared<ClientRegistry>()), manager_(manager) {}

TCPServer::~TCPServer() { stop(); }

bool TCPServer::start() {
  try {
    // This endpoint is consumed by software on the same machine. Keeping it on
    // loopback avoids exposing the tunnel to the LAN and needs no firewall rule.
    const tcp::endpoint endpoint(boost::asio::ip::address_v4::loopback(), port_);
    acceptor_.open(endpoint.protocol());
    acceptor_.set_option(tcp::acceptor::reuse_address(true));
    acceptor_.bind(endpoint);
    acceptor_.listen();

    running_ = true;
    serverThread_ = std::jthread([this](std::stop_token) {
      std::cout << "Server thread started" << std::endl;
      io_context_.run();
      std::cout << "Server thread stopped" << std::endl;
    });
    start_accept();
    std::cout << "TCP server started on port " << port_ << std::endl;
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Failed to start TCP server: " << e.what() << std::endl;
    return false;
  }
}

void TCPServer::stop() {
  if (!running_.exchange(false)) {
    return;
  }
  boost::system::error_code closeError;
  acceptor_.cancel(closeError);
  acceptor_.close(closeError);
  io_context_.stop();
  if (serverThread_.joinable()) {
    serverThread_.request_stop();
    serverThread_.join();
  }
  clients_->clear();
  clients_->setCallback({});
}

int TCPServer::getClientCount() const { return clients_->count(); }

void TCPServer::setClientCountCallback(std::function<void(int)> callback) {
  clients_->setCallback(std::move(callback));
}

void TCPServer::start_accept() {
  auto socket = std::make_shared<tcp::socket>(io_context_);
  acceptor_.async_accept(*socket, [this, socket](const boost::system::error_code &error) {
    if (!error) {
      std::cout << "New client connected" << std::endl;
      // Low latency between local TCP and Steam tunnel
      boost::system::error_code ec;
      socket->set_option(tcp::no_delay(true), ec);
      const auto registry = clients_;
      const std::weak_ptr<tcp::socket> weakSocket = socket;
      bool registered = false;
      try {
        SteamMessageHandler *handler = manager_ ? manager_->getMessageHandler() : nullptr;
        if (!handler) {
          throw std::runtime_error("Steam message handler is unavailable");
        }
        const auto multiplexManager = handler->getMultiplexManager(manager_->getConnection());
        registry->add(socket);
        registered = true;
        const std::string id = multiplexManager->addClient(
            socket, [registry, weakSocket]() { registry->remove(weakSocket); });
        std::cout << "TCP client assigned tunnel id " << id << std::endl;
      } catch (const std::exception &exception) {
        if (registered) {
          registry->remove(weakSocket);
        }
        socket->close(ec);
        std::cerr << "Failed to register TCP client: " << exception.what() << std::endl;
      }
    }
    if (running_) {
      start_accept();
    }
  });
}
