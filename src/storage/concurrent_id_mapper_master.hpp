#pragma once

#include <experimental/optional>

#include "communication/rpc/server.hpp"
#include "data_structures/concurrent/concurrent_map.hpp"
#include "storage/concurrent_id_mapper_single_node.hpp"

namespace storage {

/** Master implementation of ConcurrentIdMapper. */
template <typename TId>
class MasterConcurrentIdMapper : public SingleNodeConcurrentIdMapper<TId> {
 public:
  explicit MasterConcurrentIdMapper(communication::rpc::System &system);

 private:
  communication::rpc::Server rpc_server_;
};
}  // namespace storage
