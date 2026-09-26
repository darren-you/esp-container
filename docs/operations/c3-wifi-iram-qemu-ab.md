# C3 Wi-Fi IRAM 两项开关的 QEMU 容量对照

## 范围与输入

本次在 mac-work-1 的独立 `/private/tmp/esp32c3-wifi-iram-off-20260927/probe` 中，从[新 FRP 完整认证 QEMU 切片](five-component-qemu-capacity-probe.md)的 `/private/tmp/esp32c3-frp-lazy-exact-20260927/probe` 复制输入，**只把两项 Wi-Fi IRAM 主 Kconfig 从 `y` 改成 `n`**：`ESP_WIFI_IRAM_OPT`、`ESP_WIFI_RX_IRAM_OPT`。原仓外基线的签名 app SHA-256 为 `93fb2b027f5bf8d0acae828e4812cd5665acc2803f21621ea2b7ee169051efdc`，生成 `sdkconfig` SHA-256 为 `cb4911792bf9fc1191e4dfc90ff04e483e880ed6800a455f590e594c5ce6b62d`；[准备脚本](prepare_five_component_wifi_iram_ab.py)会验证这两个输入，且目标目录必须尚不存在。**构建时原始脚本** SHA-256 为 `598bad8a2cc8ff19a70c4a3d0f7d343a048bf3eb7b8d12a891eec17267a7ef6a`；**最终公开可重放脚本** SHA-256 为 `8d09182adc9f21f0ce05dd38bfdcba89982277404aca6399f9c87d4e60a9bbbf`。两者差异只有说明字符串与终端输出布局，复制、输入校验及两项配置替换代码未改。最终脚本在另一独立仓外目录重放，构建前 `sdkconfig` SHA-256 同为 `b9222bfaad0ca2875e6d9bcbb5ef6cb221134d40d9ca144fff9d64c4756d5247`；除 `build`、日志与已构建工程的最终 `sdkconfig` 外，两个目标目录逐文件相同。重放目录未再构建镜像或运行 QEMU；下文尺寸与运行读数都来自原始脚本的实验目录。

Base `058e965671fa0e4d417114897541710699571c52`、FRP `1f0c8f37db3765a74b3b95871bb266d0c73d1248`、MQTT `9d6d95e779f4f5ff387a6d9b54015bf4e43565f2`、OTA `207273188b984161362824c3344614e812016836`、Container `8eb805f3f12cb3cd836e9833acb4aca878ae80e7`、ESP-IDF `578cf89c343e388db43ba1f4ddcd602fedcb763c`、实际 lwIP `2758df4cd3666b3b2a5b53830148379326425c0d`、WAMR `26c235e53e29acd8b43abe7f3b524577bd4d1ae5` 不变。旧 Base 源码中的远程 FRP 声明仍指旧提交；本次比较复用前节已逐文件核对的仓外本地 `esp_frp` 覆盖，生成 `dependencies.lock` 只记录本地路径及版本，不能独立证明该组件 SHA。两个工程排除 `build` 和实验日志后，`diff -qr` 仅发现 `firmware/sdkconfig` 不同；测试键、469 字节单页 ABI 2 guest、4/64 KiB 完整 AEAD wire、探针 C、QEMU ADC2 空桩、UART0 控制台和编译选项均相同。

从空 `build` 重建后，最终 `sdkconfig` 与基线只在这两项主开关及它们自动生成的 `CONFIG_ESP32_WIFI_IRAM_OPT`、`CONFIG_ESP32_WIFI_RX_IRAM_OPT` **各两处镜像别名**上有差异，共六行由 `y` 变成 `# ... is not set`，没有第三项独立配置变化。生成锁仍为 SHA-256 `63259c2444b89238187a778563a6b52f873a5448e32eb1ac5b8d10b4b3763948`。固定 SDK Kconfig 对两项分别说明关闭会节省 IRAM，但 Wi-Fi 吞吐／性能会下降；此无网络仿真没有测性能或真实连接。

## 构建与运行结果

| 读数 | 基线：两项开启 | 本次：两项关闭 | 变化 |
| --- | ---: | ---: | ---: |
| `idf.py size` Flash `.text` | 809,154 B | 828,076 B | +18,922 B |
| `idf.py size` DRAM 合计 | 170,778 B | 151,034 B | −19,744 B |
| 其中 DRAM `.text` | 65,898 B | 46,154 B | −19,744 B |
| DRAM `.bss`／`.data` | 91,592／13,288 B | 91,592／13,288 B | 不变 |
| 未签名／RSA v2 签名 app | `0x120000`／`0x121000` | `0x120000`／`0x121000` | **同一签名尺寸档** |
| Base READY + guest 存活，8-bit free／最大连续块 | 65,480／45,056 B | **84,944／45,056 B** | +19,464／0 B |
| READY + guest，完整 64 KiB wire | 第 15 块分配失败 `-20`；消费 57,360 B；未认证 | `feed=0`、`records=1`、核对 65,536／65,536 B；16 块分配／16 块释放 | 此无会话切片认证通过 |
| READY + guest，64 KiB 认证期间 free／最大连续块 | 失败后读数回滚至 65,480／45,056 B | **19,344／8,704 B** | 不将回滚后的旧读数当峰值 |
| READY + guest，reader 清理后 free／最大连续块 | 65,480／45,056 B | **84,944／45,056 B** | 各回到进入前读数 |

