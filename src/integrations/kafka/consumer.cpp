#include "integrations/kafka/consumer.hpp"

#include <chrono>
#include <thread>

#include "glog/logging.h"

#include "integrations/kafka/exceptions.hpp"
#include "utils/on_scope_exit.hpp"
#include "utils/thread.hpp"

namespace integrations::kafka {

using namespace std::chrono_literals;

constexpr int64_t kDefaultBatchIntervalMillis = 100;
constexpr int64_t kDefaultBatchSize = 1000;
constexpr int64_t kDefaultTestBatchLimit = 1;

void Consumer::event_cb(RdKafka::Event &event) {
  switch (event.type()) {
    case RdKafka::Event::Type::EVENT_ERROR:
      LOG(WARNING) << "[Kafka] stream " << info_.stream_name << " ERROR ("
                   << RdKafka::err2str(event.err()) << "): " << event.str();
      break;
    default:
      break;
  }
}

Consumer::Consumer(
    const StreamInfo &info, const std::string &transform_script_path,
    std::function<
        void(const std::string &,
             const std::map<std::string, communication::bolt::Value> &)>
        stream_writer)
    : info_(info),
      transform_script_path_(transform_script_path),
      stream_writer_(stream_writer) {
  std::unique_ptr<RdKafka::Conf> conf(
      RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL));
  std::string error;

  if (conf->set("event_cb", this, error) != RdKafka::Conf::CONF_OK) {
    throw ConsumerFailedToInitializeException(info_.stream_name, error);
  }

  if (conf->set("enable.partition.eof", "false", error) !=
      RdKafka::Conf::CONF_OK) {
    throw ConsumerFailedToInitializeException(info_.stream_name, error);
  }

  if (conf->set("bootstrap.servers", info_.stream_uri, error) !=
      RdKafka::Conf::CONF_OK) {
    throw ConsumerFailedToInitializeException(info_.stream_name, error);
  }

  if (conf->set("group.id", "mg", error) != RdKafka::Conf::CONF_OK) {
    throw ConsumerFailedToInitializeException(info_.stream_name, error);
  }

  consumer_ = std::unique_ptr<RdKafka::KafkaConsumer,
                              std::function<void(RdKafka::KafkaConsumer *)>>(
      RdKafka::KafkaConsumer::create(conf.get(), error),
      [this](auto *consumer) {
        this->StopConsuming();
        consumer->close();
        delete consumer;
      });

  if (!consumer_) {
    throw ConsumerFailedToInitializeException(info_.stream_name, error);
  }

  // Try fetching metadata first and check if topic exists.
  RdKafka::ErrorCode err;
  RdKafka::Metadata *raw_metadata = nullptr;
  err = consumer_->metadata(true, nullptr, &raw_metadata, 1000);
  std::unique_ptr<RdKafka::Metadata> metadata(raw_metadata);
  if (err != RdKafka::ERR_NO_ERROR) {
    throw ConsumerFailedToInitializeException(info_.stream_name,
                                              RdKafka::err2str(err));
  }

  bool topic_found = false;
  for (const auto &topic_metadata : *metadata->topics()) {
    if (topic_metadata->topic() == info_.stream_topic) {
      topic_found = true;
      break;
    }
  }

  if (!topic_found) {
    throw TopicNotFoundException(info_.stream_name);
  }

  err = consumer_->subscribe({info_.stream_topic});
  if (err != RdKafka::ERR_NO_ERROR) {
    throw ConsumerFailedToInitializeException(info_.stream_name,
                                              RdKafka::err2str(err));
  }
}

void Consumer::StopConsuming() {
  is_running_.store(false);
  if (thread_.joinable()) thread_.join();

  // Set limit_batches to nullopt since it's not running anymore.
  info_.limit_batches = std::experimental::nullopt;
}

void Consumer::StartConsuming(
    std::experimental::optional<int64_t> limit_batches) {
  info_.limit_batches = limit_batches;
  is_running_.store(true);

  thread_ = std::thread([this, limit_batches]() {
    utils::ThreadSetName("StreamKafka");

    int64_t batch_count = 0;
    Transform transform(transform_script_path_);

    transform_alive_.store(false);
    if (!transform.Start()) {
      LOG(WARNING) << "[Kafka] stream " << info_.stream_name
                   << " couldn't start the transform script!";
      return;
    }
    transform_alive_.store(true);

    while (is_running_) {
      // TODO (msantl): Figure out what to do with potential exceptions here.
      auto batch = this->GetBatch();

      if (batch.empty()) continue;

      DLOG(INFO) << "[Kafka] stream " << info_.stream_name
                 << " processing a batch";

      // All exceptions that could be possibly thrown by the `Apply` function
      // must be handled here because they *will* crash the database if
      // uncaught!
      // TODO (mferencevic): Figure out what to do with all other exceptions.
      try {
        transform.Apply(batch, stream_writer_);
      } catch (const TransformExecutionException) {
        LOG(WARNING) << "[Kafka] stream " << info_.stream_name
                     << " the transform process has died!";
        break;
      } catch (const utils::BasicException &e) {
        LOG(WARNING) << "[Kafka] stream " << info_.stream_name
                     << " the transform process received an exception: "
                     << e.what();
        break;
      }

      if (limit_batches != std::experimental::nullopt) {
        if (limit_batches <= ++batch_count) {
          is_running_.store(false);
          break;
        }
      }
    }

    transform_alive_.store(false);
  });
}

