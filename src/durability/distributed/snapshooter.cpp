#include "durability/distributed/snapshooter.hpp"

#include <algorithm>

#include <glog/logging.h>

#include "database/graph_db_accessor.hpp"
#include "durability/distributed/snapshot_encoder.hpp"
#include "durability/distributed/version.hpp"
#include "durability/hashed_file_writer.hpp"
#include "durability/paths.hpp"
#include "utils/file.hpp"

namespace fs = std::experimental::filesystem;

namespace durability {

// Snapshot layout is described in durability/version.hpp
static_assert(durability::kVersion == 6,
              "Wrong snapshot version, please update!");

namespace {
bool Encode(const fs::path &snapshot_file, database::GraphDb &db,
            database::GraphDbAccessor &dba, int worker_id) {
  try {
    HashedFileWriter buffer(snapshot_file);
    SnapshotEncoder<HashedFileWriter> encoder(buffer);
    int64_t vertex_num = 0, edge_num = 0;

    encoder.WriteRAW(durability::kSnapshotMagic.data(),
                     durability::kSnapshotMagic.size());
    encoder.WriteInt(durability::kVersion);

    // Writes the worker id to snapshot, used to guarantee consistent cluster
    // state after recovery
    encoder.WriteInt(worker_id);

    // Write the number of generated vertex and edges, used to recover
    // generators internal states
    encoder.WriteInt(db.storage().VertexGenerator().LocalCount());
    encoder.WriteInt(db.storage().EdgeGenerator().LocalCount());

    // Write the ID of the transaction doing the snapshot.
    encoder.WriteInt(dba.transaction_id());

    // Write the transaction snapshot into the snapshot. It's used when
    // recovering from the combination of snapshot and write-ahead-log.
    {
      std::vector<communication::bolt::Value> tx_snapshot;
      for (int64_t tx : dba.transaction().snapshot())
        tx_snapshot.emplace_back(tx);
      encoder.WriteList(tx_snapshot);
    }

    // Write label+property indexes as list ["label", "property", ...]
    {
      std::vector<communication::bolt::Value> index_vec;
      for (const auto &key : dba.GetIndicesKeys()) {
        index_vec.emplace_back(dba.LabelName(key.label_));
        index_vec.emplace_back(dba.PropertyName(key.property_));
      }
      encoder.WriteList(index_vec);
    }

    for (const auto &vertex : dba.Vertices(false)) {
      encoder.WriteSnapshotVertex(vertex);
      vertex_num++;
    }
    for (const auto &edge : dba.Edges(false)) {
      encoder.WriteEdge(glue::ToBoltEdge(edge));
      encoder.WriteInt(edge.CypherId());
      edge_num++;
    }
    buffer.WriteValue(vertex_num);
    buffer.WriteValue(edge_num);
    buffer.WriteValue(buffer.hash());
    buffer.Close();
  } catch (const std::ifstream::failure &) {
    if (fs::exists(snapshot_file) && !fs::remove(snapshot_file)) {
      LOG(ERROR) << "Error while removing corrupted snapshot file: "
                 << snapshot_file;
    }
    return false;
  }
  return true;
}

// Removes snapshot files so that only `max_retained` latest ones are kept. If
// `max_retained == -1`, all the snapshots are retained.
void RemoveOldSnapshots(const fs::path &snapshot_dir, int max_retained) {
  if (max_retained == -1) return;
  std::vector<fs::path> files;
  for (auto &file : fs::directory_iterator(snapshot_dir))
    files.push_back(file.path());
  if (static_cast<int>(files.size()) <= max_retained) return;
  sort(files.begin(), files.end());
  for (int i = 0; i < static_cast<int>(files.size()) - max_retained; ++i) {
    if (!fs::remove(files[i])) {
      LOG(ERROR) << "Error while removing file: " << files[i];
    }
  }
}

// Removes write-ahead log files that are no longer necessary (they don't get
// used when recovering from the latest snapshot.
void RemoveOldWals(const fs::path &wal_dir,
                   const tx::Transaction &snapshot_transaction) {
  if (!fs::exists(wal_dir)) return;
  // We can remove all the WAL files that will not be used when restoring from
  // the snapshot created in the given transaction.
  auto min_trans_id = snapshot_transaction.snapshot().empty()
                          ? snapshot_transaction.id_ + 1
                          : snapshot_transaction.snapshot().front();
  for (auto &wal_file : fs::directory_iterator(wal_dir)) {
    auto tx_id = TransactionIdFromWalFilename(wal_file.path().filename());
    if (tx_id && tx_id.value() < min_trans_id) {
      bool result = fs::remove(wal_file);
      DCHECK(result) << "Unable to delete old wal file: " << wal_file;
    }
  }
}
}  // namespace

bool MakeSnapshot(database::GraphDb &db, database::GraphDbAccessor &dba,
                  int worker_id, const fs::path &durability_dir,
                  int snapshot_max_retained) {
  if (!utils::EnsureDir(durability_dir / kSnapshotDir)) return false;
  const auto snapshot_file =
      MakeSnapshotPath(durability_dir, worker_id, dba.transaction_id());
  if (fs::exists(snapshot_file)) return false;
  if (Encode(snapshot_file, db, dba, worker_id)) {
    RemoveOldSnapshots(durability_dir / kSnapshotDir, snapshot_max_retained);
    RemoveOldWals(durability_dir / kWalDir, dba.transaction());
    return true;
  } else {
    std::error_code error_code;  // Just for exception suppression.
    fs::remove(snapshot_file, error_code);
    return false;
  }
}

}  // namespace durability