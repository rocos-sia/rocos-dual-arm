# rocos_comm

基于 Fast DDS 的通用 C++17 通信框架。仓库名暂保留 `rocos-dual-arm`；核心库不依赖双臂或机器人。

**第一版验收目标：一个独立应用无需修改框架源码，就能使用自己的 IDL，完成发布订阅和服务调用，并获得明确的失败结果。**

## 已实现

- 类型化 Publisher/Subscriber、RAII 生命周期、QoS 预设和诊断。
- 异步 ServiceClient/ServiceServer、就绪探测、实例标识、并发请求关联。
- 超时清理、迟到响应过滤、有界等待表和工作队列、有限响应缓存。
- 查询控制器能力、申请租约的示例业务；业务幂等留在示例层。
- 安装导出的 CMake 包和 IDL 生成函数；独立应用可使用自有协议。

本版本不实现运动控制、Action、技能部署、Python SDK 或服务高可用。[历史双臂设计](docs/dual_arm_dds_interface_design.md) 保留原文，不是通用框架的冻结协议。

## 构建

验证平台：Ubuntu 22.04 amd64、GCC 11.4、C++17、CMake 3.31.6（最低 3.22）、Ninja。构建生成器需要 JDK 17 或更新版本。

固定依赖：Fast DDS 3.6.2、Fast CDR 2.3.6、Fast DDS-Gen 4.3.0、foonathan memory vendor 1.4.1 对应版本。来源和提交见 [dependencies.lock.json](dependencies.lock.json)。

系统先安装 `build-essential cmake ninja-build git openjdk-17-jdk-headless python3 libssl-dev`。依赖脚本仅写仓库内 `work/`，不安装到系统目录。

```bash
source scripts/env.sh
python3 scripts/bootstrap.py --jobs 4
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
cmake --install build
```

首次 bootstrap 需要网络访问 GitHub、Gradle 和 Maven。若已有 CMake 缓存关闭测试，重新配置时加 `-DBUILD_TESTING=ON`。

## 示例

两个终端在仓库根目录分别运行：

```bash
./build/rocos_demo server 42 demo
./build/rocos_demo client 42 demo
```

客户端订阅状态、查询能力、申请租约，并重复提交相同业务操作 ID 验证幂等。

跨机运行使用相同 domain 和 namespace，网络需允许 UDP 组播发现和 DDS 数据端口。`NodeConfig.interfaces` 可指定本机 IPv4 网卡地址。当前验收使用同一主机上的真实 UDP 多进程，尚未进行双物理主机或丢包网络验收。

## 外部项目

```cmake
find_package(rocos_comm 0.1 REQUIRED CONFIG)
rocos_generate_idl(my_types FILES MyTypes.idl)
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE rocos_comm::rocos_comm my_types)
```

通过 `CMAKE_PREFIX_PATH` 指向框架和依赖安装目录，通过 `FASTDDSGEN_EXECUTABLE` 指向固定版本生成器。应用 IDL 在自己的构建目录生成。

```cpp
#include <rocos/comm/service.hpp>
#include <MyTypesPubSubTypes.hpp>

rocos::comm::Node node({42, "sensor_app", "cell_a", {}});
auto pub = node.publisher<my_app::ReadingPubSubType>(
    "topics/reading/v1", rocos::comm::Qos::state());
auto sub = node.subscriber<my_app::ReadingPubSubType>(
    "topics/reading/v1", rocos::comm::Qos::state());
// pub.publish(value) 返回 Status；sub.take() 返回 Result<Reading>。
```

完整独立工程见 [tests/external_app](tests/external_app)，Service 用法见 [examples/robot/main.cpp](examples/robot/main.cpp)。

## 验证

`ctest --preset dev` 包含：

| 测试 | 内容 |
|---|---|
| comm_integration | QoS、并发、超时、迟到响应、队列满、重启、实例冲突、传输去重、业务幂等、关闭清理 |
| external_acceptance | 安装库，以独立项目自有 IDL 构建两个进程，验证带键 Topic、Service 和明确失败结果 |
| process_integration | 双客户端进程、状态订阅、能力和租约服务、SIGKILL 后重启及原客户端恢复 |

详细行为见 [通信契约](docs/communication-contract.md)，验证范围见 [验收记录](docs/validation.md)。

## 目录

- `comm/`：通用 C++ API 与 DDS 适配。
- `interfaces/`：内部服务信封，业务应用无需修改。
- `cmake/`：包配置和 IDL 生成函数。
- `examples/robot/`：独立业务消息及示例。
- `tests/`：集成、跨进程和外部项目验收。
- `scripts/`：依赖构建与环境配置。
- `controller/`、`sdk/`：历史预留目录，本版不实现控制器和 RobotClient。

项目未指定开源许可证，上游依赖保留各自许可证。
