#include <rocos/comm/node.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/publisher/Publisher.hpp>
#include <fastdds/dds/publisher/DataWriter.hpp>
#include <fastdds/dds/subscriber/Subscriber.hpp>
#include <fastdds/dds/subscriber/DataReader.hpp>
#include <fastdds/dds/subscriber/SampleInfo.hpp>
#include <fastdds/dds/topic/Topic.hpp>
#include <fastdds/rtps/transport/UDPv4TransportDescriptor.hpp>
#include <atomic>
#include <cctype>
#include <iomanip>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>

namespace rocos::comm {
namespace dds = eprosima::fastdds::dds;
const char* error_name(Error code) {
    switch (code) {
    case Error::ok: return "ok";
    case Error::unavailable: return "unavailable";
    case Error::timeout: return "timeout";
    case Error::busy: return "busy";
    case Error::conflict: return "conflict";
    case Error::invalid_argument: return "invalid_argument";
    case Error::transport: return "transport";
    case Error::serialization: return "serialization";
    case Error::rejected: return "rejected";
    case Error::handler_error: return "handler_error";
    case Error::closed: return "closed";
    case Error::no_data: return "no_data";
    case Error::incompatible_type: return "incompatible_type";
    }
    return "unknown";
}
namespace detail {
void validate_path(const std::string& path) {
    if (path.empty() || path.size() > 128 || path.front() == '/' || path.back() == '/' ||
            path.find("//") != std::string::npos)
        throw std::invalid_argument("names must be relative nonempty paths, at most 128 characters");
    for (unsigned char c : path)
        if (!std::isalnum(c) && c != '_' && c != '-' && c != '/')
            throw std::invalid_argument("names allow ASCII alphanumeric, _, -, / only");
}
std::string new_identity() {
    std::random_device random;
    std::ostringstream out;
    for (int i = 0; i < 4; ++i) out << std::hex << std::setw(8) << std::setfill('0') << random();
    return out.str();
}
class NodeState {
public:
    std::mutex mutex;
    dds::DomainParticipant* participant{};
    dds::Publisher* publisher{};
    dds::Subscriber* subscriber{};
    std::map<std::string, dds::Topic*> topics;
    explicit NodeState(const NodeConfig& config) {
        if (config.announcement_period.count() < 100 || config.discovery_lease <= config.announcement_period ||
                config.discovery_lease.count() > 3600000)
            throw std::invalid_argument("discovery lease must exceed announcement period (minimum 100ms, maximum lease 1h)");
        dds::DomainParticipantQos qos;
        qos.name(config.name);
        const auto duration = [](Milliseconds value) {
            return dds::Duration_t(static_cast<int32_t>(value.count() / 1000),
                    static_cast<uint32_t>((value.count() % 1000) * 1000000));
        };
        qos.wire_protocol().builtin.discovery_config.leaseDuration = duration(config.discovery_lease);
        qos.wire_protocol().builtin.discovery_config.leaseDuration_announcementperiod = duration(config.announcement_period);
        // UDP-only baseline makes cross-process and cross-host behavior consistent.
        qos.transport().use_builtin_transports = false;
        auto udp = std::make_shared<eprosima::fastdds::rtps::UDPv4TransportDescriptor>();
        udp->interfaceWhiteList = config.interfaces;
        qos.transport().user_transports.push_back(udp);
        participant = dds::DomainParticipantFactory::get_instance()->create_participant(config.domain_id, qos);
        if (!participant) throw std::runtime_error("DDS participant creation failed");
        publisher = participant->create_publisher(dds::PUBLISHER_QOS_DEFAULT);
        subscriber = participant->create_subscriber(dds::SUBSCRIBER_QOS_DEFAULT);
        if (!publisher || !subscriber) {
            participant->delete_contained_entities();
            dds::DomainParticipantFactory::get_instance()->delete_participant(participant);
            throw std::runtime_error("DDS publisher/subscriber creation failed");
        }
    }
    ~NodeState() {
        participant->delete_contained_entities();
        dds::DomainParticipantFactory::get_instance()->delete_participant(participant);
    }
    dds::Topic* topic(const std::string& name, dds::TypeSupport type) {
        auto found = topics.find(name);
        if (found != topics.end()) {
            if (found->second->get_type_name() != type.get_type_name())
                throw std::invalid_argument("Topic already registered with a different type");
            return found->second;
        }
        if (type.register_type(participant) != dds::RETCODE_OK)
            throw std::runtime_error("DDS type registration failed");
        auto* result = participant->create_topic(name, type.get_type_name(), dds::TOPIC_QOS_DEFAULT);
        if (!result) throw std::runtime_error("DDS topic creation failed");
        topics.emplace(name, result);
        return result;
    }
};
template<class T> void configure(T& target, Qos config) {
    if (config.depth < 1 || config.depth > 4096 || config.max_blocking.count() < 0 ||
            config.max_blocking.count() > 1000 || config.max_instances < 1 ||
            config.max_instances > 4096 || config.depth * config.max_instances > 16384)
        throw std::invalid_argument("invalid QoS bounds");
    target.reliability().kind = config.reliable ? dds::RELIABLE_RELIABILITY_QOS : dds::BEST_EFFORT_RELIABILITY_QOS;
    const auto ms = config.max_blocking.count();
    target.reliability().max_blocking_time = {static_cast<int32_t>(ms / 1000), static_cast<uint32_t>((ms % 1000) * 1000000)};
    target.history().kind = dds::KEEP_LAST_HISTORY_QOS;
    target.history().depth = config.depth;
    target.resource_limits().max_samples = config.depth * config.max_instances;
    target.resource_limits().max_samples_per_instance = config.depth;
    target.resource_limits().max_instances = config.max_instances;
    target.resource_limits().allocated_samples = config.depth;
    target.durability().kind = dds::VOLATILE_DURABILITY_QOS;
    target.data_sharing().off();
}
struct Writer::Impl {
    std::shared_ptr<NodeState> state;
    dds::DataWriter* writer{};
    mutable std::mutex mutex;
};
Writer::Writer(std::shared_ptr<NodeState> state, std::string name, dds::TypeSupport type, Qos qos)
    : impl_(new Impl) {
    impl_->state = std::move(state);
    dds::DataWriterQos writer_qos;
    configure(writer_qos, qos);
    std::lock_guard lock(impl_->state->mutex);
    impl_->writer = impl_->state->publisher->create_datawriter(impl_->state->topic(name, type), writer_qos);
    if (!impl_->writer) throw std::runtime_error("DDS writer creation failed");
}
Writer::~Writer() {
    std::lock_guard lock(impl_->state->mutex);
    impl_->state->publisher->delete_datawriter(impl_->writer);
}
Status Writer::write(const void* data) {
    std::lock_guard lock(impl_->mutex);
    auto rc = impl_->writer->write(data);
    if (rc == dds::RETCODE_OK) return {};
    if (rc == dds::RETCODE_TIMEOUT) return {Error::timeout, "DDS write timed out"};
    if (rc == dds::RETCODE_OUT_OF_RESOURCES) return {Error::busy, "DDS writer resource limit reached"};
    if (rc == dds::RETCODE_BAD_PARAMETER) return {Error::invalid_argument, "DDS rejected sample parameters"};
    return {Error::transport, "DDS write failed: " + std::to_string(rc)};
}
Diagnostics Writer::diagnostics() const {
    std::lock_guard lock(impl_->mutex);
    dds::PublicationMatchedStatus match;
    dds::OfferedIncompatibleQosStatus incompatible;
    impl_->writer->get_publication_matched_status(match);
    impl_->writer->get_offered_incompatible_qos_status(incompatible);
    return {match.current_count, incompatible.total_count, 0, match.total_count};
}
struct Reader::Impl {
    std::shared_ptr<NodeState> state;
    dds::DataReader* reader{};
    mutable std::mutex mutex;
};
Reader::Reader(std::shared_ptr<NodeState> state, std::string name, dds::TypeSupport type, Qos qos)
    : impl_(new Impl) {
    impl_->state = std::move(state);
    dds::DataReaderQos reader_qos;
    configure(reader_qos, qos);
    std::lock_guard lock(impl_->state->mutex);
    impl_->reader = impl_->state->subscriber->create_datareader(impl_->state->topic(name, type), reader_qos);
    if (!impl_->reader) throw std::runtime_error("DDS reader creation failed");
}
Reader::~Reader() {
    std::lock_guard lock(impl_->state->mutex);
    impl_->state->subscriber->delete_datareader(impl_->reader);
}
Status Reader::take(void* data) {
    std::lock_guard lock(impl_->mutex);
    dds::SampleInfo info;
    auto rc = impl_->reader->take_next_sample(data, &info);
    if (rc == dds::RETCODE_NO_DATA || (rc == dds::RETCODE_OK && !info.valid_data))
        return {Error::no_data, "no sample"};
    if (rc != dds::RETCODE_OK) return {Error::transport, "DDS read failed"};
    return {};
}
Diagnostics Reader::diagnostics() const {
    std::lock_guard lock(impl_->mutex);
    dds::SubscriptionMatchedStatus match;
    dds::RequestedIncompatibleQosStatus incompatible;
    dds::SampleLostStatus lost;
    impl_->reader->get_subscription_matched_status(match);
    impl_->reader->get_requested_incompatible_qos_status(incompatible);
    impl_->reader->get_sample_lost_status(lost);
    return {match.current_count, incompatible.total_count, lost.total_count, match.total_count};
}
}
Node::Node(NodeConfig config) : namespace_(config.name_space) {
    detail::validate_path(namespace_);
    if (config.domain_id > 232) throw std::invalid_argument("domain_id must be 0..232");
    state_ = std::make_shared<detail::NodeState>(config);
}
std::string Node::qualify(const std::string& name) const {
    detail::validate_path(name);
    return "rocos/v1/" + namespace_ + "/" + name;
}
}
