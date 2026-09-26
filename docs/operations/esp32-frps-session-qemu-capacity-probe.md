# ESP32 Base READY 与 guest 存活时的官方 FRPS 会话容量探针

## 结论

固定 ESP32 五仓 QEMU 输入升级到 `esp-frp@fbf5ec07cca6ac36cf06a780444e75928e9dddfd` 的默认关闭字宽 AEAD 实验后，本仓仅在仓外副本加入 OpenETH 和真实 `efrp_create → efrp_start → efrp_get_status → efrp_destroy` 测试入口。宿主运行官方 `github.com/fatedier/frp v0.71.0` 的 `server.NewService`，只监听 `127.0.0.1:29175`，强制 TLS 和测试 Token；QEMU guest 通过 `10.0.2.2` 访问。Base 已报 `ESP_BASE_READY`，ABI 2 标准 64 KiB guest 在整个 FRP 尝试期间存活，OpenETH 获得 DHCP 地址，Base 的本次启动 SNTP 同步门为真。测试 CA 的 `10.0.2.2` IP SAN 被固定客户端严格验证。

**实际停止点在 TLS 完成后、FRP Login 发送前的 session 对象分配。** `efrp_start` 返回 0，状态经过 `CONNECTING=1`、`TLS_HANDSHAKING=2`，随后 `failure_phase=2`、`EFRP_NO_MEMORY=-20`、`FAILED=8`。此时 `tls_error=0`、`verify=0`；失败分配回调仅一次，申请 **18,872 B**、`MALLOC_CAP_INTERNAL | MALLOC_CAP_DEFAULT`（6144）。ELF DWARF 中 `efrp_session_t` 大小恰为 18,872 B；`src/client.c` 只有在 `efrp_tls_step` 返回成功后才调用 `efrp_session_create`，后者第一项 `calloc(1, sizeof *s)` 正是该尺寸。状态未切换到 `AUTHENTICATING`，是因为创建 session 未完成，不能把 `failure_phase=2` 误判为证书或握手失败。

因此没有 Login、注册、Pong 或**会话内** 65,536 B AEAD 记录的成功证据。相同镜像中原有直接字宽 reader 的满长认证仍成功，属于**无 session** 的独立实验，不代替这些阶段。P6-03 的真实会话和五仓并发验收仍未完成。本任务没有修改 FRP 正式源码、产品默认配置或实体板。

## 固定输入与实现边界

| 项目 | 精确输入 |
| --- | --- |
| Base／FRP／MQTT／OTA | `1f43b6ff867dfcc262fc6a348b6d50285395c6e0`／`fbf5ec07cca6ac36cf06a780444e75928e9dddfd`／`9d6d95e779f4f5ff387a6d9b54015bf4e43565f2`／`207273188b984161362824c3344614e812016836` |
| Container | 五仓冻结起点 `d50128703907983187a556e3c3facc8d9e02b935`；实际嵌入组件 `8eb805f3f12cb3cd836e9833acb4aca878ae80e7`，本独立分支只含 QEMU 测试材料 |
| SDK／lwIP／WAMR | ESP-IDF `578cf89c343e388db43ba1f4ddcd602fedcb763c`／`2758df4cd3666b3b2a5b53830148379326425c0d`／`26c235e53e29acd8b43abe7f3b524577bd4d1ae5` |
| 仓外签名输入 | 上轮字宽 reader app SHA-256 `e54493c022c2e5b7e8ee3ef7906708d3dfe39e6dfe0d9f7975dabddfdb89700e`；`sdkconfig` `b1f4e090370e5ac7b20a05cf84faf941f39c847df7b18eccc1e777e90586d6ff` |

[准备脚本](prepare_esp32_frps_session_qemu.py)在复制前验证该签名 app、配置、主应用接点、FRP 字宽核心文件和生成锁的 SHA-256。它只在仓外复制件开启固定 SDK 的 `CONFIG_ETH_USE_OPENETH=y`、加入[OpenETH 生命周期探针](openeth_qemu_probe.c)和[公开 client 探针](frps_session_qemu_probe.c)，并把四个本地组件的生成锁路径改为该复制件。构建又按 SDK 默认加入 `CONFIG_ETH_OPENETH_DMA_RX_BUFFER_NUM=4`、`CONFIG_ETH_OPENETH_DMA_TX_BUFFER_NUM=1`；除此之外配置 diff 无新增项，两项 Wi-Fi IRAM 开关仍为 `n`。字宽实验仍仅由 `-DEFRP_LAB_ESP32_IRAM_AEAD_RX=ON` 明确开启，不修改产品默认。

宿主[FRPS wrapper](qemu_frps_fixture.go)直接调用官方 `server.NewService`，FRP Go 模块经 `go list -m` 确认为 v0.71.0。每轮生成临时 P-256 测试 CA 与含 IP SAN 的服务端证书，私钥仅留仓外 `fixture/`。客户端使用 `MBEDTLS_SSL_VERIFY_REQUIRED` 和最低 TLS 1.2；测试入口在 OpenETH DHCP 后等待 Base 的 `esp_base_time_ready()`，使信任门依赖本次启动的真实 SNTP 同步，而非仅看人工写入的时钟值。首次夹具曾用固定的未来时间阈值；Base SNTP 随后同步到当前日期，使该夹具误报 `EFRP_TIME_UNTRUSTED=-18`。已修正测试夹具，最终结论来自修正后两次完整运行。

## 同次运行的容量与阶段

