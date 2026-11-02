# D435 / RGB-D 相机 IDL v1 草案

本轮交付仅包含 **IDL、地址/类型目录、通信语义及编解码验证**。设备假设为用户指定的 RealSense D435，连接在其他设备上。本目录不实现采集服务、RealSense 适配器、帧配对器或独立消费应用；不声称完成真机、网络图像传输或断连行为验收。

协议目标：后续独立应用无需依赖 RealSense SDK，即可解释原始 RGB-D 数据、关联准确标定，并识别缺帧、配置变化和设备断连。本轮把这些要求定义为契约，实际运行验收留给接入阶段。

类型位于 `rocos::camera::v1`，独立于 `comm/` 和机械臂协议。唯一类型来源：[Camera.idl](v1/Camera.idl)；集中接口目录：[contracts.hpp](include/rocos/camera/contracts.hpp)。类型及文档均为待评审草案，不是已冻结 ABI。

## 范围

- 原始、未压缩 RGB8 彩色图和 Z16_LE 深度图；这里“原始”指未对齐、未滤波的像素流，不指 Bayer RAW。采集端负责将设备输出明确转换为声明的格式。
- 彩色和深度帧配对描述、标定查询、状态与能力查询、配置及启停。
- 深度对齐、点云、红外、IMU、图像压缩、录制、曝光/激光参数、手眼标定、硬件同步控制均不在本轮范围。
- D435 分辨率、帧率和组合由设备能力与 Configure 验证决定，不根据型号名写死。640×480@30 仅为线格式测试数据，不是本机实测支持列表。

## Topic 与 Service

每台相机一个部署 namespace，例如 `cell_a/cameras/wrist_left`，通过配置绑定物理序列号。不要使用 USB 枚举顺序作为持久设备 ID。

| Topic 相对名称 | 类型 | 建议 QoS |
|---|---|---|
| topics/camera/color/v1 | ImageFrame | best effort / keep last 4 / volatile |
| topics/camera/depth/v1 | ImageFrame | 同上 |
| topics/camera/frameset/v1 | FrameSet | 同上；完整配对允许因丢帧失败 |
| topics/camera/status/v1 | CameraStatus | best effort / keep last 1 / volatile；100 ms 周期心跳 |

上述 QoS 是本草案的起始配置，不是带宽和延迟性能保证。完整地址由框架添加 `rocos/v1/<namespace>/`。同一 namespace 每种 Topic 只能有一个逻辑发布者；检测到多个源时消费端必须报冲突，不合并数据。

| Service 名称 | 相对地址 | 请求 → 响应 |
|---|---|---|
| GetCapabilities | camera/capabilities/v1 | EmptyRequest → CapabilitiesResponse |
| GetStatus | camera/status/v1 | EmptyRequest → StatusResponse |
| GetConfiguration | camera/configuration/get/v1 | EmptyRequest → ConfigurationResponse |
| Configure | camera/configuration/set/v1 | ConfigureRequest → ConfigurationResponse |
| GetCalibration | camera/calibration/v1 | CalibrationRequest → CalibrationResponse |
| Start | camera/start/v1 | LifecycleRequest → StatusResponse |
| Stop | camera/stop/v1 | LifecycleRequest → StatusResponse |

服务请求/响应地址由框架添加 `services/` 和 `/request`、`/reply`。只读请求不改变采集或设备配置。`EmptyRequest.reserved` 必须为 0。

## 设备状态和控制版本

`instance_id` 为采集服务进程实例，每次重启生成新值。`device_id` 为实际或部署指定的相机序列号；不能用逻辑 namespace 冒充物理标识。`status_sequence` 在同一实例内从 1 单调递增，包括周期状态发布；旧状态序号不得覆盖新状态。

| 字段 | 变化条件及用途 |
|---|---|
| control_revision | 初始为 1；每次成功 Configure/Start/Stop、连接/断连/故障状态转换递增。用于所有写操作的并发与迟到请求校验 |
| configuration_revision | 初始为 0；成功配置或已有配置失效时递增，失败且未改变有效配置时不变 |
| calibration_revision | 初始为 0；每次产生新的标定快照递增，快照不可原地修改；无可用标定时为 0 |
| capture_session | 每次成功 Start 生成新值；STOPPED/DISCONNECTED/FAULT 时为空，旧会话不得继续发布图像 |

calibration_revision=0 只是不可用标记，不重置内部版本计数器。计数器不复用、不回绕；接近溢出时必须结束实例并更换 instance_id。`configured=false` 时 ConfigurationData.configuration 无效；不能把默认全零配置当成设备配置。

