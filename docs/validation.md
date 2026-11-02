# 第一版验收记录

日期：2026-11-02。范围：通用通信框架六项任务，不含运动控制或技能部署。

## 环境与依赖

- Ubuntu 22.04.5 amd64、GCC 11.4.0、C++17、CMake 3.31.6、Ninja。
- JDK 17 用于构建 Fast DDS-Gen，依赖构建和安装均位于 work/。
- Fast DDS 3.6.2、Fast CDR 2.3.6、Fast DDS-Gen 4.3.0。
- foonathan memory 使用官方 vendor 1.4.1 对应提交，详见 dependencies.lock.json。
- scripts/bootstrap.py 已实际执行成功；框架构建未出现编译告警。

## 验收结果

最终执行 `cmake --build --preset dev` 与 `ctest --preset dev`：3/3 通过，总耗时约 11 秒。耗时受硬件、调度与网络状态影响，不是时延保证。

| 验收项 | 证据 |
|---|---|
| 固定环境与版本 | 提交锁定清单、依赖构建脚本、CMake 精确依赖版本检查和预设 |
| Topic/Service 契约 | communication-contract.md 定义命名、版本、类型、线程、容量、超时和错误 |
| IDL 与发布订阅 | 自动生成真实 DDS 类型，验证收发、类型冲突及 QoS 不兼容诊断 |
| ServiceClient/Server | 真实 DDS 请求响应，类型契约探测、多客户端并发、有限工作队列 |
| 能力和租约示例 | 查询能力；租约独占、过期、重复操作及参数变化拒绝 |
| 故障语义 | 超时清理、迟到响应、队列满、实例冲突、进程内重启、传输重复、客户端关闭 |
| 跨进程崩溃恢复 | 两个客户端进程；SIGKILL 服务端后重启；保持原客户端存活并恢复调用 |
| 独立应用 | 安装库后复制独立工程，只通过 find_package 接入；以自有 IDL 验证双键 Topic、加法服务和 rejected 结果 |

独立应用的成功标记：

```text
EXTERNAL_ACCEPTANCE_OK pubsub=42 service=42 failure=rejected
```

## 验证中修复的问题

- Fast DDS CMake 包精确版本为 3.6.2.0，需要与发布标签的三段版本区别处理。
- 有界字符串生成类型需显式适配 std::string。
- 为本地安装依赖设置相对 RPATH，避免间接共享库找不到。
- 服务重新匹配时清除旧就绪信息，并将就绪回复绑定当前探测，防止请求发往已退出实例。
- 将发现租期与通告周期配置化，默认 5 秒和 1 秒；崩溃恢复不承诺瞬时完成。

## 尚未验证及边界

- 尚未在两台物理主机、丢包/抖动网络或高负载下测量。
- 不提供持久化去重、自动重试、业务取消、主备切换或 exactly-once 保证。
- 服务端关闭需等待当前 handler 结束；应用 handler 必须自行限制执行时间。
- Service 是框架自定义信封协议，不是 ROS 2 Service 或 OMG RPC 兼容接口。
- 当前部署配置为受控网络 UDPv4，无认证、权限或加密配置。

上述边界是当前六项实现的实际范围，不能作为真实机器人控制或硬实时性能验收。
