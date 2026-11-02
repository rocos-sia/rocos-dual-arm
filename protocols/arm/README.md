# 机械臂 DDS 协议草案 v1

状态：**可生成、可编译的设计草案，尚未冻结，也未接入控制器。**

本目录把 ROCOS App 的机械臂接口映射为有类型的 DDS Topic 和 Service。业务类型位于 `rocos::arm::v1`，不进入通用 `comm/` 或内部 `interfaces/`。单臂、左臂、右臂复用同一份 IDL；双臂同步执行、技能部署、Action 和 DDS 高频伺服数据流另行设计。

## 设计依据

- 用户提供的 `ROCOS API.openapi (1).json`，OpenAPI 3.1.0，`info.title=ROCOS API`、`info.version=3.0.0`。
- 导出文件 SHA-256：`9949eb554a1788d901cf852d3170dcbec14adbd9d93694d4e4f8229ea2511b83`。
- 本次只能读取用户提供的导出快照，**未能核实在线 Apifox 最新内容**。API 版本号相同也不代表文件内容相同，评审应按上述摘要对齐。原始导出文件不加入协议构建或版本控制。
- 源码交叉检查：相邻 `rocos-app` 仓库提交 `93a39b5` 的 `src/robot_http_server.cpp`、`src/robot.cpp`、`src/robot.hpp`。源码用于说明现有行为，不覆盖导出规范；差异列于本文末尾。
- 类型文件：[v1/Arm.idl](v1/Arm.idl)。地址、请求和响应的集中定义：[contracts.hpp](include/rocos/arm/contracts.hpp)。不要在调用方复制 IDL 或各自拼接地址。

## 寻址与 Topic

每台机械臂一个 `NodeConfig.name_space`，例如 `cell_a/arms/left`、`cell_a/arms/right`。namespace 是部署配置，IDL 不硬编码 left/right，也不重复传入 arm_id。

| 项目 | 传给框架的相对名称 | 类型 / QoS |
|---|---|---|
| 状态 Topic | `topics/robot/state/v1` | `StateSample`；`Qos::state()`：best effort、keep last 1、volatile |
| Service 示例 | `robot/movej/v1` | `MoveJRequest` → `MotionResponse`；框架可靠服务 QoS |

左臂完整状态地址为 `rocos/v1/cell_a/arms/left/topics/robot/state/v1`。MoveJ 的两个地址为 `rocos/v1/cell_a/arms/left/services/robot/movej/v1/request` 和 `/reply`。调用 ServiceClient 时不要再添加 `services/`。

状态由控制器快照发布线程发布，不直接使用实时执行线程。`StateSample.instance_id` 每次发布进程重启更换，`sample_sequence` 在同一实例内从 1 单调递增；两者是 DDS 新增字段，用于识别重启、丢帧和旧样本。`data.timestamp` 保留 HTTP 的 epoch 秒，不作为跨机超时依据。接收方用本地单调时钟记录最后收包时间；超过部署约定的新鲜度阈值后标记状态不可用，不能一直显示旧值为实时值。

发布频率与新鲜度阈值尚未冻结；建议先在应用配置中显式指定，例如 50 Hz / 200 ms，再通过实际负载验证。状态 Topic 只发送有效快照，读取失败时停止发布；`GetState` Service 用业务失败表示查询失败。控制器掉线不等于机器人已停止。

此 Topic 不提供逐条事件、最终运动结果或双臂同步时钟保证。晚加入者等待下一帧，或调用 `GetState`。`GetControlStatus`、心跳响应均不含 token；本版不另建控制权广播 Topic。

## Service 清单

以下均已定义消息类型及集中地址，**不表示已经实现服务处理函数**。地址下的 `v1` 为机械臂契约版本，与框架前缀中的 `v1` 各自独立。HTTP 列省略 `/api`。