本次签名 app 是 **1,183,744 B（`0x121000`）**、SHA-256 `4f1935a3898172cb3983600572e6808f11f9a3426783bd393ceeec45a258f059`，固定 SDK `espsecure verify-signature --version 2` 用原相同仓外 RSA 测试键验证 block 0 有效。基线签名 app 也是 `0x121000`，没有跨越 Flash 签名台阶。`idf.py size` 的总 image 从 1,175,676 降到 1,174,774 B；这不是签名文件长度。官方 C3 QEMU 在 45 秒后由宿主 SIGTERM 结束，运行日志 SHA-256 `d11d4e151b7c76ecf1f124e1c0811be849995e4df9f8c753a16c05e3ebf8a107`，无 panic。早期 guest 下完整 64 KiB 记录也认证、逐字节核对并释放；READY 阶段两条连续 4 KiB 记录均认证成功，每次 reader 清理恢复到 84,944／45,056 B；4 KiB 篡改 tag 返回 `EFRP_AUTHENTICATION_FAILED=-11`、不交付明文并释放。两次 guest 生命周期均 `open/init/event/stop/close=0`、counter 返回 3，最终探针失败数 0。

## 容量边界

关闭两项在本镜像减少 19,744 B 常驻 DRAM `.text`，READY + guest 的可用 heap 净增 19,464 B；这两个口径不同，不能用其中一项替代另一项。满长 reader 单项成功时尚余 19,344 B，却没有创建 FRP session、Yamux、TLS/FRPS 连接、Wi-Fi 数据会话、MQTT Broker 或 OTA 下载。[前轮旧 FRP 的只读内存账本](c3-frp-guest-memory-budget.md)列出同时承载 FRP 完整路径的已知申请至少 101,208 B；新 FRP 相对旧版只改 AEAD 块的申请时机，没有改变 session、Yamux、TLS 等结构大小。即使乐观地把本次 READY + guest 的全部 84,944 B 都当作可无碎片分配，按该已知路径仍差 **16,264 B**，并且该下界未计 CA、TLS 内部、TCP/lwIP、Wi-Fi、MQTT、malloc 元数据和碎片。FRP 惰性接收未降低完整 64 KiB 记录需要的 65,536 B 明文块。**P6-03 仍未验收。**

本镜像保留 QEMU 专用 ADC2 校准空桩和公开测试签名键，绝不能刷实体板；本次未修改正式 `sdkconfig.defaults`、产品分区、签名策略或设备。要判断关闭 Wi-Fi IRAM 两项能否用于产品，还须在实际 Wi-Fi／FRPS／Broker 并发和吞吐条件下验证；本仿真不能支持这个判断。

## 收据与复现

[SHA-256 收据](c3-wifi-iram-qemu-ab-sha256.txt)保存基线、新镜像、配置、构建、尺寸和 QEMU 日志的精确摘要。以下只在 mac-work-1 的**仓外目录**运行，所需原始 `probe` 必须是上述基线；先将本仓最终公开脚本复制到 `prepare_five_component_wifi_iram_ab_replay.py`，将本仓 `frp_chunked_qemu.py` 复制到 `run_qemu.py`。两项配置由公开脚本修改，`build` 从零生成，实验不执行 `flash`：

```bash
probe_root=$(mktemp -d /private/tmp/esp32c3-wifi-iram-repro-XXXXXXXX)
python3 /private/tmp/prepare_five_component_wifi_iram_ab_replay.py \
  /private/tmp/esp32c3-frp-lazy-exact-20260927/probe \
  "$probe_root/probe"
export IDF_PATH=/Users/darrenyou/.cache/darren-space/esp-idf-578cf89
source "$IDF_PATH/export.sh"
idf.py -C "$probe_root/probe/firmware" \
  -D ESP_BASE_CONTAINER_BINDING_PROBE=ON build
python -m espsecure verify-signature --version 2 \
  --keyfile "$probe_root/probe/test-key.pem" \
  "$probe_root/probe/firmware/build/esp_base.bin"
idf.py -C "$probe_root/probe/firmware" size
python3 /private/tmp/run_qemu.py "$probe_root/probe/firmware" "$probe_root/qemu.log" 45
```

准备脚本需要显式源目录和目标目录参数，没有固化宿主临时路径。`CONFIG_APP_COMPILE_TIME_DATE=y` 可能使重建二进制摘要随时间变化；验收重放应先核对精确输入、Kconfig 差分、签名尺寸、验签与 QEMU 结果，再比较当次摘要。
