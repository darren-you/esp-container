# 双目标 FRP、满长认证记录与单页 guest 同存内存下界：2026-09-27

## 范围

本页只核对 `esp-frp@1f0c8f37db3765a74b3b95871bb266d0c73d1248` 当前源码中必须同时存活的字节缓冲及 worker 栈，并对照已完成的无网络 QEMU 读数。64 KiB ABI 2 guest 已在读数中，不能再扣一次。两目标尚未建立真实 FRP session、TLS、Wi-Fi 或 MQTT 连接；下界是必要条件，不是可运行证明。

[C3 旧锁对象账本](c3-frp-guest-memory-budget.md)从精确 ELF 的 DWARF 取得对象尺寸。新 FRP 相对其 `36e1506` 基线只改 AEAD 块分配时机及注释，`session`、Yamux、TLS、client、port、connection 和 reader 的结构布局未改；完整记录在 tag 验证前仍需保存 65,536 字节。C3 的对象账本可继续作为当前同工具链结构尺寸的下界。ESP32 本轮没有对应同锁 DWARF 尺寸，不套用 C3 的 `sizeof`；下表只数两目标源码中确定存在的独立字节数组。

## 不依赖目标 ABI 的保守计数

| 同存字节区 | 字节 | 当前源码 |
| --- | ---: | --- |
| 16 个 AEAD 记录分块 | 65,536 | `src/aead.c`、`include/esp_frp_aead.h`；密文到达后存入，完整认证前保留 |
| session 控制 union 的 AEAD 发送区与 JSON 接收区 | 1,056 + 4,096 | `src/session.c`；登录结束后控制路径占用 union |
| session transport/control 接收与 control 发送区 | 3 × 1,024 | `src/session.c` |
| session Token | 1,024 | `src/session.c` |
| 三条 work 流的收发区 | 3 × 2 × 1,024 | `src/work_internal.h`；属于 session 对象 |
| Yamux 四条流的 ring | 4 × 1,024 | `include/esp_frp_yamux.h` |
| Yamux 控制帧、输出与输入头 | 96 + 1,036 + 12 | `include/esp_frp_yamux.h` |
| TLS pending 发送区 | 1,036 | `src/tls_mbedtls.c`；TLS 对象内 |
| client 独立 Token 副本 | 1,024 | `src/client.c` |
| FRP worker 栈 | 6,144 | `src/client_port_idf.c`；`malloc(EFRP_WORKER_STACK_BYTES)` |
| **直接字节区合计** | **94,372** | 未含各结构其他字段或分配开销 |

这些区域在当前实现中分属同时存活的 session、Yamux、TLS、client 与 worker；即使某些流暂时无数据，其数组仍占对象空间。登录专用 `handshake_rx` 会在控制记录前清零释放，没有重复计入。要复用上述区域，必须另行证明部分输入、双工作流、TLS 待发送字节和 tag 前认证的所有权不重叠，不能只按平均流量削减数组。

| 无网络 Base READY + guest | 已测 `MALLOC_CAP_8BIT` free／最大连续块 | 仅 65,536 字节记录所差 | 当前 94,372 字节下界所差 |
| --- | ---: | ---: | ---: |
| C3 默认 Wi-Fi IRAM 配置 | 65,480／45,056 | 56 | 28,892 |
| C3 关闭两项 Wi-Fi IRAM 的实验配置 | 84,944／45,056 | 已单独认证成功 | 9,428 |
| ESP32 默认配置 | 61,404／43,008 | 4,132 | 32,968 |

C3 更完整的同工具链对象账本为 **101,208 字节**：默认配置至少差 **35,728 字节**，关闭两项 Wi-Fi IRAM 后至少差 **16,264 字节**。这个值包含对象其余字段与 worker 栈；其对应的 reader 布局在当前 FRP 锁中未变。ESP32 只使用上表的跨目标保守计数，待同锁 Xtensa ELF 尺寸与动态会话采样后再替换。

上述下界尚未计 CA、Mbed TLS 内部分配、PSA、DNS/TCP/lwIP、无线网络、MQTT、OTA、FreeRTOS 其他分配、分配器元数据与碎片，也未额外保留计划中的 48 KiB 可用堆观察水位。C3 关闭 Wi-Fi IRAM 的单独 reader 成功时只剩 19,344／8,704 字节，不能宣称完整 FRP 路径成功。ESP32 当前单独 reader 在第 14 块申请失败；详见 [C3 A/B](c3-wifi-iram-qemu-ab.md)与 [ESP32 动态探针](esp32-authenticated-qemu-capacity-probe.md)。

下一轮需在**同一精确锁且 guest 始终存活**时，分别采样 client 创建、TLS 握手、session 注册/Pong、完整 64 KiB 控制记录、双 work 流和 MQTT 并发阶段的 `free/largest/min`、失败申请尺寸与任务栈。此页没有修改产品配置或设备，P6-03 继续进行中。
