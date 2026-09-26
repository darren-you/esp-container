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

## 2026-09-24：当前 Base v3 的签名五组件复测

旧切片的 Base `10cb851` 尚未包含现在的 MQTT/FRP owner。仓外复制当前 Base `0c9d8264a775a1bfea054db413d8b5a5da478be5` 固件源码，保留其组件锁 FRP `9158b7f2e2c555a14636aed26b5189902152d19e`、MQTT `9cac455b0184420353ff0283df3f100abaac3e6b`、OTA `bed5709fe517f62d60f2efad95491bc66756a42c`，加入 Container `567d760bf37b95ab82b02a9f3aa5daa14c578745`。SDK/lwIP 分别是公开维护 fork `855937cf9dcee13ee9c423fb0319238cdc8d53fd` / `2758df4cd3666b3b2a5b53830148379326425c0d`，WAMR 是 `a34d721b630213f59fde0b40cebbb980903660e8`。Base 在 `56bf135` 之后的固件变化只有 MQTT owner 过期事件修复；仓外复制工程已经包含该文件。使用同一官方 `qemu-riscv32` 9.2.2 和上述 64 KiB counter guest，继续保留 Classic/Normal loader、指令计量及 4 KiB WAMR 管理 heap、4 KiB 栈、8 KiB pthread 栈。

仓外工程从[当前静态五组件链接原型](five-component-capacity-probe.md)复制，额外仅加入上文的 `capacity_runtime_probe.c`、64 KiB guest 字节、QEMU ADC2 校准空实现和两处调用：首次在 Base 初始化前，第二次在 `ESP_BASE_READY` 后。控制台改为 UART0 主/USB 次；原 `capacity_references.c` 仍只在不可运行的 `volatile` 分支内保留 FRP、MQTT、OTA、Container 和 WAMR 链接路径。`esp_base.map` 再次核对 `efrp_tls_step`、`esp_mqtt_client_start`、`eota_preflight`、`econtainer_runtime_open` 与 `wasm_interp_call_wasm`。仓外测试 RSA-3072 键生成的签名镜像大小 `0x121000`，SHA-256 `eca77147bbc1a938e0194dab3af28ea3e987339e147bf606bd1bad7373c81b2c`，`espsecure verify-signature --version 2` 第 0 块 RSA 验证通过。QEMU 从合并的 4 MiB Flash 镜像启动并输出 `ESP_BASE_READY`；QEMU 的 ADC2 临时空实现意味着该镜像**不可刷实板**。

| 阶段 | 8-bit free 字节 | 最大连续块字节 | 启动以来低水位字节 |
| --- | ---: | ---: | ---: |
| Base 初始化前、首次 `open` 前 | 154,016 | 114,688 | 154,016 |
| 首次 guest 存活、`on_event` 后 | 72,136 | 40,960 | 72,136 |
| 首次 `close` 后、pthread 尚未回收 | 153,912 | 114,688 | 72,136 |
| `ESP_BASE_READY` 后 | 104,800 | 94,208 | 49,652 |
| 第二次 `open` 前、pthread 已创建 | 96,436 | 86,016 | 49,652 |
| 第二次 guest 存活、`on_event` 后 | 14,660 | 7,680 | 14,660 |
| 第二次 `close` 后、pthread 尚未回收 | 96,436 | 86,016 | 14,660 |
| pthread 回收后 | 104,800 | 94,208 | 14,660 |

两次都输出 `open=0 init=0 event=0 stop=0 guest=3`。第二次在 guest 存活时最大连续块仅 7,680 字节，比旧 Base 链接切片明显更紧；这是**无 Wi-Fi 配置、无 FRPS/Broker 连接、无 OTA 下载**时的测量。QEMU 的 Wi-Fi 校准与真实射频、网络/TLS 并发和实板堆布局不同，不能据此宣布 64 KiB profile 可交付，更不能冻结三包槽、业务尺寸或触发分区迁移。P6-03 仍未验收，后续需在实际装配和负载下测量最小 free heap、最大连续块与分配失败。
当前 `esp-frp@9158b7f` 的 `efrp_session_create` 对 AEAD 接收记录执行一次 `calloc(1, EFRP_AEAD_RX_BYTES)`，宏值为 **65,552 字节**。在上述 guest 存活的 QEMU 时点，最大连续块 7,680 字节，故该镜像无法同时建立此 FRP 会话；尚未计入 TLS、MQTT 或业务额外资源。此结论只针对本轮 QEMU 镜像与负载，真实 C3 板卡需单独测量。它是首版硬件容量和并行能力的明确待裁决条件，不能靠静态链接、停掉安全校验或复用旧镜像当作已通过。

## 后续边界

真实 C3 板卡、ADC2 校准、Wi-Fi 连接与 TLS、FRPS/Broker 并发、已签名组合镜像、业务包 Flash 安装与三槽保护、持续运行及分区迁移尚未覆盖。完成 P6-03 仍须用实际五能力装配和真实网络负载测量峰值 heap、最大连续块、栈、socket/计时器，并核对 4 MiB 分区与保留身份数据。此次仿真结果不授权设备写入，也不要求修改既有 guest 限额或删减安全能力。