| HTTP 接口 | 契约名 | Service 相对名称 | Request → Response |
|---|---|---|---|
| GET /robot/state | GetState | robot/state/v1 | EmptyRequest → StateResponse |
| GET /robot/enabled | GetEnabled | robot/enabled/v1 | EmptyRequest → EnabledResponse |
| GET /robot/tcp | GetTcp | robot/tcp/v1 | TcpRequest → TcpResponse |
| GET /robot/impedance | GetImpedance | robot/impedance/v1 | EmptyRequest → ImpedanceResponse |
| POST /control/acquire | AcquireControl | control/acquire/v1 | AcquireControlRequest → ControlGrantResponse |
| POST /control/takeover | TakeoverControl | control/takeover/v1 | TakeoverControlRequest → ControlGrantResponse |
| POST /control/release | ReleaseControl | control/release/v1 | AuthorizedRequest → ResultResponse |
| GET /control/status | GetControlStatus | control/status/v1 | EmptyRequest → ControlStatusResponse |
| POST /control/heartbeat | Heartbeat | control/heartbeat/v1 | AuthorizedRequest → ControlStatusResponse |
| POST /robot/enable | Enable | robot/enable/v1 | AuthorizedRequest → ResultResponse |
| POST /robot/disable | Disable | robot/disable/v1 | AuthorizedRequest → ResultResponse |
| POST /robot/reset | Reset | robot/reset/v1 | AuthorizedRequest → ResetResponse |
| POST /robot/workmode | SetWorkMode | robot/workmode/v1 | SetWorkModeRequest → ResultResponse |
| POST /robot/movej | MoveJ | robot/movej/v1 | MoveJRequest → MotionResponse |
| POST /robot/movej_ik | MoveJIk | robot/movej_ik/v1 | MoveJIkRequest → MotionResponse |
| POST /robot/movel | MoveL | robot/movel/v1 | MoveLRequest → MotionResponse |
| POST /robot/movel_fk | MoveLFk | robot/movel_fk/v1 | MoveLFkRequest → MotionResponse |
| POST /robot/movec | MoveC | robot/movec/v1 | MoveCRequest → MotionResponse |
| POST /robot/pause | Pause | robot/pause/v1 | AuthorizedRequest → MotionResponse |
| POST /robot/resume | Resume | robot/resume/v1 | AuthorizedRequest → MotionResponse |
| POST /robot/stop | Stop | robot/stop/v1 | AuthorizedRequest → MotionResponse |
| GET /robot/move_status（不带 task_id） | GetMotionStatus | robot/move_status/v1 | EmptyRequest → MotionStatusResponse |
| POST /robot/servo/mode | SetServoMode | robot/servo/mode/v1 | ServoModeRequest → MotionResponse |
| POST /robot/servo/start | StartServo | robot/servo/start/v1 | ServoStartRequest → MotionResponse |
| POST /robot/servo/stop | StopServo | robot/servo/stop/v1 | AuthorizedRequest → MotionResponse |
| POST /robot/drag/start | StartDrag | robot/drag/start/v1 | DragStartRequest → ResultResponse |
| POST /robot/drag/stop | StopDrag | robot/drag/stop/v1 | AuthorizedRequest → ResultResponse |
| POST /robot/jog/joint | JogJoint | robot/jog/joint/v1 | JointJogRequest → MotionResponse |
| POST /robot/jog/cartesian | JogCartesian | robot/jog/cartesian/v1 | CartesianJogRequest → MotionResponse |
| POST /robot/jog/nullspace | JogNullspace | robot/jog/nullspace/v1 | JointJogRequest → MotionResponse |
| POST /robot/jog/svd | JogSvd | robot/jog/svd/v1 | SvdJogRequest → MotionResponse |
| POST /robot/jog/stop | StopJog | robot/jog/stop/v1 | AuthorizedRequest → MotionResponse |
| GET /robot/load_params | GetLoad | robot/load_params/get/v1 | EmptyRequest → LoadResponse |
| POST /robot/load_params | SetLoad | robot/load_params/set/v1 | SetLoadRequest → SetLoadResponse |
| POST /robot/load_params/save | SaveLoad | robot/load_params/save/v1 | PathRequest → PathResponse |
| GET /robot/tool_frames | ListToolFrames | robot/tool_frames/v1 | EmptyRequest → ToolListResponse |
| GET /robot/object_frames | ListObjectFrames | robot/object_frames/v1 | EmptyRequest → FrameListResponse |
| GET /robot/tool_frame | GetToolFrame | robot/tool_frame/get/v1 | NameRequest → NamedFrameResponse |
| POST /robot/tool_frame | SetToolFrame | robot/tool_frame/set/v1 | SetToolFrameRequest → SetToolFrameResponse |
| DELETE /robot/tool_frame | RemoveToolFrame | robot/tool_frame/remove/v1 | ToolSelectionRequest → ToolSelectionResponse |
| POST /robot/active_tool_frame | ActivateToolFrame | robot/active_tool_frame/v1 | ToolSelectionRequest → ToolSelectionResponse |
| GET /robot/object_frame | GetObjectFrame | robot/object_frame/get/v1 | NameRequest → NamedFrameResponse |
| POST /robot/object_frame | SetObjectFrame | robot/object_frame/set/v1 | SetObjectFrameRequest → NamedFrameResponse |
| DELETE /robot/object_frame | RemoveObjectFrame | robot/object_frame/remove/v1 | AuthorizedNameRequest → ResultResponse |
| POST /robot/active_object_frame | ActivateObjectFrame | robot/active_object_frame/v1 | AuthorizedNameRequest → ResultResponse |
| POST /robot/frames/load | LoadFrames | robot/frames/load/v1 | PathRequest → FramesResponse |
| POST /robot/frames/save | SaveFrames | robot/frames/save/v1 | PathRequest → FramesResponse |

