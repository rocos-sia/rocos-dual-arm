#pragma once
#include <rocos/comm/service.hpp>
#include <RobotPubSubTypes.hpp>
#include <map>
#include <mutex>

namespace demo_app {
using namespace rocos::comm;
using CapClient = ServiceClient<demo::CapabilitiesRequestPubSubType, demo::CapabilitiesResponsePubSubType>;
using CapServer = ServiceServer<demo::CapabilitiesRequestPubSubType, demo::CapabilitiesResponsePubSubType>;
using LeaseClient = ServiceClient<demo::LeaseRequestPubSubType, demo::LeaseResponsePubSubType>;
using LeaseServer = ServiceServer<demo::LeaseRequestPubSubType, demo::LeaseResponsePubSubType>;

// Business idempotency is separate from transport request IDs. Retained until server restart.
// Reject new operations when the record limit is reached rather than silently evicting keys.
class LeaseManager {
public:
    Result<demo::LeaseResponse> acquire(const demo::LeaseRequest& request) {
        std::lock_guard lock(mutex_);
        if (request.operation_id().size() == 0 || request.owner().size() == 0 ||
                request.duration_ms() < 1 || request.duration_ms() > 60000)
            return Result<demo::LeaseResponse>::failure(Error::invalid_argument, "invalid lease request");
        const auto key = std::make_pair(std::string(request.owner().c_str()), std::string(request.operation_id().c_str()));
        auto found = records_.find(key);
        if (found != records_.end()) {
            if (found->second.first != request.duration_ms())
                return Result<demo::LeaseResponse>::failure(Error::conflict, "operation ID reused with different arguments");
            return Result<demo::LeaseResponse>::success(found->second.second);
        }
        if (records_.size() >= 1024)
            return Result<demo::LeaseResponse>::failure(Error::busy, "idempotency record limit reached");
        const auto now = std::chrono::steady_clock::now();
        if (!owner_.empty() && now < expires_)
            return Result<demo::LeaseResponse>::failure(Error::rejected, "lease already held; retry original operation or wait for expiry");
        owner_ = request.owner().c_str();
        expires_ = now + Milliseconds(request.duration_ms());
        demo::LeaseResponse response;
        response.lease_id(detail::new_identity());
        response.generation(++generation_);
        response.duration_ms(request.duration_ms());
        records_.emplace(key, std::make_pair(request.duration_ms(), response));
        return Result<demo::LeaseResponse>::success(response);
    }
private:
    std::mutex mutex_;
    std::string owner_;
    uint64_t generation_{0};
    std::chrono::steady_clock::time_point expires_{};
    std::map<std::pair<std::string, std::string>, std::pair<uint32_t, demo::LeaseResponse>> records_;
};
}