## 2026-09-24：新版精确锁的签名镜像与 64 KiB guest 复测

前节的 `14,660` 字节 free heap 来自旧锁，**不能作为本节组合的当前数值**。本次从公开仓检出 Base `31f5ebcc0bbc756fe5e78cb7c53f9042832ce286`、Container `00c788e05d63df5279c3ca0383a778513b973601`，使用 Base 已锁的 MQTT `5bff093646d8db810d64c50c39edc004e78bf40c`、OTA `3c3f72b823ce856b02f838fef17db1368e6d5448`，并在**仓外副本**将 Base 清单和 `dependencies.lock` 的 FRP 从 `3a40a2c` 更新到最新 `c5fbe40920ae35bceeaf3d086ad9cd9740ebbb65`。FRP 该提交只增加独立 C3 QEMU 测试及文档，不改变客户端源码；此组合是最新 FRP 候选，尚非 Base 已发布的精确依赖锁。SDK 为公开 ESP-IDF `578cf89c343e388db43ba1f4ddcd602fedcb763c`、lwIP `2758df4cd3666b3b2a5b53830148379326425c0d`，WAMR 仍为 `a34d721b630213f59fde0b40cebbb980903660e8`。Base 和 Container 的 `check_sdk.py` 均通过。官方 QEMU 仍是 `esp_develop_9.2.2_20260417`，二进制 SHA-256 `3e38982c1ea3e750edfc8c910a0fd44727fe07d9c666b84d18d2b7985ac58246`。

实验目录 `/private/tmp/esp-capacity-current-lock.6WZjJt` 内的 Base 与 Container 均是独立 clone，未修改物理工作区。沿用前节 437 字节、初始和最大各 1 页的 counter guest（SHA-256 `629a44e6b83c541224e8e6b7253142da61b9887790ac36e36d972591deded31f`），在 Base 初始化前和 READY 后各运行一次真实 WAMR Classic `open → init → on_event → stop → close`。运行限额仍为 4 KiB 宿主管理 heap、4 KiB 栈、8 KiB pthread 栈、每入口 1000 条指令；因当前 Container API 新增入口期限必填字段，仓外探针显式设置 `max_entry_duration_ms=1000`。Container 以本地组件目录参与同一 IDF 工程；不可执行的 `volatile` 链接门保留 FRP、MQTT、OTA、Container 的代表性入口，map 复核 `efrp_tls_step`、`esp_mqtt_client_start`、`eota_preflight`、`econtainer_runtime_open`、`wasm_interp_call_wasm`。FRP/MQTT/OTA **仅被链接**，没有连接 FRPS/Broker 或执行下载；Container 仍未接入 Base 主应用的产品包运行链路。

为取得 QEMU 串口日志，仓外 `sdkconfig` 选择 UART0 主控制台和 USB 次控制台，并以临时 `adc2_cal_include` 空实现跳过 QEMU 不支持的 ADC2 校准构造函数；因此镜像**不可刷实板**。使用仓外新建 RSA-3072 测试键签名，`python -m espsecure verify-signature --version 2 --keyfile <测试键> <签名镜像>` 验证第 0 块 RSA 通过，密钥没有进入仓库。`idf.py build` 和 `idf.py qemu --qemu-extra-args=-no-reboot` 使用同一固定 IDF，QEMU 在第二次探针完成后由宿主主动终止。QEMU 运行日志 SHA-256 为 `7420c317575cc44a11e842e237f91fba98bbce0f5439f66a793ed94e23a0a218`；签名镜像为 `0x121000`（1,183,744 字节），SHA-256 `4802b6535ca0c001e5fb1c3e037dd625cfc79f348c8d8e2fd446fe50da506fb2`。

| 阶段 | 8-bit free 字节 | 最大连续块字节 | 启动以来低水位字节 |
| --- | ---: | ---: | ---: |
| Base 初始化前，首次 `open` 前 | 151,088 | 114,688 | 151,088 |
| 首次 guest 存活、事件调用后 | 68,960 | 40,960 | 68,960 |
| 首次 `close` 后、pthread 尚未回收 | 150,984 | 114,688 | 68,960 |
| `ESP_BASE_READY` 后 | 101,716 | 90,112 | 49,652 |
| 再次 `open` 前、pthread 已创建 | 93,352 | 81,920 | 49,652 |
| 再次 guest 存活、事件调用后 | **11,328** | **7,680** | **11,328** |
| 再次 `close` 后、pthread 尚未回收 | 93,352 | 81,920 | 11,328 |
| pthread 回收后 | 101,716 | 90,112 | 11,328 |

