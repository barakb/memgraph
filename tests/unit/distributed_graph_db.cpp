#include <memory>
#include <thread>
#include <unordered_set>

#include "gtest/gtest.h"

#include "database/graph_db.hpp"
#include "distributed/coordination.hpp"
#include "distributed/coordination_master.hpp"
#include "distributed/coordination_worker.hpp"
#include "distributed/plan_consumer.hpp"
#include "distributed/plan_dispatcher.hpp"
#include "distributed/remote_data_rpc_clients.hpp"
#include "distributed/remote_data_rpc_server.hpp"
#include "distributed/remote_pull_rpc_clients.hpp"
#include "distributed_common.hpp"
#include "io/network/endpoint.hpp"
#include "query/frontend/ast/ast.hpp"
#include "query/frontend/ast/cypher_main_visitor.hpp"
#include "query/frontend/semantic/symbol_generator.hpp"
#include "query/frontend/semantic/symbol_table.hpp"
#include "query/interpreter.hpp"
#include "query/plan/planner.hpp"
#include "query/typed_value.hpp"
#include "query_common.hpp"
#include "query_plan_common.hpp"
#include "transactions/engine_master.hpp"

using namespace distributed;
using namespace database;

TEST_F(DistributedGraphDbTest, Coordination) {
  EXPECT_NE(master().endpoint().port(), 0);
  EXPECT_NE(worker(1).endpoint().port(), 0);
  EXPECT_NE(worker(2).endpoint().port(), 0);

  EXPECT_EQ(master().GetEndpoint(1), worker(1).endpoint());
  EXPECT_EQ(master().GetEndpoint(2), worker(2).endpoint());
  EXPECT_EQ(worker(1).GetEndpoint(0), master().endpoint());
  EXPECT_EQ(worker(1).GetEndpoint(2), worker(2).endpoint());
  EXPECT_EQ(worker(2).GetEndpoint(0), master().endpoint());
  EXPECT_EQ(worker(2).GetEndpoint(1), worker(1).endpoint());
}

TEST_F(DistributedGraphDbTest, TxEngine) {
  auto *tx1 = master_tx_engine().Begin();
  auto *tx2 = master_tx_engine().Begin();
  EXPECT_EQ(tx2->snapshot().size(), 1);
  EXPECT_EQ(
      worker(1).tx_engine().RunningTransaction(tx1->id_)->snapshot().size(), 0);
  EXPECT_EQ(worker(2).tx_engine().RunningTransaction(tx2->id_)->snapshot(),
            tx2->snapshot());

  ::testing::FLAGS_gtest_death_test_style = "threadsafe";
  EXPECT_DEATH(worker(2).tx_engine().RunningTransaction(123), "");
}

template <typename TType>
using mapper_vec =
    std::vector<std::reference_wrapper<storage::ConcurrentIdMapper<TType>>>;

TEST_F(DistributedGraphDbTest, StorageTypes) {
  auto test_mappers = [](auto mappers, auto ids) {
    for (size_t i = 0; i < mappers.size(); ++i) {
      ids.emplace_back(
          mappers[i].get().value_to_id("value" + std::to_string(i)));
    }
    EXPECT_GT(ids.size(), 0);
    for (size_t i = 0; i < mappers.size(); ++i) {
      for (size_t j = 0; j < ids.size(); ++j) {
        EXPECT_EQ(mappers[i].get().id_to_value(ids[j]),
                  "value" + std::to_string(j));
      }
    }
  };

  test_mappers(mapper_vec<storage::Label>{master().label_mapper(),
                                          worker(1).label_mapper(),
                                          worker(2).label_mapper()},
               std::vector<storage::Label>{});
  test_mappers(mapper_vec<storage::EdgeType>{master().edge_type_mapper(),
                                             worker(1).edge_type_mapper(),
                                             worker(2).edge_type_mapper()},
               std::vector<storage::EdgeType>{});
  test_mappers(mapper_vec<storage::Property>{master().property_mapper(),
                                             worker(1).property_mapper(),
                                             worker(2).property_mapper()},
               std::vector<storage::Property>{});
}