std::vector<std::unique_ptr<RdKafka::Message>> Consumer::GetBatch() {
  std::vector<std::unique_ptr<RdKafka::Message>> batch;
  auto start = std::chrono::system_clock::now();
  int64_t remaining_timeout_in_ms =
      info_.batch_interval_in_ms.value_or(kDefaultBatchIntervalMillis);
  int64_t batch_size = info_.batch_size.value_or(kDefaultBatchSize);

  batch.reserve(batch_size);

  bool run_batch = true;
  for (int64_t i = 0; remaining_timeout_in_ms > 0 && i < batch_size; ++i) {
    std::unique_ptr<RdKafka::Message> msg(
        consumer_->consume(remaining_timeout_in_ms));
    switch (msg->err()) {
      case RdKafka::ERR__TIMED_OUT:
        run_batch = false;
        break;

      case RdKafka::ERR_NO_ERROR:
        batch.emplace_back(std::move(msg));
        break;

      default:
        LOG(WARNING) << "[Kafka] stream " << info_.stream_name
                     << " consumer error: " << msg->errstr();
        run_batch = false;
        is_running_.store(false);
        break;
    }

    if (!run_batch) {
      break;
    }

    auto now = std::chrono::system_clock::now();
    auto took =
        std::chrono::duration_cast<std::chrono::milliseconds>(now - start);
    remaining_timeout_in_ms = remaining_timeout_in_ms - took.count();
    start = now;
  }

  return batch;
}

void Consumer::Start(std::experimental::optional<int64_t> limit_batches) {
  if (!consumer_) {
    throw ConsumerNotAvailableException(info_.stream_name);
  }

  if (is_running_) {
    throw ConsumerRunningException(info_.stream_name);
  }

  StartConsuming(limit_batches);
}

void Consumer::Stop() {
  if (!consumer_) {
    throw ConsumerNotAvailableException(info_.stream_name);
  }

  if (!is_running_) {
    throw ConsumerStoppedException(info_.stream_name);
  }

  StopConsuming();
}

void Consumer::StartIfStopped() {
  if (!consumer_) {
    throw ConsumerNotAvailableException(info_.stream_name);
  }

  if (!is_running_) {
    StartConsuming(std::experimental::nullopt);
  }
}

void Consumer::StopIfRunning() {
  if (!consumer_) {
    throw ConsumerNotAvailableException(info_.stream_name);
  }

  if (is_running_) {
    StopConsuming();
  }
}

std::vector<
    std::pair<std::string, std::map<std::string, communication::bolt::Value>>>
Consumer::Test(std::experimental::optional<int64_t> limit_batches) {
  // All exceptions thrown here are handled by the Bolt protocol.
  if (!consumer_) {
    throw ConsumerNotAvailableException(info_.stream_name);
  }

  if (is_running_) {
    throw ConsumerRunningException(info_.stream_name);
  }

  Transform transform(transform_script_path_);

  int64_t num_of_batches = limit_batches.value_or(kDefaultTestBatchLimit);
  std::vector<
      std::pair<std::string, std::map<std::string, communication::bolt::Value>>>
      results;

  is_running_.store(true);

  utils::OnScopeExit cleanup([this]() { is_running_.store(false); });

  transform_alive_.store(false);
  if (!transform.Start()) {
    LOG(WARNING) << "[Kafka] stream " << info_.stream_name
                 << " couldn't start the transform script!";
    throw TransformExecutionException("Couldn't start the transform script!");
  }
  transform_alive_.store(true);

  for (int64_t i = 0; i < num_of_batches; ++i) {
    auto batch = GetBatch();

    // Exceptions thrown by `Apply` are handled in Bolt.
    // Wrap the `TransformExecutionException` into a new exception with a
    // message that isn't so specific so the user doesn't get confused.
    try {
      transform.Apply(
          batch,
          [&results](
              const std::string &query,
              const std::map<std::string, communication::bolt::Value> &params) {
            results.push_back({query, params});
          });
    } catch (const TransformExecutionException) {
      LOG(WARNING) << "[Kafka] stream " << info_.stream_name
                   << " the transform process has died!";
      throw TransformExecutionException(
          "The transform script contains a runtime error!");
    }
  }

  transform_alive_.store(false);

  return results;
}

StreamStatus Consumer::Status() {
  StreamStatus ret;
  ret.stream_name = info_.stream_name;
  ret.stream_uri = info_.stream_uri;
  ret.stream_topic = info_.stream_topic;
  ret.transform_uri = info_.transform_uri;
  if (!is_running_) {
    ret.stream_status = "stopped";
  } else if (!transform_alive_) {
    ret.stream_status = "error";
  } else {
    ret.stream_status = "running";
  }
  return ret;
}

StreamInfo Consumer::Info() {
  info_.is_running = is_running_;
  return info_;
}

}  // namespace integrations::kafka