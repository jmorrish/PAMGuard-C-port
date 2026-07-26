#include "pamguard/core/DataModel.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace pamguard::core {

namespace {

std::size_t declared_channel_count(std::uint32_t bitmap) {
    std::size_t count = 0;
    for (std::size_t channel = 0; channel < 32; ++channel) {
        if ((bitmap & (std::uint32_t{1} << channel)) != 0) {
            count = channel + 1;
        }
    }
    return count;
}

class Subscriber : public std::enable_shared_from_this<Subscriber> {
public:
    Subscriber(
        DataBlock::Observer observer,
        SubscriptionOptions options,
        std::function<void(std::uint64_t)> record_delivery,
        std::function<void()> record_error)
        : observer_(std::move(observer)),
          options_(options),
          record_delivery_(std::move(record_delivery)),
          record_error_(std::move(record_error)) {
        if (options_.delivery_mode == DeliveryMode::QueuedDropOldest) {
            if (options_.queue_capacity == 0) {
                throw std::invalid_argument("Queued subscription capacity must be greater than zero");
            }
        }
    }

    ~Subscriber() {
        stop();
    }

    Subscriber(const Subscriber&) = delete;
    Subscriber& operator=(const Subscriber&) = delete;

    void start() {
        if (options_.delivery_mode ==
            DeliveryMode::QueuedDropOldest) {
            auto self = shared_from_this();
            worker_ = std::thread(
                [self = std::move(self)] { self->run(); });
        }
    }

    std::uint64_t deliver(const DataUnit& unit) {
        if (options_.delivery_mode == DeliveryMode::Synchronous) {
            try {
                observer_(unit);
                record_delivery_(1);
            }
            catch (...) {
                record_error_();
                throw;
            }
            return 0;
        }

        std::lock_guard lock(mutex_);
        if (stopped_) {
            return 0;
        }
        std::uint64_t dropped = 0;
        if (queue_.size() == options_.queue_capacity) {
            queue_.pop_front();
            dropped = 1;
        }
        queue_.push_back(unit);
        maximum_queue_depth_ =
            std::max(maximum_queue_depth_, queue_.size());
        condition_.notify_one();
        return dropped;
    }

    [[nodiscard]] std::size_t queue_depth() const {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }

    [[nodiscard]] std::size_t maximum_queue_depth() const {
        std::lock_guard lock(mutex_);
        return maximum_queue_depth_;
    }

    void stop() {
        {
            std::lock_guard lock(mutex_);
            if (stopped_) {
                return;
            }
            stopped_ = true;
            queue_.clear();
        }
        condition_.notify_all();
        if (worker_.joinable()) {
            if (worker_.get_id() == std::this_thread::get_id()) {
                worker_.detach();
            }
            else {
                worker_.join();
            }
        }
    }

private:
    void run() {
        for (;;) {
            DataUnit unit;
            {
                std::unique_lock lock(mutex_);
                condition_.wait(lock, [this] { return stopped_ || !queue_.empty(); });
                if (stopped_) {
                    return;
                }
                unit = std::move(queue_.front());
                queue_.pop_front();
            }
            try {
                observer_(unit);
                record_delivery_(1);
            }
            catch (...) {
                // Presentation subscribers are isolated: one bad browser or
                // renderer must not terminate its delivery worker or affect
                // scientific subscribers. The counter keeps the failure
                // explicit and inspectable.
                record_error_();
            }
        }
    }

    DataBlock::Observer observer_;
    SubscriptionOptions options_;
    std::function<void(std::uint64_t)> record_delivery_;
    std::function<void()> record_error_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<DataUnit> queue_;
    bool stopped_ = false;
    std::size_t maximum_queue_depth_ = 0;
    std::thread worker_;
};

} // namespace

class DataBlock::State : public std::enable_shared_from_this<DataBlock::State> {
public:
    explicit State(DataBlockDescriptor descriptor)
        : descriptor_(std::move(descriptor)) {
        if (descriptor_.id.empty() || descriptor_.name.empty() || descriptor_.data_type.empty()) {
            throw std::invalid_argument("Data block id, name, and data type are required");
        }
        if (descriptor_.schema_version == 0) {
            throw std::invalid_argument("Data block schema version must be greater than zero");
        }
        if (!descriptor_.calibration_db_offset_by_channel.empty() &&
            descriptor_.calibration_db_offset_by_channel.size() <
                declared_channel_count(descriptor_.channel_bitmap)) {
            throw std::invalid_argument(
                "Data block calibration must cover every declared channel");
        }
        if (descriptor_.volts_peak_to_peak &&
            (!std::isfinite(*descriptor_.volts_peak_to_peak) ||
             *descriptor_.volts_peak_to_peak <= 0.0)) {
            throw std::invalid_argument(
                "Data block voltage scale must be finite and positive");
        }
    }