## 字段、默认值与 HTTP 映射

### 通用规则

- 不传 JSON 字符串作为业务载荷，也不把所有命令塞入一个 `opcode + payload`。
- HTTP `success/code/message` 对应 `BusinessResult`。`code` 为有符号 32 位，保留负数，包括 `-2350`、`-2902`、`-3001`。以 `success` 判断业务成功，不假定所有非零码都是失败。
- 含业务数据的响应使用 `has_data + data`。`has_data=false` 时 `data` 无效，接收者必须忽略；发送者清空无效数据，避免复用旧响应。有效数据仍可能包含 `valid=false`，例如负载未辨识、当前模式不适用的阻抗参数。
- `ResultResponse` 仅声明标准结果，不承诺 HTTP 的未定义 `data`。例如 workmode 导出未定义成功响应结构，本草案只约定标准结果；源码额外返回的 `mode/robot_state` 暂不纳入。
- `OptionalDouble/OptionalBool/OptionalName/OptionalPose` 的 `present=false` 表示未提供；此时发送者清零/清空 value，接收者忽略 value。显式 0、false、空字符串与未提供不自动等价。
- `NullablePose`、`NullableToolPackage` 有 ABSENT / NULL / VALUE 三态：缺字段、显式 null、有效值。尤其 `tcp=null` 不得变成有效单位位姿。常规响应顶层的 absent/null data 统一映射为 `has_data=false`。
- `EmptyRequest.reserved=0`。IDL 结构固定，并不表示 HTTP 的所有可选属性均必然存在；当前 profile 要求状态基础字段齐全，缺少基础字段则适配失败；兼容性可选字段已显式建模。
- JSON 别名在适配边界归一：`pose_to` → `pose_goal`，工具/工件 `pose` → `frame`。IDL 只保留一个规范字段。多个别名同时提供且不一致时，本草案要求拒绝，不按任意顺序覆盖。
- HTTP 名称保留为业务名称；IDL 类型使用 `Pose3D/FrameName/ControllerPath`，避免 IDL 不区分大小写时与 `pose/name/path` 成员冲突。

### 单位与坐标系

