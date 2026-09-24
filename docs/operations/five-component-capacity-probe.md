# 五组件 C3/4 MiB 仓外容量原型：2026-09-23

最新 Base、FRP、MQTT、OTA、Container 和 SDK 精确提交的签名镜像、64 KiB guest QEMU 堆数据及当前 4 MiB 几何复测见[五组件 QEMU 容量切片](five-component-qemu-capacity-probe.md)末节。本页以下旧组合的镜像摘要与动态数据不可替代该复测；P6-03 仍未验收。

## 2026-09-24 Base v3 与真实 provider 静态复测

仓外复制公开 `esp-base@56bf1356e0d4b2924ff61a6f333b80e1b34aac00`，保留其精确依赖 `esp-frp@9158b7f2e2c555a14636aed26b5189902152d19e`、`esp-mqtt@9cac455b0184420353ff0283df3f100abaac3e6b`、`esp-ota@bed5709fe517f62d60f2efad95491bc66756a42c`，加入公开 `esp-container@7f12e19022b9fd7632678d6865c5d1a5255799e6` 和其中精确锁定的 WAMR fork `a34d721b630213f59fde0b40cebbb980903660e8`。固定 SDK 仍为 `esp-idf@855937cf9dcee13ee9c423fb0319238cdc8d53fd` / `esp-lwip@2758df4cd3666b3b2a5b53830148379326425c0d`。Component Manager 重新解析的锁文件分别显示 FRP、MQTT、OTA 和 WAMR 的上述完整提交；Container 作为仓外本地组件加入，仅用于容量探针，不构成 Base 的正式依赖。

沿用下文的不可执行 `volatile` 链接门，保留真实 Base FRP owner、MQTT、OTA、Container 验包/Wasm 授权/三槽、ESP-IDF Flash/NVS provider 及 WAMR Classic 符号。临时 RSA-3072 测试键签名 C3 镜像为 `0x121000`（1,183,744 字节），SHA-256 `03f61c8b693ddfe2ee805a024be7c28095e2a8ceb4f5ead74a3e6b6524fa0ed5`；ELF 符号表确认 `efrp_create`、`emqtt_create`、`eota_prepare`、`econtainer_package_verify`、`econtainer_package_wasm_check`、`econtainer_slots_reconcile`、`econtainer_slots_idf_bind` 和 `wasm_runtime_call_wasm` 均被保留，`espsecure verify-signature --version 2` 用该测试键导出的公钥验签通过。

此结果只证明当前五组件软件能链接为同一签名镜像，且镜像大小尚未改变下文 4 MiB 几何结论。provider 只有编译与链接，没有真实专用 data 分区；现有 Base 分区表无三包槽，探针也不运行 FRP/MQTT/OTA 会话或 guest。临时镜像不能刷板，P6-03、P6-08 和 P7-01 均未验收。

## 2026-09-24 当前源码静态复测

沿用仓外组合工程和临时 RSA-3072 测试键，更新到公开 `esp-base@ecf1539ee90b5c256df0bae6004d27b0b2683de5`、`esp-frp@9158b7f2e2c555a14636aed26b5189902152d19e`、`esp-mqtt@d099d0ad8d41cb2e5f7d0849baaae98bb7a3d748`、`esp-ota@bae8d13ca5f99c730c667bc55d6ea6a0d883e608`，以及本仓合并的流式验包、Wasm 静态授权和三包槽源码 `0df04118023e6be1c3bcb75f82fff061cb3c45f3`。SDK 为公开 `esp-idf@855937cf9dcee13ee9c423fb0319238cdc8d53fd`，其 lwIP 检出为 `2758df4cd3666b3b2a5b53830148379326425c0d`，WAMR 仍固定 `a34d721b630213f59fde0b40cebbb980903660e8`。

仓外 `capacity_references.c` 的不可执行 `volatile` 门保留了 FRP TLS、MQTT start、OTA HTTPS、Container 流式验包、Wasm 静态授权、三槽 `reconcile`/`write_and_prepare` 和 WAMR Classic 调用路径。固定 SDK 的 C3 链接 map 已逐项核对这些符号；临时签名镜像仍为 `0x121000`（1,183,744 字节），SHA-256 为 `e3298138c8ae38aa5c3be6278933aba7feb2e5db74d011850553c615c969c312`，`espsecure verify-signature --version 2` 通过。`0x140000` 双 app 槽布局在此镜像下各留 `0x1f000`，三包槽仍只是假设的 `0x60000` 几何。与下文 2026-09-23 原型相比，当前 Base 已删除旧 MQTT 运行层；本轮没有依靠临时移除生产组件解决链接冲突。