    Subscription subscribe(Observer observer, SubscriptionOptions options) {
        if (!observer) {
            throw std::invalid_argument("Data block observer is required");
        }

        std::lock_guard lock(mutex_);
        const auto id = next_subscriber_id_++;
        const auto weak = weak_from_this();
        auto subscriber = std::make_shared<Subscriber>(
            std::move(observer),
            options,
            [weak](std::uint64_t count) {
                if (const auto state = weak.lock()) {
                    std::lock_guard state_lock(state->mutex_);
                    state->delivered_ += count;
                }
            },
            [weak] {
                if (const auto state = weak.lock()) {
                    std::lock_guard state_lock(state->mutex_);
                    ++state->observer_errors_;
                }
            });
        subscriber->start();
        subscribers_.emplace(id, std::move(subscriber));
        return Subscription([weak, id] {
            if (const auto state = weak.lock()) {
                state->unsubscribe(id);
            }
        });
    }

    void publish(DataUnit unit) {
        if (unit.metadata.type_id.empty()) {
            unit.metadata.type_id = descriptor_.data_type;
        }
        if (unit.metadata.source_block_id.empty()) {
            unit.metadata.source_block_id = descriptor_.id;
        }
        if (unit.metadata.schema_version == 0) {
            unit.metadata.schema_version = descriptor_.schema_version;
        }
        if (unit.metadata.clock_domain_id.empty()) {
            unit.metadata.clock_domain_id =
                descriptor_.clock_domain_id;
        }
        if (unit.metadata.channel_bitmap == 0) {
            unit.metadata.channel_bitmap =
                descriptor_.channel_bitmap;
        }
        if (unit.metadata.sequence_bitmap == 0) {
            unit.metadata.sequence_bitmap =
                descriptor_.sequence_bitmap;
        }
        if (unit.metadata.type_id != descriptor_.data_type) {
            throw std::invalid_argument("Data unit type does not match its data block");
        }
        if (unit.metadata.schema_version != descriptor_.schema_version) {
            throw std::invalid_argument("Data unit schema version does not match its data block");
        }
        if (unit.metadata.source_block_id != descriptor_.id) {
            throw std::invalid_argument("Data unit source does not match its data block");
        }
        if (!descriptor_.clock_domain_id.empty() &&
            unit.metadata.clock_domain_id !=
                descriptor_.clock_domain_id) {
            throw std::invalid_argument(
                "Data unit clock domain does not match its data block");
        }
        if (descriptor_.channel_bitmap != 0 &&
            (unit.metadata.channel_bitmap &
             ~descriptor_.channel_bitmap) != 0) {
            throw std::invalid_argument(
                "Data unit channels are unavailable from its data block");
        }
        if (descriptor_.sequence_bitmap != 0 &&
            (unit.metadata.sequence_bitmap &
             ~descriptor_.sequence_bitmap) != 0) {
            throw std::invalid_argument(
                "Data unit sequences are unavailable from its data block");
        }

        std::vector<std::shared_ptr<Subscriber>> subscribers;
        {
            std::lock_guard lock(mutex_);
            ++published_;
            if (descriptor_.history_capacity > 0) {
                if (history_.size() == descriptor_.history_capacity) {
                    history_.pop_front();
                }
                history_.push_back(unit);
            }
            subscribers.reserve(subscribers_.size());
            for (const auto& [_, subscriber] : subscribers_) {
                subscribers.push_back(subscriber);
            }
        }

        std::uint64_t dropped = 0;
        for (const auto& subscriber : subscribers) {
            dropped += subscriber->deliver(unit);
        }
        if (dropped > 0) {
            std::lock_guard lock(mutex_);
            dropped_ += dropped;
        }
    }

    void unsubscribe(std::uint64_t id) {
        std::shared_ptr<Subscriber> removed;
        {
            std::lock_guard lock(mutex_);
            const auto found = subscribers_.find(id);
            if (found == subscribers_.end()) {
                return;
            }
            removed = std::move(found->second);
            subscribers_.erase(found);
        }
        removed->stop();
    }

    void stop_all() {
        std::vector<std::shared_ptr<Subscriber>> subscribers;
        {
            std::lock_guard lock(mutex_);
            for (auto& [_, subscriber] : subscribers_) {
                subscribers.push_back(std::move(subscriber));
            }
            subscribers_.clear();
        }
        for (const auto& subscriber : subscribers) {
            subscriber->stop();
        }
    }

