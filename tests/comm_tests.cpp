#include "../examples/robot/services.hpp"
#include <iostream>
#include <stdexcept>

using namespace rocos::comm;
using EchoClient = ServiceClient<demo::EchoRequestPubSubType, demo::EchoResponsePubSubType>;
using EchoServer = ServiceServer<demo::EchoRequestPubSubType, demo::EchoResponsePubSubType>;
using Clock = std::chrono::steady_clock;
void require(bool ok, const std::string& message) { if (!ok) throw std::runtime_error(message); }
template<class F> void eventually(F condition, const std::string& message, int ms = 5000) {
    const auto until = Clock::now() + Milliseconds(ms);
    do {
        if (condition()) return;
        std::this_thread::sleep_for(Milliseconds(5));
    } while (Clock::now() < until);
    throw std::runtime_error(message);
}
demo::EchoRequest request(int value, unsigned delay = 0) {
    demo::EchoRequest req; req.value(value); req.delay_ms(delay); return req;
}
Result<demo::EchoResponse> echo(const demo::EchoRequest& req) {
    if (req.value() == -1) throw std::runtime_error("test handler failure");
    if (req.value() == -2) return Result<demo::EchoResponse>::failure(Error::rejected, "business rejection");
    std::this_thread::sleep_for(Milliseconds(req.delay_ms()));
    demo::EchoResponse response; response.value(req.value());
    return Result<demo::EchoResponse>::success(response);
}

void pubsub(Node& server, Node& client) {
    auto pub = server.publisher<demo::StatePubSubType>("topics/position/v1");
    auto sub = client.subscriber<demo::StatePubSubType>("topics/position/v1");
    require(pub.wait_for_subscribers(Milliseconds(5000)), "pubsub discovery");
    demo::State value; value.sample_sequence(7); value.position(1.25);
    require(bool(pub.publish(value)), "publish failed");
    eventually([&] { auto r = sub.take(); return r && r.value->sample_sequence() == 7 && r.value->position() == 1.25; }, "sample missing");
    auto bad_pub = server.publisher<demo::StatePubSubType>("topics/mismatch/v1", Qos::state());
    auto bad_sub = client.subscriber<demo::StatePubSubType>("topics/mismatch/v1", Qos::command());
    eventually([&] { return bad_pub.diagnostics().incompatible_qos > 0 && bad_sub.diagnostics().incompatible_qos > 0; }, "incompatible QoS not diagnosed");
    require(bad_pub.diagnostics().matched == 0, "incompatible endpoints matched");
    bool rejected = false;
    try { auto wrong = server.publisher<demo::EchoRequestPubSubType>("topics/position/v1"); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected, "same topic accepted a different type");
    std::cout << "PASS pubsub and QoS diagnostics\n";
}

void services(Node& server, Node& a, Node& b) {
    EchoServer service(server, "echo/v1", echo, {128, 128});
    EchoClient client_a(a, "echo/v1"), client_b(b, "echo/v1");
    require(bool(client_a.wait_for_service(Milliseconds(5000))), "client A readiness");
    require(bool(client_b.wait_for_service(Milliseconds(5000))), "client B readiness");
    std::vector<std::future<Result<demo::EchoResponse>>> calls_a, calls_b;
    std::thread ta([&] { for (int i = 0; i < 16; ++i) calls_a.push_back(client_a.async_call(request(i), Milliseconds(5000))); });
    std::thread tb([&] { for (int i = 0; i < 16; ++i) calls_b.push_back(client_b.async_call(request(100+i), Milliseconds(5000))); });
    ta.join(); tb.join();
    for (int i = 0; i < 16; ++i) {
        auto ra = calls_a[i].get(); auto rb = calls_b[i].get();
        require(ra && ra.value->value() == i, "A response correlation failed");
        require(rb && rb.value->value() == 100+i, "B response correlation failed");
    }
    auto exception = client_a.async_call(request(-1), Milliseconds(1000)).get();
    require(exception.status.code == Error::handler_error, "handler exception escaped");
    auto rejection = client_a.async_call(request(-2), Milliseconds(1000)).get();
    require(rejection.status.code == Error::rejected, "business rejection lost");
    auto timeout = client_a.async_call(request(50, 200), Milliseconds(30)).get();
    require(timeout.status.code == Error::timeout, "slow call did not time out");
    require(client_a.metrics().pending == 0, "timed-out request leaked");
    auto later = client_a.async_call(request(51), Milliseconds(1000)).get();
    require(later && later.value->value() == 51, "late response completed a different request");
    require(client_a.metrics().ignored_replies > 0, "late/foreign reply not tracked");
    auto invalid = client_a.async_call(request(1), Milliseconds(0)).get();
    require(invalid.status.code == Error::invalid_argument, "zero timeout accepted");
    EchoClient absent(a, "absent/v1");
    require(absent.wait_for_service(Milliseconds(150)).code == Error::unavailable, "missing server readiness");
    require(absent.async_call(request(1), Milliseconds(100)).get().status.code == Error::unavailable, "missing server call");
    demo_app::CapClient wrong_type(a, "echo/v1");
    require(wrong_type.wait_for_service(Milliseconds(3000)).code == Error::incompatible_type, "service type mismatch not diagnosed");
    std::cout << "PASS multi-client concurrency, errors, timeout cleanup and late replies\n";
}

