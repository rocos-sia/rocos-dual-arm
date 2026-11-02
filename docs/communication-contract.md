# rocos_comm v0.1 通信契约

## 命名与版本

物理 Topic 为 `rocos/v1/<namespace>/<relative_name>`。namespace 表示应用或设备范围，可配置；DDS Domain ID 另行配置，默认 0，允许 0–232。

名称允许 ASCII 字母、数字、下划线、短横线和斜线，不允许空路径、前后斜线或连续斜线。namespace 和传入 Node 的相对名称分别最多 128 字符。

普通 Topic 推荐 `topics/<name>/v1`。Service API 传入 `<name>/v1`，内部映射到 `services/<name>/v1/request` 和 `services/<name>/v1/reply`。

破坏性修改必须升级应用地址版本和 IDL 模块或类型名。首版不自动迁移 schema；同名类型的结构兼容性由协议所有者负责。

## 类型

应用维护自己的 IDL。`rocos_generate_idl(target FILES ... INCLUDE_DIRS ... DEPENDS ...)` 使用 Fast DDS-Gen 4.3.0；泛型参数为生成的 `XxxPubSubType`，其 `type` 是业务值类型。额外包含的 IDL 加入 DEPENDS；需要生成的 IDL 列入 FILES。

普通 Topic 直接传输应用 DDS 类型。Service 使用本框架 v1 信封，载荷是应用生成类型的 XCDR1 数据，每个载荷最多 65536 字节；不是 OMG RPC 或 ROS 2 Service 的线协议。

服务契约标识为请求与响应类型名，最多 256 字符。类型名不匹配返回 incompatible_type；同名但字段变化不能靠此检查识别，因此必须遵守版本约定。内部信封不含机器人字段。

## 生命周期与线程

- 端点共享持有 Node 内部状态，可比 Node 外层对象存活更久。
- Publisher 并发发布、Subscriber 并发 take 由内部互斥串行化。
- Subscriber 提供非阻塞 take，应用自行选择消费线程；不在 DDS Listener 中执行应用回调。
- ServiceClient 有一个接收、探测和超时线程，可由多个应用线程调用 async_call。
- ServiceServer 有一个接收线程和一个串行处理线程，中间为有界队列。
- Handler 返回 Result<Response>，异常转为 handler_error，不在 DDS 接收线程执行。
- Client 销毁时将未完成 future 置为 closed。
- Server 销毁时等待当前 handler 结束，未开始请求不再执行，其调用方最终超时。Handler 必须能够自行结束，没有强制线程终止或业务取消功能。
- 不得在 handler 中销毁所属 Server，也不得与其他线程对同一对象的析构并发调用 API。

接口用于非硬实时线程。async_call 的编码和发送在调用线程执行，异步的是等待响应，不代表函数本身零阻塞。

## QoS 与容量

| 配置 | Reliability | History | Durability | 最大写入等待 |
|---|---|---|---|---|
| Qos::state() | Best Effort | Keep Last，1 | Volatile | 0 ms |
| 默认 / Qos::command() | Reliable | Keep Last，64 | Volatile | 10 ms |
| Service | Reliable | Keep Last，64 | Volatile | 10 ms |

普通 Topic 可调整深度、可靠性、写入等待和 max_instances。默认实例数 1，带键 Topic 按键数量配置。每实例上限为 depth，总样本上限为 depth × max_instances，最大 16384。

默认 Client 最多 64 个等待请求；Server 最多 32 个排队请求及 1 个执行中请求，已完成响应缓存最多 128 条。相应容量可配置，最大 4096。总内存还取决于实际载荷，载荷上限不等于总内存上限。

所有 Topic 为 Volatile，不向重连端自动回放旧命令。DDS 可靠传输不是业务成功确认；缓存压力和网络故障可使调用超时。

## 就绪、实例与重启

每个服务地址部署一个权威 Server，允许多个 Client。Client 检查请求/响应端点匹配数，默认每 100 ms 探测一次，就绪有效期 500 ms。

Participant 发现租期默认 5 秒、通告周期 1 秒，通过 NodeConfig.discovery_lease / announcement_period 配置。服务进程崩溃后旧端点可能保留至发现租期结束；新旧实例短暂并存时返回 conflict，移除旧端点后重新探测。500 ms 就绪过期不等于 500 ms 故障恢复保证。

只接受当前有效探测的回复；端点匹配代际变化会清除旧探测和就绪信息。就绪是最近观察结果，不保证下一次调用成功。发现多个服务实例返回 conflict。

Server 每次创建生成随机实例 ID，请求指向已发现的具体实例，其他实例忽略该请求。没有负载均衡、主备选举或自动迁移。

请求关联键为 (client_instance_id, request_id)，响应还必须匹配 server_instance_id。客户端和服务端实例 ID 均在创建时重新生成。

## 超时与错误

调用超时使用客户端本地 steady_clock，跨机不比较单调时间。预算从 async_call 入口开始，包含编码和发送；后台扫描及线程调度存在延迟，不是硬实时期限。

超时只结束客户端等待。服务端可能未收到、正在排队、执行中或已经完成；不会自动取消，也不会自动重试。迟到、其他客户端及未知请求响应会忽略并计数。

| Error | 含义 |
|---|---|
| unavailable | 无匹配端点或尚未就绪 |
| incompatible_type | 服务请求/响应类型名不一致 |
| timeout | DDS 写入或响应等待超时，业务结果可能未知 |
| busy | 等待表、工作队列或 DDS 资源达到容量 |
| conflict | 多实例或同一请求 ID 内容不同 |
| invalid_argument | 参数不合法 |
| serialization | 编解码失败或载荷超限 |
| transport | 其他 DDS 收发错误 |
| rejected | 业务拒绝 |
| handler_error | Handler 异常或返回不完整结果 |
| closed | Client 销毁导致等待终止 |
| no_data | Subscriber 当前无数据 |

初始化非法配置抛 invalid_argument；DDS 实体创建失败抛 runtime_error。应用在初始化边界捕获。

## 去重与业务幂等

Server 对执行中或仍在响应缓存里的相同传输请求重用结果，不再执行 handler。同传输 ID、不同 payload 返回 conflict。缓存淘汰或进程重启后，不再承诺传输去重。

每次 async_call 生成新传输 ID。业务重试需保留自己的幂等键，由业务保存记录或查询结果；框架不承诺任意业务的 exactly-once。

租约示例以 (owner, operation_id) 为业务键，最多保存 1024 条，满时拒绝新操作，不静默淘汰。重复申请返回原结果，不延长租约；历史授予结果不表示租约现在仍有效。记录只存在内存，重启即丢失；示例不用于驱动真实运动。

## 诊断与网络

端点诊断包含当前匹配数、累计匹配数、QoS 不兼容计数及 Reader 丢样本计数。Client 提供等待数、忽略响应数和发送失败数；Server 提供发送失败数。忽略响应包含共享 reply Topic 上其他客户端的正常响应。

首版使用 UDPv4 和 SIMPLE 发现，可限制网卡。未配置认证、权限和加密，适用于部署方控制的网络。跨网段发现、Discovery Server 和共享内存不在本次范围。