这仍是静态链接与分区算术，不执行真实 FRP/MQTT/OTA/guest 会话，不证明 Flash 擦写或跨 boot 三槽恢复，也没有 C3 实板 heap、最大连续块、网络并发或分区迁移结果。P6-03 仍未验收；仓外探针和测试签名镜像不得刷板。

## 判定

P6-03 **未验收**。仓外工程把 Base 正常应用与 FRP、MQTT、OTA、Container 的代表性运行入口链接成一份 ESP32-C3 固件，并用临时 RSA-3072 密钥签名。签名镜像为 `0x121000`（1,183,744 字节）。在保留现有 NVS、otadata、coredump 和 `base_store` 后，4 MiB 可以画出双固件加三包槽的分区表，但按当前镜像取几何上限时两个固件槽没有增长余量；当前 host 默认的 512 KiB Wasm 打成包也超过该槽。另一个留余量布局只证明分区几何与当前镜像可以共存，不是产品包限额或迁移方案。

本原型没有写设备、修改现有产品分区表、读取生产凭据或验证动态 RAM。临时签名键、二进制和组合工程均在 `/tmp`，不进入 Git。

## 精确源码与组合方法

| 输入 | 公开提交 |
| --- | --- |
| ESP-IDF | `fff9895c82d744c7237be8847347bdd1b07c6643`，内含固定 esp-lwip `2758df4cd3666b3b2a5b53830148379326425c0d` |
| esp-base | `10cb8514e8f7a3a55b8ec4622cce4f98a0f90eea` |
| esp-frp | `dd9d52ae183c0db069ba302cf850b620f61800fb` |
| esp-mqtt | `ac5c6ca2862ab2906b336e73576eb93c85d0ef00` |
| esp-ota | `9dc8aa70236b5b9b376a6ba7db39ede049007c8d` |
| esp-container | `4a37af82b44a8a4e4444a64652cee6c6e29e08eb`；本检查点仅增加 CMake profile 守卫和记录，不改组件 C 源码 |
| WAMR | Container 清单固定公开 fork `a34d721b630213f59fde0b40cebbb980903660e8`，锁文件与下载组件哈希一致 |

从以上公开提交克隆后，在仓外复制 `esp-base/firmware` 作为单一 IDF 工程。其 `EXTRA_COMPONENT_DIRS` 加入 FRP 根、MQTT 根（临时路径 basename `mqtt`）、OTA `components/esp_ota` 与 Container `components/esp_container`；Base 主应用增加对四组件和 WAMR 的依赖，并从 `app_main` 引用仓外 `capacity_references()`。该函数有始终为假的 `volatile` 门，仅为链接器保留代表性路径：FRP create/start/status/stop/destroy、MQTT create/start/subscribe/enqueue/poll/stop/destroy、OTA preflight/prepare/select/confirm，以及 Container 扫描和 WAMR init/load/instantiate/create exec env/lookup/call/destroy/unload。它传入占位参数，因此**仅能编译链接，不可刷板运行**。只引用 `wasm_runtime_init()` 的第一轮曾错误地留下仅 1,016 字节 WAMR archive；本报告不使用那轮 0x101000 镜像作容量证据。

Base 的普通 app 并不依赖 `components/mqtt_runtime`，但该目录自身的 manifest 会在 IDF 扫描时下载官方 `espressif__mqtt`，与本次本地 esp-mqtt 的 `mqtt_utils_lib`、`mqtt_outbox_lib` 冲突。仓外复制工程临时移除了这个旧试验组件和对应下载目录才完成链接。生产源码仍需 P3-08 在真实依赖图完成硬切；临时排除不能算集成完成。

仓外配置保留 Base 原有 4 MiB、回滚与分区，开启 signed app 和更新验签、全量 CA bundle、OTA HTTPS 自定义 transport、FRP 所需 `LWIP_SO_LINGER` 和 12 socket；按 `examples/c3-runtime/sdkconfig.defaults` 固定 WAMR Classic/Normal、关闭 AOT/Fast/WASI/guest pthread/shared/ref types，构建前设置指令计量开启、bulk/shared memory 关闭。`esp_container` CMake 守卫的正向样例构建通过，故意开启 shared memory 的负向配置被拒绝。签名只用新建的仓外测试键。

## 实际链接与镜像

`idf.py build` 生成的 ELF/map 保留了 `efrp_tls_step` 和 Yamux、`esp_mqtt_client_start`/订阅/发布、`eota_http_transport_create`、`wasm_runtime_call_wasm` 与 `wasm_interp_call_wasm`；以下是 `esp_idf_size --archives --format json2` 的**已保留**贡献，不是把独立样例大小相加。共享 TLS、Wi-Fi、lwIP 与 SDK 依赖不应重复归属到各组件。

