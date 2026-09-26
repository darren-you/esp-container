# ESP32 五组件认证记录与单页 guest 的 QEMU 容量探针：2026-09-27

## 结论

固定 ESP-IDF／lwIP 下的 ESP32-D0WD-V3 仓外镜像已在官方 Espressif Xtensa QEMU 中启动。镜像使用临时 ECDSA P-256 测试键签名，同时链接 Base、FRP、MQTT、OTA、Container 和 WAMR Classic；`app_main` 在 Base 初始化前及报告 READY 后分别运行一个固定 64 KiB 内存的 ABI 2 guest，均完成 `open → init → event → stop → close`，事件返回 3。**没有 ADC 或 Wi-Fi QEMU 空桩。** 两次 QEMU 运行的堆和认证结果相同。

Base READY 后 guest 存活时，两个完整的 4 KiB AES-GCM 记录均认证成功且逐字节比较 4096 字节；篡改 tag 的 4 KiB 记录返回 `EFRP_AUTHENTICATION_FAILED=-11`，没有发布明文。完整 64 KiB 记录在第 **14** 个 4096 字节缓冲申请时返回 `EFRP_NO_MEMORY=-20`：累计消费 wire **53,264** 字节，已申请的 13 块全部释放，未认证、未发布明文。Base 初始化前，同一 guest 与完整 64 KiB 记录可以认证成功并逐字节比较 65,536 字节。因此限制来自同存负载，不是记录解析器总是拒绝 64 KiB。

**P6-03 仍未验收。** QEMU 中 Wi-Fi 为 `unconfigured`；FRP session、TLS/FRPS、MQTT Broker、OTA 下载、真实三包槽和物理 ESP32 的并发峰值均没有运行。这里的探针还加入测试 guest、密文 fixture 和强制链接入口，不能当作可刷写或发布的固件。未接设备、未写 Flash/eFuse、未使用生产密钥。

## 精确输入与构建

| 输入 | 完整提交或校验值 |
| --- | --- |
| Base | `1f43b6ff867dfcc262fc6a348b6d50285395c6e0`；已具备 ESP32 自有分区、OTA 目标和 ECDSA v1 构建约束 |
| FRP | `1f0c8f37db3765a74b3b95871bb266d0c73d1248`；密文实际到达时才逐块申请 AEAD 缓冲 |
| MQTT | `9d6d95e779f4f5ff387a6d9b54015bf4e43565f2` |
| OTA | `207273188b984161362824c3344614e812016836` |
| Container | `8eb805f3f12cb3cd836e9833acb4aca878ae80e7` |
| WAMR | `26c235e53e29acd8b43abe7f3b524577bd4d1ae5`，由 Container manifest 和生成锁精确固定 |
| SDK | ESP-IDF `578cf89c343e388db43ba1f4ddcd602fedcb763c`，实际 lwIP `2758df4cd3666b3b2a5b53830148379326425c0d` |
| 本轮五份 Git 归档的合并传输包 | SHA-256 `8671468c88624bc7df8e1a7520d1fc8e30bf9a5978c4285be75dd771e077254c`；该值绑定本轮传输字节，重新打包可能因 tar 元数据而改变 |
| 官方 QEMU | `qemu-xtensa` `esp_develop_9.2.2_20260417`，macOS arm64 执行文件 SHA-256 `dab87a4b3be6493c37326ef1ef2db64d42592507ed9f381232d5a1b6fa5492fb`，`-machine help` 含 `esp32` |

