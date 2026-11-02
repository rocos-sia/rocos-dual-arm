#pragma once
#include <ArmPubSubTypes.hpp>
#include <rocos/comm/service.hpp>

namespace rocos::arm::contracts {
namespace wire = rocos::arm::v1;

// Pass name to ServiceClient/ServiceServer; the framework adds services/ and
// /request or /reply. One Node namespace identifies one arm.
#define ROCOS_ARM_SERVICE(Name, Request, Response, Address) \
    struct Name { \
        using RequestSupport = wire::Request##PubSubType; \
        using ResponseSupport = wire::Response##PubSubType; \
        static constexpr const char* name = Address; \
    };

ROCOS_ARM_SERVICE(GetState, EmptyRequest, StateResponse, "robot/state/v1")
ROCOS_ARM_SERVICE(GetEnabled, EmptyRequest, EnabledResponse, "robot/enabled/v1")
ROCOS_ARM_SERVICE(GetTcp, TcpRequest, TcpResponse, "robot/tcp/v1")
ROCOS_ARM_SERVICE(GetImpedance, EmptyRequest, ImpedanceResponse, "robot/impedance/v1")
ROCOS_ARM_SERVICE(AcquireControl, AcquireControlRequest, ControlGrantResponse, "control/acquire/v1")
ROCOS_ARM_SERVICE(TakeoverControl, TakeoverControlRequest, ControlGrantResponse, "control/takeover/v1")
ROCOS_ARM_SERVICE(ReleaseControl, AuthorizedRequest, ResultResponse, "control/release/v1")
ROCOS_ARM_SERVICE(GetControlStatus, EmptyRequest, ControlStatusResponse, "control/status/v1")
ROCOS_ARM_SERVICE(Heartbeat, AuthorizedRequest, ControlStatusResponse, "control/heartbeat/v1")
ROCOS_ARM_SERVICE(Enable, AuthorizedRequest, ResultResponse, "robot/enable/v1")
ROCOS_ARM_SERVICE(Disable, AuthorizedRequest, ResultResponse, "robot/disable/v1")
ROCOS_ARM_SERVICE(Reset, AuthorizedRequest, ResetResponse, "robot/reset/v1")
ROCOS_ARM_SERVICE(SetWorkMode, SetWorkModeRequest, ResultResponse, "robot/workmode/v1")
ROCOS_ARM_SERVICE(MoveJ, MoveJRequest, MotionResponse, "robot/movej/v1")
ROCOS_ARM_SERVICE(MoveJIk, MoveJIkRequest, MotionResponse, "robot/movej_ik/v1")
ROCOS_ARM_SERVICE(MoveL, MoveLRequest, MotionResponse, "robot/movel/v1")
ROCOS_ARM_SERVICE(MoveLFk, MoveLFkRequest, MotionResponse, "robot/movel_fk/v1")
ROCOS_ARM_SERVICE(MoveC, MoveCRequest, MotionResponse, "robot/movec/v1")
ROCOS_ARM_SERVICE(Pause, AuthorizedRequest, MotionResponse, "robot/pause/v1")
ROCOS_ARM_SERVICE(Resume, AuthorizedRequest, MotionResponse, "robot/resume/v1")
ROCOS_ARM_SERVICE(Stop, AuthorizedRequest, MotionResponse, "robot/stop/v1")
ROCOS_ARM_SERVICE(GetMotionStatus, EmptyRequest, MotionStatusResponse, "robot/move_status/v1")
ROCOS_ARM_SERVICE(SetServoMode, ServoModeRequest, MotionResponse, "robot/servo/mode/v1")
ROCOS_ARM_SERVICE(StartServo, ServoStartRequest, MotionResponse, "robot/servo/start/v1")
ROCOS_ARM_SERVICE(StopServo, AuthorizedRequest, MotionResponse, "robot/servo/stop/v1")
ROCOS_ARM_SERVICE(StartDrag, DragStartRequest, ResultResponse, "robot/drag/start/v1")
ROCOS_ARM_SERVICE(StopDrag, AuthorizedRequest, ResultResponse, "robot/drag/stop/v1")
ROCOS_ARM_SERVICE(JogJoint, JointJogRequest, MotionResponse, "robot/jog/joint/v1")
ROCOS_ARM_SERVICE(JogCartesian, CartesianJogRequest, MotionResponse, "robot/jog/cartesian/v1")
ROCOS_ARM_SERVICE(JogNullspace, JointJogRequest, MotionResponse, "robot/jog/nullspace/v1")
ROCOS_ARM_SERVICE(JogSvd, SvdJogRequest, MotionResponse, "robot/jog/svd/v1")
ROCOS_ARM_SERVICE(StopJog, AuthorizedRequest, MotionResponse, "robot/jog/stop/v1")
ROCOS_ARM_SERVICE(GetLoad, EmptyRequest, LoadResponse, "robot/load_params/get/v1")
ROCOS_ARM_SERVICE(SetLoad, SetLoadRequest, SetLoadResponse, "robot/load_params/set/v1")
ROCOS_ARM_SERVICE(SaveLoad, PathRequest, PathResponse, "robot/load_params/save/v1")
ROCOS_ARM_SERVICE(ListToolFrames, EmptyRequest, ToolListResponse, "robot/tool_frames/v1")
ROCOS_ARM_SERVICE(ListObjectFrames, EmptyRequest, FrameListResponse, "robot/object_frames/v1")
ROCOS_ARM_SERVICE(GetToolFrame, NameRequest, NamedFrameResponse, "robot/tool_frame/get/v1")
ROCOS_ARM_SERVICE(SetToolFrame, SetToolFrameRequest, SetToolFrameResponse, "robot/tool_frame/set/v1")
ROCOS_ARM_SERVICE(RemoveToolFrame, ToolSelectionRequest, ToolSelectionResponse, "robot/tool_frame/remove/v1")
ROCOS_ARM_SERVICE(ActivateToolFrame, ToolSelectionRequest, ToolSelectionResponse, "robot/active_tool_frame/v1")
ROCOS_ARM_SERVICE(GetObjectFrame, NameRequest, NamedFrameResponse, "robot/object_frame/get/v1")
ROCOS_ARM_SERVICE(SetObjectFrame, SetObjectFrameRequest, NamedFrameResponse, "robot/object_frame/set/v1")
ROCOS_ARM_SERVICE(RemoveObjectFrame, AuthorizedNameRequest, ResultResponse, "robot/object_frame/remove/v1")
ROCOS_ARM_SERVICE(ActivateObjectFrame, AuthorizedNameRequest, ResultResponse, "robot/active_object_frame/v1")
ROCOS_ARM_SERVICE(LoadFrames, PathRequest, FramesResponse, "robot/frames/load/v1")
ROCOS_ARM_SERVICE(SaveFrames, PathRequest, FramesResponse, "robot/frames/save/v1")
#undef ROCOS_ARM_SERVICE

template<class Contract>
using Client = rocos::comm::ServiceClient<typename Contract::RequestSupport, typename Contract::ResponseSupport>;
template<class Contract>
using Server = rocos::comm::ServiceServer<typename Contract::RequestSupport, typename Contract::ResponseSupport>;

struct StateTopic {
    using Support = wire::StateSamplePubSubType;
    static constexpr const char* name = "topics/robot/state/v1";
    static rocos::comm::Qos qos() { return rocos::comm::Qos::state(); }
};
} // namespace rocos::arm::contracts