两次均输出 `open=0 init=0 event=0 stop=0 guest=3`；卸载后 free 与最大块回到各自调用前。旧锁的 14,660 字节 free 在本次降至 **11,328**，最大块恰仍为 **7,680**。`min_since_boot` 是启动以来的低水位，不会因卸载回升。该读数是无 Wi-Fi 配置、无 FRPS/Broker 会话、无 OTA 下载的 QEMU 切片，不代表真实射频与并发负载。当前 FRP 源码的 `efrp_session_create` 仍对 AEAD 接收记录单次 `calloc` **65,552** 字节；在 guest 存活的这一时刻，仅该请求就比剩余 free 多 **54,224** 字节、比最大连续块多 **57,872** 字节，还未计入 FRP 会话对象、TLS、MQTT 等开销。调整分配顺序无法由此证明并发可行；在 Base READY 的 101,716 字节 free 上，WAMR 同一版本已观测到的 69,632 字节线性内存申请加 AEAD 接收记录至少需 135,184 字节，单这两块就缺 33,468 字节，未计 pthread 与运行元数据。该缺口是本 QEMU 切片的下界，不是实板预算或产品限额。

同时用这份**当前签名镜像**复核 4 MiB 分区几何。现行 Base CSV 的两个 `0x1e0000` app 槽已经占满 `0x20000..0x3e0000`，没有任何产品包分区。保留原 NVS、otadata、phy、coredump、`base_store` 和两 app 回退关系，仓外临时 CSV 经固定 IDF `gen_esp32part.py --flash-size 4MB --secure v2` 与 `check_sizes.py` 验证：

| 仅供几何裁决的临时布局 | 两个 app 槽各 | 三个等大包槽各 | 当前 app 增长余量 | 尾部未分配 |
| --- | ---: | ---: | ---: | ---: |
| 当前镜像等大包槽几何上限 | `0x121000` | `0x7f000`（520,192 字节） | **0** | `0x1000` |
| 保留 app 增长空间的候选 | `0x140000` | `0x60000`（393,216 字节） | `0x1f000`（126,976 字节） | `0x20000` |

本仓主机工具仍接收最多 512 KiB Wasm。将本轮有效 64 KiB counter 模块以合法自定义 section 填充到 524,288 字节，再使用当前 `product_package.py` 和仓外测试键执行 `manifest → sign → pack → verify`，所得 manifest 为 550 字节、签名为 384 字节、`product.pkg` 为 **532,480 字节**（SHA-256 `b0d84c8c245f91c962a66c651b37cc74e19f1e4bbd3de8bfc51ead24849abc78`）。它比无 app 余量的几何上限包槽多 **12,288** 字节，比保留增长空间的包槽多 **139,264** 字节。若保留 512 KiB 可接受 Wasm、两个当前签名 app 和所有既有保留区，单按字节总量就超出 4 MiB **32,768** 字节；在本次已验证的三等槽布局中，每槽又比该实包少 12,288 字节，三个槽合计缺口为 36,864 字节，布局末尾另有 4,096 字节未分配。两种口径都尚未计入未来固件增长；此算术不是可实施迁移方案。没有据此调低包大小上限、删除回退、削减 TLS/FRP 协议能力或修改产品分区。

P6-03 仍未验收。签名、静态链接、无网络 QEMU 与临时分区 CSV 均不能替代真实同板 Wi-Fi/TLS、FRPS/Broker/OTA/guest 并发、三包槽完整写入恢复和保留数据迁移验证；本轮不冻结客体内存、包槽、固件大小或设备写入目标。

## 2026-09-26：ABI 2 与新 WAMR 锁的五仓 QEMU 复测

前节的 437 字节 ABI 1 guest、WAMR `a34d721` 和 `11,328/7,680` 字节读数属于旧源码，不能代表当前单页运行期。本轮只在 mac-work-1 的 `/private/tmp/esp-p6-current-c3-20260926/probe-base` 仓外副本组合五仓：Base `fa4d622f7824924184039a9f365be5548241e3aa`、FRP `2ffbe9e970e6cf6e7e3b32175b96a7de6d8b5587`、MQTT `18e2395123a02eab94ae5f7c7a2452c29ae28e8b`、OTA `ca13935015bf42d9a356728c3b6d2abc7aee74ae`、Container `0024695510e2b445bfdb089247d31f8cbd84e010`。固定 SDK/lwIP 为 `578cf89c343e388db43ba1f4ddcd602fedcb763c` / `2758df4cd3666b3b2a5b53830148379326425c0d`；生成 `dependencies.lock` 仍精确指向 WAMR `26c235e53e29acd8b43abe7f3b524577bd4d1ae5`。五仓组件在仓外复制为本地 IDF 组件以强制使用上述源码，Base 正式依赖锁仍指向旧 FRP/MQTT/OTA/Container，故这份镜像不是 Base 已发布的同锁构建；仓外 `source-lock.txt` 固定五仓提交和 guest 摘要，SHA-256 为 `cfdca20bc2438c309177e8f0214fadd0d8d5f15d003ceb2247bfe8abe513bce3`。仓外 `probe-inputs.sha256` 逐项固定探针修改、生成配置和测试签名键等 16 个输入，其自身 SHA-256 为 `05552ac8bdf268fa2a76050a22c3664b981370c5c19654f1fecd3cd33cad9073`。