所有写请求携带 `{instance_id, expected_control_revision}`。服务端须在一个共享设备锁/仲裁域内完成“检查版本 → 检查状态 → 修改 → 递增版本”，而不是为三个 Service 分别独立加锁。只核对 configuration_revision 不足以保护生命周期：旧 Stop 可能误停使用相同配置的新会话，因此单独定义 control_revision。

- Configure：只允许连接正常且 STOPPED；color/depth 必须分别是能力列表中的精确 profile（ID 与字段都匹配），并验证整组同时开启的可行性。不能将两个单独支持的 profile 推断为组合可用，也不能静默降级分辨率/帧率。成功提交配置与对应标定，递增版本。
- Start：只允许已配置且 STOPPED。实际打开的流必须匹配已配置的 profile，核实标定/深度比例；若不一致，不发布图像并返回失败。准备完毕后建立新 capture_session，进入 STREAMING。
- Stop：结束当前采集并清理待发布帧后再返回成功；进入 STOPPED。已经 STOPPED 的 Stop 返回 INVALID_STATE，不生成新的成功副作用。
- 断连：撤销会话，configured=false，旧配置失效，calibration_revision=0，进入 DISCONNECTED；重连进入未配置的 STOPPED，重新读取能力并显式 Configure/Start，不自动恢复采集。
- 后端错误：能确认未改变有效状态时保持原状并返回失败；若硬件状态不能确认，进入 FAULT、撤销会话/配置并递增控制版本，要求调用者重新查询，不能报告仍在正常采集。

成功返回的是实际生效状态/配置，不是仅入队受理。具体调用超时由客户端指定；Service timeout 不能撤销已经开始的操作，也不能证明未生效。超时后查询状态和版本再决策，不自动重新提交写操作。CAS 版本只防止并发覆盖和迟到写入，不提供调用者权限控制；第一版由部署指定一个管理方，其他应用共享只读订阅。

## 图像解释

`FrameHeader` 唯一标识为 `(instance_id, capture_session, stream, frame_sequence)`。frame_sequence 在每个会话、每条流内从 1 单调递增；`source_frame_number` 保留 SDK 帧号，可因 SDK frameset 重复使用某帧而重复，不能拿它替代传输标识。

| 字段 | 契约 |
|---|---|
| width / height | 像素尺寸，均 >0，与 profile 和对应标定一致 |
| stride_bytes | 包含行填充的字节跨度；RGB8 ≥width×3，Z16_LE ≥width×2 |
| pixels | 长度必须恰为 stride_bytes×height；计算先检查溢出和容量。像素从左上到右下按行排列 |
| RGB8 | 每像素按 R、G、B 三字节排列；depth_scale_m 必须为 0 |
| Z16_LE | 每像素两个字节，小端无符号整数，和 CDR 封装字节序无关；深度米值 = raw×depth_scale_m，raw=0 为无效深度 |
| depth_scale_m | Z16 必须有限且 >0，须与所引用 Calibration 一致；不得固定假设 0.001 |
| optical_frame_id | 图像对应的光学坐标系；相机 x 向右、y 向下、z 向前，距离单位米 |

profile_id 是设备适配层分配的不透明标识，查询后原样传回，不直接使用 librealsense 的枚举整数。相同物理设备重连后应重新获取能力。Capabilities.profiles 枚举本协议 RGB8/Z16 范围内的单流候选列表，不包含红外、压缩等范围外格式。型号/固件信息读取不到时可为空，device_id 必须有效。该列表**不表示任意两项可组合**；组合可行性由 Configure 明确成功或拒绝。

## 时钟与帧配对

`CaptureTime.timestamp_ns` 是 SDK 帧时间转换成整数纳秒后的数值，并不因此提高时钟精度，也不一定是 Unix epoch。`domain` 显式映射硬件时钟、系统时间、global time；`clock_id` 标识具体时钟/映射实例。有效帧不得使用 CLOCK_UNKNOWN、FORMAT_UNKNOWN、STREAM_UNKNOWN 或空标识。

只有 domain 和 clock_id 均相同且来源确认可比较时，才允许计算跨流时间差。硬件时钟来自不同传感器或相机时不能仅因 domain 相同就比较；global time 只说明映射到对应主机系统时间，不承诺多机同步。设备重置或时钟映射重建后更换 clock_id；本版不提供跨主机同步精度保证。

`received_monotonic_ns` 是采集进程收到该帧时的本机单调时间，范围由 instance_id 限定。客户端使用自己的接收时刻测量超时，不从跨机器单调时间相减。此时间不是曝光时刻；SDK frame timestamp 也不统一宣称为曝光中点。

FrameSet 明确引用同一实例/会话/配置/标定下的 color_sequence 与 depth_sequence。发布端建议先发两个图像，再发 FrameSet，但消费者必须容忍跨 Topic 的任意到达顺序。每条 FrameSet 流的 set_sequence 从 1 单调递增。

