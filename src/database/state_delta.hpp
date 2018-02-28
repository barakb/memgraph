#pragma once

#include "communication/bolt/v1/decoder/decoder.hpp"
#include "communication/bolt/v1/encoder/primitive_encoder.hpp"
#include "durability/hashed_file_reader.hpp"
#include "durability/hashed_file_writer.hpp"
#include "storage/address_types.hpp"
#include "storage/gid.hpp"
#include "storage/property_value.hpp"
#include "utils/serialization.hpp"

namespace database {
/** Describes single change to the database state. Used for durability (WAL) and
 * state communication over network in HA and for distributed remote storage
 * changes.
 *
 * Labels, Properties and EdgeTypes are stored both as values (integers) and
 * strings (their names). The values are used when applying deltas in a running
 * database. Names are used when recovering the database as it's not guaranteed
 * that after recovery the old name<->value mapping will be preserved.
 *
 * TODO: ensure the mapping is preserved after recovery and don't save strings
 * in StateDeltas. */
struct StateDelta {
  /** Defines StateDelta type. For each type the comment indicates which values
   * need to be stored. All deltas have the transaction_id member, so that's
   * omitted in the comment. */
  enum class Type {
    TRANSACTION_BEGIN,
    TRANSACTION_COMMIT,
    TRANSACTION_ABORT,
    CREATE_VERTEX,    // vertex_id
    CREATE_EDGE,      // edge_id, from_vertex_id, to_vertex_id, edge_type,
                      // edge_type_name
    ADD_OUT_EDGE,     // vertex_id, edge_address, vertex_to_address, edge_type
    REMOVE_OUT_EDGE,  // vertex_id, edge_address
    ADD_IN_EDGE,      // vertex_id, edge_address, vertex_from_address, edge_type
    REMOVE_IN_EDGE,   // vertex_id, edge_address
    SET_PROPERTY_VERTEX,  // vertex_id, property, property_name, property_value
    SET_PROPERTY_EDGE,    // edge_id, property, property_name, property_value
    // remove property is done by setting a PropertyValue::Null
    ADD_LABEL,      // vertex_id, label, label_name
    REMOVE_LABEL,   // vertex_id, label, label_name
    REMOVE_VERTEX,  // vertex_id
    REMOVE_EDGE,    // edge_id
    BUILD_INDEX     // label, label_name, property, property_name
  };

  StateDelta() = default;
  StateDelta(const enum Type &type, tx::transaction_id_t tx_id)
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
      communication::bolt::PrimitiveEncoder<HashedFileWriter> &encoder) const;

  static StateDelta TxBegin(tx::transaction_id_t tx_id);
  static StateDelta TxCommit(tx::transaction_id_t tx_id);
  static StateDelta TxAbort(tx::transaction_id_t tx_id);
  static StateDelta CreateVertex(tx::transaction_id_t tx_id,
                                 gid::Gid vertex_id);
  static StateDelta CreateEdge(tx::transaction_id_t tx_id, gid::Gid edge_id,
                               gid::Gid vertex_from_id, gid::Gid vertex_to_id,
                               storage::EdgeType edge_type,
                               const std::string &edge_type_name);
  static StateDelta AddOutEdge(tx::transaction_id_t tx_id, gid::Gid vertex_id,
                               storage::VertexAddress vertex_to_address,
                               storage::EdgeAddress edge_address,
                               storage::EdgeType edge_type);
  static StateDelta RemoveOutEdge(tx::transaction_id_t tx_id,
                                  gid::Gid vertex_id,
                                  storage::EdgeAddress edge_address);
  static StateDelta AddInEdge(tx::transaction_id_t tx_id, gid::Gid vertex_id,
                              storage::VertexAddress vertex_from_address,
                              storage::EdgeAddress edge_address,
                              storage::EdgeType edge_type);
  static StateDelta RemoveInEdge(tx::transaction_id_t tx_id, gid::Gid vertex_id,
                                 storage::EdgeAddress edge_address);
  static StateDelta PropsSetVertex(tx::transaction_id_t tx_id,
                                   gid::Gid vertex_id,
                                   storage::Property property,
                                   const std::string &property_name,
                                   const PropertyValue &value);
  static StateDelta PropsSetEdge(tx::transaction_id_t tx_id, gid::Gid edge_id,
                                 storage::Property property,
                                 const std::string &property_name,
                                 const PropertyValue &value);
  static StateDelta AddLabel(tx::transaction_id_t tx_id, gid::Gid vertex_id,
                             storage::Label label,
                             const std::string &label_name);
  static StateDelta RemoveLabel(tx::transaction_id_t tx_id, gid::Gid vertex_id,
                                storage::Label label,
                                const std::string &label_name);
  static StateDelta RemoveVertex(tx::transaction_id_t tx_id,
                                 gid::Gid vertex_id);
  static StateDelta RemoveEdge(tx::transaction_id_t tx_id, gid::Gid edge_id);
  static StateDelta BuildIndex(tx::transaction_id_t tx_id, storage::Label label,
                               const std::string &label_name,
                               storage::Property property,
                               const std::string &property_name);

  /// Applies CRUD delta to database accessor. Fails on other types of deltas
  void Apply(GraphDbAccessor &dba) const;

  // Members valid for every delta.
  enum Type type;
  tx::transaction_id_t transaction_id;

  // Members valid only for some deltas, see StateDelta::Type comments above.
  // TODO: when preparing the WAL for distributed, most likely remove Gids and
  // only keep addresses.
  gid::Gid vertex_id;
  gid::Gid edge_id;
  storage::EdgeAddress edge_address;
  gid::Gid vertex_from_id;
  storage::VertexAddress vertex_from_address;
  gid::Gid vertex_to_id;
  storage::VertexAddress vertex_to_address;
  storage::EdgeType edge_type;
  std::string edge_type_name;
  storage::Property property;
  std::string property_name;
  PropertyValue value = PropertyValue::Null;
  storage::Label label;
  std::string label_name;

 private:
  friend class boost::serialization::access;
  BOOST_SERIALIZATION_SPLIT_MEMBER();
  template <class TArchive>
  void save(TArchive &ar, const unsigned int) const {
    ar &type;
    ar &transaction_id;
    ar &vertex_id;
    ar &edge_id;
    ar &edge_address;
    ar &vertex_from_id;
    ar &vertex_from_address;
    ar &vertex_to_id;
    ar &vertex_to_address;
    ar &edge_type;
    ar &edge_type_name;
    ar &property;
    ar &property_name;
    utils::SaveTypedValue(ar, value);
    ar &label;
    ar &label_name;
  }

  template <class TArchive>
  void load(TArchive &ar, const unsigned int) {
    ar &type;
    ar &transaction_id;
    ar &vertex_id;
    ar &edge_id;
    ar &edge_address;
    ar &vertex_from_id;
    ar &vertex_from_address;
    ar &vertex_to_id;
    ar &vertex_to_address;
    ar &edge_type;
    ar &edge_type_name;
    ar &property;
    ar &property_name;
    query::TypedValue tv;
    utils::LoadTypedValue(ar, tv);
    value = tv;
    ar &label;
    ar &label_name;
  }
};
}  // namespace database
