# 五组件链接的 C3 QEMU guest 容量切片：2026-09-23

## 判定

P6-03 **未验收**。仓外临时工程将 ESP Base 普通固件与 FRP、MQTT、OTA、Container 和 WAMR 的代表性入口链接到同一 ESP32-C3 镜像。官方 C3 QEMU 中，固定 128 KiB counter guest 在实例化时因连续内存不足失败；另一个仅将 guest 初始和最大内存改为 64 KiB 的实验候选，在 Base 初始化前及 Base 报告 READY 后各完成一次 `open → init → event → stop → close`。这证明当前仓外软件组合至少存在一个可运行的 64 KiB guest 切片，**不冻结**产品的 guest 内存、包大小或分区上限。

两次实验都没有写物理设备。FRP、MQTT、OTA 在镜像中只被强制链接，没有连接 FRPS/Broker 或执行升级；Base Wi-Fi 状态为 `unconfigured`。QEMU 缺少 ADC2 校准事件，实验镜像仅在仓外跳过对应启动构造函数，不能作为实板固件。

本报告接续[五组件静态容量与分区几何](five-component-capacity-probe.md)及[独立 Container QEMU 128 KiB counter 切片](qemu-counter-capacity-probe.md)。独立样例的成功数据与本次五组件链接的失败数据属于不同镜像和启动负载，不可相加或互相替代。

## 源码、工具链与仿真差异

| 输入 | 本次固定事实 |
| --- | --- |
| ESP-IDF / lwIP | `fff9895c82d744c7237be8847347bdd1b07c6643` / `2758df4cd3666b3b2a5b53830148379326425c0d`；启动日志的 `fff9895c-dirty` 来自固定 SDK 的 lwIP 子模块版本 |
| ESP Base | `10cb8514e8f7a3a55b8ec4622cce4f98a0f90eea` |
| ESP FRP | `dd9d52ae183c0db069ba302cf850b620f61800fb` |
| ESP MQTT | `ac5c6ca2862ab2906b336e73576eb93c85d0ef00` |
| ESP OTA | `9dc8aa70236b5b9b376a6ba7db39ede049007c8d` |
| ESP Container | 组件源码与 `ba2102be4f74262b9844c80c696d2bda16c16d57` 一致；独立 C3 工程没有参与本镜像 |
| WAMR | 公开维护 fork `a34d721b630213f59fde0b40cebbb980903660e8`；Component Manager hash `e7a23b9581c6e5232c92d3595911231f0089ccb8e00406fe3ad4a344f1443633` |
| guest 工具链 | 固定 wasi-sdk `33.0+m`、Clang `22.1.0`；128 KiB 与 64 KiB 都由同一 counter C 源码和 ABI/profile 检查生成 |
| 仿真器 | 官方 Espressif `qemu-riscv32` 9.2.2 `esp_develop_9.2.2_20260417`，macOS arm64 二进制 SHA-256 `3e38982c1ea3e750edfc8c910a0fd44727fe07d9c666b84d18d2b7985ac58246` |

原 Base 工程只启用 USB Serial/JTAG 主控制台，QEMU 前台 `-serial mon:stdio` 不能显示该通道的应用日志。仓外复制工程将 UART0 作为主控制台、USB Serial/JTAG 保留为次控制台；`device_protocol` 临时显式声明其直接包含的 VFS 依赖。切换为只有 UART0 会使 Base USB 控制代码缺少 `usb_serial_jtag_vfs_use_nonblocking`，因此本轮没有关闭其编译。

首次可见日志停在硬件初始化。GDB 回溯定位到 ESP Wi-Fi 引入的 `adc2_init_code_calibration` 启动构造函数，它在固定 IDF `adc_hal_common.c:126` 等待 ADC2 oneshot 完成事件，而官方 C3 QEMU 未产生该事件。本轮**仅在仓外 QEMU 工程**给 `adc2_cal_include` 一个空实现并由临时 `app_main` 引用，链接 map 证明原校准构造函数未进入镜像。Wi-Fi 校准未执行；不能将这份 QEMU 镜像写入真实设备，也不能用它测真实 Wi-Fi 峰值。

两份仓外固件沿用 Base 原 4 MiB 双 OTA 分区及相同 FRP/MQTT/OTA/Container 链接。`esp_base.map` 实际保留 `efrp_tls_step`、`esp_mqtt_client_start`、`eota_preflight`、`econtainer_runtime_open` 和 `wasm_interp_call_wasm`；这些符号只能证明链接路径。实验配置仍使用 Classic/Normal loader 和指令计量，关闭 AOT/Fast/WASI/guest pthread/shared memory。signed app 构建开关关闭，因此以下镜像不能替代[测试键签名的静态容量镜像](five-component-capacity-probe.md)。

## 产物和运行入口

| 实验输入 | Wasm 字节与 SHA-256 | unsigned app 字节与 SHA-256 | 初始 4 MiB QEMU Flash SHA-256 |
| --- | --- | --- | --- |
| 128 KiB guest：原工具固定初始/最大各 2 页 | 437；`1896ae9ed2390bd4fb71af9d50965533a679245806c9c07675d65b81be42c91c` | 1,014,544（`0xf7b10`）；`bdbf0d17a78ad3be84b1164957679df516ba2767f6f4c1e3945ce089756df4ac` | `1dd03a98670de2dc77c6338ec8679f9b359a652cf7af5154969f556e0f171930` |
| 64 KiB 第二候选：初始/最大各 1 页 | 437；`629a44e6b83c541224e8e6b7253142da61b9887790ac36e36d972591deded31f` | 1,014,672（`0xf7b90`）；`fbba1418c75915dc46b98d439ae821840227b88d4e4e6f70b35362fc09069ede` | `1b68cc881d8f238563d3fa6a2101c2c4be4f9521bc6981041e75db9ac76d2c07` |