第一版 RealSense 适配使用 `PAIRING_SDK_GROUPED` 表示 librealsense frameset 的分组关系，**不声称两帧同时曝光或经过硬件同步**。其他枚举用于明确语义：SOFTWARE 为有明确定义匹配策略的软件配对，HARDWARE 只能在实际硬件保证成立时使用；本轮不实现这两种模式。

消费者实现必须满足以下规则（本轮只定义，不提供实现）：

1. 先通过状态 Topic 或 GetStatus 确认活动实例/会话，再接收其图像；状态切换时清理旧会话缓存，禁止跨会话配对。
2. 按 FrameSet 引用组装，不按“两个最新帧”或“SDK 帧号相等”配对。每组需要两张图和精确标定版本全部就绪才能交付算法。
3. 缓存有界，建议每流最多 8 张图、4 个待配对集合；从最先缓存的相关成员开始计时，200 ms 内不完整则丢弃并报告 INCOMPLETE_FRAMESET。实际阈值可随帧率部署，但不能无限等待。
4. FrameSet 序号间隙报告 DESCRIPTOR_GAP，已有描述但缺图报告 MISSING_COLOR/MISSING_DEPTH；首个收到的序号不是 1 不追溯为本客户端丢帧。单流序号缺口和整组缺失分别统计，避免重复计数。
5. 不认识标定版本时发起精确查询；失败报告 CALIBRATION_UNAVAILABLE，不套用最新标定。缺标定超时同样释放缓存。
6. 同一标识重复且内容相同可忽略；内容不同是协议错误。不得用迟到数据重建已经丢弃/交付的集合。图像尺寸、格式、比例或 optical_frame 与标定不一致时拒绝该组。

## 标定快照

CalibrationRequest 精确指定 `(instance_id, configuration_revision, calibration_revision)`，不存在或已不保留时返回 CAMERA_CALIBRATION_UNAVAILABLE。服务端至少保留当前有效快照；历史版本不保证可查，消费者应缓存已查询版本。同一键必须始终对应完全相同的内容。

- color/depth 各自带 profile_id、光学坐标系、width/height、fx/fy/cx/cy、畸变模型、5 个系数。
- fx/fy 为正的有限像素焦距，cx/cy 为有限像素坐标；畸变系数含义按模型解释。Brown-Conrady 顺序为 `[k1,k2,p1,p2,k3]`；不能把 inverse/modified 模型误当普通 Brown。NONE 时系数全零。
- depth_to_color 明确 `source_frame=depth.optical_frame_id`，`target_frame=color.optical_frame_id`。
- rotation 是**行主序** 3×3 正交右手旋转矩阵，translation_m 是米；`p_color = R × p_depth + t`。RealSense SDK 的 rotation 是列主序，适配层须显式转换为本协议行主序，禁止直接 memcpy 后假设相同布局。
- 深度比例、配置或有效标定发生变化时必须建立新版本；本版采集中不支持改变这些参数，应先 Stop 再 Configure。
- 未读取到标定返回明确失败，不能用零内参或单位外参冒充有效标定。相机到法兰/机器人基座的手眼变换另属系统标定，不放进本类型。

## 缺帧、配置变化与断连的区别

| 可观测事实 | 客户端应表达的状态 |
|---|---|
| FrameSet 缺少图像成员或序号有间隙 | 缺帧/配对失败；不能推断相机已拔出 |
| 新 configuration_revision / calibration_revision / capture_session | 配置或会话变化；隔离并清理旧缓存，查询对应新标定 |
| 收到 DEVICE_DISCONNECTED | 采集服务明确报告设备断连 |
| 收到 DEVICE_FAULT | 设备/采集后端故障，错误见 last_error |
| 500 ms 未收到新状态心跳 | SOURCE_UNREACHABLE；可能是进程、网络或设备，不伪装为确定的 USB 断连 |
| 状态仍为 STREAMING，但持续没有完整 RGB-D 帧 | FRAME_TIMEOUT；状态存活不代表数据流健康 |

发布端状态心跳必须独立于图像到达，即使设备断连也继续发布 DISCONNECTED。published_sets/dropped_sets 是当前或最近一次采集会话的发布端计数，新 Start 清零，不包括客户端网络丢失，不能当成端到端接收计数。set_sequence 在每次提交待发布组时分配，失败时也不复用；published_sets 表示三项 DDS publish 均返回成功，仍不等于客户端收到。会话结束后的计数仅供诊断，不表示旧会话可继续使用。

## 错误、边界与兼容

`comm::Result<Response>` 表示传输调用结果；`Response.result.code` 是相机业务结果，只有 CAMERA_OK 为成功。所有响应的 has_data 显式表示数据是否可读；没有数据时必须清空数据区。失败响应可以附带当前状态/配置供重新同步，但不得因 has_data=true 把失败当成功。

