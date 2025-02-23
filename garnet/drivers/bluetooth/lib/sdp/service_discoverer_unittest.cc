// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/drivers/bluetooth/lib/sdp/service_discoverer.h"

#include <lib/async/default.h>

#include "gtest/gtest.h"

#include "garnet/drivers/bluetooth/lib/common/test_helpers.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel.h"
#include "garnet/drivers/bluetooth/lib/l2cap/fake_channel_test.h"

namespace btlib {
namespace sdp {
namespace {

using TestingBase = ::gtest::TestLoopFixture;

using common::CreateStaticByteBuffer;

class FakeClient : public Client {
 public:
  // |destroyed_cb| will be called when this client is destroyed, with true if
  // there are outstanding expected requests.
  FakeClient(fit::closure destroyed_cb)
      : destroyed_cb_(std::move(destroyed_cb)) {}

  virtual ~FakeClient() override { destroyed_cb_(); }

  virtual void ServiceSearchAttributes(
      std::unordered_set<common::UUID> search_pattern,
      const std::unordered_set<AttributeId> &req_attributes,
      SearchResultCallback result_cb,
      async_dispatcher_t *cb_dispatcher) override {
    if (!service_search_attributes_cb_) {
      FAIL() << "ServiceSearchAttributes with no callback set";
    }

    service_search_attributes_cb_(std::move(search_pattern),
                                  std::move(req_attributes),
                                  std::move(result_cb), cb_dispatcher);
  }

  using ServiceSearchAttributesCalllback = fit::function<void(
      std::unordered_set<common::UUID>, std::unordered_set<AttributeId>,
      SearchResultCallback, async_dispatcher_t *)>;
  void SetServiceSearchAttributesCallback(
      ServiceSearchAttributesCalllback callback) {
    service_search_attributes_cb_ = std::move(callback);
  }

 private:
  ServiceSearchAttributesCalllback service_search_attributes_cb_;
  fit::closure destroyed_cb_;
};

class SDP_ServiceDiscovererTest : public TestingBase {
 public:
  SDP_ServiceDiscovererTest() = default;
  ~SDP_ServiceDiscovererTest() = default;

 protected:
  void SetUp() override {
    clients_created_ = 0;
    clients_destroyed_ = 0;
  }

  void TearDown() override {}

  // Connect an SDP client to a fake channel, which is available in channel_
  std::unique_ptr<FakeClient> GetFakeClient() {
    SCOPED_TRACE("Connect Client");
    clients_created_++;
    return std::make_unique<FakeClient>([this]() { clients_destroyed_++; });
  }

  size_t clients_created() const { return clients_created_; };
  size_t clients_destroyed() const { return clients_destroyed_; };

