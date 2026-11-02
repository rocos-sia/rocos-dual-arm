# 验收测试

运行 ctest --preset dev。

- comm_tests.cpp：真实 DDS Participant 之间的通信和异常语义。
- process_integration.py：多个独立进程，包括 SIGKILL 与重启。
- external_acceptance.py：安装库，复制独立 consumer 工程，以其自有 IDL 构建并运行。

external_app 仅通过 find_package 和公开头文件接入，不引用框架源码。测试使用独立 namespace。

验证范围是本机 UDP 多进程，不代表跨物理主机时延、丢包网络或运动控制已经验证。
