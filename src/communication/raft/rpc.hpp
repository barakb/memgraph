#pragma once

#include <unordered_map>

#include "glog/logging.h"

#include "communication/messaging/distributed.hpp"
#include "communication/raft/network_common.hpp"
#include "communication/raft/raft.hpp"
#include "communication/rpc/rpc.hpp"
#include "io/network/network_endpoint.hpp"

/* Implementation of `RaftNetworkInterface` using RPC. Raft RPC requests and
 * responses are wrapped in `PeerRpcRequest` and `PeerRpcReply`. */

// TODO(mtomic): Unwrap RPCs and use separate request-response protocols instead
// of `PeerProtocol`, or at least use an union to avoid sending unnecessary data
// over the wire.

namespace communication::raft {

const char *kRaftChannelName = "raft-peer-rpc-channel";

template <class State>
using PeerProtocol =
    communication::rpc::RequestResponse<PeerRpcRequest<State>, PeerRpcReply>;

template <class State>
class RpcNetwork : public RaftNetworkInterface<State> {
 public:
  RpcNetwork(
      communication::messaging::System &system,
      std::unordered_map<std::string, io::network::NetworkEndpoint> directory)
      : system_(system),
        directory_(std::move(directory)),
        server_(system, kRaftChannelName) {}

  ~RpcNetwork() {
    DCHECK(!is_running_)
        << "`Shutdown()` should be called before destructing `RpcNetwork`";
    /* We don't want to call `Shutdown` here, instead we push that
     * responsibility to caller of `Start`, so `server_` doesn't end up holding
     * a reference to a destructed `RaftMember`. */
  }

  virtual void Start(RaftMember<State> &member) override {
    server_.Register<PeerProtocol<State>>([&member](
        const PeerRpcRequest<State> &request) {
      auto reply = std::make_unique<PeerRpcReply>();
      reply->type = request.type;
      switch (request.type) {
        case RpcType::REQUEST_VOTE:
          reply->request_vote = member.OnRequestVote(request.request_vote);
          break;
        case RpcType::APPEND_ENTRIES:
          reply->append_entries =
              member.OnAppendEntries(request.append_entries);
          break;
        default:
          LOG(ERROR) << "Unknown RPC type: " << static_cast<int>(request.type);
      }
      return reply;
    });
    server_.Start();
  }

  virtual bool SendRequestVote(const MemberId &recipient,
                               const RequestVoteRequest &request,
                               RequestVoteReply &reply,
                               std::chrono::milliseconds timeout) override {
    PeerRpcRequest<State> req;
    PeerRpcReply rep;

    req.type = RpcType::REQUEST_VOTE;
    req.request_vote = request;

    if (!SendRpc(recipient, req, rep, timeout)) {
      return false;
    }

    reply = rep.request_vote;
    return true;
  }

  virtual bool SendAppendEntries(const MemberId &recipient,
                                 const AppendEntriesRequest<State> &request,
                                 AppendEntriesReply &reply,
                                 std::chrono::milliseconds timeout) override {
    PeerRpcRequest<State> req;
    PeerRpcReply rep;

    req.type = RpcType::APPEND_ENTRIES;
    req.append_entries = request;

    if (!SendRpc(recipient, req, rep, timeout)) {
      return false;
    }

    reply = rep.append_entries;
    return true;
  }

 private:
  bool SendRpc(const MemberId &recipient, const PeerRpcRequest<State> &request,
               PeerRpcReply &reply, std::chrono::milliseconds timeout) {
    auto &client = GetClient(recipient);
    auto response = client.template Call<PeerProtocol<State>>(timeout, request);

    if (!response) {
      return false;
    }

    reply = *response;
    return true;
  }

  rpc::Client &GetClient(const MemberId &id) {
    auto it = clients_.find(id);
    if (it == clients_.end()) {
      auto ne = directory_[id];
      it = clients_
               .try_emplace(id, system_, ne.address(), ne.port(),
                            kRaftChannelName)
               .first;
    }
    return it->second;
  }

  virtual void Shutdown() override {
    is_running_ = false;
    server_.Shutdown();
  }

  communication::messaging::System &system_;
  // TODO(mtomic): how to update and distribute this?
  std::unordered_map<MemberId, io::network::NetworkEndpoint> directory_;
  rpc::Server server_;

  std::unordered_map<MemberId, communication::rpc::Client> clients_;

  bool is_running_ = true;
};

}  // namespace communication::raft
