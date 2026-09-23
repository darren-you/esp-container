# ESP32-S3 五组件仓外静态探针：2026-09-24

## 判定与范围

维护者尚未选择新的硬件目标。本报告只检验 ESP32-S3-DevKitC-1-N8R8（8 MiB Flash、8 MiB Octal PSRAM）作为容量候选时，当前五仓源码能否在固定 ESP-IDF 6.1 工具链下编译、链接、签名，以及一个 8 MiB 分区布局是否满足静态几何约束。**它不支持 S3 发布，也不完成 P6-03。**全部操作在 `/tmp/esp-s3-five-probe.W2i9dz` 的仓外副本执行；没有修改五仓源码、刷写设备、运行 S3 固件或改变当前 C3 产品目标。

第一次加入 Container 后，CMake 因没有启用其要求的 WAMR Classic/Normal 配置而按预期拒绝构建。仓外配置补齐后，Xtensa 编译、五组件强制链接、测试键签名与镜像槽大小检查全部通过；没有遇到需要修改五仓 C 源码才能通过的 S3 编译错误。这个结果只说明静态工具链通路存在，不说明原样 C3 固件可以在 S3 上正确运行。

## 精确输入

| 输入 | 本次提交或版本 |
| --- | --- |
| ESP-IDF 公开维护 fork | `855937cf9dcee13ee9c423fb0319238cdc8d53fd`，IDF v6.1 |
| SDK 内 esp-lwip | `2758df4cd3666b3b2a5b53830148379326425c0d` |
| esp-base | `ab8d839669ff262b40b20a7ba51c0f4b72c00e3d` |
| esp-frp | `9158b7f2e2c555a14636aed26b5189902152d19e` |
| esp-mqtt | `d099d0ad8d41cb2e5f7d0849baaae98bb7a3d748` |
| esp-ota | `3731db7da35a262ff06c67951cd0e359dd1711a7` |
| esp-container | `6e2bd21f79451dd8a04dc57f511e4c3b51b09f9d` |
| Container 锁定的 WAMR fork | `a34d721b630213f59fde0b40cebbb980903660e8` |
| Xtensa 编译器 | SDK 工具定义的 `xtensa-esp-elf@esp-15.2.0_20251204` |

在临时副本中，Base 的 `device_protocol/idf_component.yml` 将 MQTT 精确版本由当前 Base 主线仍锁定的 `9cac455b0184420353ff0283df3f100abaac3e6b` 改为上表当前公开版本；生成的 `dependencies.lock` 回读了 FRP、MQTT、OTA、WAMR 的上述完整 SHA，target 为 `esp32s3`。Container 作为临时 `EXTRA_COMPONENT_DIRS` 参与同一个 Base 工程；Base 当前产品源码并未正式消费 Container。

临时 `sdkconfig.defaults` 将 target 与 Flash 选为 `esp32s3` / `8MB`，启用 `CONFIG_SPIRAM=y`、`CONFIG_SPIRAM_MODE_OCT=y`、`CONFIG_SPIRAM_SPEED_80M=y`、`CONFIG_SPIRAM_USE_MALLOC=y`，并保留 `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=16384`、`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=32768`。WAMR 使用组件要求的 Classic/Normal 解释器与指令计量，关闭 AOT、Fast Interpreter、WASI、guest pthread、共享内存、bulk memory 等未采用特性。使用仓外 RSA-3072 测试签名键启用 signed apps 与 update 验签；测试键不进入仓库或报告。签名会改变制品字节，因此下列 SHA-256 只对应本次测试键与本次构建。

为了防止链接器剔除未装配的 Container，临时 `app_main` 调用了一个仅受 `volatile` 假门控制的不可执行保留函数，引用 `econtainer_runtime_open`、`econtainer_package_verify`、`econtainer_package_wasm_check` 和 `econtainer_slots_idf_bind`。这个函数传入占位参数，**只能用于链接分析，不能刷板运行**；它不是产品安装接口，也不改变 Container 私有运行期的边界。FRP/MQTT/OTA 使用 Base 现有 owner/调用链进入同一 ELF。

复核方法是使用上述精确仓源在独立临时目录准备 Base 工程与 Container 组件，先运行各仓 `tools/check_sdk.py` 核对 IDF/lwIP，按本节临时配置运行 `idf.py -C <临时 Base 固件目录> build`，再核对 `dependencies.lock`、ELF 符号、分区表、signed bin 的大小与 RSA 签名。工具链检查与构建均不打开串口。不要把本探针的临时分区、私有运行期引用或 C3 policy 改写直接合入发布工程。