TEST_F(DistributedGraphDbTest, Counters) {
  EXPECT_EQ(master().counters().Get("a"), 0);
  EXPECT_EQ(worker(1).counters().Get("a"), 1);
  EXPECT_EQ(worker(2).counters().Get("a"), 2);

  EXPECT_EQ(worker(1).counters().Get("b"), 0);
  EXPECT_EQ(worker(2).counters().Get("b"), 1);
  EXPECT_EQ(master().counters().Get("b"), 2);
}

TEST_F(DistributedGraphDbTest, DispatchPlan) {
  auto kRPCWaitTime = 600ms;
  int64_t plan_id = 5;
  SymbolTable symbol_table;
  AstTreeStorage storage;

  auto scan_all = MakeScanAll(storage, symbol_table, "n");

  master().plan_dispatcher().DispatchPlan(plan_id, scan_all.op_, symbol_table);
  std::this_thread::sleep_for(kRPCWaitTime);

  auto check_for_worker = [plan_id, &symbol_table](auto &worker) {
    auto &cached = worker.plan_consumer().PlanForId(plan_id);
    EXPECT_NE(dynamic_cast<query::plan::ScanAll *>(cached.plan.get()), nullptr);
    EXPECT_EQ(cached.symbol_table.max_position(), symbol_table.max_position());
    EXPECT_EQ(cached.symbol_table.table(), symbol_table.table());
  };
  check_for_worker(worker(1));
  check_for_worker(worker(2));
}

TEST_F(DistributedGraphDbTest, BuildIndexDistributed) {
  storage::Label label;
  storage::Property property;

  {
    GraphDbAccessor dba0{master()};
    label = dba0.Label("label");
    property = dba0.Property("property");
    auto tx_id = dba0.transaction_id();

    GraphDbAccessor dba1{worker(1), tx_id};
    GraphDbAccessor dba2{worker(2), tx_id};
    auto add_vertex = [label, property](GraphDbAccessor &dba) {
      auto vertex = dba.InsertVertex();
      vertex.add_label(label);
      vertex.PropsSet(property, 1);
    };
    for (int i = 0; i < 100; ++i) add_vertex(dba0);
    for (int i = 0; i < 50; ++i) add_vertex(dba1);
    for (int i = 0; i < 300; ++i) add_vertex(dba2);
    dba0.Commit();
  }

  {
    GraphDbAccessor dba{master()};
    dba.BuildIndex(label, property);
    EXPECT_TRUE(dba.LabelPropertyIndexExists(label, property));
    EXPECT_EQ(CountIterable(dba.Vertices(label, property, false)), 100);
  }

  GraphDbAccessor dba_master{master()};

  {
    GraphDbAccessor dba{worker(1), dba_master.transaction_id()};
    EXPECT_TRUE(dba.LabelPropertyIndexExists(label, property));
    EXPECT_EQ(CountIterable(dba.Vertices(label, property, false)), 50);
  }

  {
    GraphDbAccessor dba{worker(2), dba_master.transaction_id()};
    EXPECT_TRUE(dba.LabelPropertyIndexExists(label, property));
    EXPECT_EQ(CountIterable(dba.Vertices(label, property, false)), 300);
  }
}

TEST_F(DistributedGraphDbTest, WorkerOwnedDbAccessors) {
  GraphDbAccessor dba_w1(worker(1));
  auto v = dba_w1.InsertVertex();
  auto prop = dba_w1.Property("p");
  v.PropsSet(prop, 42);
  auto v_ga = v.GlobalAddress();
  dba_w1.Commit();

  GraphDbAccessor dba_w2(worker(2));
  VertexAccessor v_in_w2{v_ga, dba_w2};
  EXPECT_EQ(v_in_w2.PropsAt(prop).Value<int64_t>(), 42);
}
