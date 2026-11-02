#include <rocos/camera/contracts.hpp>
#include <iostream>
#include <stdexcept>
namespace w = rocos::camera::v1;
namespace comm = rocos::comm;
namespace api = rocos::camera::contracts;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
template<class Support> typename Support::type roundtrip(const typename Support::type& input) {
    Support support;
    const auto representation=eprosima::fastdds::dds::XCDR_DATA_REPRESENTATION;
    eprosima::fastdds::rtps::SerializedPayload_t payload(support.calculate_serialized_size(&input,representation));
    require(support.serialize(&input,payload,representation),"serialize failed");
    typename Support::type output;
    require(support.deserialize(payload,&output),"deserialize failed");
    require(output==input,"wire fields changed");
    return output;
}
w::StreamProfile profile(w::StreamKind kind) {
    w::StreamProfile p;
    p.profile_id(kind==w::StreamKind::STREAM_COLOR ? "rgb_640_480_30" : "depth_640_480_30");
    p.stream(kind); p.width(640); p.height(480); p.fps(30);
    p.format(kind==w::StreamKind::STREAM_COLOR ? w::PixelFormat::FORMAT_RGB8 : w::PixelFormat::FORMAT_Z16_LE);
    return p;
}
int main() {
    try {
        w::ConfigureRequest request;
        request.control().instance_id("camera-instance-1");
        request.control().expected_control_revision(7);
        request.configuration().color(profile(w::StreamKind::STREAM_COLOR));
        request.configuration().depth(profile(w::StreamKind::STREAM_DEPTH));
        auto encoded=comm::detail::encode<api::Configure::RequestSupport>(request);
        require(bool(encoded),"configuration does not fit ServiceFrame");
        auto decoded_request=comm::detail::decode<api::Configure::RequestSupport>(*encoded.value);
        require(bool(decoded_request) && *decoded_request.value==request,"ServiceFrame configuration roundtrip failed");

        w::CalibrationResponse calibration;
        calibration.result().code(w::BusinessCode::CAMERA_OK); calibration.has_data(true);
        auto& c=calibration.data();
        c.instance_id("camera-instance-1"); c.configuration_revision(2); c.calibration_revision(3); c.depth_scale_m(0.0005);
        for (auto* intr : {&c.color(),&c.depth()}) {
            intr->width(640); intr->height(480); intr->fx(600); intr->fy(601); intr->cx(320); intr->cy(240);
            intr->distortion(w::DistortionModel::DISTORTION_BROWN_CONRADY);
            intr->coefficients({0.01,-0.02,0.003,0.004,0.05});
        }
        c.color().profile_id("rgb_640_480_30"); c.color().optical_frame_id("color_optical");
        c.depth().profile_id("depth_640_480_30"); c.depth().optical_frame_id("depth_optical");
        c.depth_to_color().source_frame("depth_optical"); c.depth_to_color().target_frame("color_optical");
        // Non-symmetric rotation catches row/column order changes on the wire.
        c.depth_to_color().rotation({0,-1,0,1,0,0,0,0,1});
        c.depth_to_color().translation_m({0.015,0.001,-0.002});
        auto calbytes=comm::detail::encode<api::GetCalibration::ResponseSupport>(calibration);
        require(bool(calbytes),"calibration exceeds service payload");
        roundtrip<api::GetCalibration::ResponseSupport>(calibration);

        w::ImageFrame depth;
        auto& h=depth.header();
        h.instance_id("camera-instance-1"); h.capture_session("session-1");
        h.configuration_revision(2); h.calibration_revision(3); h.profile_id("depth_640_480_30");
        h.optical_frame_id("depth_optical"); h.stream(w::StreamKind::STREAM_DEPTH);
        h.frame_sequence(4); h.source_frame_number(102);
        h.captured_at().timestamp_ns(123456789); h.captured_at().domain(w::ClockDomain::CLOCK_DEVICE);
        h.captured_at().clock_id("device-depth-clock-1"); h.received_monotonic_ns(2000000000);
        depth.width(640); depth.height(480); depth.stride_bytes(1284); // include row padding
        depth.format(w::PixelFormat::FORMAT_Z16_LE); depth.depth_scale_m(c.depth_scale_m());
        depth.pixels().resize(1284*480,0);
        depth.pixels()[0]=0xd0; depth.pixels()[1]=0x07; // LE raw 2000 -> 1 meter
        auto decoded=roundtrip<w::ImageFramePubSubType>(depth);
        unsigned raw=decoded.pixels()[0] | (unsigned(decoded.pixels()[1])<<8);
        require(raw*decoded.depth_scale_m()==1.0,"depth scale or endianness changed");
        require(decoded.pixels()[2]==0 && decoded.pixels()[3]==0,"invalid depth sentinel changed");
        require(!comm::detail::encode<w::ImageFramePubSubType>(depth),"large image unexpectedly fits ServiceFrame");

        w::ImageFrame color=depth;
        color.header().stream(w::StreamKind::STREAM_COLOR); color.header().profile_id("rgb_640_480_30");
        color.header().optical_frame_id("color_optical"); color.header().frame_sequence(5);
        color.format(w::PixelFormat::FORMAT_RGB8); color.stride_bytes(1920); color.depth_scale_m(0);
        color.pixels().assign(1920*480,127);
        roundtrip<w::ImageFramePubSubType>(color);

        w::FrameSet set;
        set.instance_id(h.instance_id()); set.capture_session(h.capture_session());
        set.configuration_revision(h.configuration_revision()); set.calibration_revision(h.calibration_revision());
        set.set_sequence(3); set.color_sequence(5); set.depth_sequence(4); set.pairing(w::PairingMethod::PAIRING_SDK_GROUPED);
        roundtrip<w::FrameSetPubSubType>(set);

        w::CameraStatus status;
        status.instance_id("camera-instance-1"); status.device_id("d435-test-serial"); status.status_sequence(1);
        status.control_revision(9); status.configured(true); status.configuration_revision(2); status.calibration_revision(3);
        status.capture_session("session-1"); status.state(w::DeviceState::DEVICE_STREAMING);
        roundtrip<w::CameraStatusPubSubType>(status);
        status.status_sequence(2); status.control_revision(10); status.state(w::DeviceState::DEVICE_DISCONNECTED);
        status.configured(false); status.capture_session(""); status.configuration_revision(3); status.calibration_revision(0);
        status.last_error().code(w::BusinessCode::CAMERA_DISCONNECTED); status.last_error().message("device unplugged");
        roundtrip<w::CameraStatusPubSubType>(status);

        w::CapabilitiesResponse caps;
        caps.result().code(w::BusinessCode::CAMERA_OK); caps.has_data(true);
        caps.data().instance_id(std::string(64,'i')); caps.data().control_revision(7);
        caps.result().message(std::string(512,'m')); caps.data().device_id(std::string(64,'d'));
        caps.data().model(std::string(128,'m')); caps.data().firmware(std::string(128,'f'));
        caps.data().image_capacity_bytes(w::MAX_IMAGE_BYTES);
        caps.data().profiles().resize(w::MAX_PROFILES,profile(w::StreamKind::STREAM_COLOR));
        for (auto& p : caps.data().profiles()) p.profile_id(std::string(64,'p'));
        require(bool(comm::detail::encode<api::GetCapabilities::ResponseSupport>(caps)),"maximum capability response exceeds 64 KiB");
        roundtrip<api::GetCapabilities::ResponseSupport>(caps);
        std::cout << "PASS camera IDL: XCDR1 RGB/Z16, byte order, scale, padding, calibration, pairing references, revisions and disconnect state\n";
    } catch (const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