| 字段 | 约定与依据 |
|---|---|
| Pose3D | position 为米，orientation 为无量纲四元数 x/y/z/w；来自 TCP/工具接口说明。DDS 草案要求显式完整有效四元数；默认构造全零不是单位姿态，不自动补 w=1。单位姿态须写 `(0,0,0,1)` |
| state.flange | 基座 → 法兰；不叠加工具偏置 |
| state.tcp / GetTcp | 基座 → TCP；GetTcp 的 HTTP `reference_frame` 恒为 `base`，IDL 省略这个常量字段 |
| active_tool_frame / 工具 frame | 法兰 → TCP 的固定变换，不能当成实时 TCP |
| active_object_frame / 工件 frame | 草案按基座 → 工件表达；导出缺少明确参考系说明，冻结前需确认 |
| MoveJ / MoveLFk joints | rad，长度严格等于机械臂 DOF，按控制器 joint_states 的稳定关节顺序；本草案针对旋转关节机械臂，不推断支持直线关节 |
| joint_states | 建议 position rad、velocity rad/s、torque/load_torque Nm；导出未写单位，须与控制器确认后冻结；id 为稳定标识，不等于数组下标 |
| MotionLimits | velocity/acceleration/jerk 缺省 1/2/10。MoveJ/MoveJIk 建议 rad/s、rad/s²、rad/s³；MoveL/MoveLFk/MoveC 建议 m/s、m/s²、m/s³。导出未完整定义，不能统一解释为速度百分比，旋转限制语义仍待确认 |
| MoveC.theta | rad；pose_start 缺省为控制器处理请求时的当前法兰位姿 |
| CartesianJog.twist | 固定 6 元 `[vx,vy,vz,wx,wy,wz]`；参考系 BASE/FLANGE/TOOL/OBJECT，缺省 BASE。线速度/角速度与 speed 的缩放、归一化关系尚需补全规范 |
| JointJog / NullspaceJog | joints 为方向向量，不是目标关节角；direction 作用于整个向量，POSITIVE/NEGATIVE，不能传 NONE |
| SvdJog.dim_speeds | SVD 各维度速度标量；导出未规定维度个数、顺序与单位，保持待确认，不假定固定为 6 或等于 DOF |
| JogOptions | speed 缺省 1、timeout 缺省 0.1 秒、dir_threshold 缺省 0.99；超时是点动保活参数，区别于 Service 等待超时 |
| DragStart | flag/direction 按导出枚举映射；max_speed/max_acceleration 缺省 1/2，具体单位及 NONE 行为待确认；不要与向量 jog 混用 |
| 阻抗查询 | 关节刚度 Nm/rad、阻尼 Nm·s/rad；笛卡尔平动 N/m、N·s/m，转动 Nm/rad、Nm·s/rad；valid=false 时数值无效 |
| LoadParameters | mass kg，有限且 ≥0；com 为法兰坐标系下米，HTTP 三元素数组对应 Vector3；不填 com 可规范化为零向量 |
| 时间 | state.timestamp 为 epoch 秒 double；控制权持有/空闲/剩余/TTL 为秒整数，heartbeat_interval_seconds 为秒 double；不据跨机墙钟判断请求有效期 |

其他默认值：`ServoStartRequest.has_port=false` → 8081，显式端口必须为 1..65535；`persist=false` → 仅内存；`PathRequest.path` 为空 → 控制器启动配置的默认路径。路径属于控制器文件系统，不属于客户端。

GetTcp.name 为空使用活动工具；没有活动工具时返回法兰。**不要把这一规则推广到 MoveL.tool_name**：当前源码 MoveL 的空名称使用单位工具偏置，即目标法兰；指定工具时将目标 TCP 换算为目标法兰。MoveLFk 当前先 FK 再调用 MoveL，非空 tool_name 的组合语义需要在 Apifox 写明。MoveJIk / MoveC 的目标参考系也须在冻结前补齐，目前源码按法兰运动链解释。