CAMERA_INVALID_ARGUMENT：字段/布局非法；DISCONNECTED：设备不在线；UNSUPPORTED_PROFILE：模式或组合不支持；CONFIGURATION_CONFLICT：实例或控制版本不匹配；INVALID_STATE：当前状态不允许操作；CALIBRATION_UNAVAILABLE：精确版本不可获得；BACKEND_ERROR：驱动失败；CAPACITY_EXCEEDED：能力列表或数据超过协议容量。保留消息文本作为诊断，不解析文本进行业务分支。

- Identifier ≤64 UTF-8 字节、model/firmware ≤128、message ≤512；禁止内嵌 NUL，原始输入超长必须拒绝。生成 fixed_string 可能截断，检查必须发生在赋值前。
- 单图最多 8 MiB，候选 profiles 最多 128。若设备能力超过本协议容量，返回 CAPACITY_EXCEEDED，不静默截断或宣称完整。能力里的 image_capacity_bytes 可以小于等于 8 MiB，声明部署实际接收上限。
- 有界 IDL sequence 不等同于已实现运行时校验；此前固定生成器路径已发现 vector 上限不自动检查。采集端与消费端均须检查字段和总长度；本轮没有实现这些校验器。
- 图像直接走普通 DDS Topic，不放入已有 65536 字节上限的 ServiceFrame。大小符合上限不代表网络吞吐达标；需要后续验证 DDS 分片、内存、队列和网络预算。
- `@final` 与 XCDR1 保持现有框架一致。冻结后字段、顺序、枚举值、容量或语义的破坏性修改使用新模块/类型/地址版本；不要只增加一个 schema_version 数值。

例如 RGB8+Z16 均为 1280×720@30 时，纯像素带宽约 138.24 MB/s（1.106 Gbit/s），尚未计入协议开销。第一版设计不承诺所有 D435 配置都适合既有 UDP 网络，也不宣称零拷贝或共享内存已实现。

## 构建与验证范围

可选开关 `ROCOS_BUILD_CAMERA=ON` 只生成业务类型并构建线格式测试，默认不启用、不改通用库 API，也不依赖 RealSense SDK。当前独立 worktree 复用主工作目录已有依赖，可在本 worktree 根目录运行：

```bash
source /home/rocos/Documents/GitHub/rocos-dual-arm/scripts/env.sh
cmake -S . -B build -G Ninja -DROCOS_BUILD_CAMERA=ON -DBUILD_TESTING=ON \
  -DFASTDDSGEN_EXECUTABLE="$ROCOS_COMM_ROOT/work/deps-src/fastddsgen/scripts/fastddsgen" \
  -DROCOS_DEPENDENCY_PREFIX="$ROCOS_COMM_ROOT/work/deps-install"
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

独立应用可以通过已有 `rocos_generate_idl` 引用本目录唯一 IDL 来源，链接自己的生成类型和 rocos_comm。`contracts.hpp` 提供地址及 Client/Server 类型别名，但不包含服务 handler。草案未安装为稳定相机 SDK 包。

`camera_protocol_wire` 验证 RGB/Z16 的 XCDR1 编解码、行填充、深度比例和字节顺序、内外参、帧配对引用、版本及断连状态字段，以及最大能力响应能否放入 ServiceFrame。图像测试直接调用生成的类型支持，不发送 DDS 流量。测试证明字段可表达并传递上述信息，**不证明相机行为或消费端配对逻辑已经实现**。

后续实现验收应另行验证：无 RealSense SDK 的独立进程接收 RGB-D；乱序/缺帧/标定丢失；配置切换和迟到 Stop；相机拔插、采集进程退出与网络中断的区别；实际大图像网络传输与多客户端资源占用。本轮没有执行这些运行验收。

## 官方参考

- [RealSense 投影、光学坐标系、深度比例和流间标定](https://github.com/realsenseai/librealsense/wiki/Projection-in-RealSense-SDK-2.0)
- [rs_types.h：内参与畸变模型定义](https://github.com/realsenseai/librealsense/blob/master/include/librealsense2/h/rs_types.h)
- [rs_frame.h：时钟域、帧及元数据定义](https://github.com/realsenseai/librealsense/blob/master/include/librealsense2/h/rs_frame.h)
- [rs_sensor.h：流间外参定义](https://github.com/realsenseai/librealsense/blob/master/include/librealsense2/h/rs_sensor.h)

以上用于协议字段语义参考，不表示已固定/安装 librealsense 构建版本；本轮只使用仓库已固定的 Fast DDS 3.6.2、Fast CDR 2.3.6、Fast DDS-Gen 4.3.0。
