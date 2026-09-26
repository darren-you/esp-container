# ESP32 Wi-Fi IRAM 两项开关的 QEMU 容量对照

## 结论与边界

在[ESP32 五组件认证 QEMU 基线](esp32-authenticated-qemu-capacity-probe.md)的**同一组源码、guest、密文、测试键和探针**下，只关闭 `ESP_WIFI_IRAM_OPT` 与 `ESP_WIFI_RX_IRAM_OPT`，ESP32 的静态 IRAM `.text` 少用 **25,376 B**，Flash `.text` 多用 **24,368 B**，静态 DRAM 不变。Base READY 后 64 KiB ABI 2 guest 存活时，运行堆仍为 **61,404／43,008 B**（8-bit free／最大连续块）；完整 64 KiB AEAD wire 仍在第 14 块申请时以 `EFRP_NO_MEMORY=-20` 失败，消费 53,264 B，已申请的 13 块全部释放。两条完整 4 KiB 记录认证并逐字节核对，坏 tag 被拒绝，早期 Base 初始化前的 64 KiB 记录成功，均与基线相同。

这说明 C3 的[同两项开关对照](c3-wifi-iram-qemu-ab.md)所获得的动态堆收益不能套到 ESP32。**P6-03 仍未验收。** 本次只运行 Base、guest 与 AEAD reader；没有 FRP session、TLS/FRPS、MQTT Broker、OTA、真实包槽或物理板的同存峰值。无 Wi-Fi 连接或吞吐测试，不能据此改产品默认值。本镜像使用仓外测试键签名并包含密文 fixture，不可刷写或发布；本次未写 Flash/eFuse。

## 精确输入和单变量

| 输入 | 固定值 |
| --- | --- |
| Base／FRP／MQTT／OTA | `1f43b6ff867dfcc262fc6a348b6d50285395c6e0`／`1f0c8f37db3765a74b3b95871bb266d0c73d1248`／`9d6d95e779f4f5ff387a6d9b54015bf4e43565f2`／`207273188b984161362824c3344614e812016836` |
| Container 嵌入组件／本报告所在仓版本 | `8eb805f3f12cb3cd836e9833acb4aca878ae80e7`／从 `d50128703907983187a556e3c3facc8d9e02b935` 建立的独立工作树；后者只增加实验报告和脚本，并非不同的嵌入组件 |
| WAMR／ESP-IDF／实际 lwIP | `26c235e53e29acd8b43abe7f3b524577bd4d1ae5`／`578cf89c343e388db43ba1f4ddcd602fedcb763c`／`2758df4cd3666b3b2a5b53830148379326425c0d` |
| 基线 `sdkconfig`／ECDSA v1 签名 app SHA-256 | `8822f1b68067673594f5392928af8ef997bb95bdf19aef30e0ec89414ad1c49f`／`60c1d4d824fc4ef2237ecd30d27b6d5d30bb46fdfda36878be60a9a734758991` |
| 469 B 单页 ABI 2 guest／两条 AEAD wire SHA-256 | `b9422cb4cb72983141988c4a9a59b602026d94729e2362403a04d1723f98a739`／4 KiB：`2855df4bd4199f7ce21526c33bcc0b21776adf4d6e5b9f631e43491ea9d30e20`／64 KiB：`35979812621d6b6778c4086f937991353cfa7f73a4c1aa60dfcfc5507b2542aa` |

[准备脚本](prepare_esp32_wifi_iram_ab.py)先核对原实验的配置、签名 app、两个锁、探针和 fixture 摘要，再复制至新的仓外目录，排除构建与实验日志。准备时只将生成配置中的两项主 Kconfig 从 `y` 改为 `n`。ESP32 的 `dependencies.lock.esp32` 记录四个仓外本地组件的**原物理路径**；为了能在新目录重建，脚本只把这四个 `source.path` 重定向到复制后的同内容组件目录。这个锁的路径重定向是目录搬运所需的构建输入，不是组件版本或功能选项变化。测试签名私钥仍在原仓外位置，未复制进仓库。

新工程从空 `build` 编译后，生成 `sdkconfig` 与基线的 `diff -u` **只有六行差异**：两项主开关和 `CONFIG_ESP32_WIFI_IRAM_OPT`、`CONFIG_ESP32_WIFI_RX_IRAM_OPT` 各两处自动镜像别名，均由 `y` 变为 `# ... is not set`。排除 `build`、`sdkconfig`、四路径锁和未复制的旧实验日志后，两工程的逐文件 `diff -qr` 没有其他差异。生成锁仍为 `target: esp32` 和固定 WAMR 完整提交；本地路径版本号 `0.1.0` 不代替上表的源码来源证明。

## 静态尺寸与 QEMU 动态结果