MoveC 使用带判别符的 `ArcTarget` union：只能是 `center_frame + theta` 或 `pose_via + pose_goal`。默认 ARC_UNSPECIFIED 无效，控制器须拒绝。这样不产生“两个模式同时存在”或“字段只填一半”的线格式。

状态枚举按同名 HTTP 值映射，`ERROR_STATE` → `STATE_ERROR`；工作模式分别映射 position / ee_admit_teach / jnt_admit_teach / jnt_imp / cart_imp。伺服模式映射 joint / cartesian。UNKNOWN/UNSPECIFIED 是 DDS 占位值，不能作为有效写操作参数；遇到未来未知 HTTP 状态值应明确报适配不兼容，不能悄悄变为 STOPPED。

### 控制权

所有有副作用的服务都携带 `ControlCredentials {token, client_id}`，对应两个 HTTP 鉴权头，申请/抢占除外。此 `client_id` 是业务会话身份，与框架 `ServiceFrame.client_id`（每个 ServiceClient 的传输关联身份）不同；不能互相替代。

申请已有有效持有者时返回业务 3008；缺 token 为 3006，token 过期或身份不匹配为 3007。TTL 以服务端响应为准，不硬编码 300 秒。正常写操作和心跳按原接口语义续期，只读查询不续期。takeover 保留原规范“覆盖旧持有者、旧 token 立即失效”的含义，不能把普通 acquire 实现为隐式抢占。

Acquire/Takeover 返回 `ControlGrantData`，其中 owner 对应不含凭据的状态字段；token/client_id/client_id_auto 仅在 grant 中返回。DDS takeover 显式携带 client_id 是本草案补充：导出 body 只有 client_name，须映射稳定会话身份，不依赖网络源端口。acquire/takeover 的 client_id 为空按现有 App 的自动生成机制处理；后续写请求必须使用返回值。

owner_ip/owner_port/owner_connection/owner_agent 保留为可选审计来源：没有 HTTP 来源时使用空串/0，不捏造 DDS 客户端 TCP 地址；这些值不参与身份校验。has_owner=false 时其他 owner 字段不代表当前持有者。

不在状态 Topic 放 token 只减少暴露面，**共享 DDS reply Topic 本身不提供凭据保密或访问控制**。当前框架未配置 DDS Security，传输 client_id 筛选也不是认证。生产部署的认证、访问控制和凭据保护需另行实现；本次只定义应用控制权消息格式。

### 运动受理、失败和重试

调用结果分两层：`comm::Result<Response>` 表示是否获得可用服务响应；`Response.result` 表示控制器业务结果。传输成功仍可能因未持有控制权、状态不允许、参数不合法等而业务失败。

`MotionResponse.result.success=true` 只表示本次命令受理成功，不能表示运动完成。状态 Topic 或 GetMotionStatus 只能观察整臂状态；没有 operation/task 关联，不能证明“某个客户端的某条运动成功完成”。本版不虚构 task_id，也不增加一个无法兑现的 exactly-once 承诺。

Service timeout 表示未及时收到响应，**不表示命令未执行或已取消**。超时后可能发生运动、文件写入、控制权抢占或点动续期，客户端不得自动重新提交写操作。框架的有限传输请求缓存不等同于业务幂等，新的 async_call 会生成新请求；重启后缓存也不持久化。需要通过状态/控制权查询以及上层业务判定处理不确定结果。

所有写服务未来应进入同一个机械臂仲裁域：持有者校验、状态检查、工具切换和运动启动不得各自无锁并行。Service 工作线程之间不天然互斥；不能仅因为每个 ServiceServer 串行调用 handler 就假定跨服务安全。业务服务应快速返回受理结果，执行进入有界控制器队列，不能在 DDS 或实时线程等待运动结束。服务端执行前的过期命令、失效租约和重启后的旧 token 处理仍需在控制器接入阶段实现并验证。

### 坐标系和工具包

