#include <algorithm>
#include <climits>
#include <string>
#include <unordered_map>
#include <vector>

#include "antlr4-runtime.h"
#include "dbms/dbms.hpp"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "query/context.hpp"
#include "query/frontend/ast/cypher_main_visitor.hpp"
#include "query/frontend/opencypher/parser.hpp"

namespace {

using namespace query;
using namespace query::frontend;
using testing::UnorderedElementsAre;
using testing::Pair;

class AstGenerator {
 public:
  AstGenerator(const std::string &query)
      : dbms_(),
        db_accessor_(dbms_.active()),
        context_(Config{}, *db_accessor_),
        query_string_(query),
        parser_(query),
        visitor_(context_),
        query_([&]() {
          visitor_.visit(parser_.tree());
          return visitor_.query();
        }()) {}

  Dbms dbms_;
  std::unique_ptr<GraphDbAccessor> db_accessor_;
  Context context_;
  std::string query_string_;
  ::frontend::opencypher::Parser parser_;
  CypherMainVisitor visitor_;
  Query *query_;
};

TEST(CypherMainVisitorTest, SyntaxException) {
  ASSERT_THROW(AstGenerator("CREATE ()-[*1...2]-()"), std::exception);
}

TEST(CypherMainVisitorTest, PropertyLookup) {
  AstGenerator ast_generator("RETURN n.x");
  auto *query = ast_generator.query_;
  ASSERT_EQ(query->clauses_.size(), 1U);
  auto *return_clause = dynamic_cast<Return *>(query->clauses_[0]);
  auto *property_lookup = dynamic_cast<PropertyLookup *>(
      return_clause->named_expressions_[0]->expression_);
  ASSERT_TRUE(property_lookup->expression_);
  auto identifier = dynamic_cast<Identifier *>(property_lookup->expression_);
  ASSERT_TRUE(identifier);
  ASSERT_EQ(identifier->name_, "n");
  ASSERT_EQ(property_lookup->property_,
            ast_generator.db_accessor_->property("x"));
}

TEST(CypherMainVisitorTest, ReturnNamedIdentifier) {
  AstGenerator ast_generator("RETURN var AS var5");
  auto *query = ast_generator.query_;
  auto *return_clause = dynamic_cast<Return *>(query->clauses_[0]);
  auto *named_expr = return_clause->named_expressions_[0];
  ASSERT_EQ(named_expr->name_, "var5");
  auto *identifier = dynamic_cast<Identifier *>(named_expr->expression_);
  ASSERT_EQ(identifier->name_, "var");
}

TEST(CypherMainVisitorTest, IntegerLiteral) {
  AstGenerator ast_generator("RETURN 42");
  auto *query = ast_generator.query_;
  auto *return_clause = dynamic_cast<Return *>(query->clauses_[0]);
  auto *literal = dynamic_cast<Literal *>(
      return_clause->named_expressions_[0]->expression_);
  ASSERT_TRUE(literal);
  ASSERT_EQ(literal->value_.Value<int64_t>(), 42);
}

TEST(CypherMainVisitorTest, IntegerLiteralTooLarge) {
  ASSERT_THROW(AstGenerator("RETURN 10000000000000000000000000"),
               std::exception);
}

TEST(CypherMainVisitorTest, BooleanLiteralTrue) {
  AstGenerator ast_generator("RETURN TrUe");
  auto *query = ast_generator.query_;
  auto *return_clause = dynamic_cast<Return *>(query->clauses_[0]);
  auto *literal = dynamic_cast<Literal *>(
      return_clause->named_expressions_[0]->expression_);
  ASSERT_TRUE(literal);
  ASSERT_EQ(literal->value_.Value<bool>(), true);
}

TEST(CypherMainVisitorTest, BooleanLiteralFalse) {
  AstGenerator ast_generator("RETURN faLSE");
  auto *query = ast_generator.query_;
  auto *return_clause = dynamic_cast<Return *>(query->clauses_[0]);
  auto *literal = dynamic_cast<Literal *>(
      return_clause->named_expressions_[0]->expression_);
  ASSERT_TRUE(literal);
  ASSERT_EQ(literal->value_.Value<bool>(), false);
}

TEST(CypherMainVisitorTest, NullLiteral) {
  AstGenerator ast_generator("RETURN nULl");
  auto *query = ast_generator.query_;
  auto *return_clause = dynamic_cast<Return *>(query->clauses_[0]);
  auto *literal = dynamic_cast<Literal *>(
      return_clause->named_expressions_[0]->expression_);
  ASSERT_TRUE(literal);
  ASSERT_EQ(literal->value_.type(), TypedValue::Type::Null);
}

TEST(CypherMainVisitorTest, StringLiteralDoubleQuotes) {
  AstGenerator ast_generator("RETURN \"mi'rko\"");
  auto *query = ast_generator.query_;
  auto *return_clause = dynamic_cast<Return *>(query->clauses_[0]);
  auto *literal = dynamic_cast<Literal *>(
      return_clause->named_expressions_[0]->expression_);
  ASSERT_TRUE(literal);
  ASSERT_EQ(literal->value_.Value<std::string>(), "mi'rko");
}

TEST(CypherMainVisitorTest, StringLiteralSingleQuotes) {
  AstGenerator ast_generator("RETURN 'mi\"rko'");
  auto *query = ast_generator.query_;
  auto *return_clause = dynamic_cast<Return *>(query->clauses_[0]);
  auto *literal = dynamic_cast<Literal *>(
      return_clause->named_expressions_[0]->expression_);
  ASSERT_TRUE(literal);
  ASSERT_EQ(literal->value_.Value<std::string>(), "mi\"rko");
}

TEST(CypherMainVisitorTest, StringLiteralEscapedChars) {
  AstGenerator ast_generator(
      "RETURN '\\\\\\'\\\"\\b\\B\\f\\F\\n\\N\\r\\R\\t\\T'");
  auto *query = ast_generator.query_;
  auto *return_clause = dynamic_cast<Return *>(query->clauses_[0]);
  auto *literal = dynamic_cast<Literal *>(
      return_clause->named_expressions_[0]->expression_);
  ASSERT_TRUE(literal);
  ASSERT_EQ(literal->value_.Value<std::string>(), "\\'\"\b\b\f\f\n\n\r\r\t\t");
}

TEST(CypherMainVisitorTest, StringLiteralEscapedUtf16) {
  AstGenerator ast_generator("RETURN '\\u221daaa\\U221daaa'");
  auto *query = ast_generator.query_;
  auto *return_clause = dynamic_cast<Return *>(query->clauses_[0]);
  auto *literal = dynamic_cast<Literal *>(
      return_clause->named_expressions_[0]->expression_);
  ASSERT_TRUE(literal);
  ASSERT_EQ(literal->value_.Value<std::string>(), u8"\u221daaa\u221daaa");
}

TEST(CypherMainVisitorTest, StringLiteralEscapedUtf32) {
  AstGenerator ast_generator("RETURN '\\u0001F600aaaa\\U0001F600aaaaaaaa'");
  auto *query = ast_generator.query_;
  auto *return_clause = dynamic_cast<Return *>(query->clauses_[0]);
  auto *literal = dynamic_cast<Literal *>(
      return_clause->named_expressions_[0]->expression_);
  ASSERT_TRUE(literal);
  ASSERT_EQ(literal->value_.Value<std::string>(),
            u8"\U0001F600aaaa\U0001F600aaaaaaaa");
}

TEST(CypherMainVisitorTest, DoubleLiteral) {
  AstGenerator ast_generator("RETURN 3.5");
  auto *query = ast_generator.query_;
  auto *return_clause = dynamic_cast<Return *>(query->clauses_[0]);
  auto *literal = dynamic_cast<Literal *>(
      return_clause->named_expressions_[0]->expression_);
  ASSERT_TRUE(literal);
  ASSERT_EQ(literal->value_.Value<double>(), 3.5);
}

TEST(CypherMainVisitorTest, DoubleLiteralExponent) {
  AstGenerator ast_generator("RETURN 5e-1");
  auto *query = ast_generator.query_;
  auto *return_clause = dynamic_cast<Return *>(query->clauses_[0]);
  auto *literal = dynamic_cast<Literal *>(
      return_clause->named_expressions_[0]->expression_);
  ASSERT_TRUE(literal);
  ASSERT_EQ(literal->value_.Value<double>(), 0.5);
}

TEST(CypherMainVisitorTest, NodePattern) {
  AstGenerator ast_generator("MATCH (:label1:label2:label3 {a : 5, b : 10})");
  auto *query = ast_generator.query_;
  ASSERT_EQ(query->clauses_.size(), 1U);
  auto *match = dynamic_cast<Match *>(query->clauses_[0]);
  ASSERT_TRUE(match);
  ASSERT_EQ(match->patterns_.size(), 1U);
  ASSERT_TRUE(match->patterns_[0]);
  ASSERT_EQ(match->patterns_[0]->atoms_.size(), 1U);
  auto node = dynamic_cast<NodeAtom *>(match->patterns_[0]->atoms_[0]);
  ASSERT_TRUE(node);
  ASSERT_TRUE(node->identifier_);
  ASSERT_EQ(node->identifier_->name_,
            CypherMainVisitor::kAnonPrefix + std::to_string(1));
  ASSERT_THAT(node->labels_, UnorderedElementsAre(
                                 ast_generator.db_accessor_->label("label1"),
                                 ast_generator.db_accessor_->label("label2"),
                                 ast_generator.db_accessor_->label("label3")));
  std::unordered_map<GraphDb::Property, int64_t> properties;
  for (auto x : node->properties_) {
    auto *literal = dynamic_cast<Literal *>(x.second);
    ASSERT_TRUE(literal);
    ASSERT_TRUE(literal->value_.type() == TypedValue::Type::Int);
    properties[x.first] = literal->value_.Value<int64_t>();
  }
  ASSERT_THAT(properties,
              UnorderedElementsAre(
                  Pair(ast_generator.db_accessor_->property("a"), 5),
                  Pair(ast_generator.db_accessor_->property("b"), 10)));
}

TEST(CypherMainVisitorTest, NodePatternIdentifier) {
  AstGenerator ast_generator("MATCH (var)");
  auto *query = ast_generator.query_;
  auto *match = dynamic_cast<Match *>(query->clauses_[0]);
  auto node = dynamic_cast<NodeAtom *>(match->patterns_[0]->atoms_[0]);
  ASSERT_TRUE(node->identifier_);
  ASSERT_EQ(node->identifier_->name_, "var");
  ASSERT_THAT(node->labels_, UnorderedElementsAre());
  ASSERT_THAT(node->properties_, UnorderedElementsAre());
}

TEST(CypherMainVisitorTest, RelationshipPatternNoDetails) {
  AstGenerator ast_generator("MATCH ()--()");
  auto *query = ast_generator.query_;
  auto *match = dynamic_cast<Match *>(query->clauses_[0]);
  ASSERT_EQ(match->patterns_.size(), 1U);
  ASSERT_TRUE(match->patterns_[0]);
  ASSERT_EQ(match->patterns_[0]->atoms_.size(), 3U);
  auto *node1 = dynamic_cast<NodeAtom *>(match->patterns_[0]->atoms_[0]);
  ASSERT_TRUE(node1);
  auto *edge = dynamic_cast<EdgeAtom *>(match->patterns_[0]->atoms_[1]);
  ASSERT_TRUE(edge);
  auto *node2 = dynamic_cast<NodeAtom *>(match->patterns_[0]->atoms_[2]);
  ASSERT_TRUE(node2);
  ASSERT_EQ(edge->direction_, EdgeAtom::Direction::BOTH);
  ASSERT_TRUE(edge->identifier_);
  ASSERT_THAT(edge->identifier_->name_,
              CypherMainVisitor::kAnonPrefix + std::to_string(2));
}

TEST(CypherMainVisitorTest, RelationshipPatternDetails) {
  AstGenerator ast_generator("MATCH ()<-[:type1|type2 {a : 5, b : 10}]-()");
  auto *query = ast_generator.query_;
  auto *match = dynamic_cast<Match *>(query->clauses_[0]);
  auto *edge = dynamic_cast<EdgeAtom *>(match->patterns_[0]->atoms_[1]);
  ASSERT_EQ(edge->direction_, EdgeAtom::Direction::LEFT);
  ASSERT_THAT(
      edge->edge_types_,
      UnorderedElementsAre(ast_generator.db_accessor_->edge_type("type1"),
                           ast_generator.db_accessor_->edge_type("type2")));
  std::unordered_map<GraphDb::Property, int64_t> properties;
  for (auto x : edge->properties_) {
    auto *literal = dynamic_cast<Literal *>(x.second);
    ASSERT_TRUE(literal);
    ASSERT_TRUE(literal->value_.type() == TypedValue::Type::Int);
    properties[x.first] = literal->value_.Value<int64_t>();
  }
  ASSERT_THAT(properties,
              UnorderedElementsAre(
                  Pair(ast_generator.db_accessor_->property("a"), 5),
                  Pair(ast_generator.db_accessor_->property("b"), 10)));
}

TEST(CypherMainVisitorTest, RelationshipPatternVariable) {
  AstGenerator ast_generator("MATCH ()-[var]->()");
  auto *query = ast_generator.query_;
  auto *match = dynamic_cast<Match *>(query->clauses_[0]);
  auto *edge = dynamic_cast<EdgeAtom *>(match->patterns_[0]->atoms_[1]);
  ASSERT_EQ(edge->direction_, EdgeAtom::Direction::RIGHT);
  ASSERT_TRUE(edge->identifier_);
  ASSERT_THAT(edge->identifier_->name_, "var");
}

// // Relationship with unbounded variable range.
// TEST(CypherMainVisitorTest, RelationshipPatternUnbounded) {
//   ParserTables parser("CREATE ()-[*]-()");
//   ASSERT_EQ(parser.identifiers_map_.size(), 0U);
//   ASSERT_EQ(parser.relationships_.size(), 1U);
//   CompareRelationships(*parser.relationships_.begin(),
//                        Relationship::Direction::BOTH, {}, {}, true, 1,
//                        LLONG_MAX);
// }
//
// // Relationship with lower bounded variable range.
// TEST(CypherMainVisitorTest, RelationshipPatternLowerBounded) {
//   ParserTables parser("CREATE ()-[*5..]-()");
//   ASSERT_EQ(parser.identifiers_map_.size(), 0U);
//   ASSERT_EQ(parser.relationships_.size(), 1U);
//   CompareRelationships(*parser.relationships_.begin(),
//                        Relationship::Direction::BOTH, {}, {}, true, 5,
//                        LLONG_MAX);
// }
//
// // Relationship with upper bounded variable range.
// TEST(CypherMainVisitorTest, RelationshipPatternUpperBounded) {
//   ParserTables parser("CREATE ()-[*..10]-()");
//   ASSERT_EQ(parser.identifiers_map_.size(), 0U);
//   ASSERT_EQ(parser.relationships_.size(), 1U);
//   CompareRelationships(*parser.relationships_.begin(),
//                        Relationship::Direction::BOTH, {}, {}, true, 1, 10);
// }
//
// // Relationship with lower and upper bounded variable range.
// TEST(CypherMainVisitorTest, RelationshipPatternLowerUpperBounded) {
//   ParserTables parser("CREATE ()-[*5..10]-()");
//   ASSERT_EQ(parser.identifiers_map_.size(), 0U);
//   ASSERT_EQ(parser.relationships_.size(), 1U);
//   CompareRelationships(*parser.relationships_.begin(),
//                        Relationship::Direction::BOTH, {}, {}, true, 5, 10);
// }
//
// // Relationship with fixed number of edges.
// TEST(CypherMainVisitorTest, RelationshipPatternFixedRange) {
//   ParserTables parser("CREATE ()-[*10]-()");
//   ASSERT_EQ(parser.identifiers_map_.size(), 0U);
//   ASSERT_EQ(parser.relationships_.size(), 1U);
//   CompareRelationships(*parser.relationships_.begin(),
//                        Relationship::Direction::BOTH, {}, {}, true, 10, 10);
// }
//
// // Relationship with invalid bound (larger than long long).
// TEST(CypherMainVisitorTest, RelationshipPatternInvalidBound) {
//   ASSERT_THROW(
//       ParserTables parser("CREATE ()-[*100000000000000000000000000]-()"),
//       SemanticException);
// }
//
// // PatternPart.
// TEST(CypherMainVisitorTest, PatternPart) {
//   ParserTables parser("CREATE ()--()");
//   ASSERT_EQ(parser.identifiers_map_.size(), 0U);
//   ASSERT_EQ(parser.pattern_parts_.size(), 1U);
//   ASSERT_EQ(parser.relationships_.size(), 1U);
//   ASSERT_EQ(parser.nodes_.size(), 2U);
//   ASSERT_EQ(parser.pattern_parts_.begin()->second.nodes.size(), 2U);
//   ASSERT_EQ(parser.pattern_parts_.begin()->second.relationships.size(), 1U);
// }
//
// // PatternPart in braces.
// TEST(CypherMainVisitorTest, PatternPartBraces) {
//   ParserTables parser("CREATE ((()--()))");
//   ASSERT_EQ(parser.identifiers_map_.size(), 0U);
//   ASSERT_EQ(parser.pattern_parts_.size(), 1U);
//   ASSERT_EQ(parser.relationships_.size(), 1U);
//   ASSERT_EQ(parser.nodes_.size(), 2U);
//   ASSERT_EQ(parser.pattern_parts_.begin()->second.nodes.size(), 2U);
//   ASSERT_EQ(parser.pattern_parts_.begin()->second.relationships.size(), 1U);
// }
//
// // PatternPart with variable.
// TEST(CypherMainVisitorTest, PatternPartVariable) {
//   ParserTables parser("CREATE var=()--()");
//   ASSERT_EQ(parser.identifiers_map_.size(), 1U);
//   ASSERT_EQ(parser.pattern_parts_.size(), 1U);
//   ASSERT_EQ(parser.relationships_.size(), 1U);
//   ASSERT_EQ(parser.nodes_.size(), 2U);
//   ASSERT_EQ(parser.pattern_parts_.begin()->second.nodes.size(), 2U);
//   ASSERT_EQ(parser.pattern_parts_.begin()->second.relationships.size(), 1U);
//   ASSERT_NE(parser.identifiers_map_.find("var"),
//   parser.identifiers_map_.end());
//   auto output_identifier = parser.identifiers_map_["var"];
//   ASSERT_NE(parser.pattern_parts_.find(output_identifier),
//             parser.pattern_parts_.end());
// }
//
// // Multiple nodes with same variable and properties.
// TEST(CypherMainVisitorTest, MultipleNodesWithVariableAndProperties) {
//   ASSERT_THROW(ParserTables parser("CREATE (a {b: 5})-[]-(a {c: 5})"),
//                SemanticException);
// }

TEST(CypherMainVisitorTest, ReturnUnanemdIdentifier) {
  AstGenerator ast_generator("RETURN var");
  auto *query = ast_generator.query_;
  ASSERT_EQ(query->clauses_.size(), 1U);
  auto *return_clause = dynamic_cast<Return *>(query->clauses_[0]);
  ASSERT_TRUE(return_clause);
  ASSERT_EQ(return_clause->named_expressions_.size(), 1U);
  auto *named_expr = return_clause->named_expressions_[0];
  ASSERT_TRUE(named_expr);
  ASSERT_EQ(named_expr->name_, "var");
  auto *identifier = dynamic_cast<Identifier *>(named_expr->expression_);
  ASSERT_TRUE(identifier);
  ASSERT_EQ(identifier->name_, "var");
}

TEST(CypherMainVisitorTest, Create) {
  AstGenerator ast_generator("CREATE (n)");
  auto *query = ast_generator.query_;
  ASSERT_EQ(query->clauses_.size(), 1U);
  auto *create = dynamic_cast<Create *>(query->clauses_[0]);
  ASSERT_TRUE(create);
  ASSERT_EQ(create->patterns_.size(), 1U);
  ASSERT_TRUE(create->patterns_[0]);
  ASSERT_EQ(create->patterns_[0]->atoms_.size(), 1U);
  auto node = dynamic_cast<NodeAtom *>(create->patterns_[0]->atoms_[0]);
  ASSERT_TRUE(node);
  ASSERT_TRUE(node->identifier_);
  ASSERT_EQ(node->identifier_->name_, "n");
}
}
