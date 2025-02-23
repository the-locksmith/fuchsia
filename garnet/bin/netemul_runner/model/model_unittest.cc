// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <iostream>
#include "config.h"
#include "gtest/gtest.h"

namespace netemul {
namespace config {
namespace testing {

class ModelTest : public ::testing::Test {
 protected:
  void ExpectFailedParse(const char* json, const char* msg) {
    Config config;
    json::JSONParser parser;
    auto doc = parser.ParseFromString(
        json, ::testing::UnitTest::GetInstance()->current_test_info()->name());

    ASSERT_FALSE(parser.HasError());

    EXPECT_FALSE(config.ParseFromJSON(doc, &parser)) << msg;
    ASSERT_TRUE(parser.HasError());
    std::cout << "Parse failed as expected: " << parser.error_str()
              << std::endl;
  }
};

TEST_F(ModelTest, ParseTest) {
  const char* json =
      R"(
    {
      "default_url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/default.cmx",
      "environment": {
        "children": [
          {
            "name": "child-1",
            "test": [
              {
                "arguments": [
                  "-t",
                  "1",
                  "-n",
                  "child-1-url"
                ],
                "url": "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/env_build_run.cmx"
              },
              {
                "arguments": [
                  "-t",
                  "1",
                  "-n",
                  "child-1-no-url"
                ]
              }
            ]
          },
          {
            "inherit_services": false,
            "name": "child-2",
            "test": [ "fuchsia-pkg://fuchsia.com/some_test#meta/some_test.cmx" ],
            "apps" : [ "fuchsia-pkg://fuchsia.com/some_app#meta/some_app.cmx" ]
          }
        ],
        "devices": [
          "ep0",
          "ep1"
        ],
        "name": "root",
        "setup": [
          {
            "url": "fuchsia-pkg://fuchsia.com/some_setup#meta/some_setup.cmx",
            "arguments": ["-arg"]
          }
        ],
        "services": {
          "fuchsia.net.SocketProvider": "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx",
          "fuchsia.netstack.Netstack": "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx",
          "fuchsia.some.Service" : {
            "url" : "fuchsia-pkg://fuchsia.com/some_service#meta/some_service.cmx",
            "arguments" : ["-a1", "-a2"]
          }
        }
      },
      "networks": [
        {
          "endpoints": [
            {
              "mac": "70:00:01:02:03:04",
              "mtu": 1000,
              "name": "ep0",
              "up": false
            },
            {
              "name": "ep1"
            }
          ],
          "name": "test-net"
        }
      ]
    }
)";

  config::Config config;
  json::JSONParser parser;
  auto doc = parser.ParseFromString(json, "ParseTest");
  EXPECT_FALSE(parser.HasError()) << "Parse error: " << parser.error_str();

  EXPECT_TRUE(config.ParseFromJSON(doc, &parser))
      << "Parse error: " << parser.error_str();

  EXPECT_EQ(config.default_url(),
            "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/default.cmx");
  EXPECT_EQ(config.disabled(), false);

  // sanity check the objects:
  auto& root_env = config.environment();
  EXPECT_EQ(root_env.name(), "root");
  EXPECT_EQ(root_env.inherit_services(), true);
  EXPECT_EQ(root_env.children().size(), 2ul);
  EXPECT_EQ(root_env.devices().size(), 2ul);
  EXPECT_EQ(root_env.services().size(), 3ul);
  EXPECT_TRUE(root_env.test().empty());
  EXPECT_TRUE(root_env.apps().empty());
  EXPECT_EQ(root_env.setup().size(), 1ul);

  // check the devices
  EXPECT_EQ(root_env.devices()[0], "ep0");
  EXPECT_EQ(root_env.devices()[1], "ep1");

  // check the services
  EXPECT_EQ(root_env.services()[0].name(), "fuchsia.net.SocketProvider");
  EXPECT_EQ(root_env.services()[0].launch().url(),
            "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx");
  EXPECT_TRUE(root_env.services()[0].launch().arguments().empty());
  EXPECT_EQ(root_env.services()[1].name(), "fuchsia.netstack.Netstack");
  EXPECT_EQ(root_env.services()[1].launch().url(),
            "fuchsia-pkg://fuchsia.com/netstack#meta/netstack.cmx");
  EXPECT_TRUE(root_env.services()[1].launch().arguments().empty());
  EXPECT_EQ(root_env.services()[2].name(), "fuchsia.some.Service");
  EXPECT_EQ(root_env.services()[2].launch().url(),
            "fuchsia-pkg://fuchsia.com/some_service#meta/some_service.cmx");
  EXPECT_EQ(root_env.services()[2].launch().arguments().size(), 2ul);

  // check the child environments
  auto& c0 = root_env.children()[0];
  EXPECT_EQ(c0.name(), "child-1");
  EXPECT_EQ(c0.inherit_services(), true);
  EXPECT_TRUE(c0.children().empty());
  EXPECT_TRUE(c0.devices().empty());
  EXPECT_TRUE(c0.services().empty());
  EXPECT_EQ(c0.test().size(), 2ul);
  EXPECT_TRUE(c0.apps().empty());
  EXPECT_TRUE(c0.setup().empty());
  auto& c1 = root_env.children()[1];
  EXPECT_EQ(c1.name(), "child-2");
  EXPECT_EQ(c1.inherit_services(), false);
  EXPECT_TRUE(c1.children().empty());
  EXPECT_TRUE(c1.devices().empty());
  EXPECT_TRUE(c1.services().empty());
  EXPECT_TRUE(c1.setup().empty());
  EXPECT_EQ(c1.test().size(), 1ul);
  EXPECT_EQ(c1.apps().size(), 1ul);