仓外探针沿用先前的强制链接门、前后两次 pthread 生命周期与 ADC2 QEMU 空桩，并适配当前 ABI 2：移除已不存在的 `heap_size_bytes` 限额字段，WAMR 实例的宿主管理 heap 为 0；guest 是当前 `counter_guest.py check` 通过的 469 字节固定 64 KiB 模块，SHA-256 `b9422cb4cb72983141988c4a9a59b602026d94729e2362403a04d1723f98a739`。仓外主控制台改为 UART0，USB 保留次控制台，显式关闭 WAMR shrunk memory；本地组件的 cJSON CMake 依赖也在仓外写明。官方 Espressif C3 QEMU 为 9.2.2 `esp_develop_9.2.2_20260417`，二进制 SHA-256 `3e38982c1ea3e750edfc8c910a0fd44727fe07d9c666b84d18d2b7985ac58246`。可在保留的仓外工程复核：

```bash
export IDF_PATH=/Users/darrenyou/.cache/darren-space/esp-idf-578cf89
source "$IDF_PATH/export.sh"
python3 /private/tmp/esp-p6-current-c3-20260926/container/tools/counter_guest.py check --wasm /private/tmp/abi2-counter.wasm
idf.py -C /private/tmp/esp-p6-current-c3-20260926/probe-base/firmware -D ESP_BASE_CONTAINER_BINDING_PROBE=ON build
python -m espsecure verify-signature --version 2 --keyfile /private/tmp/esp-p6-current-c3-20260926/probe-base/test-key.pem /private/tmp/esp-p6-current-c3-20260926/probe-base/firmware/build/esp_base.bin
idf.py -C /private/tmp/esp-p6-current-c3-20260926/probe-base/firmware qemu --qemu-extra-args=-no-reboot
```

签名 app 为 `0x121000`（1,183,744）字节，SHA-256 `bc80d9a600dd37cc596ed2a6639d0d41ae675de292d1203e0be6f4955b0bc9b6`；仓外测试键的 RSA 第 0 签名块验签成功。ELF 保留 `efrp_tls_step`、`esp_mqtt_client_start`、`eota_preflight`、`econtainer_runtime_open` 与 `wasm_interp_call_wasm`；这只证明深链接。QEMU 运行 22 秒后由宿主 SIGTERM 结束，串口日志位于仓外 `probe-base/qemu.log`，SHA-256 `d4e74115f4c7331bee5cac96b6bec79bdbede1fbc5a1c99caff3aab46b6a0655`。

| 阶段 | 8-bit free 字节 | 最大连续块字节 | 启动以来低水位字节 |
| --- | ---: | ---: | ---: |
| Base 初始化前，首次 `open` 前 | 201,484 | 114,688 | 201,484 |
| 首次 guest 存活、事件调用后 | 123,812 | 59,392 | 123,812 |
| `ESP_BASE_READY` 后 | 152,520 | 114,688 | 82,884 |
| 第二次 `open` 前，pthread 已创建 | 144,156 | 114,688 | 74,520 |
| 第二次 guest 存活、事件调用后 | **66,588** | **45,056** | **66,588** |
| 第二次 `close` 后、pthread 尚未回收 | 144,156 | 114,688 | 66,588 |
| pthread 回收后 | 152,520 | 114,688 | 66,588 |

两次均为 `open=0 init=0 event=0 stop=0 guest=3`，关闭后 free 与最大块回到对应进入前。本轮 FRP `src/session.c:115` 仍对 AEAD 接收区执行单次 `calloc(1, EFRP_AEAD_RX_BYTES)`，其中 `EFRP_AEAD_RX_BYTES=65,552` 字节；未使用任何仓外分块 AEAD 优化。第二次 guest 存活时最大连续块只有 45,056 字节，比这一次 FRP 必需申请少 **20,496** 字节，尽管 free 总量仍有 66,588 字节。此时未创建 FRP 会话；Base reported 为 `provisioned=false`、`wifi_state=unconfigured`、`time_ready=false`、`mqtt_state=unconfigured`、`frp_state=unconfigured`、`frp_sessions=0`。所以本轮只能裁定该 QEMU 时点不能完成这笔连续内存申请，不能声称网络并发或实板内存已通过。ADC2 校准被仓外空桩跳过，镜像不可刷实板；签名镜像也没有三包槽安装与真实网络负载，P6-03 仍未验收。

## 2026-09-26：FRP 分块 AEAD 的 ABI 2 同镜像分配复测

