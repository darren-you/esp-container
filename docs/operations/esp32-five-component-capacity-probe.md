# ESP32-D0WD-V3 五组件签名容量探针：2026-09-26

## 2026-09-26 当前五仓精确锁复测

上一轮 `0xffff4` 镜像使用 Base `bcde4832`、FRP `533e294`、OTA `5da4a0d`，**不是**当前五仓组合。本轮在 mac-work-1 的仓外新目录 `/private/tmp/esp32-five-exact-20260926`，从下表 Git 提交逐份导出源码，再复制为独立 ESP32 工程；FRP／MQTT／OTA／Container 组件源码均与各自归档逐文件一致。没有使用工作树中的未提交更改，也没有接设备或串口。

| 输入 | 精确提交 | `git archive --format=tar \| gzip -n` 的 SHA-256 |
| --- | --- | --- |
| Base | `058e965671fa0e4d417114897541710699571c52` | `5dd4ee1bc055f80e1cbea1029471b167f7ee91eecc0ec0d870728ea3ddd99e1e` |
| FRP | `36e1506a2145321fc292294de59c0aa4532f73a7` | `cb8da748b88beb06539ed790eee834aab55a03165215aeed0ecfd3c5cd2b0d3b` |
| MQTT | `9d6d95e779f4f5ff387a6d9b54015bf4e43565f2` | `9a23528c250e158392b8cbe592c232d70869f199e8b` |
| OTA | `207273188b984161362824c3344614e812016836` | `ec27b304eaa7b573dc978c789673a39292b3f2fe37806e07981769f199e8b110` |
| Container | `8eb805f3f12cb3cd836e9833acb4aca878ae80e7` | `bbde8a28677171fc2c728c77fa9049ae77b2b2055c948b2272a57d320f50e235` |

固定 ESP-IDF 实际 HEAD 为 `578cf89c343e388db43ba1f4ddcd602fedcb763c`，其 lwIP 源码实际 HEAD 为 `2758df4cd3666b3b2a5b53830148379326425c0d`；SDK 根目录唯一状态差异是预定的 lwIP gitlink 从 `c6f2f87` 指向该固定版本，lwIP 自身工作树干净。仓外工程从上表 Base 的真实 ESP32 CSV、`sdkconfig.defaults.esp32` 和 OTA policy 构建，只在副本主应用增加 `volatile` 默认 false 的静态链接门；这个门下引用 FRP TLS／分块 AEAD、MQTT start、OTA HTTPS/preflight、Container runtime/slot runtime/slots IDF provider/Base 固件集合适配及 WAMR Classic。它不会进入实际调用路径。仓外装配脚本 SHA-256 为 `cf3b1517acd3e617497be7a7da7325aa72259ac0c61cd9d50c3418a943d2b3a3`，注入的 `capacity_references.c` SHA-256 为 `68f9dedcb7f501827ac9553790452d4d4faaaa5473fe0bb5ad1a6624bc0c23da`；测试 P-256 私钥和全部镜像只留在仓外，不入库。

本轮 `-DIDF_TARGET=esp32 -DESP_BASE_CONTAINER_BINDING_PROBE=ON` 全量构建通过，完整构建日志 SHA-256 为 `4972ac1554a2d9d197f43d498874e419b11cbd5dfc0904ca983e5170dfcf9726`；生成 `dependencies.lock.esp32` SHA-256 为 `d8b748c3bdaade4d83b4245ba775cc329e256a56a8434d77bf9bff4a3e34b57c`，记录本地五组件源码、`target: esp32` 和 WAMR `26c235e53e29acd8b43abe7f3b524577bd4d1ae5`。最终 `sdkconfig` 确认 ECDSA v1 软件签名的 boot/update 验签、rollback、STA-only、TLS client-only 和 Classic/normal WAMR；CMake profile 启用指令计量。硬件 Secure Boot 未启用。ELF 符号表分别保留 `efrp_tls_step`、`efrp_aead_reader_init_chunked`、`esp_mqtt_client_start`、`eota_preflight`、`eota_http_transport_create`、`econtainer_runtime_open`、`econtainer_slot_runtime_open`、`econtainer_slots_idf_bind`、`econtainer_slots_reconcile`、`econtainer_slots_write_and_prepare`、`esp_base_container_reconcile` 和 `wasm_interp_call_wasm`；ELF 字符串含 ESP32 对应的 `xtensa`。