| 读数 | 基线：两项开启 | 本次：两项关闭 | 变化 |
| --- | ---: | ---: | ---: |
| Flash `.text` | 731,500 B | 755,868 B | +24,368 B |
| IRAM 合计／`.text` | 81,223／80,195 B | 55,847／54,819 B | −25,376 B |
| DRAM 合计，`.bss`／`.data` | 110,082 B，92,536／17,546 B | 相同 | 0 B |
| `idf.py size` 未填充 image | 1,110,373 B | 1,109,301 B | −1,072 B |
| ECDSA v1 签名 `esp_base.bin` | 1,114,100 B（`0x10fff4`） | **1,114,100 B（`0x10fff4`）** | 同一尺寸档 |
| Base READY，未开 guest 的 free／最大块 | 147,340／110,592 B | **147,340／110,592 B** | 0／0 B |
| Base READY，guest 存活的 free／最大块 | 61,404／43,008 B | **61,404／43,008 B** | 0／0 B |
| READY + guest，完整 64 KiB AEAD wire | 第 14 块 OOM；53,264 B 已消费；13 块释放 | **完全相同** | 无容量改善 |

新签名文件 SHA-256 为 `d57ca7be871f59b6366b8614e1349dffb3cba9506c5438571956e1bfae6c4e32`，`espsecure verify-signature --version 1` 报告 `Verifying 1114032 bytes of data... Signature is valid.`。1,114,032 B 是验签数据长，不是签名文件长度；固定 SDK 的 app 分区尺寸检查仍剩 `0x1000c` B。关闭两项时 QEMU Wi-Fi 初始化没有再报告 IRAM 优化开启，但 Wi-Fi 状态一直是 `unconfigured`。

同一 QEMU 运行中，Base 初始化前的 guest 完成 `open/init/event/stop/close`，完整 65,568 B wire 消费成功并逐字节核对 65,536 B，16 块申请及释放；首次 reader 后 free 比进入前少 812 B、首次 guest 关闭后比线程开始少 916 B，来源仍未证明。READY 阶段两条 4 KiB wire 各认证并核对 4,096 B，坏 tag 返回 `-11`、`records=0`、`compare=0`，每条 reader 清理都回到 61,404／43,008 B。满长失败后未认证、未交付明文，13 块已释放并回到同一检查点；guest 关闭与探针线程结束后分别回到 138,972／110,592 B 和 147,340／110,592 B。所有 guest 生命周期结果为 0，事件返回 3，探针失败数 0。`minimum_free_size` 曾到 4,904 B，不把失败回滚后的读数当作认证期间峰值。

官方 Espressif Xtensa QEMU 为 `esp_develop_9.2.2_20260417`，使用 `-M esp32`、合成 4 MiB Flash/eFuse 与 `open_eth` 用户态 NIC；应用没有接入模拟 Ethernet。运行 45 秒后由宿主 SIGTERM 结束，无 panic。[完整脱敏 trace](esp32-wifi-iram-qemu-trace.txt)遮盖了 QEMU 生成的 `device_id`／`boot_id`，并去掉终端控制码和行尾空格；它不是物理 ESP32、无线链路或真实 TLS 的证据。

## 收据与复现

[SHA-256 收据](esp32-wifi-iram-qemu-ab-sha256.txt)保存准备脚本、基线、新配置、构建、验签、原始 QEMU 日志与脱敏 trace 的摘要。以下在具备上述固定 SDK、官方 QEMU 和**原精确实验目录及测试键**的 mac-work-1 仓外执行；先将本仓两个脚本复制至该宿主的 `/private/tmp`，新目标必须尚不存在。没有该冻结基线时，应先按[原探针报告](esp32-authenticated-qemu-capacity-probe.md)重建并重新冻结输入；重生成测试键会改变签名文件 SHA，不应绕过本脚本的摘要核对。

```bash
probe_root=/private/tmp/esp32-auth-wifi-iram-off-exact-20260927
python3 /private/tmp/prepare_esp32_wifi_iram_ab.py \
  /private/tmp/esp32-auth-capacity-20260927/probe "$probe_root/probe"
export PATH="/opt/homebrew/bin:$PATH"
export IDF_PATH=/Users/darrenyou/.cache/darren-space/esp-idf-578cf89
source "$IDF_PATH/export.sh"
idf.py -C "$probe_root/probe/firmware" -DIDF_TARGET=esp32 \
  -DESP_BASE_CONTAINER_BINDING_PROBE=ON build
idf.py -C "$probe_root/probe/firmware" size
espsecure verify-signature --version 1 \
  --keyfile /private/tmp/esp32-auth-capacity-20260927/test-key.pem \
  "$probe_root/probe/firmware/build/esp_base.bin"
python3 /private/tmp/run_esp32_wifi_iram_qemu.py \
  "$probe_root/probe/firmware" "$probe_root/qemu.log" 45
```

`run_esp32_wifi_iram_qemu.py` 是本仓[限时 QEMU 启动脚本](frp_chunked_qemu.py)的逐字节副本。`CONFIG_APP_COMPILE_TIME_DATE=y` 可使重建二进制摘要变化；复现判断先看精确输入、生成配置六行差分、签名验真与尺寸、认证结果、分配次数和清理读数，再比较当次 SHA。整个过程不执行 `flash`。