`ToolListData` 保留 names/active/file_path 以及可选 package_support/robot_id。`NamedFrameData.tool_package` 对应 HTTP 的 `package`；`RobotStateData.active_tool_package` 保留同名含义。ToolPackage 中 mount/tcp/default_mount/default_tcp 分别是安装变换、本地 TCP 及可选默认值；状态摘要不包含默认值或 calibration_yaml，对应 present=false。revision 使用 uint64，必须在导出范围 1..9007199254740990 内；digest 为 64 位小写十六进制，不含 `sha256:` 前缀。

包工具实时 TCP 为 flange × mount × tcp，模型为 flange × mount。state.flange/tcp/active_tool_frame/active_tool_package 必须来自同一关节和工具注册表快照，不能在适配器中分多次查询后拼出貌似同步的状态。

工具写入保留 persist 与 previous_name；包工具不允许用普通 SetToolFrame 编辑。删除包工具只解除本机绑定，不删除不可变模型资源。ActivateToolFrame 空名称清空活动工具；激活工具不自动修改全局负载。工具写操作及 frames/load/save 需符合规范中的 IDLE/STOPPED 和原子持久化约束。工件接口不擅自增加工具接口专属的 persist/previous_name。

## 有界数据和版本策略

| 类型 / 内容 | 当前上限 | 依据 |
|---|---|---|
| JointVector / joint_states | 16 | DDS 草案容量；不是机器人固定 DOF |
| FrameNames | 每类 128 | DDS 草案容量；超过容量返回失败，不静默取前 128 个 |
| FrameName | 64 字节 | 工具名来自 API 的 1..64 ASCII 限制；对其他名称是新增容量约束 |
| ClientId / client_name / owner_name | 128 字节 | DDS 草案 |
| ControlToken | 256 字节 | DDS 草案 |
| ControllerPath / message | 1024 字节 | DDS 草案 |
| calibration_yaml | 4096 字节 | DDS 草案；模型资源不内嵌 |
| Service 业务序列化载荷 | 65536 字节 | 已有框架限制，包含 XCDR1 encapsulation |

除 API 已明确的名称约束外，所有新增容量都需要参与方评审。字符串按 UTF-8 字节计，禁止内嵌 NUL。生成的 `fixed_string<N>` 赋值可能直接截断，**未来 SDK/适配器必须在赋给生成类型之前检查原始长度**；仅靠序列化失败无法识别已被截断的 token/名称。当前未实现这些业务校验器。

实测 Fast DDS-Gen 4.3.0 生成的有界 sequence 在 C++ 中仍是 vector，框架的 XCDR1 编码和解码均接受 17 元素的 `sequence<double,16>`。因此 **IDL 上限不是运行时防线**：未来客户端构造入口、服务端 handler 入口及 Topic 消费入口都必须检查长度；此草案只声明容量，没有实现校验层。框架现有的 65536 字节总载荷检查有效，但不能代替字段容量校验。测试显式记录这个固定版本的限制，不将其记为“序列长度越界已拒绝”。

需要校验的其他条件：所有运动数值有限、速度限制为正、关节数/顺序与设备匹配、有效非零单位四元数、名称规则、控制权、FSM 状态和每种坐标系的意义。生成类型只保证线格式，不保证运动参数合法。业务参数错误沿用 API 错误码；编码过界等传输失败走 comm::Error，不混用两者。

结构和 union 均采用 `@final`，服务载荷使用框架已有 XCDR1。冻结后修改字段、类型、顺序、枚举值/顺序、容量或语义均需评估为契约破坏，采用新 module/address/type 的 v2，并做新旧节点验证。不要只增加 schema_version 数字，或误以为相同类型名称能验证内容一致。v1 在本轮评审完成前仍可修改，不能作为已发布稳定 ABI。

## 暂不定义及待确认项