| 项目 | 旧组合的缩减版 | 当前精确锁 |
| --- | ---: | ---: |
| ECDSA v1 signed app | `0xffff4`，SHA-256 `9763b55620f5c9a12301b66c299f92d0d0be298568e05348ae6fcf715efdc08c` | **1,048,564 B／`0xffff4`**，SHA-256 `32cc57e32883985a64c9ca3a477e5161b73103337f7aa380c9fdb7f5967fc3f4` |
| 未签名 app | 1,048,496 B | 1,048,496 B，SHA-256 `d93c79d885e23e974f2bc8601c76ff9044a6a7fea630109281ef040b7c132d5a` |
| `0x120000` 单 app 槽剩余 | `0x2000c` | **131,084 B／`0x2000c`**；两个等长 app 槽均满足 |
| Flash Code／Data | 728,716／210,304 B | 728,984／208,960 B |
| 静态 DRAM 已用／可用 | 109,930／70,806 B | 109,922／70,814 B |
| 静态 IRAM 已用／可用 | 81,223／49,849 B | 81,223／49,849 B |
| 已保留的 Container／FRP／OTA archive | 16,302／25,385／6,483 B | 17,459／25,361／6,688 B |

镜像虽然与旧组合落在同一个签名长度台阶，SHA、ELF 符号和静态资源账本不同；旧 `0xffff4` **没有被沿用为新版本尺寸证据**。archive 差异还包含链接入口变化，不能单独解释成某一个组件源码的增减。固定 SDK 的 `espsecure verify-signature --version 1 --keyfile <仓外测试键>` 对本轮 app 返回 `Signature is valid`；官方 `check_sizes.py partition --type app` 报告最小 `0x120000` 槽剩余 `0x2000c`。Base 的真实 ESP32 CSV SHA-256 为 `f3f29e52f2ed0ccb3fbb3faf9e3d978d359aa9a958c2b3a6120399af70f11b73`，固定 SDK `gen_esp32part.py --flash-size 4MB --secure v1 --disable-md5sum` 接受它；生成的未签名 3,072 字节表与构建分区表前缀逐字节一致，构建表另有 68 字节 ECDSA v1 签名，官方验签也通过。构建分区表 SHA-256 `aa5d0153914e5c1faecd9fbe31f5e65c8af940911992bd273b3ba130c1d0a576`。bootloader 为 `0x5b10` B，官方检查在 `0x8000` 分区表前还余 `0x14f0` B。

这是一份**静态容量收据**，不证明网络三会话、64 KiB guest 与 Base 同时运行后的堆／最大连续块、三份真实签名业务包安装运行、上电签名启动、OTA 回滚、旧 AT 数据迁移或新分区的实板持久状态。仓外 `volatile` 门内传占位参数，只允许链接，不能作为运行探针刷板。P6-03、P7-01 和双目标完整容量合同仍未验收；本轮没有修改 Base/Container 产品源码、生产密钥或设备 Flash。

## 旧组合结论（历史）

ESP32-D0WD-V3 的仓外五组件深链接镜像已在固定 ESP-IDF 中完成 ECDSA v1 测试键签名和正式工具验签。基线 signed bin 为 **`0x10fff4`**；关闭未使用的 Wi-Fi SoftAP、只保留 TLS client 后为 **`0xffff4`**。两者均装得进本轮离线候选的两个 `0x120000` app 槽，分别余 `0x1000c` 和 `0x2000c` 字节。后者静态 DRAM 只减少 160 字节，不能据镜像缩减量推断运行堆。

**P6-03 与 P7-01 均未验收。** 当时 ESP32 产品构建守卫仍阻断正式镜像；此探针的 OTA 策略和入口改动只在仓外副本中。没有 ESP32 QEMU 动态读数、真实 Wi-Fi／MQTT／FRP／OTA 会话、Wasm 包槽执行、签名启动与回滚实板验证，也没有写板或触碰串口。

## 旧组合输入与实验差分

| 输入 | 固定版本 |
| --- | --- |
| Base | `bcde4832d171aea160f33f8b421e0d3309ff5f01` |
| FRP | `533e29467b24d01157ff3b5229e62c93d101be61` |
| MQTT | `9d6d95e779f4f5ff387a6d9b54015bf4e43565f2` |
| OTA | `5da4a0dfbbe97723286e1a9b050e7029cff6e718` |
| Container | `cb7d8b5a0bfd5d579ce4900281fa98d1708deebe`；其 `components/esp_container` Git tree `4571169742245b1a42d2874073dcaead33caaab8`，与 `8eb805f3f12cb3cd836e9833acb4aca878ae80e7` 的组件 tree 完全一致 |
| SDK | ESP-IDF `578cf89c343e388db43ba1f4ddcd602fedcb763c`、实际 lwIP `2758df4cd3666b3b2a5b53830148379326425c0d`、WAMR `26c235e53e29acd8b43abe7f3b524577bd4d1ae5`；生成的 `dependencies.lock` 为 `target: esp32` 并记录该 WAMR 完整版本 |

