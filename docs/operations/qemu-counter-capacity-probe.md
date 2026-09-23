# ESP32-C3 QEMU counter 容量切片：2026-09-23

## 结论与范围

固定 wasi-sdk 33 生成的 437 字节 counter guest，在官方 Espressif QEMU 的 ESP32-C3 机器上完成 WAMR Classic 加载、实例化、`econtainer_init`、两次 `econtainer_on_event`、`econtainer_stop` 和卸载。guest 的 Wasm 线性内存初始值与最大值均为 128 KiB。实例和执行环境存在时，8-bit heap 的最低阶段采样空闲量为 179,436 字节，最大连续块为 114,688 字节；卸载后两项恢复到 WAMR 初始化完成时的 325,864 和 188,416 字节。另一个无限循环 Wasm 在正数 1,000 条指令额度下得到精确的 `Exception: instruction limit exceeded`。

这是**独立 C3 样例的 QEMU 软件证据**。没有实板写入、五组件同时运行、签名业务包安装、Flash 包槽或设备资源配额验证，不能据此验收 P6-02 的实板部分或 P6-03。

## 固定输入与制品

| 输入 | 本次事实 | 在本切片中的作用 |
| --- | --- | --- |
| esp-base | 公开提交 `10cb8514e8f7a3a55b8ec4622cce4f98a0f90eea` | 仅标明[五组件容量原型](five-component-capacity-probe.md)的 Base 上下文；**未链接到本次 QEMU 固件** |
| esp-container | 公开提交 `6e69bb0352378d10ad2455789a80a3661212d955` | 独立临时 clone 的基线，含 C3 pthread 探针；本次注入 guest 与阶段日志只发生在仓外临时副本，后续组件私有单实例运行 API 未参与本镜像 |
| ESP-IDF / esp-lwip | `fff9895c82d744c7237be8847347bdd1b07c6643` / `2758df4cd3666b3b2a5b53830148379326425c0d` | 固定 C3 构建，`check_sdk.py` 核对通过 |
| WAMR | 公开维护 fork `a34d721b630213f59fde0b40cebbb980903660e8`；Component Manager 哈希 `e7a23b9581c6e5232c92d3595911231f0089ccb8e00406fe3ad4a344f1443633` | `dependencies.lock` 固定的 Classic Interpreter；开启指令计量，关闭 Fast/AOT/WASI/guest pthread/shared memory/bulk memory |
| guest 工具链 | wasi-sdk `33.0+m`，LLVM `22.1.0`、源码标识 `4434dabb6991`；官方 macOS arm64 发行包 SHA-256 `85c997a2665ead91673b5bb88b7d0df3fc8900df3bfa244f720d478187bbdc78` | `tools/counter_guest.py` 的固定 freestanding 编译和静态 ABI/profile 检查 |
| C3 工具链 | `riscv32-esp-elf-gcc` `15.2.0`，crosstool-NG `esp-15.2.0_20251204` | ESP-IDF 官方 C3 编译入口 |
| 仿真器 | 官方 `qemu-riscv32`：QEMU `9.2.2`，发行标识 `esp_develop_9.2.2_20260417`；本机 `qemu-system-riscv32` SHA-256 `3e38982c1ea3e750edfc8c910a0fd44727fe07d9c666b84d18d2b7985ac58246` | `-M esp32c3`，4 MiB 仿真 Flash；运行环境为 macOS 27.0 arm64 |
| counter guest | 437 字节，SHA-256 `1896ae9ed2390bd4fb71af9d50965533a679245806c9c07675d65b81be42c91c` | 检查器确认一个非共享线性内存，初始/最大均为 2 页即 131,072 字节，无 imports/start |
| 仿真 app bin | 233,456 字节（`0x38ff0`），SHA-256 `e20d523d708b3f2120122f920f28beb5110b14fdf1f73f18a95208646892b446` | 仓外临时样例构建；`0x100000` app 分区尺寸检查通过 |