8BIT `free/largest/min` 单位为字节；`min` 是开机低水位，不随释放回升。以下取第一次修正后的 90 秒运行；[脱敏完整 QEMU trace](esp32-frps-session-qemu-trace.txt)和[独立复跑 trace](esp32-frps-session-qemu-rerun-trace.txt)保留两次原始顺序。

| 阶段 | 8BIT free／largest／min | 动态结果 |
| --- | ---: | --- |
| Base READY、guest event 成功，OpenETH 启动前 | 61,160／43,008／4,696 | 直接字宽 reader 消费 65,568 B wire，完整认证并核对 65,536 B 明文；满长坏 tag `-11`、零交付。此时**尚无 FRP session** |
| OpenETH DHCP 与 Base SNTP 完成 | 47,148／43,008／2,996 | `got_ip=1`；`base_sntp_ready=1` |
| `efrp_create` 前／后 | 46,952／43,008 → 36,504／32,768 | `efrp_create=0`；`efrp_start=0` 已排队 |
| `CONNECTING=1` | 35,780／32,768／2,424 | attempts=1 |
| `TLS_HANDSHAKING=2` 采样 | 7,908／6,912／2,372 | 严格 TLS 仍存活；这一采样不是失败回调的瞬时堆快照 |
| session 创建失败并清理 TLS | 35,976／32,768／1,432 | `error=-20`，`failure_phase=2`，`ready=0`，`pongs=0`，`tls_error=0`，`verify=0`；失败申请 18,872 B，能力掩码 6144 |
| `efrp_destroy`／OpenETH 清理 | 46,552／43,008 → 60,484／43,008 | destroy 返回 0，client 指针清空；比 OpenETH 前少 676 B，单次运行不能归因泄漏 |

独立复跑使用**同一**签名 app、同一测试入口与端口；`efrp_start=0`、SNTP ready、`tls_error=0`、`verify=0`、失败申请 18,872 B、`error=-20`、`ready=0`、`pongs=0` 均重复。复跑 TLS 阶段采样为 **7,904／6,912 B**；低水位为 936 B，反映仿真调度差异。两次均有 `probe_summary runs=2 failures=1`，其中唯一失败就是预期记录的真实会话容量停止点；两次 guest 的 `open/init/event/stop/close` 均返回 0、事件返回 3。宿主 FRPS 均正常 READY 与 STOPPED、没有遗留端口监听；QEMU 两次各运行 90 秒后由宿主 SIGTERM 停止，**这两次**日志无 panic。更早的首次启动在 IDF `task_wdt_init` 时曾于 `app_main` 前发生一次 QEMU `LoadProhibited`；同一签名镜像重跑进入应用，那个尚有错误时间门的尝试不用于会话容量结论。该偶发启动异常仍是仿真稳定性限制。

新 ECDSA v1 测试签名 app 为 **1,179,636 B（`0x11fff4`）**、SHA-256 `1568df0ddd2075a42fd63289458bd5edd0215763a5f4bb7ea3e0a7621107cf38`；固定 `espsecure v5.4.0 verify-signature --version 1` 输出 `Verifying 1179568 bytes of data... Signature is valid.`。最小 app 槽 `0x120000` 仅余 **12 B**。这是含测试探针和临时 CA 的仿真镜像尺寸，不是正式镜像余量。

## 复现与收据

将本仓 `prepare_esp32_frps_session_qemu.py`、两个 C fixture、`qemu_frps_fixture.go`、`run_esp32_frps_session_qemu.sh` 和既有 `frp_chunked_qemu.py` 复制到 mac-work-1 的同一仓外目录。下面的输出目录必须尚不存在；端口需空闲。仓外测试签名键与固定 SDK 已在该主机，运行命令没有 `flash`：

```bash
probe_root=/private/tmp/esp32-frps-session-qemu-20260927
python3 "$probe_root/prepare_esp32_frps_session_qemu.py" \
  /private/tmp/esp32-frp-iram-aead-qemu-20260927/probe \
  "$probe_root/probe-replay" --port 29175
export IDF_PATH=/Users/darrenyou/.cache/darren-space/esp-idf-578cf89
source "$IDF_PATH/export.sh"
idf.py -C "$probe_root/probe-replay/firmware" -DIDF_TARGET=esp32 \
  -DESP_BASE_CONTAINER_BINDING_PROBE=ON \
  -DEFRP_LAB_ESP32_IRAM_AEAD_RX=ON build
espsecure verify-signature --version 1 \
  --keyfile /private/tmp/esp32-auth-capacity-20260927/test-key.pem \
  "$probe_root/probe-replay/firmware/build/esp_base.bin"
"$probe_root/run_esp32_frps_session_qemu.sh" \
  "$probe_root/probe-replay" \
  /Users/darrenyou/darren-space/tooling/esp-frp/tests/crypto-interop 29175
```

测试证书每次随机生成，重新构建的签名 app 摘要可能不同；须核对输入锁、签名有效、阶段、失败尺寸及全部未到达阶段。[SHA-256 收据](esp32-frps-session-qemu-sha256.txt)同时记录本轮原始日志摘要；[脱敏工具](sanitize_esp32_frps_qemu.py)仅替换串口日志中的运行时 `boot_id`／`device_id`，保留内存与协议阶段证据。宿主 FRPS 的[脱敏日志](esp32-frps-session-frps-trace.txt)只有 READY／STOPPED，不提供逐条服务器协议 trace。实体板、真实 Wi-Fi、MQTT、OTA 与生产 FRPS 不在本实验范围。
