#pragma once
#include <rocos/comm/result.hpp>
#include <fastdds/dds/topic/TypeSupport.hpp>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace rocos::comm {
using Milliseconds = std::chrono::milliseconds;
struct NodeConfig {
    unsigned domain_id{0};
    std::string name{"rocos_node"};
    std::string name_space{"default"};
    // Empty uses all interfaces and multicast discovery. IPv4 addresses only.
    std::vector<std::string> interfaces;
    Milliseconds discovery_lease{5000};
    Milliseconds announcement_period{1000};
};
struct Qos {
    bool reliable{true};
    int depth{64};
    Milliseconds max_blocking{10};
    int max_instances{1};
    static Qos state() { return {false, 1, Milliseconds(0)}; }
    static Qos command() { return {}; }
};
struct Diagnostics {
    int matched{0};
    uint32_t incompatible_qos{0};
    int lost_samples{0};
    int total_matches{0};
};
namespace detail {
class NodeState;
class Writer {
public:
    Writer(std::shared_ptr<NodeState>, std::string, eprosima::fastdds::dds::TypeSupport, Qos);
    ~Writer();
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    Status write(const void* data);
    Diagnostics diagnostics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class Reader {
public:
    Reader(std::shared_ptr<NodeState>, std::string, eprosima::fastdds::dds::TypeSupport, Qos);
    ~Reader();
    Reader(const Reader&) = delete;
    Reader& operator=(const Reader&) = delete;
    Status take(void* data);
    Diagnostics diagnostics() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
std::string new_identity();
void validate_path(const std::string&);
}

template<class Support> class Publisher {
public:
    using value_type = typename Support::type;
    explicit Publisher(std::shared_ptr<detail::Writer> writer) : writer_(std::move(writer)) {}
    Status publish(const value_type& value) { return writer_->write(&value); }
    Diagnostics diagnostics() const { return writer_->diagnostics(); }
    bool wait_for_subscribers(Milliseconds timeout) const;
private:
    std::shared_ptr<detail::Writer> writer_;
};
template<class Support> class Subscriber {
public:
    using value_type = typename Support::type;
    explicit Subscriber(std::shared_ptr<detail::Reader> reader) : reader_(std::move(reader)) {}
    Result<value_type> take() {
        value_type value;
        auto status = reader_->take(&value);
        if (!status) return {std::move(status), std::nullopt};
        return Result<value_type>::success(std::move(value));
    }
    Diagnostics diagnostics() const { return reader_->diagnostics(); }
private:
    std::shared_ptr<detail::Reader> reader_;
};
class Node {
public:
    explicit Node(NodeConfig config = {});
    template<class Support> Publisher<Support> publisher(const std::string& topic, Qos qos = {}) const {
        return Publisher<Support>(std::make_shared<detail::Writer>(state_, qualify(topic),
                   eprosima::fastdds::dds::TypeSupport(new Support), qos));
    }
    template<class Support> Subscriber<Support> subscriber(const std::string& topic, Qos qos = {}) const {
        return Subscriber<Support>(std::make_shared<detail::Reader>(state_, qualify(topic),
                   eprosima::fastdds::dds::TypeSupport(new Support), qos));
    }
private:
    std::string qualify(const std::string&) const;
    std::string namespace_;
    std::shared_ptr<detail::NodeState> state_;
};
}

#include <thread>
namespace rocos::comm {
template<class Support>
bool Publisher<Support>::wait_for_subscribers(Milliseconds timeout) const {
    const auto until = std::chrono::steady_clock::now() + timeout;
    do {
        if (diagnostics().matched > 0) return true;
        std::this_thread::sleep_for(Milliseconds(2));
    } while (std::chrono::steady_clock::now() < until);
    return false;
}
}