准备脚本 [prepare_esp32_five_component_probe.py](prepare_esp32_five_component_probe.py) 从上述五份精确 Git 归档创建两份独立工程，使用既有精确 C3 探针的 `capacity_references.c`、`capacity_runtime_probe.c`、64 KiB ABI 2 guest 字节和固定 WAMR managed component。FRP／MQTT／OTA／Container 组件 C 源码不改。仓外 Base 差分仅为：

1. 暂时移除 ESP32 顶层构建阻断，在探针中指定 `esp32/esp_base`、ECDSA v1 P-256 scheme、`ota_1@0x140000` 与 app 槽 `0x120000`；产品守卫和产品 CSV 不改。
2. 本地引用各精确组件源码，替换远端 Component Manager 声明以避免混入其他版本；仍由生成锁核对 WAMR。主应用增加强制链接入口及 64 KiB guest 生命周期探针，没有复制 C3 的 QEMU ADC2 空桩。
3. 使用单独仓外 ECDSA P-256 测试键、`CONFIG_SECURE_SIGNED_APPS_NO_SECURE_BOOT`、signed-on-boot/update、rollback、`CONFIG_PARTITION_TABLE_MD5=n` 与 bootloader 日志 `NONE`。这些是软件签名构建设置，不启用硬件 Secure Boot／eFuse，也不构成当前旧 AT 板卡的签名启动基线。

两份工程除 `sdkconfig.defaults.esp32` 中 `CONFIG_ESP_WIFI_SOFTAP_SUPPORT=n` 和 `CONFIG_MBEDTLS_TLS_CLIENT_ONLY=y` 外逐文件相同。最终生成配置也分别确认基线 SoftAP 开启、TLS server/client 均开启，缩减版 SoftAP 关闭、TLS client-only 开启；两者保留 Classic/Normal WAMR、指令计量及禁用 AOT、WASI、guest pthread。Base 只设置 `WIFI_MODE_STA`；FRP 和 OTA 使用 mbedTLS client 路径，因此这两个 Kconfig 选择与当前源码的角色相符。尚未做相同选项下的 ESP32 真实联网回归。

复核时将五份精确 Git 归档分别解到仓外目录的 `base-src`、`frp-src`、`mqtt-src`、`ota-src`、`container-src`，准备仓外 P-256 测试键和下表所列的 CSV，再执行：

```bash
probe_root=/private/tmp/esp32-five-capacity-20260926
c3_probe_root=/private/tmp/esp-p6-exact-c3-20260926/probe-base
fixed_idf_path=/Users/darrenyou/.cache/darren-space/esp-idf-578cf89
test_key="$probe_root/test-key.pem"
partition_csv="$probe_root/esp32-capacity-probe.csv"
umask 077
openssl ecparam -name prime256v1 -genkey -noout -out "$test_key"
python3 docs/operations/prepare_esp32_five_component_probe.py \
  "$probe_root" "$c3_probe_root" "$test_key" "$partition_csv"
source "$fixed_idf_path/export.sh"
idf.py -C "$probe_root/baseline/firmware" -DIDF_TARGET=esp32 build
idf.py -C "$probe_root/reduced/firmware" -DIDF_TARGET=esp32 build
espsecure verify-signature --version 1 --keyfile "$test_key" \
  "$probe_root/reduced/firmware/build/esp_base.bin"
```

上述命令只构建与读取制品，不运行 `flash`。基线镜像也按同一 `verify-signature` 入口单独验证；重新生成测试键时镜像签名字节及 SHA-256 会改变。

## 签名镜像、静态资源与分区

| 项目 | 基线 | SoftAP 关闭 + TLS client-only |
| --- | ---: | ---: |
| unsigned app | 1,114,032 B | 1,048,496 B |
| ECDSA v1 signed app | **1,114,100 B，`0x10fff4`** | **1,048,564 B，`0xffff4`** |
| signed SHA-256 | `483d8ef6d4c4c4f82f7006eed29b187254e68093db9b875e55eb06251357b2ec` | `9763b55620f5c9a12301b66c299f92d0d0be298568e05348ae6fcf715efdc08c` |
| `0x120000` 单 app 槽余量 | 65,548 B，`0x1000c` | 131,084 B，`0x2000c` |
| Flash Code / Data | 784,148 / 213,492 B | 728,716 / 210,304 B |
| 静态 DRAM 已用／可用 | 110,090 / 70,646 B | 109,930 / 70,806 B |
| 静态 IRAM 已用／可用 | 81,311 / 49,761 B | 81,223 / 49,849 B |