本节接续上一节的 ABI 2 工程，只在 mac-work-1 的仓外副本 `/private/tmp/esp-p6-frp-chunked-20260926` 把 FRP 从 `2ffbe9e970e6cf6e7e3b32175b96a7de6d8b5587` 换成已提交的 `533e29467b24d01157ff3b5229e62c93d101be61`。Base、MQTT、OTA、Container 分别仍为 `fa4d622f7824924184039a9f365be5548241e3aa`、`18e2395123a02eab94ae5f7c7a2452c29ae28e8b`、`ca13935015bf42d9a356728c3b6d2abc7aee74ae`、`0024695510e2b445bfdb089247d31f8cbd84e010`；固定 IDF/lwIP/WAMR 分别为 `578cf89c343e388db43ba1f4ddcd602fedcb763c`、`2758df4cd3666b3b2a5b53830148379326425c0d`、`26c235e53e29acd8b43abe7f3b524577bd4d1ae5`。锁定 469 字节 ABI 2 counter guest 的 SHA-256 仍为 `b9422cb4cb72983141988c4a9a59b602026d94729e2362403a04d1723f98a739`。仓外 [源码锁](frp-chunked-source-lock.txt) SHA-256 为 `bfa700b61464383615f64c4510d77f88de78848720e74f0ce2956e5c5a4cc863`，[17 项输入摘要](frp-chunked-probe-inputs.sha256)文件自身 SHA-256 为 `028e28f25975b3355b4b1dca23f6aa2bf31f2549e17957a7db7014f161a65d63`。

该 QEMU 专用 [分配探针源码](frp_chunked_capacity_probe.c) SHA-256 为 `7b1ff273eb50a68b34b2e1e9206ffc6276b0ae91a553afda54eb941fc91935b1`。它在 `open → init → on_event` 后、guest 尚未 `stop/close` 时，调用**真实** `efrp_aead_reader_init_chunked`，随后只投喂 12 字节测试 nonce 与合法 4 字节记录长度头，分别声明明文 4、32、48、56、60、64 KiB；每轮调用 `efrp_aead_reader_destroy` 并记录 free/最大连续块。FRP 在解析长度头时立即分配实际大小的 4 KiB 块，所以该检查能直接验证分块申请在同一 WAMR 存活时点的结果。它**没有**创建 FRP session/TLS/FRPS 连接，没有投喂密文和 tag，也没有交付已认证明文；`feed=0` 只表示记录头和对应块申请成功，不能视为整条记录通过。

固定 SDK 构建与 RSA 测试键签名验证通过；最终签名 app 为 **1,183,744 字节**（`0x121000`），SHA-256 `f6026b3ac6d5b8e14e8794265b831e70e447286da70daf743f8d991f0b647b24`。ELF map 保留 `efrp_aead_reader_init_chunked`、`efrp_tls_step`、`esp_mqtt_client_start`、`eota_preflight`、`econtainer_runtime_open`、`wasm_interp_call_wasm`。官方 Espressif C3 QEMU 9.2.2 的二进制 SHA-256 仍为 `3e38982c1ea3e750edfc8c910a0fd44727fe07d9c666b84d18d2b7985ac58246`；25 秒运行日志 SHA-256 为 `f47404da2f2b23c8b0d9543b7d331d3426638b40ee166f167a1fb1217970b373`，由宿主主动 SIGTERM 结束，无 panic。替换 FRP 后、尚未加分配调用的基线镜像 SHA-256 为 `31bfbfa23c7293bebf26c205c2af6c1c0b990cae41a678556cb3c81a45db643b`，日志 SHA-256 `7d9bb7d19ec91ea3dab32cb60d4362a3ba64b051ca87dad40c1759e00a7e16bf`；其 Base READY 后 guest 存活的 free/最大连续块仍为 **66,588/45,056** 字节。

| guest 存活时点 | 声明明文长度 | `feed` | 成功申请块数／字节 | 分配后 free／最大连续块 | 销毁后 free／最大连续块 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Base 初始化前 | 64 KiB | `0` | 16／65,536 | 58,212／45,056 | 123,812／59,392 |
| Base READY 后 | 4 KiB | `0` | 1／4,096 | 62,488／45,056 | 66,588／45,056 |
| Base READY 后 | 32 KiB | `0` | 8／32,768 | 33,788／20,480 | 66,588／45,056 |
| Base READY 后 | 48 KiB | `0` | 12／49,152 | 17,388／7,680 | 66,588／45,056 |
| Base READY 后 | 56 KiB | `0` | 14／57,344 | 9,188／3,584 | 66,588／45,056 |
| Base READY 后 | 60 KiB | `-20` | 14／57,344 | 回滚后 66,588／45,056 | 66,588／45,056 |
| Base READY 后 | 64 KiB | `-20` | 14／57,344 | 回滚后 66,588／45,056 | 66,588／45,056 |

`-20` 是 `EFRP_NO_MEMORY`：60 与 64 KiB 均在第 15 笔 4 KiB 申请处失败；FRP reader 清理此前的 14 块，堆读数恢复。失败期间的启动以来低水位为 **9,012** 字节，失败后打印的 free 是回滚后值。该扫描只界定被测长度集合：56 KiB 分配成功，60 和 64 KiB 分配失败，不推断精确最大可接受长度。尤其是真实会话还需 17,848 字节 session、5,552 字节 Yamux、握手临时区、TLS/网络和密码库资源；本实验均未同时申请。因此新版分块消除了旧版单笔 65,552 字节连续申请，**仍不能让本 QEMU 切片在 Base READY + 64 KiB guest 存活时接收满长控制记录**。P6-03 保持未验收。

可在保留的仓外工程复核最终制品与日志。下列准备命令在 darren-space 工作区执行，从上一节仓外五仓副本复制实验输入，再用精确 FRP 提交替换组件；[探针源码](frp_chunked_capacity_probe.c)及[QEMU 运行脚本](frp_chunked_qemu.py)均保存在本仓：