64 KiB 候选只在**仓外**以相同源码和 wasi-sdk 调用 `counter_guest.py` 的构建、ABI/profile 检查，将 `MEMORY_PAGES` 实验参数设为 1；仓库正式工具仍固定 2 页。本候选还在 Base READY 后额外重复一次同一 guest 生命周期，因此两个 app 字节不只相差 Wasm 内存节，不能用镜像大小差额推导内存成本。Wasm 内存节均为非共享且最小值等于最大值；counter 调用合同、4 KiB 宿主管理 heap、4 KiB WAMR 栈、8 KiB pthread 栈与指令预算保持一致。

固定 SDK 的 `idf.py build` 为两份镜像各自通过。运行时先导出固定 SDK 环境，再将上述官方 QEMU 的 `bin` 目录置于 `PATH` 前端，执行：

```bash
idf.py -C <仓外五组件工程> qemu --qemu-extra-args=-no-reboot
```

实际仿真参数为 `-M esp32c3`、`-drive file=<build>/qemu_flash.bin,if=mtd,format=raw`、仿真 eFuse drive、`wdt_disable=true`、`-nic user,model=open_eth`、`-nographic -serial mon:stdio -no-reboot`。两轮都是构建后由 SDK 合并 bootloader、分区表、初始 otadata 与 app，初始 Flash 摘要见表；QEMU 运行可写入 otadata/NVS，因此运行后的 Flash 摘要不等同初始值。日志分别采样 11 秒后主动停止 QEMU，`SIGTERM` 不是 guest 或固件崩溃。Homebrew 通用 QEMU 11 不支持 `-M esp32c3`，不能代替官方版本。

## 128 KiB：实例化失败

| 阶段 | 8-bit free 字节 | 最大连续块字节 | 启动以来低水位字节 |
| --- | ---: | ---: | ---: |
| Base 初始化前、WAMR `open` 前 | 257,336 | 122,880 | 257,336 |
| `open=7` 后 | 257,232 | 122,880 | 255,384 |
| `close` 后 | 257,232 | 122,880 | 255,384 |
| Base 报告 READY 后 | 207,860 | 114,688 | 207,860 |

`open=7` 是组件当前的 `ECONTAINER_RUNTIME_ENGINE_FAILURE`。GDB 在最终 app 的 WAMR `wasm_allocate_linear_memory` 看到单次连续分配参数 `num_bytes_per_page=135168`、`init_page_count=1`：固定 128 KiB guest 之外，WAMR 在此配置加入 4096 字节宿主管理 heap。该请求比当时最大连续块多 12,288 字节；`runtime.c:185` 的 `instance=NULL`，错误为 `WASM module instantiate failed: allocate linear memory failed`。实际 guest `init/event/stop` 均未调用；探针记录的后续三个 7 仅是继承 `open` 结果的占位值。执行环境、事件复制和运行中 TLS 等分配尚未发生。

## 64 KiB 第二候选：两次生命周期

| 阶段 | 8-bit free 字节 | 最大连续块字节 | 启动以来低水位字节 |
| --- | ---: | ---: | ---: |
| 首次 `open` 前，Base 初始化前 | 257,336 | 122,880 | 257,336 |
| 首次 `open` 后 | 175,508 | 114,688 | 175,508 |
| 首次事件调用后 | 175,508 | 114,688 | 175,508 |
| 首次 `close` 后、pthread 尚未回收 | 257,232 | 122,880 | 175,508 |
| Base 报告 READY 后 | 207,860 | 114,688 | 175,508 |
| 再次 `open` 前，pthread 已创建 | 199,496 | 114,688 | 175,508 |
| 再次 `open`、事件调用后 | 117,772 | 57,344 | 101,776 |
| 再次 `close` 后、pthread 尚未回收 | 199,496 | 114,688 | 101,776 |
| pthread 回收后 | 207,860 | 114,688 | 101,776 |

GDB 在两次实例化的 `wasm_allocate_linear_memory` 都读到单次连续申请 69,632 字节，即 64 KiB guest 加 4096 字节宿主管理 heap；这不包含 WAMR 元数据、执行环境或 pthread 资源。两次日志均为 `open=0 init=0 event=0 stop=0 guest=3`：事件内容 3 字节，counter guest 返回 3；`close` 后同阶段的 free 和最大块回到调用前，第二次线程回收后又回到 Base READY 的采样值。`min_since_boot` 是 SDK 各内存区启动以来低水位之和，卸载后不会回升，也不代表所有区域在同一时刻达到该值。

第二次生命周期发生于 Base 输出 `ESP_BASE_READY` 与 Wi-Fi 驱动初始化日志之后，但设备无 Wi-Fi 配置，reported 状态仍为 `wifi_state=unconfigured`、`mqtt_state=unsupported`、`frp_state=unsupported`。它只能证明这个 QEMU 仿真配置下，Base 已初始化时仍可短暂承载 64 KiB guest；不能证明真实网络流量、FRP/MQTT 活跃会话或 OTA 期间的峰值。第二次 guest 存活时最大连续块仅 57,344 字节，应视为后续组合测试的风险信号，不能直接用作产品限额。

## 后续边界

真实 C3 板卡、ADC2 校准、Wi-Fi 连接与 TLS、FRPS/Broker 并发、已签名组合镜像、业务包 Flash 安装与三槽保护、持续运行及分区迁移尚未覆盖。完成 P6-03 仍须用实际五能力装配和真实网络负载测量峰值 heap、最大连续块、栈、socket/计时器，并核对 4 MiB 分区与保留身份数据。此次仿真结果不授权设备写入，也不要求修改既有 guest 限额或删减安全能力。