## 分区与镜像结果

本次只在仓外用以下分区几何验证 8 MiB Flash；`product_store` 是一个 3 MiB 的专用 `data/undefined` 分区，三份 1 MiB 包槽是其内部几何，不是三个 IDF 分区：

| 区域 | 偏移 | 大小 |
| --- | ---: | ---: |
| 现有前置 NVS/otadata/PHY/coredump | `0x9000` 至 `0x20000` | 保留原有标签、类型与尺寸 |
| `ota_0` | `0x20000` | `0x200000` |
| `ota_1` | `0x220000` | `0x200000` |
| `product_store` | `0x420000` | `0x300000` |
| `base_store` | `0x720000` | `0x20000` |
| 未分配尾部 | `0x740000` | `0xc0000`（786,432 字节） |

固定 SDK 的 `gen_esp32part.py --flash-size 8MB --secure v2` 通过。当前签名镜像 `esp_base.bin` 为 `0x111000`（1,118,208）字节；每个 2 MiB app 槽尚有 `0xef000`（978,944）字节静态空间。镜像 SHA-256 为 `aa4c0c320831eb3cfe5c054ff330cf8ef0a47f0357cd58814de8513bab485949`，`espsecure verify-signature --version 2` 对第 0 块 RSA 验证通过。ELF 符号回读包含 `efrp_create`、`emqtt_create`、`eota_prepare`、`econtainer_runtime_open`、`econtainer_package_verify`、`econtainer_package_wasm_check`、`econtainer_slots_idf_bind`、`wasm_interp_call_wasm`。

先前 [C3 五组件容量原型](five-component-capacity-probe.md)在现有包格式下把 512 KiB Wasm 打成 532,480 字节的 `product.pkg`；单个 1 MiB 包槽在几何上可容纳该样本并余 516,096 字节。它没有证明真实业务包的大小、三槽元数据/擦写策略或产品可用额度；本次没有把任何包写入候选 Flash。

## 尚未闭合的 S3 运行边界

- Base 的 `sdkconfig.defaults`、设备型号字符串和 OTA 产品 policy 仍专属于 C3：`ESP_BASE_OTA_TARGET` 为 `esp32c3/esp_base`，芯片 ID 为 `0x0005`，OTA 双槽仍是 C3 的 `0x20000/0x200000/0x1e0000`。本次只改了仓外分区 CSV，未把 OTA policy 改成 S3 的真实分区，因此该镜像不能用于 S3 升级或回退。
- FRP Login 中的 `arch` 仍硬编码 `riscv32`，对 Xtensa S3 是错误事实；设备身份只将 C3 识别为受支持型号。芯片切换还需要检查 USB/恢复、真实 Bridge/设备绑定和新板初始身份，不把 C3 镜像或 Flash 数据当作跨芯片可迁移制品。
- `SPIRAM_USE_MALLOC` 的 16 KiB 阈值可使 FRP 的 65,552 字节 AEAD 接收区及 WAMR 大块分配优先申请外部 RAM，但本次没有启动固件、测量实际分配地址、最小空闲 heap、最大连续块或内部栈余量。FRP 的会话/Yamux、MQTT、TLS、Wi-Fi、HTTPS OTA 与 guest 同时运行的资源峰值仍未知，不能把 8 MiB PSRAM 标称容量当作组合容量验收。
- 固定 IDF 的 `docs/en/api-guides/external-ram.rst` 明确指出，默认 Flash 写入使 cache 失效时 PSRAM 也不可访问；本次 `CONFIG_SPIRAM_XIP_FROM_PSRAM`、`CONFIG_SPIRAM_FETCH_INSTRUCTIONS`、`CONFIG_SPIRAM_RODATA` 均未启用。业务包写入与固件 OTA 期间的 guest 暂停/恢复、双核调度及内部 RAM 资源尚未设计和实测。FreeRTOS 任务栈默认保留在内部 RAM，不能按 8 MiB 外部 RAM 计算。
- 没有 S3 实板或仿真运行、PSRAM 启动检测、真实产品包安装/激活、签名双固件切换、三槽保护集、NVS 持久/掉电恢复、72 小时稳定性以及真实 Flash 迁移证据。P6-03、P7 及 S3 发布状态保持未验收。维护者确认硬件目标后，才能据此开始正式移植与设备级验证；本探针不构成设备写入授权。