| 项目 | 本轮处理与原因 |
|---|---|
| POST /robot/wait_move | 暂不建立服务。原接口先延时再阻塞到 FSM 结束；当前框架超时不取消 handler。直接迁移会占用工作线程且仍无法关联特定任务。后续应与任务生命周期/Action 一起定义 |
| GET /robot/move_status?task_id=... | 仅定义不带 task_id 的整臂查询。导出的 task 是无字段 object/null，MoveJ 等也不返回 task_id；当前源码运动 handler 未形成完整任务关联链 |
| POST /robot/stiffness、/robot/damping | 暂不定义。damping 查询参数名仍是 stiffness，参数 required=false，响应为空对象；当前检查的 HTTP 路由也未注册这两个接口。先修正规范与实现关系 |
| POST /robot/drag/start、/robot/drag/stop | 根据导出保留请求/结果类型；当前源码未注册这两个路径，不能把 `jog (compat flag mode)` 当成相同接口。接入前需确认实际实现及参数含义 |
| POST /robot/workmode 响应 | 导出只有响应描述；草案暂定标准结果，待规范补齐，不把源码额外 data 当成已冻结规范 |
| MoveL/MoveLFk tool_name、目标参考系、运动单位、SVD维度 | 消息形状可生成，执行语义待参与方明确；具体差异见上文 |
| GET /robot/info | 本轮聚焦控制消息，暂不搬迁控制器/机器人/主站/从站资产信息模型；继续 HTTP。现有 examples/robot 的 CapabilitiesRequest 是演示，不能当作该接口的生产替代品 |
| GET /robot/urdf、/robot/urdf/mesh | 文件/模型资源继续 HTTP，不放进状态 Topic 或 ServiceFrame |
| tool_packages、tool_package 的导入/导出/资源/安装/覆盖接口 | 本轮只保留状态及坐标系所需元数据，包资源与管理接口继续 HTTP |
| Lua/标定/诊断/日志/技能部署/双臂协同 | 不属于本轮机械臂运动消息设计 |
| servo/start/stop/mode | 保留对现有 localhost UDP 伺服生命周期的控制，不表示远程 DDS 客户端可以直接使用该 UDP 通道。MotionGeneratorCommand/高频 DDS 伺服输入需单独定义 |

冻结前优先确认：运动单位与目标参考系、待补齐的 HTTP 契约、容量限制、控制权跨协议仲裁、状态发布频率与新鲜度。以上未决项不阻止编译和线格式评审，但不能据此声称具备可用的真机控制协议。

## 构建与调用方式

本目录默认不参与通用库构建，也不安装到 `rocos_comm` 包。仓库中可显式启用：

```bash
source scripts/env.sh
cmake --preset dev -DROCOS_BUILD_ARM_PROTOCOL=ON -DBUILD_TESTING=ON
cmake --build --preset dev
ctest --preset dev
```

外部应用保持 `find_package(rocos_comm CONFIG REQUIRED)`，用 `rocos_generate_idl` 生成本目录唯一来源的 `v1/Arm.idl`，链接自己的生成库和 `rocos_comm::rocos_comm`，并添加本目录 `include/`。冻结前从同一仓库修订引用协议，不复制后各自修改；此草案尚无单独安装包。

```cpp
#include <rocos/arm/contracts.hpp>
namespace api = rocos::arm::contracts;
rocos::comm::Node node({42, "arm_observer", "cell_a/arms/left", {}});
auto states = node.subscriber<api::StateTopic::Support>(
    api::StateTopic::name, api::StateTopic::qos());
api::Client<api::GetState> query(node, api::GetState::name);
// 先 wait_for_service，再 async_call(EmptyRequest{}, timeout)。
// 依次检查 transport Result、response.result().success()、has_data()。
// 对端须另行实现对应 Server；本目录不会启动或控制机械臂。
```

验证覆盖：固定版本生成器生成并编译全部 IDL；通过框架实际使用的 XCDR1 编解码路径验证可选/显式零、TCP 三态、MoveC 两个分支、有符号错误码、较大状态/坐标系响应、65536 字节总载荷拒绝，以及生成代码不检查 sequence 上限的已知限制；运行已有通用通信回归。验证不包含控制器适配、业务参数校验、运动执行、硬件、双臂同步、跨机或丢包场景。
