#include "services.hpp"
#include <csignal>
#include <iostream>

namespace { volatile std::sig_atomic_t running = 1; void stop(int) { running = 0; } }
int main(int argc, char** argv) {
    using namespace rocos::comm;
    using namespace demo_app;
    try {
        if (argc < 2) { std::cerr << "usage: rocos_demo server|client|watch [domain=42] [namespace=demo]\n"; return 2; }
        const std::string role = argv[1];
        NodeConfig config;
        config.domain_id = argc > 2 ? static_cast<unsigned>(std::stoul(argv[2])) : 42;
        config.name_space = argc > 3 ? argv[3] : "demo";
        config.name = "demo_" + role;
        Node node(config);
        if (role == "server") {
            LeaseManager leases;
            const std::string epoch = detail::new_identity();
            CapServer capabilities(node, "capabilities/v1", [epoch](const demo::CapabilitiesRequest& req) {
                if (req.device() != "sim") return Result<demo::CapabilitiesResponse>::failure(Error::rejected, "unknown device");
                demo::CapabilitiesResponse result;
                result.device("sim"); result.joints(14); result.instance(epoch);
                return Result<demo::CapabilitiesResponse>::success(result);
            });
            LeaseServer lease(node, "lease/v1", [&](const auto& req) { return leases.acquire(req); });
            auto states = node.publisher<demo::StatePubSubType>("topics/state/v1", Qos::state());
            std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
            std::cout << "SERVER_READY " << epoch << std::endl;
            uint64_t sequence = 0;
            while (running) {
                demo::State state; state.sample_sequence(++sequence); state.position(0.25);
                auto status = states.publish(state);
                if (!status) std::cerr << error_name(status.code) << ": " << status.detail << '\n';
                std::this_thread::sleep_for(Milliseconds(20));
            }
        } else if (role == "watch") {
            CapClient capabilities(node, "capabilities/v1");
            if (!capabilities.wait_for_service(Milliseconds(8000))) return 1;
            demo::CapabilitiesRequest req; req.device("sim");
            auto initial = capabilities.async_call(req, Milliseconds(1000)).get();
            if (!initial) return 1;
            const std::string old_instance = initial.value->instance().c_str();
            std::cout << "WATCH_READY " << old_instance << std::endl;
            const auto until = std::chrono::steady_clock::now() + Milliseconds(35000);
            while (std::chrono::steady_clock::now() < until) {
                if (capabilities.wait_for_service(Milliseconds(100))) {
                    auto reply = capabilities.async_call(req, Milliseconds(500)).get();
                    if (reply && old_instance != reply.value->instance().c_str()) {
                        std::cout << "WATCH_RECOVERED " << reply.value->instance() << std::endl;
                        return 0;
                    }
                }
                std::this_thread::sleep_for(Milliseconds(50));
            }
            throw std::runtime_error("persistent client did not recover within 35 seconds");
        } else if (role == "client") {
            CapClient capabilities(node, "capabilities/v1");
            LeaseClient leases(node, "lease/v1");
            auto states = node.subscriber<demo::StatePubSubType>("topics/state/v1", Qos::state());
            auto ready = capabilities.wait_for_service(Milliseconds(8000));
            if (!ready) throw std::runtime_error(ready.detail);
            ready = leases.wait_for_service(Milliseconds(8000));
            if (!ready) throw std::runtime_error(ready.detail);
            demo::CapabilitiesRequest req; req.device("sim");
            auto cap = capabilities.async_call(req, Milliseconds(2000)).get();
            if (!cap || cap.value->joints() != 14) throw std::runtime_error("capabilities failed");
            demo::LeaseRequest acquire;
            acquire.owner("example_client"); acquire.operation_id("example_operation"); acquire.duration_ms(1000);
            auto first = leases.async_call(acquire, Milliseconds(2000)).get();
            auto duplicate = leases.async_call(acquire, Milliseconds(2000)).get();
            if (!first || !duplicate || first.value->lease_id() != duplicate.value->lease_id())
                throw std::runtime_error("lease idempotency failed");
            const auto until = std::chrono::steady_clock::now() + Milliseconds(3000);
            bool received = false;
            do {
                auto sample = states.take();
                if (sample) { received = true; break; }
                std::this_thread::sleep_for(Milliseconds(10));
            } while (std::chrono::steady_clock::now() < until);
            if (!received) throw std::runtime_error("no state received");
            std::cout << "CLIENT_OK instance=" << cap.value->instance() << " lease=" << first.value->lease_id() << std::endl;
        } else return 2;
    } catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
    return 0;
}
