# 双目标 4 MiB Flash 容量与首次切换边界

## 核对输入

本检查点只使用公开源码、已记录的只读现物事实和已验签的**测试键**镜像长度，不读取或写入设备。C3 候选表为本仓的 [`five-component-kconfig-ab-partition.csv`](five-component-kconfig-ab-partition.csv)；ESP32 候选表为 `esp-base@7ec2738` 的 [`esp32-partition-table.csv`](https://github.com/esp-space/esp-base/blob/7ec2738/firmware/partitions/esp32-partition-table.csv)。两个表分别经固定 ESP-IDF `578cf89c` 的 `gen_esp32part.py --flash-size 4MB --secure v2/v1` 验证；ESP32 表还由 Base 仓测试逐项解析生成的二进制表。当前 C3 **产品**表仍是旧双 `0x1e0000` app、末端 `base_store@0x3e0000/0x20000`，不包含业务包分区。

| 目标 | Base 产品测试键签名镜像 | 当前五仓 QEMU 测试键签名镜像 | 旧数据保存边界 |
| --- | ---: | ---: | --- |
| ESP32-C3 | `0x101000`（1,052,672 B），RSA v2；[Base 精确锁收据](https://github.com/esp-space/esp-base/blob/7ec2738/docs/operations/development-checkpoint.md) | `0x121000`（1,183,744 B）；含 guest、wire fixture、强制链接和仿真入口 | 原 `base_store@0x3e0000/0x20000` 的全部原始字节须原位保全；后 31 页来源未证明 |
| ESP32-D0WD-V3 | `0xefff4`（983,028 B），ECDSA v1；同一 Base 收据 | `0x10fff4`（1,114,100 B）；含 guest、wire fixture 和强制链接 | 旧 AT `nvs`、`at_customize` 前两页各非空；私有双份完整 Flash 已验证可用 16 KiB 归档无损重建这两个**完整旧分区** |

产品 Base 镜像还没有主应用业务包入口；QEMU 镜像又有正式产品不需要的探针字节。两组尺寸均不能直接充作最后五能力产品的签名长度。下面的容量结论分别以表中**实际尺寸**为条件，不用探针字节推算产品增长。

## 三个包槽的当前格式边界

[`product_package.py`](../../tools/product_package.py) 和设备流式解析器同时限制 manifest 不超过 4,096 B、RSA-PSS 签名恰为 384 B、Wasm 不超过 512 KiB。三个规范 ustar 成员各有 512 B header，内容补齐到 512 B；末端至少两个零块并补齐到 10,240 B record。于是当前格式的规范包长度上界为：

```text
manifest 512 + 4096 =   4608
signature 512 + 512 =   1024
Wasm      512 + 524288 = 524800
end                       1024
before record padding   531456
ceil(531456 / 10240) * 10240 = 532480 = 0x82000
```

[既有签名样包](five-component-capacity-probe.md#保留区与两组仅供容量分析的布局)的 manifest 仅 550 B、Wasm 恰为 512 KiB，实际包也达到 `0x82000`；所以这个上界可达到。若继续接受当前全部规范包，三个互不覆盖、各可保留一份确认/回退/trial 引用的槽至少需要 `3 × 0x82000 = 0x186000` B。`0x82000` 是**当前格式的可达存储上界**，尚不是已证明 512 KiB Wasm 业务模块能够在两台板上实际运行的 `max_package_size_bytes`；不靠静默降低当前接收范围挤出空间。

## C3：原位保留 128 KiB 历史区的条件几何

前 `0x20000` 留给启动链、表、默认 NVS、otadata、PHY 和 coredump；末 `0x20000` 保持旧 `base_store` 原始地址，改标 `base_archive`，只让新程序在另一处分配活动 `base_store`。若新 NVS 取已在固定 SDK 合成 QEMU 验证过 100 代最大 v3 配置与两份旁侧 blob 的 `0x8000`，其余 `0x3c0000` 中须放双 app 与连续三包分区。静态原始字节预算留给双 app `0x3c0000 - 0x186000 - 0x8000 = 0x232000`，不计 app 起点对齐时平均最多 `0x119000`。

IDF app 起点须在 64 KiB 边界，分区长度与包槽须在 4 KiB 边界。若两个 app 都取原始上界 `0x119000`，四块恰好填满中段而不容许任何对齐空隙；首个 app 结束后的偏移模 `0x10000` 为 `0x9000`，在第二个 app 前插入的包区和新 NVS 长度模 `0x10000` 只可能是 `0`、`0x6000`、`0x8000`、`0xe000`，均不能补齐所需的 `0x7000`。因此两个等大 app 不可能都到 `0x119000`。现有 C3 原型用 `0x118000` 双槽达到下一档 4 KiB 大小，故它是这些条件下的最大共同值：

| 分区 | 起点 | 大小 | 结束 |
| --- | ---: | ---: | ---: |
| `ota_0` | `0x20000` | `0x118000` | `0x138000` |
| 新 `base_store` | `0x138000` | `0x8000` | `0x140000` |
| `ota_1` | `0x140000` | `0x118000` | `0x258000` |
| `product_pkgs` | `0x258000` | `0x186000` | `0x3de000` |
| 未分配 | `0x3de000` | `0x2000` | `0x3e0000` |
| 旧 `base_archive` | `0x3e0000` | `0x20000` | `0x400000` |

三个包槽依次为 `0x258000..0x2da000`、`0x2da000..0x35c000`、`0x35c000..0x3de000`，每槽 `0x82000`，原 `econtainer_slots_geometry_valid` 已接受。当前 **Base 产品** signed `0x101000` 可装入原型双 app，每槽余 `0x17000`（94,208 B）；总未分配尾隙仅 8,192 B，不能视作 FRP 64 KiB 暂存区。[八页 NVS 合成验证](https://github.com/esp-space/esp-base/blob/7ec2738/docs/operations/c3-eight-page-nvs-capacity.md)证明固定 SDK 的正常 CAS/换页路径，尚未证明实际 Container metadata、写入中断电或当前异常旧区可迁移。

若把本次带测试 fixture 的 `0x121000` 五仓 QEMU 镜像当成两个正式 app，单槽超过原型 `0x9000`（36,864 B）；即使完全忽略所有对齐缝隙，`0x20000 + 2 × 0x121000 + 0x186000 + 0x8000 + 0x20000 = 0x410000`，已经超过 4 MiB `0x10000`（65,536 B）。这严格否定**该镜像长度**与上述保存/包/NVS条件的同存布局，但不否定去掉探针后的最终产品镜像。产品主应用装入 Container 安装入口后须重新签名、验签和量尺寸；大于 `0x118000` 时此条件布局不能使用。

旧 128 KiB 标 `readonly` 只使 ESP-IDF 分区 API 拒绝擦写，不约束绝对地址 Flash 调用或外部烧录器。现物旧区第一页是有效 v1 NVS，后 31 页均无效且非空，不能把后者假设成可丢弃填充。[逐页只读分类](https://github.com/esp-space/esp-base/blob/7ec2738/docs/operations/c3-base-store-page-forensics.md)仅证明部分字节与旧 `ota_1` 重合；完整 v3 预检继续阻断。原位留存方案必须在离线候选及写后回读中证明 128 KiB 每字节均未变，同时就新活动 NVS 中的 revision 5/v1 配置转换和历史异常页的后续保存边界取得维护者决定。

## ESP32：现行离线产品表用满全片

| 分区 | 起点 | 大小 | 结束 |
| --- | ---: | ---: | ---: |
| 前段启动链/默认持久区 | `0x0` | `0x20000` | `0x20000` |
| `ota_0` | `0x20000` | `0x120000` | `0x140000` |
| `ota_1` | `0x140000` | `0x120000` | `0x260000` |
| `product_pkgs` | `0x260000` | `0x186000` | `0x3e6000` |
| `at_old_raw` | `0x3e6000` | `0x4000` | `0x3ea000` |
| 新 `base_store` | `0x3ea000` | `0x16000` | `0x400000` |

该表的 `0x20000 + 2 × 0x120000 + 0x186000 + 0x4000 + 0x16000 = 0x400000`，没有未分配 Flash。三个包槽依次为 `0x260000..0x2e2000`、`0x2e2000..0x364000`、`0x364000..0x3e6000`。当前 **Base 产品** ECDSA v1 signed `0xefff4` 在每个 app 槽余 `0x3000c`（196,620 B）；带探针的五仓 QEMU signed `0x10fff4` 在每槽余 `0x1000c`（65,548 B）。ESP32 的 22 页新 NVS 尚无与 C3 八页同等的实际产品写入/回收和断电验收；全片无空隙，未来增长只能消耗已说明的 app 或其他分区容量。

旧 ESP-AT 的 `nvs@0x12000/0xe000` 与 `at_customize@0x20000/0xe0000` 各仅前两个 4 KiB 页非空；Base 的[归档器](https://github.com/esp-space/esp-base/blob/7ec2738/tools/archive_esp32_at.py)已用两份一致的真实全片备份证明，把四页汇成 `0x4000` 可逐字节重建**这两个旧分区**。新表的 `at_old_raw` 位置尚未写入实板；旧内容在首次迁移前若变化必须用当轮新鲜双备份重算。旧 AT 没有 Base UUID，也没有可迁入的 Wi-Fi SSID/密码；新 UUID/活动配置须分别按新 Base 合同建立，不能从 MAC 伪造旧身份。完整旧设备恢复仍依靠逐板双份完整 Flash，而非 16 KiB 归档。

## 首次切换与 P6-03/P7-01 状态

两台的旧/新 app 地址或长度均变化，旧 bootloader、分区表与 otadata 的既有组合不能推定可解释新签名双槽。应用 OTA 无法把“匹配的新 bootloader + 新分区表 + 两份有效签名 app + 持久配置/归档 + otadata”做成一次原子回退。几何可装入也不证明断电中间态可由设备自身启动。首次迁移应先完成下列可核对事实，**本检查点未实施写板**：

1. 分别重新确认当轮芯片、4 MiB、物理端点、旧 bootloader/分区/otadata/身份及安全状态；同板独立读取两份逐字节一致的完整 Flash，并备好外部完整恢复程序与读回验收。不能跨板用恢复件。
2. 冻结最终五能力产品的精确源码锁、签名方案、bootloader、两个可启动 signed app、完整包格式上限和目标分区；用固定 SDK 官方工具逐镜像验签/尺寸/芯片，并在仓外离线拼装及逐区比对目标 4 MiB 图像。测试键与 QEMU 空桩制品不进入该图像。
3. C3 保留默认 `nvs/base_identity/device_uuid`、原 128 KiB 旧区全部原始字节，并在异常页处置决定与正式预检闭合后建立新活动 v3 配置，保证双槽都能读；ESP32 对当轮 AT 归档做完整旧分区重建验证，写入新表 `at_old_raw` 并读回，新 Base 身份与空配置按实际设备结果验收。两板都不得用全擦 NVS 代替转换。
4. 在明确设备授权和同板独占维护窗口执行一次性切换；写后从物理 Flash 逐区读回 bootloader、表、两个 app、otadata、默认/活动 NVS、旧区归档及未授权区，并独立核对签名、身份、配置和可回退槽。中断或事实不明时保留现场，按同板完整旧 Flash 恢复并逐字节读回，不自动重试或把旧 app 当成新表的回退镜像。

当前 C3 产品表尚未加入包区，C3 正式 v3 预检被异常页阻断；ESP32 新表虽在 Base 源码中，旧 AT 到新签名启动链和新 NVS 尚未实板切换。两台最终五能力产品镜像、真实业务包大小与运行内存/最大连续块、FRP/TLS/MQTT/OTA 同存、签名双槽回退及写入中断电均未验收。因此这些静态容量证明仅能推进 P6-03 设计，不能勾选 P6-03 或开始 P7-01 物理迁移。