QEMU 版本来自固定 SDK 的 `tools/tools.json`，按[官方安装与运行说明](https://docs.espressif.com/projects/esp-idf/en/release-v6.1/esp32/api-guides/tools/qemu.html)用 `idf_tools.py install qemu-xtensa` 安装。Homebrew QEMU 11 没有 `esp32` machine，不能代替该执行文件。固定 ESP-IDF fork 的父提交将 lwIP gitlink 记为 `c6f2f878...`，本工作区 SDK 锁要求实际 checkout 为 `2758df4c...`，故版本串显示 `578cf89c-dirty`；lwIP 工作树无源码改动，构建的 `check_sdk.py` 校验了这两个实际提交和组件路径。

仓外准备脚本 [prepare_esp32_authenticated_qemu.py](prepare_esp32_authenticated_qemu.py) 从五份完整 Git 归档建立独立工程，并加入 [固定 ABI 2 guest 字节](runtime_guest_bytes.h)、[入口强制链接 fixture](esp32_capacity_references.c)与[认证记录探针](frp_authenticated_capacity_probe.c)。该脚本没有修改五仓归档中的组件 C 实现；只在仓外 Base 主应用加入探针调用和两个 AEAD wire fixture，启用原产品的 `ESP_BASE_CONTAINER_BINDING_PROBE=ON`。链接 fixture 的 volatile 门在本次运行始终为零，实际运行的是 Base、guest 和 AEAD reader；map 中保留 `efrp_tls_step`、`esp_mqtt_client_start`、`eota_preflight`、`esp_base_container_reconcile`、`econtainer_runtime_open` 和 `wasm_interp_call_wasm`。强制链接不代表这些联网或安装入口执行。

guest fixture 解码为 469 B，SHA-256 `b9422cb4cb72983141988c4a9a59b602026d94729e2362403a04d1723f98a739`，通过本仓 `counter_guest.check_wasm` 的固定单页 ABI 检查。AES-GCM wire fixture 分别为 4,128 B／SHA-256 `2855df4bd4199f7ce21526c33bcc0b21776adf4d6e5b9f631e43491ea9d30e20` 和 65,568 B／SHA-256 `35979812621d6b6778c4086f937991353cfa7c4a1aa60dfcfc5507b2542aa`；包含 12 B nonce、4 B 长度头、密文及 16 B tag，明文字节由探针按偏移独立计算并比较。

生成配置读回 `target=esp32`、4 MiB 产品分区、UART0、STA-only、TLS client-only、WAMR Classic/Normal、单页标准 Wasm、指令计量、ECDSA v1 signed-on-boot/update 和 rollback；硬件 Secure Boot 为关闭。测试私钥仅在仓外，未进入 Git 或固件源码。生成的 `dependencies.lock.esp32` 将四个组件记为**本地归档路径**，所以其 `0.1.0` 字段不是源码版本证明；上表完整 Git 提交与归档 SHA 才是该实验的源码来源。生成锁另记 `target: esp32` 与上述 WAMR 完整提交。

| 制品或静态尺寸 | 结果 |
| --- | ---: |
| 测试键 signed `esp_base.bin` | **1,114,100 B**，`0x10fff4`，SHA-256 `60c1d4d824fc4ef2237ecd30d27b6d5d30bb46fdfda36878be60a9a734758991` |
| `espsecure verify-signature --version 1` | `Verifying 1114032 bytes of data... Signature is valid.`；1,114,032 B 是验签数据长，**不是文件大小** |
| 产品 `0x120000` 最小 app 槽 | 固定 SDK `check_sizes.py` 通过，余 `0x1000c`，约 6%；bootloader `0x5b10`，距分区表余 `0x14f0` |
| 产品表二进制 SHA-256 | `042856a7d817fd5e16bc5fd126459f99cb5fd929606768a7b7cd03566c58efdb` |
| `idf.py size` | 静态 DRAM 已用／剩余 **110,082／70,654 B**；静态 IRAM **81,223／49,849 B** |

[Base 同锁独立签名构建](https://github.com/esp-space/esp-base/blob/b29ef074e93ae004fb580171c33d303112b3f9c2/docs/operations/development-checkpoint.md)的产品候选是 983,028 B；本探针多出 131,072 B 的签名镜像台阶，包含 guest、记录 fixture、探针和签名 padding，不代表产品固件新增了同额业务代码。先前[ESP32 静态五组件报告](esp32-five-component-capacity-probe.md)锁定旧 Base `bcde483`／FRP `533e294`，且未运行 QEMU；其静态 DRAM 与镜像数字不能直接当作本次动态结果。

## 两次相同的动态结果

以下 `free/largest` 均为 `heap_caps_get_free_size`／`heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)`，单位字节。`idf.py size` 的静态 DRAM 剩余量与这个运行时能力堆不是同一指标；仿真器的合成 RAM、设备驱动、无线和 TLS 状态也与实板不同。

| 阶段 | free／最大连续块 | 记录结果 |
| --- | ---: | --- |
| Base 初始化前、探针线程开始 | 194,132／110,592 | 尚未打开 guest |
| 首轮 guest 事件后 | 116,452／57,344 | 完整 64 KiB wire 消费 65,568 B；16 块申请及释放，认证并比较 65,536 B |
| 首轮 reader 销毁后 | 115,640／57,344 | 首次释放后 free 比申请前少 **812 B**；不推断原因 |
| 首轮 guest 关闭后 | 193,216／110,592 | 比首轮线程开始少 **916 B**；不宣称首次全量恢复 |
| Base READY | **147,340／110,592** | Wi-Fi `unconfigured`，MQTT/FRP `unconfigured` |
| READY 后第二轮 guest 事件后 | **61,404／43,008** | 两次完整 4 KiB wire 各消费 4,128 B、认证并比较 4096 B |
| READY 下坏 tag | 61,404／43,008 | `-11`，`records=0`、`compare=0`、申请及释放各一块 |
| READY 下完整 64 KiB wire | 61,404／43,008 | 第 14 块申请失败 `-20`；`consumed=53,264`、`records=0`、`compare=0`，13 块全部释放 |
| 第二轮 guest 关闭／探针线程结束 | 138,972／110,592；147,340／110,592 | 相对各自 READY 前检查点完全恢复；全程 `minimum_free_size` 曾到 **4,904 B** |

首次完整记录后 free 的 812 B 差额及 guest 关闭后的 916 B 差额没有来源证明，不能写成“无泄漏”；后续 READY 阶段每个记录及 guest 生命周期均返回其对应检查点。无论潜在首次初始化差额的归属如何，**READY + guest 下完整 64 KiB 记录没有完成认证**。

本 QEMU 探针只在 `feed` 完整成功后调用明文读取，故坏 tag 与内存不足时的 `compare=0` 表示探针未交付明文；它本身未额外调用错误后的明文 API。同锁 FRP 的 `tests/aead_test.c` 分别断言坏 tag 后 `efrp_aead_plaintext` 返回 `EFRP_AUTHENTICATION_FAILED`、内存不足后返回 `EFRP_NO_MEMORY`，两者均输出空指针与零长度；这是另一个主机测试证据，不能伪装成 QEMU 调用。

两次 QEMU 原始日志 SHA-256 分别为 `0d00ad7f3607f15ecb63534e02920951771b62ae85f0b2213da380c03e14fbf1` 与 `825a46731364f76797b9fc01ac2c51950a4ac39f1da2f899dee69c5a3f1f754f`；第二次去除终端颜色、CR 和行尾空格，并遮盖 QEMU 合成 UUID 的[完整启动及探针 trace](esp32-authenticated-qemu-trace.txt) SHA-256 为 `5487c3133cfae45c6d0d6a08c94ffbcf1c1ba418a08e0416e871a350abadd4a5`。两次的上述数值、认证状态、失败块号和清理结果逐项相同；时戳及 QEMU 生成的设备 UUID 不同。

## 复现和边界

在隔离目录分别从表内五个精确提交使用 `git archive` 展开为 `base-src`、`frp-src`、`mqtt-src`、`ota-src`、`container-src`，并校验传输包 SHA。以下命令中的 `source_dir` 指包含这五个目录的仓外路径；`managed_cache` 可省略，省略时 Component Manager 从固定 manifest 下载 WAMR 与 cJSON。本轮使用的是先前 C3 探针已校验的两份受锁缓存，且准备脚本的新版本在另一仓外目录生成了与本次实际构建**逐文件相同**的主应用、fixture 和 `sdkconfig.defaults.esp32`。

```bash
fixed_idf_path="$HOME/.cache/darren-space/esp-idf-578cf89"
probe_root=/private/tmp/esp32-auth-capacity-20260927
source_dir="$probe_root/source"
managed_cache=/private/tmp/esp-p6-exact-c3-20260926/probe-base/firmware/managed_components
umask 077
openssl ecparam -name prime256v1 -genkey -noout -out "$probe_root/test-key.pem"
python3 docs/operations/prepare_esp32_authenticated_qemu.py \
  "$source_dir" "$probe_root/probe" "$probe_root/test-key.pem" \
  --managed-cache "$managed_cache"
source "$fixed_idf_path/export.sh"
idf.py -C "$probe_root/probe/firmware" -DIDF_TARGET=esp32 \
  -DESP_BASE_CONTAINER_BINDING_PROBE=ON build
espsecure verify-signature --version 1 --keyfile "$probe_root/test-key.pem" \
  "$probe_root/probe/firmware/build/esp_base.bin"
idf.py -C "$probe_root/probe/firmware" qemu --qemu-extra-args=-no-reboot
```

`idf.py qemu` 构造 4 MiB **仿真** Flash 与合成 eFuse，并用 `-global ... wdt_disable=true` 关闭 QEMU watchdog。它的默认命令还附带 `open_eth` 用户态 NIC；本应用并未初始化该网卡，也不能把它等同真实 Wi-Fi。官方[ESP32 QEMU 网络说明](https://github.com/espressif/esp-toolchain-docs/blob/main/qemu/esp32/README.md)要求应用明确接入模拟 Ethernet；本轮未做此改造。上述命令只运行仿真器，绝不能改为 `flash` 指向旧 AT 实板。P6-03 仍需真实业务包、FRP/TLS/MQTT/OTA 并发、物理 RAM/栈/最大块、签名启动和新分区迁移的受控实测。

重新生成测试私钥或改变编译时间会改变 signed bin 和日志 SHA；复现判断应以精确源码／配置、官方验签、逐字节记录比较、分配次数、失败边界及清理读数为准。