 private:
  size_t clients_created_, clients_destroyed_;
};

// When there are no searches registered, it just disconnects the client.
TEST_F(SDP_ServiceDiscovererTest, NoSearches) {
  ServiceDiscoverer discoverer;

  discoverer.StartServiceDiscovery("test", GetFakeClient());

  RETURN_IF_FATAL(RunLoopUntilIdle());

  EXPECT_EQ(1u, clients_destroyed());
}

// Happy path test with one registered service and no results.
TEST_F(SDP_ServiceDiscovererTest, NoResults) {
  ServiceDiscoverer discoverer;

  size_t cb_count = 0;

  auto result_cb = [&cb_count](std::string, const auto &) { cb_count++; };

  ServiceDiscoverer::SearchId id = discoverer.AddSearch(
      profile::kSerialPort,
      {kServiceId, kProtocolDescriptorList, kBluetoothProfileDescriptorList},
      std::move(result_cb));
  ASSERT_NE(ServiceDiscoverer::kInvalidSearchId, id);

  auto client = GetFakeClient();

  std::vector<std::unordered_set<common::UUID>> searches;

  client->SetServiceSearchAttributesCallback(
      [&searches](auto pattern, auto attributes, auto callback,
                  auto *cb_dispatcher) {
        searches.emplace_back(std::move(pattern));
        async::PostTask(cb_dispatcher, [cb = std::move(callback)]() {
          cb(Status(common::HostError::kNotFound), {});
        });
      });

  discoverer.StartServiceDiscovery("test", std::move(client));

  RETURN_IF_FATAL(RunLoopUntilIdle());

  EXPECT_EQ(1u, searches.size());
  ASSERT_EQ(0u, cb_count);
  ASSERT_EQ(1u, clients_destroyed());
}

// Happy path test with two registered searches.
// No results, then two results.
// Unregister one search.
// Then one result are searched for and two returned.
TEST_F(SDP_ServiceDiscovererTest, SomeResults) {
  ServiceDiscoverer discoverer;

  std::vector<std::pair<std::string, std::map<AttributeId, DataElement>>>
      results;

  ServiceDiscoverer::ResultCallback result_cb =
      [&results](std::string id, const auto &attributes) {
        std::map<AttributeId, DataElement> attributes_clone;
        for (const auto &it : attributes) {
          auto [inserted_it, added] =
              attributes_clone.try_emplace(it.first, it.second.Clone());
          ASSERT_TRUE(added);
        }
        results.emplace_back(id, std::move(attributes_clone));
      };

  ServiceDiscoverer::SearchId one = discoverer.AddSearch(
      profile::kSerialPort,
      {kServiceId, kProtocolDescriptorList, kBluetoothProfileDescriptorList},
      result_cb.share());
  ASSERT_NE(ServiceDiscoverer::kInvalidSearchId, one);
  ServiceDiscoverer::SearchId two = discoverer.AddSearch(
      profile::kAudioSink,
      {kProtocolDescriptorList, kBluetoothProfileDescriptorList},
      result_cb.share());
  ASSERT_NE(ServiceDiscoverer::kInvalidSearchId, two);

  auto client = GetFakeClient();

  std::vector<std::unordered_set<common::UUID>> searches;

  client->SetServiceSearchAttributesCallback(
      [&searches](auto pattern, auto attributes, auto callback,
                  auto *cb_dispatcher) {
        searches.emplace_back(std::move(pattern));
        async::PostTask(cb_dispatcher, [cb = std::move(callback)]() {
          cb(Status(common::HostError::kNotFound), {});
        });
      });

  discoverer.StartServiceDiscovery("unused", std::move(client));

  RETURN_IF_FATAL(RunLoopUntilIdle());

  EXPECT_EQ(2u, searches.size());
  ASSERT_EQ(0u, results.size());
  ASSERT_EQ(1u, clients_destroyed());

  client = GetFakeClient();

  searches.clear();

  client->SetServiceSearchAttributesCallback([&searches](auto pattern,
                                                         auto attributes,
                                                         auto callback,
                                                         auto *cb_dispatcher) {
    searches.emplace_back(pattern);
    if (pattern.count(profile::kSerialPort)) {
      async::PostTask(cb_dispatcher, [cb = std::move(callback)]() {
        ServiceSearchAttributeResponse rsp;
        rsp.SetAttribute(0, kServiceId, DataElement(common::UUID(uint16_t(1))));
        // This would normally be a element list. uint32_t for Testing.
        rsp.SetAttribute(0, kBluetoothProfileDescriptorList,
                         DataElement(uint32_t(1)));

        if (!cb(Status(), rsp.attributes(0))) {
          return;
        }
        cb(Status(common::HostError::kNotFound), {});
      });
    } else if (pattern.count(profile::kAudioSink)) {
      async::PostTask(cb_dispatcher, [cb = std::move(callback)]() {
        ServiceSearchAttributeResponse rsp;
        // This would normally be a element list. uint32_t for Testing.
        rsp.SetAttribute(0, kBluetoothProfileDescriptorList,
                         DataElement(uint32_t(1)));

        if (!cb(Status(), rsp.attributes(0))) {
          return;
        }
        cb(Status(common::HostError::kNotFound), {});
      });
    } else {
      std::cerr << "Searched for " << pattern.size() << std::endl;
      for (auto it : pattern) {
        std::cerr << it.ToString() << std::endl;
      }
      FAIL() << "Unexpected search called";
    }
  });

  discoverer.StartServiceDiscovery("two", std::move(client));

  RETURN_IF_FATAL(RunLoopUntilIdle());

  EXPECT_EQ(2u, searches.size());
  ASSERT_EQ(2u, results.size());
  ASSERT_EQ(2u, clients_destroyed());

  results.clear();
  searches.clear();

  ASSERT_TRUE(discoverer.RemoveSearch(one));

  client = GetFakeClient();

  client->SetServiceSearchAttributesCallback([&searches](auto pattern,
                                                         auto attributes,
                                                         auto callback,
                                                         auto *cb_dispatcher) {
    searches.emplace_back(pattern);
    if (pattern.count(profile::kAudioSink)) {
      async::PostTask(cb_dispatcher, [cb = std::move(callback)]() {
        ServiceSearchAttributeResponse rsp;
        // This would normally be a element list. uint32_t for Testing.
        rsp.SetAttribute(0, kBluetoothProfileDescriptorList,
                         DataElement(uint32_t(1)));
        rsp.SetAttribute(1, kProtocolDescriptorList, DataElement(uint32_t(2)));

        if (!cb(Status(), rsp.attributes(0))) {
          return;
        }
        if (!cb(Status(), rsp.attributes(1))) {
          return;
        }
        cb(Status(common::HostError::kNotFound), {});
      });
    } else {
      std::cerr << "Searched for " << pattern.size() << std::endl;
      for (auto it : pattern) {
        std::cerr << it.ToString() << std::endl;
      }
      FAIL() << "Unexpected search called";
    }
  });

  discoverer.StartServiceDiscovery("three", std::move(client));

  RETURN_IF_FATAL(RunLoopUntilIdle());

  EXPECT_EQ(1u, searches.size());
  ASSERT_EQ(2u, results.size());
  ASSERT_EQ(3u, clients_destroyed());
}

// Disconnected on the other end before the discovery completes
TEST_F(SDP_ServiceDiscovererTest, Disconnected) {
  ServiceDiscoverer discoverer;

  size_t cb_count = 0;

  auto result_cb = [&cb_count](std::string, const auto &) { cb_count++; };

  ServiceDiscoverer::SearchId id = discoverer.AddSearch(
      profile::kSerialPort,
      {kServiceId, kProtocolDescriptorList, kBluetoothProfileDescriptorList},
      std::move(result_cb));
  ASSERT_NE(ServiceDiscoverer::kInvalidSearchId, id);

  auto client = GetFakeClient();

  std::vector<std::unordered_set<common::UUID>> searches;

  client->SetServiceSearchAttributesCallback(
      [&searches](auto pattern, auto attributes, auto callback,
                  auto *cb_dispatcher) {
        searches.emplace_back(pattern);
        if (pattern.count(profile::kSerialPort)) {
          async::PostTask(cb_dispatcher, [cb = std::move(callback)]() {
            cb(Status(common::HostError::kLinkDisconnected), {});
          });
        } else {
          std::cerr << "Searched for " << pattern.size() << std::endl;
          for (auto it : pattern) {
            std::cerr << it.ToString() << std::endl;
          }
          FAIL() << "Unexpected search called";
        }
      });

  discoverer.StartServiceDiscovery("unused", std::move(client));

  RETURN_IF_FATAL(RunLoopUntilIdle());

  EXPECT_EQ(1u, searches.size());
  ASSERT_EQ(0u, cb_count);
  ASSERT_EQ(1u, clients_destroyed());
}

// Unregistered Search when partway through the discovery
TEST_F(SDP_ServiceDiscovererTest, UnregisterInProgress) {
  ServiceDiscoverer discoverer;

  std::optional<std::pair<std::string, std::map<AttributeId, DataElement>>>
      result;

  ServiceDiscoverer::SearchId id = ServiceDiscoverer::kInvalidSearchId;

  ServiceDiscoverer::ResultCallback one_result_cb =
      [&discoverer, &result, &id](std::string peer_id, const auto &attributes) {
        // We should only be called once
        ASSERT_TRUE(!result.has_value());
        std::map<AttributeId, DataElement> attributes_clone;
        for (const auto &it : attributes) {
          auto [inserted_it, added] =
              attributes_clone.try_emplace(it.first, it.second.Clone());
          ASSERT_TRUE(added);
        }
        result.emplace(peer_id, std::move(attributes_clone));
        discoverer.RemoveSearch(id);
      };

  id = discoverer.AddSearch(
      profile::kAudioSink,
      {kProtocolDescriptorList, kBluetoothProfileDescriptorList},
      one_result_cb.share());
  ASSERT_NE(ServiceDiscoverer::kInvalidSearchId, id);

  auto client = GetFakeClient();

  std::vector<std::unordered_set<common::UUID>> searches;

  client->SetServiceSearchAttributesCallback([&searches](auto pattern,
                                                         auto attributes,
                                                         auto callback,
                                                         auto *cb_dispatcher) {
    searches.emplace_back(pattern);
    if (pattern.count(profile::kAudioSink)) {
      async::PostTask(cb_dispatcher, [cb = std::move(callback)]() {
        ServiceSearchAttributeResponse rsp;
        // This would normally be a element list. uint32_t for Testing.
        rsp.SetAttribute(0, kBluetoothProfileDescriptorList,
                         DataElement(uint32_t(1)));
        rsp.SetAttribute(1, kProtocolDescriptorList, DataElement(uint32_t(2)));

        if (!cb(Status(), rsp.attributes(0))) {
          return;
        }
        if (!cb(Status(), rsp.attributes(1))) {
          return;
        }
        cb(Status(common::HostError::kNotFound), {});
      });
    } else {
      std::cerr << "Searched for " << pattern.size() << std::endl;
      for (auto it : pattern) {
        std::cerr << it.ToString() << std::endl;
      }
      FAIL() << "Unexpected search called";
    }
  });

  discoverer.StartServiceDiscovery("test", std::move(client));

  RETURN_IF_FATAL(RunLoopUntilIdle());

  EXPECT_EQ(1u, searches.size());

  ASSERT_TRUE(result.has_value());
  ASSERT_EQ("test", result->first);
  auto value = result->second[kBluetoothProfileDescriptorList].Get<uint32_t>();
  ASSERT_TRUE(value);
  ASSERT_EQ(1u, *value);

  ASSERT_EQ(1u, clients_destroyed());
}

}  // namespace
}  // namespace sdp
}  // namespace btlib