  // check test structures:
  auto& t0 = c0.test()[0];
  auto& t1 = c0.test()[1];
  auto& t2 = c1.test()[0];
  EXPECT_EQ(
      t0.url(),
      "fuchsia-pkg://fuchsia.com/netemul_sandbox_test#meta/env_build_run.cmx");
  EXPECT_EQ(t0.arguments().size(), 4ul);

  EXPECT_TRUE(t1.url().empty());
  EXPECT_EQ(t1.arguments().size(), 4ul);
  EXPECT_EQ(t2.url(), "fuchsia-pkg://fuchsia.com/some_test#meta/some_test.cmx");
  EXPECT_TRUE(t2.arguments().empty());

  // check apps:
  auto& app0 = c1.apps()[0];
  EXPECT_EQ(app0.url(), "fuchsia-pkg://fuchsia.com/some_app#meta/some_app.cmx");
  EXPECT_TRUE(app0.arguments().empty());

  // check setup:
  auto& setup = root_env.setup()[0];
  EXPECT_EQ(setup.url(),
            "fuchsia-pkg://fuchsia.com/some_setup#meta/some_setup.cmx");
  EXPECT_EQ(setup.arguments().size(), 1ul);
  EXPECT_EQ(setup.arguments()[0], "-arg");

  // check network object:
  EXPECT_EQ(config.networks().size(), 1ul);
  auto& net = config.networks()[0];
  EXPECT_EQ(net.name(), "test-net");
  EXPECT_EQ(net.endpoints().size(), 2ul);

  // check endpoints:
  auto& ep0 = net.endpoints()[0];
  auto& ep1 = net.endpoints()[1];
  EXPECT_EQ(ep0.name(), "ep0");
  EXPECT_EQ(ep0.mtu(), 1000u);
  EXPECT_TRUE(ep0.mac());
  const uint8_t mac_cmp[] = {0x70, 0x00, 0x01, 0x02, 0x03, 0x04};
  EXPECT_EQ(memcmp(mac_cmp, ep0.mac()->d, sizeof(mac_cmp)), 0);
  EXPECT_EQ(ep0.up(), false);

  EXPECT_EQ(ep1.name(), "ep1");
  EXPECT_EQ(ep1.mtu(), 1500u);  // default mtu check
  EXPECT_FALSE(ep1.mac());      // mac not set
  EXPECT_EQ(ep1.up(), true);    // default up
}

TEST_F(ModelTest, NetworkNoName) {
  const char* json = R"({"networks":[{}]})";
  ExpectFailedParse(json, "network without name accepted");

  const char* json2 = R"({"networks":[{"name":""}]})";
  ExpectFailedParse(json2, "network with empty name accepted");
};

TEST_F(ModelTest, EndpointNoName) {
  const char* json = R"({"networks":[{"name":"net","endpoints":[{}]}]})";
  ExpectFailedParse(json, "endpoint without name accepted");

  const char* json2 =
      R"({"networks":[{"name":"net","endpoints":[{"name":""}]}]})";
  ExpectFailedParse(json2, "endpoint with empty name accepted");
};

TEST_F(ModelTest, EndpointBadMtu) {
  const char* json =
      R"({"networks":[{"name":"net","endpoints":[{"name":"a","mtu":0}]}]})";
  ExpectFailedParse(json, "endpoint without 0 mtu accepted");
}

TEST_F(ModelTest, EndpointBadMac) {
  const char* json =
      R"({"networks":[{"name":"net","endpoints":[{"name":"a","mac":"xx:xx:xx"}]}]})";
  ExpectFailedParse(json, "endpoint with invalid mac accepted");
}

TEST_F(ModelTest, TestBadUrl) {
  const char* json = R"({"environment":{"test":[{"url":"blablabla"}]}})";
  ExpectFailedParse(json, "test with bad url accepted");
}

TEST_F(ModelTest, ServiceBadUrl) {
  const char* json =
      R"({"environment":{"services":{"some.service":"blablabla"}}})";
  ExpectFailedParse(json, "service with bad url accepted");
}

TEST_F(ModelTest, LaunchAppGetOrDefault) {
  const char* json1 =
      R"({"url":"fuchsia-pkg://fuchsia.com/some_url#meta/some_url.cmx"})";
  json::JSONParser parser;
  auto doc1 = parser.ParseFromString(json1, "LaunchApGetOrDefault");
  config::LaunchApp app1;

  EXPECT_FALSE(parser.HasError()) << "Parse error: " << parser.error_str();
  EXPECT_TRUE(app1.ParseFromJSON(doc1, &parser))
      << "Parse error: " << parser.error_str();

  const char* json2 = R"({"url":""})";
  auto doc2 = parser.ParseFromString(json2, "LaunchApGetOrDefault");

  config::LaunchApp app2;
  EXPECT_FALSE(parser.HasError()) << "Parse error: " << parser.error_str();
  EXPECT_TRUE(app2.ParseFromJSON(doc2, &parser))
      << "Parse error: " << parser.error_str();

  const char* fallback = "fuchsia-pkg://fuchsia.com/fallback#meta/fallback.cmx";
  EXPECT_EQ(app1.GetUrlOrDefault(fallback),
            "fuchsia-pkg://fuchsia.com/some_url#meta/some_url.cmx");
  EXPECT_EQ(app2.GetUrlOrDefault(fallback), fallback);
}

}  // namespace testing
}  // namespace config
}  // namespace netemul
