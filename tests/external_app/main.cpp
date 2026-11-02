#include <rocos/comm/service.hpp>
#include <ApplicationPubSubTypes.hpp>
#include <iostream>

int main(int argc, char** argv) {
    using namespace rocos::comm;
    if (argc != 3) return 2;
    try {
        Node node({193, "independent_application", argv[2], {}});
        if (std::string(argv[1]) == "server") {
            ServiceServer<independent::AddRequestPubSubType, independent::AddResponsePubSubType> service(
                node, "add/v1", [](const independent::AddRequest& req) {
                    if (req.lhs() < 0 || req.rhs() < 0 || req.lhs() > 100000 || req.rhs() > 100000)
                        return Result<independent::AddResponse>::failure(Error::rejected, "arguments outside example range");
                    independent::AddResponse reply; reply.sum(req.lhs() + req.rhs());
                    return Result<independent::AddResponse>::success(reply);
                });
            auto qos = Qos::state(); qos.max_instances = 2;
            auto pub = node.publisher<independent::ReadingPubSubType>("topics/reading/v1", qos);
            for (int i = 0; i < 1500; ++i) {
                independent::Reading value; value.channel(i % 2); value.number(42);
                if (!pub.publish(value)) return 1;
                std::this_thread::sleep_for(Milliseconds(10));
            }
        } else {
            auto qos = Qos::state(); qos.max_instances = 2;
            auto sub = node.subscriber<independent::ReadingPubSubType>("topics/reading/v1", qos);
            ServiceClient<independent::AddRequestPubSubType, independent::AddResponsePubSubType> client(node, "add/v1");
            if (!client.wait_for_service(Milliseconds(5000))) return 1;
            independent::AddRequest req; req.lhs(20); req.rhs(22);
            auto result = client.async_call(req, Milliseconds(1000)).get();
            if (!result || result.value->sum() != 42) return 1;
            req.lhs(-1);
            auto rejected = client.async_call(req, Milliseconds(1000)).get();
            if (rejected.status.code != Error::rejected) return 1;
            auto until = std::chrono::steady_clock::now() + Milliseconds(2000);
            unsigned channels = 0;
            while (std::chrono::steady_clock::now() < until) {
                auto reading = sub.take();
                if (reading && reading.value->number() == 42 && reading.value->channel() < 2)
                    channels |= 1u << reading.value->channel();
                if (channels == 3) {
                    std::cout << "EXTERNAL_ACCEPTANCE_OK pubsub=42 service=42 failure=rejected\n";
                    return 0;
                }
                std::this_thread::sleep_for(Milliseconds(5));
            }
            return 1;
        }
    } catch (const std::exception& ex) { std::cerr << ex.what() << '\n'; return 1; }
    return 0;
}
