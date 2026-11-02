#pragma once
#include <rocos/comm/node.hpp>
#include <ServiceFramePubSubTypes.hpp>
#include <fastdds/rtps/common/SerializedPayload.hpp>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <thread>

namespace rocos::comm {
namespace detail {
using Frame = rocos_wire::ServiceFrame;
using FrameType = rocos_wire::ServiceFramePubSubType;
using Clock = std::chrono::steady_clock;
constexpr size_t max_payload = 65536;

template<class Support>
Result<std::vector<uint8_t>> encode(const typename Support::type& data) {
    try {
        Support support;
        auto size = support.calculate_serialized_size(&data, eprosima::fastdds::dds::XCDR_DATA_REPRESENTATION);
        if (size > max_payload) return Result<std::vector<uint8_t>>::failure(Error::serialization, "payload exceeds 65536 bytes");
        eprosima::fastdds::rtps::SerializedPayload_t payload(size);
        if (!support.serialize(&data, payload, eprosima::fastdds::dds::XCDR_DATA_REPRESENTATION))
            return Result<std::vector<uint8_t>>::failure(Error::serialization, "payload serialization failed");
        return Result<std::vector<uint8_t>>::success({payload.data, payload.data + payload.length});
    } catch (const std::exception& ex) {
        return Result<std::vector<uint8_t>>::failure(Error::serialization, ex.what());
    }
}
template<class Support>
Result<typename Support::type> decode(const std::vector<uint8_t>& bytes) {
    if (bytes.empty() || bytes.size() > max_payload)
        return Result<typename Support::type>::failure(Error::serialization, "invalid payload size");
    try {
        Support support;
        eprosima::fastdds::rtps::SerializedPayload_t payload(static_cast<uint32_t>(bytes.size()));
        std::memcpy(payload.data, bytes.data(), bytes.size());
        payload.length = static_cast<uint32_t>(bytes.size());
        typename Support::type value;
        if (!support.deserialize(payload, &value))
            return Result<typename Support::type>::failure(Error::serialization, "payload deserialization failed");
        return Result<typename Support::type>::success(std::move(value));
    } catch (const std::exception& ex) {
        return Result<typename Support::type>::failure(Error::serialization, ex.what());
    }
}
template<class RequestSupport, class ResponseSupport>
std::string contract_name() {
    RequestSupport request;
    ResponseSupport response;
    auto name = std::string(request.get_name()) + "->" + response.get_name();
    if (name.size() > 256) throw std::invalid_argument("service type names exceed contract bound");
    return name;
}
inline Frame response_to(const Frame& request, const std::string& server, Error status, std::string detail = {}) {
    Frame frame;
    frame.protocol_version(1);
    frame.kind(3);
    frame.client_id(request.client_id());
    frame.request_id(request.request_id());
    frame.server_id(server);
    frame.contract(request.contract());
    frame.status(static_cast<uint32_t>(status));
    frame.detail(detail.substr(0, 256));
    return frame;
}
}

struct ClientOptions {
    size_t max_pending{64};
    Milliseconds probe_period{100};
    Milliseconds ready_ttl{500};
};
struct ServiceMetrics {
    uint64_t ignored_replies{0};
    uint64_t send_failures{0};
    size_t pending{0};
};

template<class RequestSupport, class ResponseSupport> class ServiceClient {
public:
    using Request = typename RequestSupport::type;
    using Response = typename ResponseSupport::type;
    ServiceClient(const Node& node, const std::string& name, ClientOptions options = {})
        : requests_(node.publisher<detail::FrameType>("services/" + name + "/request")),
          replies_(node.subscriber<detail::FrameType>("services/" + name + "/reply")),
          options_(options), contract_(detail::contract_name<RequestSupport, ResponseSupport>()) {
        if (!options_.max_pending || options_.max_pending > 4096 || options_.probe_period.count() < 1 ||
                options_.ready_ttl <= options_.probe_period)
            throw std::invalid_argument("invalid client bounds");
        thread_ = std::thread([this] { run(); });
    }
    ~ServiceClient() {
        stop_ = true;
        thread_.join();
        std::lock_guard lock(mutex_);
        for (auto& item : pending_)
            item.second.promise.set_value(Result<Response>::failure(Error::closed, "client destroyed"));
    }
    ServiceClient(const ServiceClient&) = delete;
    ServiceClient& operator=(const ServiceClient&) = delete;

    Status wait_for_service(Milliseconds timeout) {
        const auto deadline = detail::Clock::now() + timeout;
        do {
            {
                std::lock_guard lock(mutex_);
                auto status = readiness_locked();
                if (status || status.code == Error::conflict || status.code == Error::incompatible_type) return status;
            }
            if (detail::Clock::now() >= deadline) break;
            std::this_thread::sleep_for(Milliseconds(5));
        } while (true);
        return {Error::unavailable, "service did not become ready"};
    }

    std::future<Result<Response>> async_call(const Request& request, Milliseconds timeout) {
        const auto deadline = detail::Clock::now() + timeout;
        std::promise<Result<Response>> promise;
        auto future = promise.get_future();
        auto encoded = detail::encode<RequestSupport>(request);
        if (!encoded) {
            promise.set_value({encoded.status, std::nullopt});
            return future;
        }
        std::lock_guard lock(mutex_);
        if (timeout.count() <= 0) {
            promise.set_value(Result<Response>::failure(Error::invalid_argument, "timeout must be positive"));
            return future;
        }
        auto ready = readiness_locked();
        if (!ready) {
            promise.set_value({ready, std::nullopt});
            return future;
        }
        if (pending_.size() >= options_.max_pending) {
            promise.set_value(Result<Response>::failure(Error::busy, "client pending limit reached"));
            return future;
        }
        const uint64_t id = ++sequence_;
        const auto server = ready_.begin()->first;
        detail::Frame frame;
        frame.protocol_version(1);
        frame.kind(2);
        frame.client_id(client_id_);
        frame.request_id(id);
        frame.server_id(server);
        frame.contract(contract_);
        frame.payload(std::move(*encoded.value));
        pending_.emplace(id, Pending{std::move(promise), deadline, server});
        const auto result = requests_.publish(frame);
        if (!result) {
            ++send_failures_;
            pending_.at(id).promise.set_value({result, std::nullopt});
            pending_.erase(id);
        }
        return future;
    }
    ServiceMetrics metrics() const {
        std::lock_guard lock(mutex_);
        return {ignored_, send_failures_, pending_.size()};
    }
private:
    struct Pending {
        std::promise<Result<Response>> promise;
        detail::Clock::time_point deadline;
        std::string server;
    };
    Status readiness_locked() {
        const auto now = detail::Clock::now();
        for (auto it = ready_.begin(); it != ready_.end();)
            if (now - it->second > options_.ready_ttl) it = ready_.erase(it); else ++it;
        for (auto it = probes_.begin(); it != probes_.end();)
            if (now - it->second > options_.ready_ttl) it = probes_.erase(it); else ++it;
        const auto request_diagnostics = requests_.diagnostics();
        const auto reply_diagnostics = replies_.diagnostics();
        const auto request_peers = request_diagnostics.matched;
        const auto reply_peers = reply_diagnostics.matched;
        if (last_request_matches_ != request_diagnostics.total_matches ||
                last_reply_matches_ != reply_diagnostics.total_matches ||
                request_peers != 1 || reply_peers != 1) {
            ready_.clear();
            probes_.clear();
            incompatible_until_ = detail::Clock::time_point{};
        }
        last_request_matches_ = request_diagnostics.total_matches;
        last_reply_matches_ = reply_diagnostics.total_matches;
        if (request_peers > 1 || reply_peers > 1 || ready_.size() > 1)
            return {Error::conflict, "multiple servers at one service address"};
        if (now < incompatible_until_)
            return {Error::incompatible_type, "service request/response type names differ"};
        if (request_peers != 1 || reply_peers != 1 || ready_.empty())
            return {Error::unavailable, "service not ready"};
        return {};
    }
    void run() {
        auto next_probe = detail::Clock::now();
        while (!stop_) {
            const auto now = detail::Clock::now();
            bool probe_needed;
            {
                std::lock_guard lock(mutex_);
                readiness_locked();
                probe_needed = now >= next_probe;
            }
            if (probe_needed) {
                detail::Frame probe;
                probe.protocol_version(1);
                probe.kind(0);
                probe.client_id(client_id_);
                probe.contract(contract_);
                {
                    std::lock_guard lock(mutex_);
                    probe.request_id(++probe_sequence_);
                    probes_[probe_sequence_] = now;
                    if (probes_.size() > 64) probes_.erase(probes_.begin());
                }
                if (!requests_.publish(probe)) { std::lock_guard lock(mutex_); ++send_failures_; }
                next_probe = now + options_.probe_period;
            }
            for (int batch = 0; batch < 64; ++batch) {
                auto sample = replies_.take();
                if (!sample) break;
                auto& frame = *sample.value;
                std::lock_guard lock(mutex_);
                readiness_locked();
                if (frame.protocol_version() != 1 || frame.client_id() != client_id_ || frame.contract() != contract_) {
                    ++ignored_; continue;
                }
                if (frame.kind() == 1 && frame.server_id().size() != 0 &&
                        probes_.erase(frame.request_id()) != 0) {
                    if (frame.status() == static_cast<uint32_t>(Error::incompatible_type))
                        incompatible_until_ = detail::Clock::now() + options_.ready_ttl;
                    else if (frame.status() == 0)
                        ready_[frame.server_id().c_str()] = detail::Clock::now();
                    // Bound identities even if many restarted servers are observed.
                    if (ready_.size() > 16) ready_.erase(ready_.begin());
                    continue;
                }
                auto it = pending_.find(frame.request_id());
                if (frame.kind() != 3 || it == pending_.end() || it->second.server != frame.server_id().c_str()) {
                    ++ignored_; continue;
                }
                if (detail::Clock::now() >= it->second.deadline) {
                    it->second.promise.set_value(Result<Response>::failure(Error::timeout, "response deadline exceeded; execution unknown"));
                    ++ignored_;
                } else if (frame.status() != 0) {
                    const auto code = frame.status() <= static_cast<uint32_t>(Error::incompatible_type) ?
                        static_cast<Error>(frame.status()) : Error::transport;
                    it->second.promise.set_value(Result<Response>::failure(code, frame.detail().c_str()));
                } else {
                    it->second.promise.set_value(detail::decode<ResponseSupport>(frame.payload()));
                }
                pending_.erase(it);
            }
            {
                std::lock_guard lock(mutex_);
                for (auto it = pending_.begin(); it != pending_.end();) {
                    if (detail::Clock::now() >= it->second.deadline) {
                        it->second.promise.set_value(Result<Response>::failure(Error::timeout, "response deadline exceeded; execution unknown"));
                        it = pending_.erase(it);
                    } else ++it;
                }
            }
            std::this_thread::sleep_for(Milliseconds(2));
        }
    }
    Publisher<detail::FrameType> requests_;
    Subscriber<detail::FrameType> replies_;
    ClientOptions options_;
    const std::string contract_;
    const std::string client_id_{detail::new_identity()};
    mutable std::mutex mutex_;
    std::map<uint64_t, Pending> pending_;
    std::map<std::string, detail::Clock::time_point> ready_;
    std::map<uint64_t, detail::Clock::time_point> probes_;
    detail::Clock::time_point incompatible_until_{};
    uint64_t sequence_{0}, ignored_{0}, send_failures_{0};
    uint64_t probe_sequence_{0};
    int last_request_matches_{0}, last_reply_matches_{0};
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

struct ServerOptions {
    size_t queue_capacity{32};
    size_t reply_cache_capacity{128};
};
template<class RequestSupport, class ResponseSupport> class ServiceServer {
public:
    using Request = typename RequestSupport::type;
    using Response = typename ResponseSupport::type;
    using Handler = std::function<Result<Response>(const Request&)>;
    ServiceServer(const Node& node, const std::string& name, Handler handler, ServerOptions options = {})
        : requests_(node.subscriber<detail::FrameType>("services/" + name + "/request")),
          replies_(node.publisher<detail::FrameType>("services/" + name + "/reply")),
          handler_(std::move(handler)), options_(options),
          contract_(detail::contract_name<RequestSupport, ResponseSupport>()) {
        if (!handler_ || !options_.queue_capacity || options_.queue_capacity > 4096 ||
                !options_.reply_cache_capacity || options_.reply_cache_capacity > 4096)
            throw std::invalid_argument("invalid server handler or bounds");
        worker_ = std::thread([this] { execute(); });
        try { receiver_ = std::thread([this] { receive(); }); }
        catch (...) { stop_ = true; cv_.notify_all(); worker_.join(); throw; }
    }
    ~ServiceServer() {
        stop_ = true;
        cv_.notify_all();
        receiver_.join();
        worker_.join(); // Current handler must finish; handlers must have bounded execution.
    }
    ServiceServer(const ServiceServer&) = delete;
    ServiceServer& operator=(const ServiceServer&) = delete;
    const std::string& instance_id() const { return server_id_; }
    uint64_t send_failures() const { return send_failures_.load(); }
private:
    using Key = std::pair<std::string, uint64_t>;
    struct Cached { std::vector<uint8_t> request; detail::Frame response; };
    void send(const detail::Frame& frame) { if (!replies_.publish(frame)) ++send_failures_; }
    void receive() {
        while (!stop_) {
            auto sample = requests_.take();
            if (!sample) { std::this_thread::sleep_for(Milliseconds(2)); continue; }
            auto frame = std::move(*sample.value);
            if (frame.protocol_version() != 1 || frame.client_id().size() == 0) continue;
            if (frame.contract() != contract_) {
                if (frame.kind() == 0) {
                    auto mismatch = detail::response_to(frame, server_id_, Error::incompatible_type);
                    mismatch.kind(1); send(mismatch);
                }
                continue;
            }
            if (frame.kind() == 0) {
                auto ready = detail::response_to(frame, server_id_, Error::ok);
                ready.kind(1);
                send(ready);
                continue;
            }
            if (frame.kind() != 2 || frame.server_id() != server_id_) continue;
            const Key key{frame.client_id().c_str(), frame.request_id()};
            std::unique_lock lock(mutex_);
            const auto cached = cache_.find(key);
            if (cached != cache_.end()) {
                auto response = cached->second.request == frame.payload() ? cached->second.response :
                    detail::response_to(frame, server_id_, Error::conflict, "same request ID with different payload");
                lock.unlock(); send(response); continue;
            }
            const auto active = inflight_.find(key);
            if (active != inflight_.end()) {
                const bool changed = active->second != frame.payload();
                lock.unlock();
                if (changed) send(detail::response_to(frame, server_id_, Error::conflict, "request ID already active with different payload"));
                continue;
            }
            if (queue_.size() >= options_.queue_capacity) {
                lock.unlock(); send(detail::response_to(frame, server_id_, Error::busy, "server queue full")); continue;
            }
            inflight_.emplace(key, frame.payload());
            queue_.push_back(std::move(frame));
            cv_.notify_one();
        }
    }
    void execute() {
        while (true) {
            detail::Frame request;
            {
                std::unique_lock lock(mutex_);
                cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
                if (stop_) return;
                request = std::move(queue_.front());
                queue_.pop_front();
            }
            auto response = detail::response_to(request, server_id_, Error::ok);
            try {
                auto decoded = detail::decode<RequestSupport>(request.payload());
                Result<Response> result = decoded ? handler_(*decoded.value) : Result<Response>{decoded.status, std::nullopt};
                if (!result) {
                    auto code = result.status.code == Error::ok ? Error::handler_error : result.status.code;
                    response.status(static_cast<uint32_t>(code));
                    response.detail(result.status.detail.substr(0, 256));
                } else {
                    auto encoded = detail::encode<ResponseSupport>(*result.value);
                    if (!encoded) {
                        response.status(static_cast<uint32_t>(encoded.status.code));
                        response.detail(encoded.status.detail.substr(0, 256));
                    } else response.payload(std::move(*encoded.value));
                }
            } catch (const std::exception& ex) {
                response.status(static_cast<uint32_t>(Error::handler_error));
                response.detail(std::string(ex.what()).substr(0, 256));
            } catch (...) {
                response.status(static_cast<uint32_t>(Error::handler_error));
                response.detail("unknown handler exception");
            }
            {
                std::lock_guard lock(mutex_);
                Key key{request.client_id().c_str(), request.request_id()};
                inflight_.erase(key);
                cache_.emplace(key, Cached{request.payload(), response});
                order_.push_back(key);
                if (order_.size() > options_.reply_cache_capacity) {
                    cache_.erase(order_.front()); order_.pop_front();
                }
            }
            send(response);
        }
    }
    Subscriber<detail::FrameType> requests_;
    Publisher<detail::FrameType> replies_;
    Handler handler_;
    ServerOptions options_;
    const std::string contract_;
    const std::string server_id_{detail::new_identity()};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<detail::Frame> queue_;
    std::map<Key, std::vector<uint8_t>> inflight_;
    std::map<Key, Cached> cache_;
    std::deque<Key> order_;
    std::atomic<bool> stop_{false};
    std::atomic<uint64_t> send_failures_{0};
    std::thread receiver_, worker_;
};
}