void backpressure(Node& server, Node& client) {
    std::atomic<int> entered{0};
    EchoServer bounded(server, "bounded/v1", [&](const auto& req) { ++entered; return echo(req); }, {1, 8});
    EchoClient calls(client, "bounded/v1");
    require(bool(calls.wait_for_service(Milliseconds(5000))), "bounded readiness");
    auto slow = calls.async_call(request(1, 250), Milliseconds(1500));
    eventually([&] { return entered.load() == 1; }, "handler not started");
    std::vector<std::future<Result<demo::EchoResponse>>> burst;
    for (int i = 0; i < 8; ++i) burst.push_back(calls.async_call(request(2+i), Milliseconds(1500)));
    int busy = 0;
    for (auto& call : burst) { auto r = call.get(); if (r.status.code == Error::busy) ++busy; else require(bool(r), "unexpected bounded result"); }
    require(bool(slow.get()) && busy > 0, "queue did not reject overflow");
    EchoClient limited(client, "bounded/v1", {1, Milliseconds(100), Milliseconds(500)});
    require(bool(limited.wait_for_service(Milliseconds(5000))), "limited readiness");
    auto first = limited.async_call(request(20, 100), Milliseconds(1000));
    auto second = limited.async_call(request(21), Milliseconds(1000)).get();
    require(second.status.code == Error::busy, "pending call limit not enforced");
    require(bool(first.get()), "pending call failed");
    std::cout << "PASS server queue and client pending bounds\n";
}

void restart(Node& server, Node& client) {
    auto active = std::make_unique<EchoServer>(server, "restart/v1", echo);
    EchoClient calls(client, "restart/v1");
    require(bool(calls.wait_for_service(Milliseconds(5000))), "restart initial readiness");
    auto before = calls.async_call(request(3), Milliseconds(1000)).get();
    require(bool(before), "before restart failed");
    auto old_id = active->instance_id();
    active.reset();
    eventually([&] { return calls.wait_for_service(Milliseconds(0)).code == Error::unavailable; }, "dead server remained ready");
    active = std::make_unique<EchoServer>(server, "restart/v1", echo);
    require(active->instance_id() != old_id, "server epoch reused");
    eventually([&] { return bool(calls.wait_for_service(Milliseconds(0))); }, "client did not recover after restart");
    auto after = calls.async_call(request(4), Milliseconds(1000)).get();
    require(after && after.value->value() == 4, "after restart failed");
    {
        EchoServer duplicate(server, "restart/v1", echo);
        eventually([&] { return calls.wait_for_service(Milliseconds(0)).code == Error::conflict; }, "duplicate servers not detected");
        require(calls.async_call(request(5), Milliseconds(1000)).get().status.code == Error::conflict, "ambiguous call sent");
    }
    std::cout << "PASS server restart and duplicate-service conflict\n";
}

