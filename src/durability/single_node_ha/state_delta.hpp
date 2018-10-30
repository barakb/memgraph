// -*- buffer-read-only: t; -*-
// vim: readonly
// DO NOT EDIT! Generated using LCP from 'state_delta.lcp'

#pragma once

#include "communication/bolt/v1/decoder/decoder.hpp"
#include "communication/bolt/v1/encoder/base_encoder.hpp"
#include "durability/hashed_file_reader.hpp"
#include "durability/hashed_file_writer.hpp"
#include "mvcc/single_node_ha/version_list.hpp"
#include "storage/common/property_value.hpp"
#include "storage/common/types.hpp"
#include "storage/single_node_ha/gid.hpp"

class Vertex;
class Edge;

namespace database {

class GraphDbAccessor;

/// Describes single change to the database state. Used for durability (WAL) and
/// state communication over network in HA and for distributed remote storage
/// changes.
///
/// Labels, Properties and EdgeTypes are stored both as values (integers) and
/// strings (their names). The values are used when applying deltas in a running
/// database. Names are used when recovering the database as it's not guaranteed
/// that after recovery the old name<->value mapping will be preserved.
///
/// TODO: ensure the mapping is preserved after recovery and don't save strings
/// in StateDeltas.
struct StateDelta {
  /// Defines StateDelta type. For each type the comment indicates which values
  /// need to be stored. All deltas have the transaction_id member, so that's
  /// omitted in the comment.
  enum class Type {
    TRANSACTION_BEGIN,
    TRANSACTION_COMMIT,
    TRANSACTION_ABORT,
    CREATE_VERTEX,
    CREATE_EDGE,
    SET_PROPERTY_VERTEX,
    SET_PROPERTY_EDGE,
    ADD_LABEL,
    REMOVE_LABEL,
    REMOVE_VERTEX,
    REMOVE_EDGE,
    BUILD_INDEX,
    DROP_INDEX
  };

  StateDelta() = default;
  StateDelta(const enum Type &type, tx::TransactionId tx_id)
      : type(type), transaction_id(tx_id) {}

  /** Attempts to decode a StateDelta from the given decoder. Returns the
   * decoded value if successful, otherwise returns nullopt. */
  static std::experimental::optional<StateDelta> Decode(
      HashedFileReader &reader,
      communication::bolt::Decoder<HashedFileReader> &decoder);

  /** Encodes the delta using primitive encoder, and writes out the new hash
   * with delta to the writer */
  void Encode(
      HashedFileWriter &writer,
      communication::bolt::BaseEncoder<HashedFileWriter> &encoder) const;

  static StateDelta TxBegin(tx::TransactionId tx_id);
  static StateDelta TxCommit(tx::TransactionId tx_id);
  static StateDelta TxAbort(tx::TransactionId tx_id);
  static StateDelta CreateVertex(tx::TransactionId tx_id, gid::Gid vertex_id);
  static StateDelta CreateEdge(tx::TransactionId tx_id, gid::Gid edge_id,
                               gid::Gid vertex_from_id, gid::Gid vertex_to_id,
                               storage::EdgeType edge_type,
                               const std::string &edge_type_name);
  static StateDelta PropsSetVertex(tx::TransactionId tx_id, gid::Gid vertex_id,
                                   storage::Property property,
                                   const std::string &property_name,
                                   const PropertyValue &value);
  static StateDelta PropsSetEdge(tx::TransactionId tx_id, gid::Gid edge_id,
                                 storage::Property property,
                                 const std::string &property_name,
                                 const PropertyValue &value);
  static StateDelta AddLabel(tx::TransactionId tx_id, gid::Gid vertex_id,
                             storage::Label label,
                             const std::string &label_name);
  static StateDelta RemoveLabel(tx::TransactionId tx_id, gid::Gid vertex_id,
                                storage::Label label,
                                const std::string &label_name);
  static StateDelta RemoveVertex(tx::TransactionId tx_id, gid::Gid vertex_id,
                                 bool check_empty);
  static StateDelta RemoveEdge(tx::TransactionId tx_id, gid::Gid edge_id);
  static StateDelta BuildIndex(tx::TransactionId tx_id, storage::Label label,
                               const std::string &label_name,
                               storage::Property property,
                               const std::string &property_name, bool unique);
  static StateDelta DropIndex(tx::TransactionId tx_id, storage::Label label,
                              const std::string &label_name,
                              storage::Property property,
                              const std::string &property_name);

  /// Applies CRUD delta to database accessor. Fails on other types of deltas
  void Apply(GraphDbAccessor &dba) const;

  Type type;
  tx::TransactionId transaction_id;
  gid::Gid vertex_id;
  gid::Gid edge_id;
  mvcc::VersionList<Edge> *edge_address;
  gid::Gid vertex_from_id;
  mvcc::VersionList<Vertex> *vertex_from_address;
  gid::Gid vertex_to_id;
  mvcc::VersionList<Vertex> *vertex_to_address;
  storage::EdgeType edge_type;
  std::string edge_type_name;
  storage::Property property;
  std::string property_name;
  PropertyValue value{PropertyValue::Null};
  storage::Label label;
  std::string label_name;
  bool check_empty;
  bool unique;
};

}  // namespace database

// Cap'n Proto serialization declarations