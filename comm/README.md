# 通用通信 SDK

已实现 Node、类型化 Publisher/Subscriber、异步 ServiceClient/ServiceServer。

公共 API 位于 `include/rocos/comm/`，DDS 适配位于 `src/node.cpp`。业务端通过生成的类型支持类调用，无需创建原生 DDS 实体。

行为和限制见 [通信契约](../docs/communication-contract.md)。
