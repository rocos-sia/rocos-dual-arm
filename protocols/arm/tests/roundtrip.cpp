#include <rocos/arm/contracts.hpp>
#include <iostream>
#include <stdexcept>
#include <string>

namespace arm = rocos::arm::v1;
namespace contracts = rocos::arm::contracts;
namespace comm = rocos::comm;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
template<class Support>
typename Support::type roundtrip(const typename Support::type& input) {
    const auto encoded = comm::detail::encode<Support>(input);
    require(bool(encoded), "XCDR1 encoding failed or exceeds ServiceFrame payload bound");
    const auto decoded = comm::detail::decode<Support>(*encoded.value);
    require(bool(decoded), "XCDR1 decoding failed");
    require(*decoded.value == input, "XCDR1 roundtrip changed fields");
    return *decoded.value;
}
arm::Pose3D pose(double x) {
    arm::Pose3D value;
    value.position().x(x);
    value.orientation().w(1.0);
    return value;
}

int main() {
    try {
        // Defaults are represented by absence, never by an explicit numeric zero.
        arm::MoveJRequest joint;
        joint.control().token("test-token");
        joint.control().client_id("test-client");
        joint.joints().assign(arm::MAX_JOINTS, 0.25);
        require(!joint.limits().velocity().present(), "default velocity must be absent");
        roundtrip<contracts::MoveJ::RequestSupport>(joint);
        joint.limits().velocity().present(true);
        joint.limits().velocity().value(0.0); // must survive wire; controller rejects it
        auto explicit_zero = roundtrip<contracts::MoveJ::RequestSupport>(joint);
        require(explicit_zero.limits().velocity().present(), "explicit zero became default");

        arm::MoveCRequest circle;
        arm::CenterArc center;
        center.center_frame(pose(0.1));
        center.theta(1.57);
        circle.target().center(center);
        require(!roundtrip<contracts::MoveC::RequestSupport>(circle).pose_start().present(),
                "implicit current-flange start lost");
        arm::ThreePointArc points;
        points.pose_via(pose(0.2));
        points.pose_goal(pose(0.3));
        circle.target().three_point(points);
        circle.pose_start().present(true);
        circle.pose_start().value(pose(0.0));
        roundtrip<contracts::MoveC::RequestSupport>(circle);

        arm::StateResponse state;
        state.result().success(true);
        state.has_data(true);
        state.data().robot_state(arm::RobotState::STATE_STOPPED);
        state.data().work_mode(arm::WorkMode::WORK_POSITION);
        state.data().timestamp(1700000000.125);
        state.data().flange(pose(0.4));
        state.data().joint_states().resize(arm::MAX_JOINTS);
        for (const auto presence : {arm::Presence::FIELD_ABSENT, arm::Presence::FIELD_NULL, arm::Presence::FIELD_VALUE}) {
            state.data().tcp().presence(presence);
            state.data().tcp().value(pose(0.5));
            roundtrip<contracts::GetState::ResponseSupport>(state);
        }
        state.data().active_tool_package().presence(arm::Presence::FIELD_VALUE);
        auto& package = state.data().active_tool_package().value();
        package.name(std::string(64, 'n'));
        package.package_version(std::string(128, 'v'));
        package.digest(std::string(64, 'a'));
        package.urdf(std::string(1024, 'u'));
        package.revision(9007199254740990ULL);
        package.has_calibration_yaml(true);
        package.calibration_yaml(std::string(4096, 'c'));
        state.result().message(std::string(1024, 'm'));
        for (auto& item : state.data().joint_states()) item.name(std::string(64, 'j'));
        roundtrip<contracts::GetState::ResponseSupport>(state);

        arm::FramesResponse frames;
        frames.result().success(true);
        frames.result().message(std::string(1024, 'm'));
        frames.has_data(true);
        frames.data().path(std::string(1024, 'p'));
        frames.data().tool_frames().resize(arm::MAX_FRAMES);
        frames.data().object_frames().resize(arm::MAX_FRAMES);
        for (auto& name : frames.data().tool_frames()) name = std::string(64, 't');
        for (auto& name : frames.data().object_frames()) name = std::string(64, 'o');
        frames.data().active_tool_frame(std::string(64, 't'));
        frames.data().active_object_frame(std::string(64, 'o'));
        roundtrip<contracts::LoadFrames::ResponseSupport>(frames);

        arm::TcpResponse invalid_tcp;
        invalid_tcp.result().success(false);
        invalid_tcp.result().code(-2350);
        invalid_tcp.result().message("FK unavailable");
        invalid_tcp.has_data(false);
        auto error = roundtrip<contracts::GetTcp::ResponseSupport>(invalid_tcp);
        require(error.result().code() == -2350 && !error.has_data(), "business failure was lost");

        arm::ControlGrantResponse grant;
        grant.result().success(true);
        grant.has_data(true);
        grant.data().token(std::string(256, 't'));
        grant.data().client_id("session-1");
        grant.data().owner().has_owner(true);
        grant.data().owner().ttl_seconds(300);
        grant.data().owner().heartbeat_interval_seconds(5.0);
        roundtrip<contracts::AcquireControl::ResponseSupport>(grant);

        // Fast DDS-Gen 4.3.0 does NOT enforce this IDL sequence bound in this
        // codec path. Record the limitation: business ingress must validate it.
        joint.joints().resize(arm::MAX_JOINTS + 1);
        roundtrip<contracts::MoveJ::RequestSupport>(joint);
        // The framework's actual payload-byte bound is independent of IDL bounds.
        joint.joints().resize(9000);
        auto oversized = comm::detail::encode<contracts::MoveJ::RequestSupport>(joint);
        require(!oversized && oversized.status.code == comm::Error::serialization,
                "ServiceFrame byte bound was not enforced");
        require(std::string(contracts::MoveJ::name) == "robot/movej/v1", "service address changed");
        std::cout << "PASS arm XCDR1: optional/null fields, arc union, signed errors and payload sizes\n"
                  << "KNOWN LIMITATION: generated codec accepts over-bound sequences; validate at business ingress\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