    [[nodiscard]] std::vector<DataUnit> recent_history() const {
        std::lock_guard lock(mutex_);
        return {history_.begin(), history_.end()};
    }

    [[nodiscard]] DataBlockStats stats() const {
        std::lock_guard lock(mutex_);
        std::size_t queued_units = 0;
        std::size_t maximum_queued_units = 0;
        for (const auto& [_, subscriber] : subscribers_) {
            queued_units += subscriber->queue_depth();
            maximum_queued_units +=
                subscriber->maximum_queue_depth();
        }
        return {
            published_,
            delivered_,
            dropped_,
            observer_errors_,
            subscribers_.size(),
            history_.size(),
            queued_units,
            maximum_queued_units,
        };
    }

    void configure_clock_domain(std::string clock_domain_id) {
        std::lock_guard lock(mutex_);
        if (published_ != 0 || !subscribers_.empty() || !history_.empty()) {
            throw std::logic_error(
                "Data block clock domain can only be configured before use");
        }
        descriptor_.clock_domain_id = std::move(clock_domain_id);
    }

    void configure_calibration(
        std::vector<double> calibration_db_offset_by_channel) {
        std::lock_guard lock(mutex_);
        if (published_ != 0 || !subscribers_.empty() || !history_.empty()) {
            throw std::logic_error(
                "Data block calibration can only be configured before use");
        }
        if (!calibration_db_offset_by_channel.empty() &&
            calibration_db_offset_by_channel.size() <
                declared_channel_count(descriptor_.channel_bitmap)) {
            throw std::invalid_argument(
                "Data block calibration must cover every declared channel");
        }
        descriptor_.calibration_db_offset_by_channel =
            std::move(calibration_db_offset_by_channel);
    }

    void configure_voltage_scale(double volts_peak_to_peak) {
        std::lock_guard lock(mutex_);
        if (published_ != 0 || !subscribers_.empty() || !history_.empty()) {
            throw std::logic_error(
                "Data block voltage scale can only be configured before use");
        }
        if (!std::isfinite(volts_peak_to_peak) ||
            volts_peak_to_peak <= 0.0) {
            throw std::invalid_argument(
                "Data block voltage scale must be finite and positive");
        }
        descriptor_.volts_peak_to_peak = volts_peak_to_peak;
    }

    DataBlockDescriptor descriptor_;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::uint64_t, std::shared_ptr<Subscriber>> subscribers_;
    std::deque<DataUnit> history_;
    std::uint64_t next_subscriber_id_ = 1;
    std::uint64_t published_ = 0;
    std::uint64_t delivered_ = 0;
    std::uint64_t dropped_ = 0;
    std::uint64_t observer_errors_ = 0;
};

Subscription::Subscription(std::function<void()> cancel)
    : cancel_(std::move(cancel)) {}

Subscription::~Subscription() {
    cancel();
}

Subscription::Subscription(Subscription&& other) noexcept
    : cancel_(std::move(other.cancel_)) {
    other.cancel_ = {};
}

Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        cancel();
        cancel_ = std::move(other.cancel_);
        other.cancel_ = {};
    }
    return *this;
}

void Subscription::cancel() {
    if (cancel_) {
        auto cancel = std::move(cancel_);
        cancel_ = {};
        cancel();
    }
}

bool Subscription::active() const noexcept {
    return static_cast<bool>(cancel_);
}

DataBlock::DataBlock(DataBlockDescriptor descriptor)
    : state_(std::make_shared<State>(std::move(descriptor))) {}

DataBlock::~DataBlock() {
    state_->stop_all();
}

const DataBlockDescriptor& DataBlock::descriptor() const noexcept {
    return state_->descriptor_;
}

Subscription DataBlock::subscribe(Observer observer, SubscriptionOptions options) {
    return state_->subscribe(std::move(observer), options);
}

void DataBlock::publish(DataUnit unit) {
    state_->publish(std::move(unit));
}

std::vector<DataUnit> DataBlock::recent_history() const {
    return state_->recent_history();
}

DataBlockStats DataBlock::stats() const {
    return state_->stats();
}

void DataBlock::configure_clock_domain(std::string clock_domain_id) {
    state_->configure_clock_domain(std::move(clock_domain_id));
}

void DataBlock::configure_calibration(
    std::vector<double> calibration_db_offset_by_channel) {
    state_->configure_calibration(
        std::move(calibration_db_offset_by_channel));
}

void DataBlock::configure_voltage_scale(double volts_peak_to_peak) {
    state_->configure_voltage_scale(volts_peak_to_peak);
}

} // namespace pamguard::core
