#pragma once
#include <CameraPubSubTypes.hpp>
#include <rocos/comm/service.hpp>
namespace rocos::camera {
namespace wire = v1;
namespace contracts {
#define ROCOS_CAMERA_SERVICE(Name, Request, Response, Address) \
struct Name { \
    using RequestSupport = wire::Request##PubSubType; \
    using ResponseSupport = wire::Response##PubSubType; \
    static constexpr const char* name = Address; \
};
ROCOS_CAMERA_SERVICE(GetCapabilities, EmptyRequest, CapabilitiesResponse, "camera/capabilities/v1")
ROCOS_CAMERA_SERVICE(GetStatus, EmptyRequest, StatusResponse, "camera/status/v1")
ROCOS_CAMERA_SERVICE(GetConfiguration, EmptyRequest, ConfigurationResponse, "camera/configuration/get/v1")
ROCOS_CAMERA_SERVICE(Configure, ConfigureRequest, ConfigurationResponse, "camera/configuration/set/v1")
ROCOS_CAMERA_SERVICE(GetCalibration, CalibrationRequest, CalibrationResponse, "camera/calibration/v1")
ROCOS_CAMERA_SERVICE(Start, LifecycleRequest, StatusResponse, "camera/start/v1")
ROCOS_CAMERA_SERVICE(Stop, LifecycleRequest, StatusResponse, "camera/stop/v1")
#undef ROCOS_CAMERA_SERVICE
inline constexpr auto color_topic = "topics/camera/color/v1";
inline constexpr auto depth_topic = "topics/camera/depth/v1";
inline constexpr auto frameset_topic = "topics/camera/frameset/v1";
inline constexpr auto status_topic = "topics/camera/status/v1";
inline rocos::comm::Qos image_qos() { return {false, 4, rocos::comm::Milliseconds(0)}; }
inline rocos::comm::Qos status_qos() { return rocos::comm::Qos::state(); }
template<class Contract> using Client = rocos::comm::ServiceClient<typename Contract::RequestSupport, typename Contract::ResponseSupport>;
template<class Contract> using Server = rocos::comm::ServiceServer<typename Contract::RequestSupport, typename Contract::ResponseSupport>;
} // namespace contracts
} // namespace rocos::camera
