# C3 注册会话中的 32BIT 堆能力实验

## 范围与结论

2026-09-27 在独立仓外 ESP32-C3 QEMU 工程中，以已到官方 FRPS `REGISTERED`、首个 Pong 的五仓冻结输入为基线，仅给 QEMU 应用增加[只观察分配的探针](c3_registered_32bit_qemu_probe.c)。Base 报告 READY，ABI 2 标准 64 KiB guest 持续存活；OpenETH 获得 DHCP 地址，FRP 公共客户端到达 `client_phase=5`、`ready_sessions=1`、`pongs=1` 后才运行实验。`efrp_destroy`、OpenETH 清理和 guest 第二轮仍按原有顺序执行。本轮没有改五仓产品源码、默认配置、实际设备或生产服务。

**停止点是第 4 笔 4,096 B 的 `MALLOC_CAP_32BIT` 申请。**前三笔合计 12,288 B，地址全被固定 SDK 的 `esp_ptr_byte_accessible` 判定为可字节访问，且 `esp_ptr_in_iram` 对相同范围为假。每一步 `MALLOC_CAP_8BIT` 与 `MALLOC_CAP_32BIT` 的 free/largest 读数完全相同；第 4 笔失败时均为 7,792/3,584 B。该运行状态没有独立的 64 KiB 32BIT 池，不能据此把现有 AEAD 字节缓冲搬到另一片内存，也没有证明真实 FRP session 内满长 64 KiB 认证记录可用。

固定 ESP-IDF fork `578cf89c343e388db43ba1f4ddcd602fedcb763c` 的 `components/heap/port/esp32c3/memory_layout.c`（SHA-256 `2a385b71188cd70b2ba277caa70ce334bbf6ba90f454341dd391ef5a7fce29a6`）将 C3 RAM 的 common caps 同时标为 `MALLOC_CAP_32BIT | MALLOC_CAP_8BIT`；Retention RAM 的后备能力和启用为堆的 RTCRAM 后备能力也使用同一 common caps。当前 `sdkconfig` 设有 `CONFIG_ESP_SYSTEM_MEMPROT=y` 与 `CONFIG_ESP_SYSTEM_ALLOW_RTC_FAST_MEM_AS_HEAP=y`。因此这份静态布局与动态观测一致：`MALLOC_CAP_32BIT` 在此镜像中是对现有可字节访问堆的另一种申请条件，并未提供可单独加总的 64 KiB 内存。

## 冻结输入与实验接线

基线为 `mac-work-1:/private/tmp/esp-c3-lazy-work-ab-20260927/new/probe`。其五仓来源为 Base `058e965671fa0e4d417114897541710699571c52`、FRP 基础版 `1f0c8f37db3765a74b3b95871bb266d0c73d1248` 加 C3 Yamux `3816255b19570b0c11a4943402ad61ed65331d5d` 与按需工作流缓冲 `e94afa6c98992501c54d6ff5571d6d49d0ce0855`、MQTT `9d6d95e779f4f5ff387a6d9b54015bf4e43565f2`、OTA `207273188b984161362824c3344614e812016836`、Container `8eb805f3f12cb3cd836e9833acb4aca878ae80e7`；固定 SDK、lwIP、WAMR 锁与前一轮 [C3 官方 FRPS 容量实验](c3-frps-qemu-session-capacity-probe.md)相同。C3 Yamux 改动见 FRP 的 `docs/operations/c3-yamux-stream-ring-capacity.md`，工作流按需分配与 A/B 构建见 FRP 的 `docs/operations/p6-frp-lazy-work-stream-capacity.md`。该基线自己的 QEMU 日志已记录 `REGISTERED`、`ready=1`、`pongs=1`、`destroy=0` 和 `probe_summary runs=2 failures=0`；本轮不是从原先尚未登录成功的旧 FRP 镜像开始。

[准备脚本](prepare_c3_registered_32bit_qemu.py)核对输入 `sdkconfig`、FRPS 探针、三份 Yamux 及两份工作流源码摘要，复制仓外工程时排除旧 `build`，只增加 `c3_registered_32bit_qemu_probe.c`，修改应用 CMake 和 FRPS 探针接线。构建后逐文件核对原工程与派生工程（排除 `build`）：原有 2,757 个文件中仅 `firmware/apps/esp_base/main/CMakeLists.txt`、`frps_session_qemu_probe.c` 改变，另新增一份 C 探针；没有其他文件增删或内容差异。最终准备脚本另向仓外新目录重放，新增探针、CMake 和注入后 FRPS 探针摘要与实际运行工程相同；重放工程未再构建或运行。签名测试键、临时 CA/证书/私钥、FRPS fixture 与所有运行日志都保留在仓外，未提交进 Git。

探针只对 16 个预定位置逐笔调用 `heap_caps_malloc(4096, MALLOC_CAP_32BIT)`，失败即停止申请；对每个成功块的首末地址使用 SDK 判定函数分类、读取两类堆的 free/largest，从不读取或写入这些分配地址的字节。已分配块逆序全部 `heap_caps_free`，随后再次核对 FRP 状态。`iram_alias=0` 只表示指针不在 `esp_ptr_in_iram` 判定的 IRAM **地址别名**，不表示物理 SRAM 与可执行映射没有关系。

## QEMU 动态读数

`free8/largest8` 与 `free32/largest32` 均为字节数。日志中 `address=0` 的分类 `-1` 表示无待分类地址。当前一笔申请成功时，两种 caps 的 free 均下降 4,100 B，包含分配器开销；失败时不会改变 free。