固定 SDK `espsecure verify-signature --version 1 --keyfile <仓外测试键>` 对两份 signed bin 均返回 `Signature is valid`；`check_sizes.py partition --type app` 对两份均通过。ELF 中均有 `efrp_tls_step`、`esp_mqtt_client_start`、`eota_preflight`、`econtainer_runtime_open` 与 `wasm_interp_call_wasm`。缩减版签名大小比基线少 65,536 B，map 的 Flash Code 少 55,432 B、Flash Data 少 3,188 B；差值不应解释为同额堆释放。

默认 INFO 日志的 ESP32 bootloader 曾生成 `0x7c30` B，超过分区表 `0x8000` 前的 `0x7000` B 上限并被官方构建拒绝。只在仓外把 bootloader 日志改为 `NONE` 后得到 `0x5b10` B，留 `0x14f0` B，基线和缩减版均构建通过。该 bootloader 尚未在旧板上启动，也未实测签名验证或 rollback。

本轮仓外候选 CSV 使用以下几何；[Container 几何调用](esp32_five_component_geometry_check.c)保存在本仓，CSV 本身没有进入产品分区表。

| 区域 | 偏移／大小 | 当前验证 |
| --- | --- | --- |
| `nvs`、`phy_init`、`otadata`、`coredump` | `0x9000/0x6000`、`0xf000/0x1000`、`0x10000/0x2000`、`0x12000/0xe000` | 固定 SDK 生成分区表 |
| `ota_0`、`ota_1` | `0x20000/0x120000`、`0x140000/0x120000` | secure-v1 app 起点和长度对齐，两个真实 signed bin 均通过 SDK 容量检查 |
| `product_pkgs` | `0x260000/0x186000` | 三槽各 `0x82000`：`0x260000`、`0x2e2000`、`0x364000`；Container 原实现 `econtainer_slots_geometry_valid` 返回 1，末端精确为 `0x3e6000` |
| `at_old_raw` | `0x3e6000/0x4000`，readonly | 仅为旧 AT NVS／自定义区非空扇区的候选归档位置；未读取私有旧区、未验证完整重建 SHA 或硬件防写 |
| `base_store` | `0x3ea000/0x16000` | 静态分区通过；新 NVS 写入、CAS、GC 和断电保持未测 |

固定 SDK `gen_esp32part.py --flash-size 4MB --secure v1` 接受该 CSV，生成的分区表在两份构建中摘要同为 `5dee2f8a6745ddc56822923d356f0215f1ede7e1171df4fff5afcba8d8d2d27e`。`product_pkgs` 的 `0x186000` 恰好容纳三个 `0x82000` 槽；`0x82000` 是先前默认 512 KiB Wasm 样本所得的 532,480 B 签名包大小，并非已冻结真实业务包上限。`at_old_raw` 是否足以逐字节重建全部旧 AT 持久区，仍须使用两份完整备份与私有扇区摘要证明。新布局覆盖旧 AT app 和持久区，静态通过不能授权迁移。

## 明确未验边界

- 本探针锁定的 `esp-frp@533e294` 把登录 JSON wire 字段固定为 `"arch":"riscv32"`；后续 `esp-frp@36e1506` 已按 ESP32 target 修正为 `xtensa`，并完成双目标 host 官方握手互操作，但没有进入本轮容量镜像。本探针没有创建 FRP session 或连接官方 FRPS，未验证服务端对该值的运行处理；ESP32 实际联网仍须用修正后的精确版本完成真机 FRPS/TLS 回归。
- mac-work-1 当前只有 Espressif `qemu-riscv32`，Homebrew `qemu-system-xtensa -machine help` 没有 ESP32 machine。因此本轮不能得到 ESP32 guest 与 FRP/MQTT/OTA 并发的 free heap、最大连续块、栈和 TLS 资源读数；静态 DRAM 的 70,806 B 余量不是运行时可用堆或 64 KiB 完整 FRP 记录验收。
- 探针把真实组件入口深链接，并在临时 `app_main` 调用 guest 生命周期，但没有持久包槽绑定、实际签名包写入／读取或所有网络会话。临时测试签名键、实验配置与候选分区均没有进入产品仓，也没有写入两台设备。
- `0x120000` app 槽对于缩减版还有 `0x2000c` 余量；产品 Container 公开安装入口、真实业务接线和后续增长可能继续占用。P6-03 仍需真实组合负载、三槽实际包、两目标各自运行资源表；P7-01 仍需签名双固件和完整恢复基线。