本次 guest 由公开 `examples/counter/counter.c` 通过 `tools/counter_guest.py build --wasi-sdk /tmp/wasi-sdk-33.0-arm64-macos` 生成，随后通过同一工具的 `check` 命令。临时副本把这 437 字节作为可写 C 数组嵌入样例；在 `main.c` 中增加阶段堆采样，并按 `econtainer_wasm_check` → `wasm_runtime_load` → `wasm_runtime_instantiate(module, 4096, 0, ...)` → `wasm_runtime_create_exec_env(instance, 4096)` → guest 调用 → 销毁执行环境、实例和模块的顺序执行。每次 guest 调用前均调用 `wasm_runtime_set_instruction_count_limit(environment, 1000)`。用于证明额度异常的无限循环 Wasm 也独立设置相同正数额度。临时 C 源码、嵌入头文件、guest 字节与构建目录均未进入源仓；该 bin 摘要只绑定这份仓外实验制品，不能由公开提交直接逐字节重建。

wasi-sdk 发行包摘要沿用仓内[来源记录](../design/source-provenance.md)；本轮已解压目录回读 `VERSION` 与 clang 版本，并重建、检查产物摘要，没有重新校验已不在本机的压缩归档。

## QEMU 运行记录

`idf.py -C examples/c3-runtime build` 完成后，使用官方 `idf.py -C examples/c3-runtime qemu --qemu-extra-args=-no-reboot` 启动。应用在仿真启动约 88 毫秒内返回；QEMU 随后保持空闲，实验在 10 秒后结束仿真进程。启动和调用期间没有断言或 panic。

| 阶段 | 8-bit free（字节） | 最大连续块（字节） | `min_since_boot`（字节） |
| --- | ---: | ---: | ---: |
| `before`：WAMR 初始化前 | 325,968 | 188,416 | 325,968 |
| `runtime_ready`：WAMR 初始化后 | 325,864 | 188,416 | 325,864 |
| `counter_loaded`：模块加载后 | 325,184 | 188,416 | 324,540 |
| `counter_instantiated`：128 KiB guest 实例化后 | 185,328 | 114,688 | 185,328 |
| `counter_exec_env`：执行环境创建后 | 179,436 | 114,688 | 179,436 |
| `counter_after_calls`：init/event/stop 后 | 179,436 | 114,688 | 179,436 |
| `counter_unloaded`：执行环境、实例和模块释放后 | 325,864 | 188,416 | 179,436 |
| `after`：额外额度探针与 WAMR destroy 后 | 325,864 | 188,416 | 179,436 |

从 `counter_loaded` 到 `counter_instantiated`，阶段空闲量减少 139,856 字节；其中包含固定 131,072 字节线性内存及 WAMR 实例开销，不能全部归作 guest 数据。从 `runtime_ready` 到 `counter_exec_env` 的阶段空闲量减少 146,428 字节，最大连续块减少 73,728 字节。卸载后 `free` 和 `largest` 均回到 `runtime_ready` 的采样值；最终 `after` 较初始化前 `before` 少 104 字节，这两个阶段还包含运行时与 pthread 的生命周期差异，不能单独据此判定泄漏。

`min_since_boot` 是 IDF `heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)` 的**启动以来各内存区低水位之和**，卸载后不会回升，且各区低水位可能发生在不同时刻。表中的值是阶段采样与该 API 的保守低水位，不是持续采样的单一时刻总堆峰值，也不是五组件运行峰值。

实际调用日志显示：`econtainer_init` 返回 0；`econtainer_on_event(1024, 3)` 返回 3；第二次 `econtainer_on_event(1024, 2)` 返回 5；`econtainer_stop` 返回 0。四次调用均 `call_ok=1`、`exception=none`。独立 `run` 返回 0；`looping` 返回 `call_ok=0` 和精确额度异常；最终 `counter=1 normal=1 instruction_limit=1`。

## 尚未证明

- 本镜像没有 Base、Wi-Fi、TLS、FRP、MQTT、OTA、签名业务包或三包槽；[五组件容量原型](five-component-capacity-probe.md)只完成组合链接与 Flash 几何，不能把两份独立数据相加推算同机 RAM 峰值。
- QEMU 内存分配与 C3 实板、网络并发任务、TLS 握手、包存储和持续运行负载不同。128 KiB 固定 guest 在本切片成功实例化，不等于生产组合有足够 heap 或最大连续块。
- counter 只按事件长度计数，没有读取事件字节；此次仅验证 guest 地址范围及调用 ABI，未验证宿主复制、授权、句柄、设备验包、资源配额、超时或取消。
- 没有刷板、改分区、改生产密钥或执行掉电测试；P6-02 的实板项及 P6-03/P6-04/P6-05 均未因此验收。