```bash
ssh mac-work-1 'mkdir -p /private/tmp/esp-p6-frp-chunked-20260926 && rsync -a --exclude=build /private/tmp/esp-p6-current-c3-20260926/probe-base/ /private/tmp/esp-p6-frp-chunked-20260926/'
git -C tooling/esp-frp archive 533e29467b24d01157ff3b5229e62c93d101be61 | ssh mac-work-1 'mkdir -p /private/tmp/esp-p6-frp-chunked-20260926/frp-source && tar -xf - -C /private/tmp/esp-p6-frp-chunked-20260926/frp-source && rsync -a --delete /private/tmp/esp-p6-frp-chunked-20260926/frp-source/ /private/tmp/esp-p6-frp-chunked-20260926/firmware/components/esp_frp/'
scp tooling/esp-container/docs/operations/frp_chunked_capacity_probe.c mac-work-1:/private/tmp/esp-p6-frp-chunked-20260926/firmware/apps/esp_base/main/capacity_runtime_probe.c
scp tooling/esp-container/docs/operations/frp_chunked_qemu.py mac-work-1:/private/tmp/esp-p6-frp-chunked-20260926/run_qemu.py
```

以下命令在 mac-work-1 终端执行。原工程的 `qemu_adc2_stub.c`、UART0 主控制台和仓外 RSA 测试键继续保留：

```bash
export IDF_PATH=/Users/darrenyou/.cache/darren-space/esp-idf-578cf89
source "$IDF_PATH/export.sh"
python3 /private/tmp/esp-p6-current-c3-20260926/container/tools/counter_guest.py check --wasm /private/tmp/abi2-counter.wasm
idf.py -C /private/tmp/esp-p6-frp-chunked-20260926/firmware -D ESP_BASE_CONTAINER_BINDING_PROBE=ON build
python -m espsecure verify-signature --version 2 --keyfile /private/tmp/esp-p6-frp-chunked-20260926/test-key.pem /private/tmp/esp-p6-frp-chunked-20260926/firmware/build/esp_base.bin
python3 /private/tmp/esp-p6-frp-chunked-20260926/run_qemu.py /private/tmp/esp-p6-frp-chunked-20260926/firmware /private/tmp/esp-p6-frp-chunked-20260926/qemu-scan.log 25
```

上述 `0x121000` 镜像带仓外容量探针、固定 guest 字节、ADC2 QEMU 空桩、UART0 控制台和测试签名键，FRP/MQTT/OTA 的业务能力仍主要通过 map 深链接，未运行真实网络负载。此轮没有逐符号 map 差分去除探针与空桩，因此**不能把 `0x121000` 当作生产五组件固件大小**，也不能据此更新上一节的 Flash 产品布局结论。QEMU 空桩镜像不可刷实板；本轮没有设备写入、真实 Wi-Fi/FRPS/Broker/OTA 会话、完整密文记录解密或包槽迁移。

## 2026-09-26：本地五候选精确源码同镜像复测

本次不沿用上一节旧 Base/MQTT/OTA 组件源码，而是在 mac-work-1 的独立 `/private/tmp/esp-p6-exact-c3-20260926` 用五个本地完整提交重新组装：Base `6976bc43be5c4ec6321abdae53a22e89d7d742ea`、FRP `533e29467b24d01157ff3b5229e62c93d101be61`、MQTT `9d6d95e779f4f5ff387a6d9b54015bf4e43565f2`、OTA `5da4a0dfbbe97723286e1a9b050e7029cff6e718`、Container `8eb805f3f12cb3cd836e9833acb4aca878ae80e7`。ESP-IDF、lwIP、WAMR 仍分别固定 `578cf89c343e388db43ba1f4ddcd602fedcb763c`、`2758df4cd3666b3b2a5b53830148379326425c0d`、`26c235e53e29acd8b43abe7f3b524577bd4d1ae5`。完整[源码锁](five-component-exact-source-lock.txt) SHA-256 `dd3423b297d093667892c0b2684f131612b9e49de802f10ea8653033509a5b58`，[24 项探针输入摘要](five-component-exact-probe-inputs.sha256)自身 SHA-256 `63cfb8668440ca926ccf4eddf399692a259e8ab21fe4418e2c3d72793600b99d`。`diff -qr` 核对仓外四个本地组件与各自提交归档的组件源码相等。

**依赖锁边界**：Base `6976bc43` 正式 `idf_component.yml`/`dependencies.lock` 仍指向旧 FRP、MQTT、OTA、Container。仓外实验按[准备脚本](prepare_exact_five_component_qemu.sh)把五仓提交归档直接复制成 IDF 本地组件，并只在该实验副本删去旧远程版本声明，重新生成只含 cJSON/WAMR 的 `dependencies.lock`（SHA-256 `ef846a7917fe1c87f0e3d429c923a5f072310268d49efb8e598b65fa4eae49e4`）。这不是 Base 正式锁升级；五仓精确来源由上面的源码锁与组件字节核对固定。没有添加兼容接口或替换真实组件。实验还复用前节 QEMU 专用 UART0 主控制台、ADC2 校准空桩、RSA 测试键、固定 469 字节 ABI 2 counter guest 和[FRP 记录头探针](frp_chunked_capacity_probe.c)；只在 Base `CMakeLists.txt` 中明确关闭 WAMR shrunk memory。`esp_base_main.c` 相对 Base 提交仅增加探针调用和堆采样，普通产品源未在工作区修改。