| 阶段 | 申请地址 | byte-accessible / IRAM alias | 8BIT free / largest | 32BIT free / largest |
| --- | --- | ---: | ---: | ---: |
| REGISTERED、Pong 后 | — | — | 20,092 / 9,216 | 20,092 / 9,216 |
| 第 1 笔成功 | `0x3fcda180` | 1 / 0 | 15,992 / 7,680 | 15,992 / 7,680 |
| 第 2 笔成功 | `0x3fcdb184` | 1 / 0 | 11,892 / 7,680 | 11,892 / 7,680 |
| 第 3 笔成功 | `0x500001a4` | 1 / 0 | 7,792 / 3,584 | 7,792 / 3,584 |
| 第 4 笔失败 | — | — | 7,792 / 3,584 | 7,792 / 3,584 |
| 逆序释放全部 3 笔后 | — | — | 20,092 / 9,216 | 20,092 / 9,216 |

第三笔地址落在启用为堆的 RTC 地址范围，SDK 仍将其判为可字节访问；它不是另一块只可 32 位访问的 IRAM。失败记录的 heap hook 报告 `last_size=4096`、`last_caps=2`，与本探针的 `MALLOC_CAP_32BIT` 申请一致。释放后客户端仍为 `client_phase=5`、`ready=1`、`pongs=1`；`efrp_destroy` 返回 0，OpenETH `after_cleanup` 出现，guest 最终 `probe_summary runs=2 failures=0`。无 panic 或 OpenETH 接收缓冲持续失败日志。20 秒 QEMU 由宿主有界终止，FRPS fixture 记录 `QEMU_FRPS_STOPPED`，没有测试进程遗留。释放后 8/32BIT free/largest 回到**同一实验申请前**数值；不从这项读数推断整条 FRP/网卡生命周期无泄漏。

## 复现与收据

先按照 FRP 的 C3 Yamux 与按需工作流容量报告生成达到 REGISTERED/Pong 的冻结 `new/probe`，再从本工作树复制准备脚本、探针、运行脚本和本仓现有 `frp_chunked_qemu.py` 到 `mac-work-1` 仓外目录。下列命令只构建并运行 QEMU；要求输出和日志目录事先不存在，且本地测试端口 `29173` 空闲。运行脚本只启动该工程自己的官方 FRPS v0.71 fixture，并按 PID 清理。

```bash
ssh mac-work-1 'mkdir -p /private/tmp/esp-c3-registered-32bit-20260927'
scp docs/operations/prepare_c3_registered_32bit_qemu.py \
  docs/operations/c3_registered_32bit_qemu_probe.c \
  docs/operations/run-c3-registered-32bit-qemu.sh \
  docs/operations/frp_chunked_qemu.py \
  mac-work-1:/private/tmp/esp-c3-registered-32bit-20260927/
```

```bash
python3 /private/tmp/esp-c3-registered-32bit-20260927/prepare_c3_registered_32bit_qemu.py \
  /private/tmp/esp-c3-lazy-work-ab-20260927/new/probe \
  /private/tmp/esp-c3-registered-32bit-20260927/probe
bash /private/tmp/esp-c3-registered-32bit-20260927/run-c3-registered-32bit-qemu.sh \
  /private/tmp/esp-c3-registered-32bit-20260927/probe \
  /private/tmp/esp-c3-registered-32bit-20260927/frp_chunked_qemu.py \
  /private/tmp/esp-c3-registered-32bit-20260927/run
```

这些命令应在具有固定 SDK 和源工程的 `mac-work-1` 上、脚本已复制到所示仓外目录后执行。本轮签名 app 通过公开测试 RSA v2 signature block 0 校验，镜像含测试键、QEMU 控制台和校准空桩，**不能刷实体板**。

| 本轮收据 | SHA-256 |
| --- | --- |
| 输入 / 派生后相同 `sdkconfig` | `a66858cf817841457a8557a46ec18e5757e4889a5e31dffc0978adf8a277fb52` |
| 输入 FRPS 探针 | `601f69047acdca637bbab49dd76f5a29168577705a9b5e8352281b8a334e2ddd` |
| 输入 FRP `work.c` / `work_internal.h` | `f0da8a9852e339012c82180e8ac73451a3bc4347b881ff17a1141be40f445f7b` / `3b5db34f163aad4771d89ee7cc9b23a5cb778831220f1f4f8e45e56961da7b28` |
| 本轮 C 探针 / 注入后 FRPS 探针 | `516e29bd30c8859fa8db03ebc818a441e3a0d2838eeeb036e7fb6dd84535bd65` / `437c4f86d7e0184584bbfcd097b8fb2e35affcd3dd3a2aaa3eba5c2245292678` |
| 签名 app / ELF | `b8257f5c0c815be4186b4c3444c340aa9bd52534583c822cb45033a50773121b` / `8ed5899dbb0f8561e72bf3932ee1cfaf34f27ed7037ded839f464bb7d3bc0913` |
| QEMU 全日志 / FRPS 停止日志 | `d079e4e53dc1d9ad5df855f2db887dab24616685fc17757cd0d27489aedf559c` / `650e345436ea149e58a5ba7bcfd579c5ff2aaf5d8f4e5be26a5d79be37df8e75` |

签名测试证书每次随机生成，重新派生的 app/ELF 摘要可能不同；输入源码与动态容量阶段是复现重点。此结果限定于固定 SDK、当前 QEMU `OpenETH`、关闭两项 Wi-Fi IRAM 的仿真配置和本次五仓冻结代码，不能替代实体板 Wi-Fi、Broker、OTA 与真实 FRP/guest 并发容量验收。
