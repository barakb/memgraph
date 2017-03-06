#include <iostream>

#include "gtest/gtest.h"

#include "io/network/network_endpoint.hpp"
#include "io/network/network_error.hpp"

using endpoint_t = io::network::NetworkEndpoint;
using exception_t = io::network::NetworkEndpointException;

TEST(NetworkEndpoint, IPv4) {
  endpoint_t endpoint;

  // test first constructor
  endpoint = endpoint_t("127.0.0.1", "12345");
  EXPECT_STREQ(endpoint.address(), "127.0.0.1");
  EXPECT_STREQ(endpoint.port_str(), "12345");
  EXPECT_EQ(endpoint.port(), 12345);
  EXPECT_EQ(endpoint.family(), 4);

  // test second constructor
  std::string addr("127.0.0.2"), port("12346");
  endpoint = endpoint_t(addr, port);
  EXPECT_STREQ(endpoint.address(), "127.0.0.2");
  EXPECT_STREQ(endpoint.port_str(), "12346");
  EXPECT_EQ(endpoint.port(), 12346);
  EXPECT_EQ(endpoint.family(), 4);

  // test third constructor
  endpoint = endpoint_t("127.0.0.1", 12347);
  EXPECT_STREQ(endpoint.address(), "127.0.0.1");
  EXPECT_STREQ(endpoint.port_str(), "12347");
  EXPECT_EQ(endpoint.port(), 12347);
  EXPECT_EQ(endpoint.family(), 4);

  // test address null
  EXPECT_THROW(endpoint_t(nullptr, nullptr), exception_t);

  // test address invalid
  EXPECT_THROW(endpoint_t("invalid", "12345"), exception_t);

  // test port invalid
  EXPECT_THROW(endpoint_t("127.0.0.1", "invalid"), exception_t);
}

TEST(NetworkEndpoint, IPv6) {
  endpoint_t endpoint;

  // test first constructor
  endpoint = endpoint_t("ab:cd:ef::1", "12345");
  EXPECT_STREQ(endpoint.address(), "ab:cd:ef::1");
  EXPECT_STREQ(endpoint.port_str(), "12345");
  EXPECT_EQ(endpoint.port(), 12345);
  EXPECT_EQ(endpoint.family(), 6);

  // test second constructor
  std::string addr("ab:cd:ef::2"), port("12346");
  endpoint = endpoint_t(addr, port);
  EXPECT_STREQ(endpoint.address(), "ab:cd:ef::2");
  EXPECT_STREQ(endpoint.port_str(), "12346");
  EXPECT_EQ(endpoint.port(), 12346);
  EXPECT_EQ(endpoint.family(), 6);

  // test third constructor
  endpoint = endpoint_t("ab:cd:ef::3", 12347);
  EXPECT_STREQ(endpoint.address(), "ab:cd:ef::3");
  EXPECT_STREQ(endpoint.port_str(), "12347");
  EXPECT_EQ(endpoint.port(), 12347);
  EXPECT_EQ(endpoint.family(), 6);

  // test address null
  EXPECT_THROW(endpoint_t(nullptr, nullptr), exception_t);

  // test address invalid
  EXPECT_THROW(endpoint_t("::g", "12345"), exception_t);

  // test port invalid
  EXPECT_THROW(endpoint_t("::1", "invalid"), exception_t);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