固定 SDK 完整链接、`counter_guest.py check`、RSA 第 0 签名块验证均通过。ELF map 实际保留 `efrp_aead_reader_init_chunked`、`efrp_tls_step`、`esp_mqtt_client_start`、`eota_preflight`、`econtainer_runtime_open`、`wasm_interp_call_wasm`。QEMU 日志 SHA-256 `ecb08ff2d798583d2f357683d0d20ee80ea7a0e6b33ec821632b639f0acb6213`；25 秒后由宿主 SIGTERM 结束，无 panic。Base READY 后 guest 的 `open/init/event/stop` 均返回 0，counter 结果为 3；其存活时 free/最大连续块仍为 **66,588/45,056 字节**。此时 reported 为 `provisioned=false`、`wifi_state=unconfigured`、`mqtt_state=unconfigured`、`frp_state=unconfigured`、`frp_sessions=0`。

| Base READY 后 guest 存活时声明的明文长度 | 实际 reader 记录头分配 | 分配期间 free／最大连续块 | 清理后 free／最大连续块 |
| --- | --- | ---: | ---: |
| 4 KiB | 1 块成功 | 62,488／45,056 | 66,588／45,056 |
| 32 KiB | 8 块成功 | 33,788／20,480 | 66,588／45,056 |
| 48 KiB | 12 块成功 | 17,388／7,680 | 66,588／45,056 |
| 56 KiB | 14 块成功 | 9,188／3,584 | 66,588／45,056 |
| 60 KiB | 第 15 块失败，`EFRP_NO_MEMORY=-20` | 回滚后 66,588／45,056 | 66,588／45,056 |
| 64 KiB | 第 15 块失败，`EFRP_NO_MEMORY=-20` | 回滚后 66,588／45,056 | 66,588／45,056 |

60/64 KiB 失败过程的启动以来低水位为 9,012 字节；已分配的 14 块共 57,344 字节由 reader 的失败路径释放，堆回到进入前。Base 初始化前 guest 存活的 64 KiB 长度头则能成功分配全部 16 块，分配后 free/最大连续块 58,212/45,056，清理后恢复 123,812/59,392。以上只投喂 nonce 与长度头，**没有 FRP session/TLS/FRPS、密文、tag 或已认证明文**；56 KiB 的头分配成功不是网络会话或整条记录成功。当前精确五候选的这个 QEMU 切片，甚至在省略真实会话资源后也不能与 64 KiB guest 同时申请满长 FRP 记录；P6-03 仍未验收。

### 固件与常驻内存尺寸

| 制品或内存段 | 本次读数 |
| --- | ---: |
| `idf.py size` 的 Flash `.text` | 864,266 字节 |
| `idf.py size` 的 Flash `.rodata` | 220,564 字节 |
| `idf.py size` 的 DRAM 总使用 | 170,830 字节（`.bss` 91,616、`.text` 65,910、`.data` 13,304） |
| `idf.py size` 的总 image | 1,164,352 字节；不是签名制品长度 |
| 未签名 `esp_base-unsigned.bin` | `0x120000`，SHA-256 `cc295491037d7bf60c10dd656eaf802338668d0ea8d5bfc15553df86e67266a5` |
| RSA 测试键签名 `esp_base.bin` | `0x121000`，SHA-256 `8efec6a722d666b86b4098fa9a13059056999ee8b8a14e8c3aa1e66e484d77d0` |

`esptool image-info` 在未签名镜像中读到第 6 段为 `PADDING`：段头 `0x11c580..0x11c588`，填充数据 `0x11c588..0x11ffc8`，长 `0x3a40`（14,912）字节；其后 56 字节 checksum/hash footer 到 `0x120000`。RSA 签名扇区占 `0x120000..0x121000`。若保持此段结构、目标进入前一个 64 KiB 签名台阶 `0x111000`，未签名镜像必须进入 `0x110000`，非 padding 段末端须从 `0x11c580` 降至不高于 `0x10ffc0`，当前布局差 **`0xc5c0`（50,624）字节**。这是按当前段/对齐结构计算的镜像边界门槛，必须重建验签才能证明跨档；它不等于需要删去的源码行或某个单独函数大小，尤其不能把上一节三包槽的 32 KiB 原始几何缺口直接当作代码裁剪量。镜像还包含 QEMU 专用探针、ADC2 空桩、固定 guest 与测试签名输入，未做生产镜像的 map 差分，所以本表不是正式五组件固件大小。