void dedup(Node& server, Node& client) {
    std::atomic<int> executions{0};
    EchoServer service(server, "dedup/v1", [&](const auto& req) { ++executions; return echo(req); });
    auto publisher = client.publisher<detail::FrameType>("services/dedup/v1/request");
    auto subscriber = client.subscriber<detail::FrameType>("services/dedup/v1/reply");
    require(publisher.wait_for_subscribers(Milliseconds(5000)), "raw request discovery");
    eventually([&] { return subscriber.diagnostics().matched == 1; }, "raw reply discovery");
    detail::Frame frame;
    frame.protocol_version(1); frame.kind(2); frame.client_id("raw_test_client"); frame.request_id(100);
    frame.server_id(service.instance_id());
    frame.contract(detail::contract_name<demo::EchoRequestPubSubType, demo::EchoResponsePubSubType>());
    frame.payload(*detail::encode<demo::EchoRequestPubSubType>(request(8, 80)).value);
    require(bool(publisher.publish(frame)), "raw publish");
    require(bool(publisher.publish(frame)), "duplicate raw publish");
    eventually([&] { auto r = subscriber.take(); return r && r.value->status() == 0; }, "first duplicate response");
    require(bool(publisher.publish(frame)), "cached raw publish");
    eventually([&] { auto r = subscriber.take(); return r && r.value->status() == 0; }, "cached response");
    require(executions == 1, "duplicate transport request executed twice");
    frame.payload(*detail::encode<demo::EchoRequestPubSubType>(request(9)).value);
    publisher.publish(frame);
    eventually([&] { auto r = subscriber.take(); return r && r.value->status() == static_cast<uint32_t>(Error::conflict); }, "same ID changed payload not rejected");
    require(executions == 1, "changed duplicate executed");
    std::cout << "PASS inflight/cached transport deduplication\n";
}

void lease(Node& server, Node& client) {
    demo_app::LeaseManager manager;
    demo_app::LeaseServer service(server, "lease/v1", [&](const auto& req) { return manager.acquire(req); });
    demo_app::LeaseClient calls(client, "lease/v1");
    require(bool(calls.wait_for_service(Milliseconds(5000))), "lease readiness");
    demo::LeaseRequest req; req.owner("a"); req.operation_id("op1"); req.duration_ms(250);
    auto first = calls.async_call(req, Milliseconds(1000)).get();
    auto duplicate = calls.async_call(req, Milliseconds(1000)).get();
    require(first && duplicate && first.value->lease_id() == duplicate.value->lease_id(), "business deduplication");
    req.duration_ms(300);
    require(calls.async_call(req, Milliseconds(1000)).get().status.code == Error::conflict, "lease changed duplicate");
    req.owner("b"); req.operation_id("op2");
    require(calls.async_call(req, Milliseconds(1000)).get().status.code == Error::rejected, "concurrent owner accepted");
    std::this_thread::sleep_for(Milliseconds(270));
    auto next = calls.async_call(req, Milliseconds(1000)).get();
    require(next && next.value->generation() == first.value->generation()+1, "expired lease not reassigned");
    std::cout << "PASS lease exclusivity, idempotency and expiry\n";
}

void shutdown(Node& server, Node& client) {
    EchoServer service(server, "shutdown/v1", echo);
    auto calls = std::make_unique<EchoClient>(client, "shutdown/v1");
    require(bool(calls->wait_for_service(Milliseconds(5000))), "shutdown readiness");
    auto pending = calls->async_call(request(1, 200), Milliseconds(2000));
    calls.reset();
    require(pending.get().status.code == Error::closed, "client shutdown stranded promise");
    std::cout << "PASS pending future closed on destruction\n";
}
int main() {
    try {
        bool invalid_config = false;
        try { NodeConfig bad; bad.discovery_lease = bad.announcement_period; Node invalid(bad); }
        catch (const std::invalid_argument&) { invalid_config = true; }
        require(invalid_config, "invalid discovery lease accepted");
        const auto ns = "test_" + detail::new_identity();
        Node server({191, "server", ns, {}}), a({191, "client_a", ns, {}}), b({191, "client_b", ns, {}});
        pubsub(server, a);
        services(server, a, b);
        backpressure(server, a);
        restart(server, a);
        dedup(server, a);
        lease(server, a);
        shutdown(server, a);
        std::cout << "ALL TESTS PASSED\n";
    } catch (const std::exception& ex) { std::cerr << "FAIL " << ex.what() << '\n'; return 1; }
}