| Archive | 已保留总字节 | 其中 Flash Code | 其中 Flash Data |
| --- | ---: | ---: | ---: |
| WAMR | 55,905 | 54,718 | 1,115 |
| FRP | 27,443 | 26,966 | 477 |
| MQTT | 17,331 | 17,072 | 255 |
| `esp_http_client` | 8,504 | 8,198 | 306 |
| `esp-tls` | 7,000 | 6,924 | 72 |
| OTA | 6,501 | 6,500 | 0 |
| Container | 490 | 470 | 20 |

单一组合镜像 unsigned `0x120000`（1,179,648 字节），签名扇区后 `0x121000`（1,183,744 字节），SHA-256 `9e9683229a7dfa58e6b5f592d5141d2aa012c6f38bac744d2a33e3d426b36946`；`espsecure verify-signature --version 2` 用该测试键验证第 0 签名块通过。`esp_idf_size` 显示静态 DRAM 115,386/321,296 字节、表面空余 205,910 字节；这不是 Wi-Fi、TLS、FRP、MQTT、OTA 与 128 KiB guest 同时运行后的 heap 或最大连续块。

## 保留区与两组仅供容量分析的布局

现有产品分区的前段保持：`nvs` `0x9000/0x6000`、`otadata` `0xf000/0x2000`、`phy_init` `0x11000/0x1000`、`coredump` `0x12000/0xe000`；末端 `base_store` 保持 `0x3e0000/0x20000`。中间仅 `0x20000..0x3e0000` 共 `0x3c0000` 字节可放双 app 与三个独立 data 包槽。以下 CSV 均在仓外用固定 IDF 的 `gen_esp32part.py --flash-size 4MB --secure v2` 通过，且 `check_sizes.py` 用上述真实签名镜像核对 app 槽。

| 布局 | ota_0 | product_0 | ota_1 | product_1 | product_2 | 每个 app 增长空间 | 尾部未分配 |
| --- | --- | --- | --- | --- | --- | ---: | ---: |
| 当前镜像的等大包槽几何上限 | `0x20000/0x121000` | `0x141000/0x7f000` | `0x1c0000/0x121000` | `0x2e1000/0x7f000` | `0x360000/0x7f000` | 0 | `0x1000` |
| 留余量的可行性候选 | `0x20000/0x140000` | `0x160000/0x60000` | `0x1c0000/0x140000` | `0x300000/0x60000` | `0x360000/0x60000` | `0x1f000`（126,976 字节） | `0x20000`（131,072 字节） |

第一行的 `0x7f000` 是在本次 `0x121000` 镜像、4 KiB data 与 64 KiB app 起点对齐、保留区不动的前提下穷举两 app/三等包先后顺序得到的**几何上限**，不是可交付 `max_package_size_bytes`。它没有任何固件增长空间。第二行的 `0x60000`（384 KiB）包槽有余量，但仍只是测试输入，不能直接改生产分区。

当前 `product_package.py` 的默认 Wasm 上限是 512 KiB，ustar 内含规范 manifest、384 字节 RSA-PSS 签名和 `app.wasm`。用主机包工具接受的 Wasm v1 小样附加自定义节填充至 524,288 字节，再按 `examples/counter/spec.example.json` 和仓外测试键实际打包、验签，manifest 为 550 字节，`product.pkg` 为 532,480 字节；它比极限包槽 `0x7f000`（520,192 字节）仍大 12,288 字节。同一格式和清单下，507,904 字节 Wasm 形成 512,000 字节包，增加 1 字节使 ustar 包跳至 522,240 字节，已超 `0x7f000`。对留余量候选 `0x60000`（393,216 字节）包槽，同法测得 385,024 字节 Wasm 对应 389,120 字节包可放，增加 1 字节对应 399,360 字节包不可放。这些是当前格式/清单的边界样本，不是业务 Wasm 的有效资源额度或已冻结包上限。`pack` 对给定 manifest/签名/payload 产生固定字节，但 RSA-PSS 签名使用随机 salt，每次 `sign` 的完整包字节可能不同；长度结论不受影响。

## 尚未闭合

- 这个仓外工程仅强制链接路径；Base 的实际五组件装配、任务调度、权限和真实联机负载尚未实现。生产 MQTT 依赖冲突尚未硬切。
- 没有实板或仿真联机 heap/最大连续块数据，未同时运行 Wi-Fi、TLS、FRP、MQTT、OTA 和 WAMR guest；静态 DRAM 空余不能替代 P6-03 的 RAM 峰值和各栈/队列资源表。
- 未验证真实业务包的大小、签名包流式读写、三槽保护集与连续更新，也未设计保留 NVS/回滚的分区迁移。P6-03 不冻结 offset、`max_package_size_bytes` 或 RAM 预算，P7-01 不因此获得刷板授权。
