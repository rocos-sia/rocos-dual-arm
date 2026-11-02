# 框架内部协议

ServiceFrame.idl 定义服务信封 v1，包括关联标识、服务实例、契约、结果和有界载荷。

应用业务 IDL 由应用维护，通过 rocos_generate_idl 生成，无需放入本目录。机器人示例位于 examples/robot，外部接入示例位于 tests/external_app。

普通 Topic 使用原生应用类型，只有 Service 使用信封。
