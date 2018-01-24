#pragma once

#include <atomic>
#include <experimental/optional>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include <fmt/format.h>
#include <glog/logging.h>

#include "communication/worker.hpp"
#include "io/network/socket.hpp"
#include "io/network/socket_event_dispatcher.hpp"

namespace communication {

/**
 * TODO (mferencevic): document methods
 */

/**
 * Communication server.
 * Listens for incomming connections on the server port and assigns them in a
 * round-robin manner to it's workers. Started automatically on constructor, and
 * stopped at destructor.
 *
 * Current Server achitecture:
 * incomming connection -> server -> worker -> session
 *
 * @tparam TSession the server can handle different Sessions, each session
 *         represents a different protocol so the same network infrastructure
 *         can be used for handling different protocols
 * @tparam TSessionData the class with objects that will be forwarded to the
 *         session
 */
template <typename TSession, typename TSessionData>
class Server {
 public:
  using WorkerT = Worker<TSession, TSessionData>;
  using Socket = io::network::Socket;

  /**
   * Constructs and binds server to endpoint, operates on session data and
   * invokes n workers
   */
  Server(const io::network::Endpoint &endpoint, TSessionData &session_data,
         size_t n)
      : session_data_(session_data) {
    // Without server we can't continue with application so we can just
    // terminate here.
    if (!socket_.Bind(endpoint)) {
      LOG(FATAL) << "Cannot bind to socket on " << endpoint.address() << " at "
                 << endpoint.port();
    }
    socket_.SetNonBlocking();
    if (!socket_.Listen(1024)) {
      LOG(FATAL) << "Cannot listen on socket!";
    }
    working_thread_ = std::thread([this, n]() {
      std::cout << fmt::format("Starting {} workers", n) << std::endl;
      workers_.reserve(n);
      for (size_t i = 0; i < n; ++i) {
        workers_.push_back(std::make_unique<WorkerT>(session_data_));
        worker_threads_.emplace_back(
            [this](WorkerT &worker) -> void { worker.Start(alive_); },
            std::ref(*workers_.back()));
      }
      std::cout << "Server is fully armed and operational" << std::endl;
      std::cout << fmt::format("Listening on {} at {}",
                               socket_.endpoint().address(),
                               socket_.endpoint().port())
                << std::endl;

      io::network::SocketEventDispatcher<ConnectionAcceptor> dispatcher;
      ConnectionAcceptor acceptor(socket_, *this);
      dispatcher.AddListener(socket_.fd(), acceptor, EPOLLIN);
      while (alive_) {
        dispatcher.WaitAndProcessEvents();
      }

      std::cout << "Shutting down..." << std::endl;
      for (auto &worker_thread : worker_threads_) {
        worker_thread.join();
      }
    });
  }

  ~Server() {
    Shutdown();
    AwaitShutdown();
  }

  const auto &endpoint() const { return socket_.endpoint(); }

  /// Stops server manually
  void Shutdown() {
    // This should be as simple as possible, so that it can be called inside a
    // signal handler.
    alive_.store(false);
  }

  /// Waits for the server to be signaled to shutdown
  void AwaitShutdown() {
    if (working_thread_.joinable()) working_thread_.join();
  }

 private:
  class ConnectionAcceptor {
   public:
    ConnectionAcceptor(Socket &socket, Server<TSession, TSessionData> &server)
        : socket_(socket), server_(server) {}

    void OnData() {
      DCHECK(server_.idx_ < server_.workers_.size()) << "Invalid worker id.";
      DLOG(INFO) << "On connect";
      auto connection = AcceptConnection();
      if (!connection) {
        // Connection is not available anymore or configuration failed.
        return;
      }
      server_.workers_[server_.idx_]->AddConnection(std::move(*connection));
      server_.idx_ = (server_.idx_ + 1) % server_.workers_.size();
    }

    void OnClose() { socket_.Close(); }

    void OnException(const std::exception &e) {
      LOG(FATAL) << "Exception was thrown while processing event on socket "
                 << socket_.fd() << " with message: " << e.what();
    }

    void OnError() { LOG(FATAL) << "Error on server side occured in epoll"; }

   private:
    // Accepts connection on socket_ and configures new connections. If done
    // successfuly new socket (connection) is returner, nullopt otherwise.
    std::experimental::optional<Socket> AcceptConnection() {
      DLOG(INFO) << "Accept new connection on socket: " << socket_.fd();

      // Accept a connection from a socket.
      auto s = socket_.Accept();
      if (!s) return std::experimental::nullopt;

      DLOG(INFO) << fmt::format(
          "Accepted a connection: socket {}, address '{}', family {}, port {}",
          s->fd(), s->endpoint().address(), s->endpoint().family(),
          s->endpoint().port());

      s->SetTimeout(1, 0);
      s->SetKeepAlive();
      s->SetNoDelay();
      return s;
    }

    Socket &socket_;
    Server<TSession, TSessionData> &server_;
  };

  std::vector<std::unique_ptr<WorkerT>> workers_;
  std::vector<std::thread> worker_threads_;
  std::thread working_thread_;
  std::atomic<bool> alive_{true};
  int idx_{0};

  Socket socket_;
  TSessionData &session_data_;
};

}  // namespace communication
