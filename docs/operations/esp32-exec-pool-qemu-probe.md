# ESP32 READY + guest 的 EXEC／32BIT 能力池 QEMU 探针

## 结论

在[ESP32 两项 Wi-Fi IRAM 关闭后的精确五仓 QEMU 输入](esp32-wifi-iram-qemu-ab.md)中，Base READY 且 64 KiB ABI 2 guest 存活时，`MALLOC_CAP_EXEC | MALLOC_CAP_32BIT` 报告 free／最大连续块 **132,228／73,728 B**，同一时刻 `MALLOC_CAP_8BIT` 为 **61,404／43,008 B**。**这两个能力集合有共享 D/IRAM，不能相加。**

只申请和释放、不读写申请区域的仓外探针有两组结果：

| 同阶段申请 | 成功数／地址归属 | 申请期间 8BIT 堆 | 释放后 |
| --- | --- | --- | --- |
| 16 × 4,096 B，**4 字节对齐** | **16／16**；全部落在纯 IRAM `0x4008de04` 至 `0x4009de3f`；返回地址均不支持 byte access | 始终 **61,404／43,008 B**，无下降 | EXEC／32BIT **132,228／73,728 B**；8BIT **61,404／43,008 B** |
| 16 × 4,096 B，**4,096 字节对齐** | **13／16**；前 8 块纯 IRAM，后 5 块为 D/IRAM 的 I 总线别名；第 14 块返回空指针 | 5 个共享块各使 free 减少 4,104 B；失败时 **40,884／11,776 B** | 两池均回到进入前读数 |

严格页对齐的第 14 块失败时，EXEC／32BIT 仍报告 **78,908／11,776 B**。在保持前 13 块占用的**同一状态**下，再申请一块 4 字节对齐的 4,096 B，成功返回纯 IRAM `0x4009d004`（非页对齐），8BIT free／最大块仍为 40,884／11,776 B。因此该失败受到页对齐和当时空闲块布局约束，**不是能力池总 free 已小于 4,096 B**；只靠 free／largest 不能还原完整空闲链表。该补充块也已释放。

这些地址不能直接交给 AEAD、PSA 或其他按字节访问的 API：探针返回的所有 I 总线地址 `esp_ptr_byte_accessible=0`，没有写入、读取、执行、转址或调用加密 API。此结果只说明固定 QEMU 时点可作 32 位对齐申请的容量，不证明现有 FRP 64 KiB 明文块能够安全迁入 IRAM，更不证明 FRP session／TLS／Wi-Fi／MQTT／OTA 全路径容量。**P6-03 仍未验收**；产品默认配置、正式源码、物理设备及 Flash/eFuse 均未改动。

## 固定 SDK 的区域含义

固定 ESP-IDF `578cf89c343e388db43ba1f4ddcd602fedcb763c` 的 `components/heap/port/esp32/memory_layout.c`（SHA-256 `db0af7ab0ff9cfb827929c3505f2edf365c4fd2c1c9e8b352c3363fa5bcd0c13`）将纯 IRAM 标为 `MALLOC_CAP_EXEC | MALLOC_CAP_32BIT` 优先区域，将共享 D/IRAM 同时列为 8BIT 与 EXEC／32BIT 候选。`soc.h` 将 D/IRAM 的 I 总线别名定义为 `0x400a0000`–`0x400bffff`，D 总线别名定义为 `0x3ffe0000`–`0x3fffffff`。探针先调用固定 SDK 的 `esp_ptr_in_diram_iram`／`esp_ptr_in_diram_dram`，再调用宽泛的 IRAM／DRAM 范围判断，且核对每块起点与末端属于同一区域。