用[第一方 DRAM map 解析器](map_first_party_dram.py)只筛本次 ELF map 中设备地址范围的第一方 `.bss`/`.data`，排除 SDK、WAMR、上游 MQTT 核心和 QEMU 探针，常驻符号前十如下。这些 BSS 字节影响可用 RAM，**不按同额减少签名 Flash 镜像**；FRP session、Yamux、TLS 和分块 reader 的动态堆申请也不在此表。

| 符号 | 源码对象 | 常驻 BSS 字节 |
| --- | --- | ---: |
| `s_reader` | `device_protocol/esp_base_protocol.c:49` | 9,228 |
| `command` | `device_protocol/esp_base_protocol.c:62` | 8,400 |
| `s_work` | `device_protocol/mqtt_owner.c:14` | 7,716 |
| `s_context` | `device_protocol/esp_base_protocol.c:46` | 7,632 |
| `s_load_bytes` | `remote_config/esp_base_remote_config.c:17` | 7,618 |
| `s_commit_bytes` | `remote_config/esp_base_remote_config.c:18` | 7,618 |
| `s_candidate` | `device_protocol/esp_base_protocol.c:61` | 7,608 |
| `s_guard` | `device_protocol/esp_base_protocol.c:48` | 4,872 |
| `s_config` | `device_protocol/frp_owner.c:7` | 2,730 |
| `s_outcomes` | `device_protocol/esp_base_protocol.c:86` | 2,048 |

复核入口：先在 darren-space 工作区执行下面命令，五个完整提交从本地 Git 对象导入 mac-work-1 独立目录；[准备脚本](prepare_exact_five_component_qemu.sh)固定复制及仅供 QEMU 的修改。它仍需上一节保留的仓外 ABI 2 工程与测试键：

```bash
ssh mac-work-1 'mkdir -p /private/tmp/esp-p6-exact-c3-20260926/{base,frp,mqtt,ota,container}-src'
git -C tooling/esp-base archive 6976bc43be5c4ec6321abdae53a22e89d7d742ea | ssh mac-work-1 'tar -xf - -C /private/tmp/esp-p6-exact-c3-20260926/base-src'
git -C tooling/esp-frp archive 533e29467b24d01157ff3b5229e62c93d101be61 | ssh mac-work-1 'tar -xf - -C /private/tmp/esp-p6-exact-c3-20260926/frp-src'
git -C tooling/esp-mqtt archive 9d6d95e779f4f5ff387a6d9b54015bf4e43565f2 | ssh mac-work-1 'tar -xf - -C /private/tmp/esp-p6-exact-c3-20260926/mqtt-src'
git -C tooling/esp-ota archive 5da4a0dfbbe97723286e1a9b050e7029cff6e718 | ssh mac-work-1 'tar -xf - -C /private/tmp/esp-p6-exact-c3-20260926/ota-src'
git -C tooling/esp-container archive 8eb805f3f12cb3cd836e9833acb4aca878ae80e7 | ssh mac-work-1 'tar -xf - -C /private/tmp/esp-p6-exact-c3-20260926/container-src'
scp tooling/esp-container/docs/operations/prepare_exact_five_component_qemu.sh mac-work-1:/private/tmp/esp-p6-exact-c3-20260926/prepare_exact_qemu.sh
scp tooling/esp-container/docs/operations/frp_chunked_qemu.py mac-work-1:/private/tmp/esp-p6-exact-c3-20260926/run_qemu.py
scp tooling/esp-container/docs/operations/map_first_party_dram.py mac-work-1:/private/tmp/esp-p6-exact-c3-20260926/map_first_party_dram.py
ssh mac-work-1 'zsh /private/tmp/esp-p6-exact-c3-20260926/prepare_exact_qemu.sh'
```

随后在 mac-work-1 终端执行：

```bash
export IDF_PATH=/Users/darrenyou/.cache/darren-space/esp-idf-578cf89
source "$IDF_PATH/export.sh"
idf.py -C /private/tmp/esp-p6-exact-c3-20260926/probe-base/firmware -D ESP_BASE_CONTAINER_BINDING_PROBE=ON build
python -m espsecure verify-signature --version 2 --keyfile /private/tmp/esp-p6-exact-c3-20260926/probe-base/test-key.pem /private/tmp/esp-p6-exact-c3-20260926/probe-base/firmware/build/esp_base.bin
idf.py -C /private/tmp/esp-p6-exact-c3-20260926/probe-base/firmware size
python3 /private/tmp/esp-p6-exact-c3-20260926/run_qemu.py /private/tmp/esp-p6-exact-c3-20260926/probe-base/firmware /private/tmp/esp-p6-exact-c3-20260926/qemu.log 25
python3 /private/tmp/esp-p6-exact-c3-20260926/map_first_party_dram.py /private/tmp/esp-p6-exact-c3-20260926/probe-base/firmware/build/esp_base.map
```

这份镜像借助 QEMU 不提供的 ADC2 校准空桩，**不可刷实板**；本轮没有真实设备写入、Wi-Fi/FRPS/Broker/OTA 并发、完整 AEAD 密文验证、产品包安装或分区迁移。正式 Base 依赖锁与实板组合仍须独立闭合。