第 9 个页对齐块返回 `0x400be000`，按 `memory_layout.c` 属于共享 D/IRAM 的 I 总线别名，使 8BIT free 从 61,404 降到 57,300 B。其 `esp_ptr_executable=0`，因为固定 SDK 的 `esp_memory_utils.c` 将该辅助判断限制在 `SOC_IRAM_HIGH=0x400aa000` 以下；这与 heap capability 的区域表口径不同。探针如实记录两者，**没有试图执行该地址**。第 10 至 13 块位于 `0x400a9000`、`0x400a7000`、`0x400a5000`、`0x400a3000`，同属共享 D/IRAM；逐块地址及两个能力池读数见[脱敏完整 trace](esp32-exec-pool-qemu-trace.txt)。

## 输入、签名和运行结果

本探针从上一轮签名镜像 SHA-256 `d57ca7be871f59b6366b8614e1349dffb3cba9506c5438571956e1bfae6c4e32` 的**仓外副本**开始。[准备脚本](prepare_esp32_exec_pool_qemu.py)校验原 `sdkconfig` `b1f4e090370e5ac7b20a05cf84faf941f39c847df7b18eccc1e777e90586d6ff`、镜像、锁、主应用 CMake、认证探针与密文 fixture，再复制到新目录，仅在复制件加入[能力池 C fixture](esp32_exec_pool_probe.c)、一次 READY + guest 调用、直接 `esp_hw_support` 构建依赖，并将生成锁的四个本地组件物理路径重定向到新目录。Base `1f43b6f`、FRP `1f0c8f3`、MQTT `9d6d95e`、OTA `2072731`、Container 嵌入组件 `8eb805f`、WAMR `26c235e`、实际 lwIP `2758df4`、guest 和密文均与上一轮相同；**生成 `sdkconfig` 字节完全相同**。测试签名键始终在原仓外位置，不进入 Git。

仓外 fixture 仅在第二次（Base READY 后）`open → init → event` 成功且 guest 返回 3 时运行两组申请；第一轮 Base 初始化前的原认证探针照常运行。能力池申请全部清理后，原认证探针仍显示两条 4 KiB 记录认证成功、坏 tag 返回 `-11`、完整 64 KiB 记录在第 14 块返回 `-20`，消费 53,264 B 并释放 13 块；guest 关闭及探针结束回到原检查点，`probe_summary failures=0`。这验证能力池探针对随后 8BIT reader 没有持续占用，并非读写兼容证明。

新 ECDSA v1 测试键签名 app **1,114,100 B（`0x10fff4`）**，SHA-256 `9d77303c2b6854465b77dd4e9e05b5433186751fbfa48d4e5acf300ebdc1e792`；固定 SDK `espsecure verify-signature --version 1` 报告 `Verifying 1114032 bytes of data... Signature is valid.`。验签数据长不是签名文件长。官方 Espressif Xtensa QEMU 在合成 4 MiB Flash/eFuse 下运行 45 秒后由宿主 SIGTERM 结束，无 panic；Wi-Fi 仍 `unconfigured`，没有 FRP session 或真实网络。仓外实验不执行 `flash`。

## 收据与复现

[SHA-256 收据](esp32-exec-pool-qemu-sha256.txt)记录固定 SDK 区域表、原签名输入、fixture、生成配置与锁、新签名镜像、构建、验签、原始 QEMU 日志及脱敏 trace。脱敏 trace 遮盖 QEMU 生成的 `device_id`／`boot_id`，移除终端控制码和行尾空格，保留了所有申请地址与两种能力池读数。

在拥有上一轮精确仓外工程、测试键、固定 SDK 与官方 QEMU 的 mac-work-1，将本仓准备脚本和 C fixture 复制到同一仓外目录后执行；新目标目录必须尚不存在：

```bash
probe_root=/private/tmp/esp32-exec-pool-final-qemu-20260927
python3 /private/tmp/prepare_esp32_exec_pool_qemu.py \
  /private/tmp/esp32-auth-wifi-iram-off-exact-20260927/probe \
  "$probe_root/probe"
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

`run_esp32_wifi_iram_qemu.py` 与本仓[限时 QEMU 启动脚本](frp_chunked_qemu.py)逐字节相同。重编时间可能改变签名镜像 SHA；复验应先核对精确源码／配置、签名尺寸与验真、逐块地址区域、读数、分配／释放和原认证探针结果。
