# ESP32-C3 单页 guest 开发切片：2026-09-24

## 目标与实际约束

本分支为 ESP32-C3／4 MiB 保留现有签名、导入授权、指令预算、入口期限、单实例隔离和三包槽恢复合同，将业务 guest 的 Wasm 线性内存固定为一页（64 KiB）。当前 guest ABI 为 2：五项导出包含 4 KiB 页内事件区地址；运行期禁用 WAMR 附加 host heap 和 shrink，真实可访问页与标准 `memory.size` 一致。下文早期观测保留当时四导出 ABI 和附加 heap 的输入；最新修正见文末。仍未完成五能力并发或真实设备验收。

此前[五组件 QEMU 容量切片](five-component-qemu-capacity-probe.md)在同一旧组合中测得：固定两页 counter 使 WAMR 单次连续分配请求达到 **135,168 字节**（128 KiB 线性内存加 4 KiB 宿主管理 heap），因连续块不足无法实例化；将同一源码改成一页后，该请求为 **69,632 字节**，减少 **65,536 字节**，并完成两次 guest 生命周期。新版精确锁的独立复测也让一页 guest 在 Base READY 后完成 `open → init → event → stop → close`，但存活时只余 **11,328 字节**空闲堆、**7,680 字节**最大连续块，且没有 FRPS、Broker、Wi-Fi 实际连接或 OTA 下载。

初期锁定 WAMR 构建显示 `Shrunk memory enabled`。其 Normal loader 仅在同时找到数据结束、堆基址和栈顶全局导出且无可能的 memory grow 时收缩声明内存；当时 ABI 精确要求三个函数加 `memory` 共四个导出，counter 也只有一个栈指针全局，故不能靠重复打开该开关节省这份 guest 的 128 KiB。额外导出这些全局会改变既有 ABI，当时未采用；现在显式关闭 shrink，保持完整标准页。

## 同批接线

- `counter_guest.py` 和 `spec.example.json` 均固定一页，保持 4 KiB 栈和原三函数签名；旧两页 counter 不再通过样例检查器。
- 主机 `product_package.py` 要求签名清单的 `memory_limit_bytes` 精确为 65,536，Wasm 最小和最大页数均须为一页。设备包槽回读路径在验签后检查同一清单值及原始 Wasm 内存节，不让较宽的独立设备授权扩张此上限。
- 组件私有运行期只接受 `max_memory_pages=1` 的调用配置，并从原始 Wasm 内存节拒绝旧两页 guest。其余资源限额、能力授权、指令与期限检查维持原合同。
- 测试使用真实 wasi-sdk 33 编译的一页 guest，以及由该产物只改内存节得到的结构正确的两页负例。主机和设备端分别检查旧清单、旧 guest 的拒绝；WAMR 真解释器回归仍执行 counter、事件、宿主导入、计时器和失败关闭重开。

固定 wasi-sdk 33 生成的单页 counter 为 **437 字节**，SHA-256 为 `629a44e6b83c541224e8e6b7253142da61b9887790ac36e36d972591deded31f`；旧两页产物同为 437 字节，SHA-256 为 `1896ae9ed2390bd4fb71af9d50965533a679245806c9c07675d65b81be42c91c`。因此这项变更解决的是单实例 RAM 申请，**没有为 4 MiB Flash 的双固件或三业务包槽增加实质空间**。512 KiB Wasm 字节上限仍是扫描器上限，不是 C3 可安装包槽容量。

`60b65d` 基线运行期另有独立 RAM 上界：`econtainer_runtime_open` 对完整 Wasm 执行 `malloc(wasm_size_bytes)` 和复制，再交给 Classic/Normal `wasm_runtime_load`；该接口要求完整输入缓冲可写，并持续有效至 `wasm_runtime_unload`。此前五组件 Base READY 的 101,716 字节空闲堆和 Base C3 小内存分支的 124,924 字节，均属于旧组合的历史观测，不能当成当前精确锁的余量。

## 只读节装载候选

本候选分支从 `codex/c3-low-memory@60b65d` 切出，只改变私有运行期的装载方式，不改变签名包、四导出 ABI、静态准入、资源限额或 guest 行为。`open` 在静态扫描之后解析 Wasm 节，短暂分配节描述符；代码节与数据节复制到各自可写缓冲，保留至 `wasm_runtime_unload` 之后释放。其余节仅在锁定的 WAMR Classic/Normal `wasm_runtime_load_from_sections` 加载期间引用原输入；加载完成即释放描述符，调用者可释放原输入。锁定 WAMR 的函数体和活动数据段分别保留代码节、数据节的指针；名称在节列表入口复制；关闭路径不会代替调用者释放这两个缓冲。这个所有权判断依赖固定 WAMR `a34d721` 的 Normal loader 实现。Fast Interpreter、Debug Interpreter、保留自定义节和名称节的构建均被组件编译配置拒绝。

主机回归把只读 `mmap` 输入传给 `open`，立即 `munmap`，再运行真实编译的计时/日志 guest。该模块包含非空 **22 字节数据节**，调用后验证从数据节读出的 `init` 和 `first` 日志；另有畸形节长及非法 UTF-8 自定义节负例。373 KiB 自定义节填充 counter 同样完成只读映射、解除映射和后续入口调用，macOS 主机 `open` 的 malloc 用量增量为 **10,896 字节**。这只证明完整 Wasm 总长度的常驻副本已消除：填充节不含实际业务代码，也不说明 373 KiB 业务模块能运行。

以同一固定 IDF/WAMR 和五组件 Base QEMU 仓外切片测得以下**网络未连接**结果。每个模块均声明一页 guest 内存；代码边界模块在 437 字节 counter 上增加一个未调用、含 NOP 的有效函数，属于代码节而非自定义填充。表中存活余量是在 Base READY 后 `open → init → event → stop` 期间测得，`largest` 为最大连续可分配块。

| Wasm 输入 | 字节数 | Base READY 后调用 | 存活空闲 / largest | 签名 app 大小 |
| --- | ---: | --- | ---: | ---: |
| 原始 counter | 437 | 全部成功，`guest=3` | 46,724 / 34,816 字节 | `0x121000` |
| 自定义节填充 counter | 381,952 | 全部成功，`guest=3` | 46,724 / 34,816 字节 | `0x181000` |
| 代码节增加 39 KiB NOP | 40,383 | 全部成功，`guest=3` | 5,520 / 1,920 字节 | `0x131000` |
| 代码节增加 40 KiB NOP | 41,407 | `open=7`，未创建 guest | 不适用 | `0x131000` |

这组切片的 Base READY 为 **136,936 / 114,688 字节**；执行前的空闲堆为 128,572 字节。41,407 字节模块在 Base 启动**前**可以运行，在 READY 后返回 `ECONTAINER_RUNTIME_ENGINE_FAILURE`；单独加诊断输出的仓外构建取得 WAMR 原因 `allocate linear memory failed`。因此上表是当前堆占用下对该代码形状的 **40,383 成功 / 41,407 失败**边界，不是所有业务模块通用的最大 Wasm 长度。真实代码、数据、导入及网络分配会改变边界。无大 Wasm 内嵌的五组件签名 app 为 `0x121000`，小于候选 `0x140000` app 槽；381,952 字节输入内嵌在测试 app 导致 `0x181000`，**超过**候选槽，不能作为 4 MiB 可刷写固件证据。

设备 `slots_idf.c` 的 `product.pkg` provider 原先仅通过 `esp_partition_read` 提供有界回调。本轮增加短时 `esp_partition_mmap/munmap`，由组件私有槽装载入口在共享锁内选择真实绑定，对同一映射包重新验签授权并调用 `runtime_open`，随后解除映射；详见[三槽存储检查点](three-slot-storage-checkpoint.md)。真实签名包、合成槽和真实 WAMR 的主机集成已覆盖该链路，但 Base 尚无已冻结的包分区及执行器装配，因此不能称为设备包槽已运行。仍须用真实代码和数据模块、FRP/TLS/MQTT 同时运行及物理板测峰值。此前 **373 KiB** 三槽 Flash 几何上界与本表 RAM 边界是两个不同约束。

## 本分支验证

从公开 `esp-container master@77155349795f3b6564e6f6fbbacb61e884e583ad` 创建独立 `codex/c3-low-memory` 分支。固定 wasi-sdk 33 下，Python unittest **19/19** 通过；公开 WAMR fork `a34d721b630213f59fde0b40cebbb980903660e8` 的 Classic/Normal loader、指令计量主机 CTest **8/8** 通过，`runtime_instance` 在一次期限负例的宿主耗时波动被修正后额外连续通过三次。固定 ESP-IDF fork `578cf89c343e388db43ba1f4ddcd602fedcb763c` 与 esp-lwip `2758df4cd3666b3b2a5b53830148379326425c0d` 的 C3 独立样例完成 SDK 锁检查及 `idf.py build`，生成 app `0x39360` 字节。这个原样样例没有读取新 counter，也没有执行本分支私有运行期；实际一页 guest 调用由锁定 WAMR 主机测试和既有仓外 QEMU 切片分别覆盖。没有在本分支刷写物理设备。

只读节装载候选另以固定 WAMR 和 wasi-sdk 运行主机 CTest **8/8**、AddressSanitizer/UndefinedBehaviorSanitizer CTest **8/8**、Python unittest **19/19**；`runtime_instance` 增加只读映射、非空数据节、输入解除映射后的入口调用及畸形节负例。固定 IDF 的独立 C3 样例重新构建通过；另以 `-DWAMR_BUILD_CUSTOM_NAME_SECTION=1` 独立重配置，确认组件 CMake 守卫拒绝该 profile。该样例不调用私有运行期；上表实际 C3 运行来自仓外五组件 QEMU。QEMU 使用测试签名键与 ADC2 专用空校准桩，未建立 Wi-Fi、FRPS 或 Broker 连接，也未刷写物理板。

## VM 额外内存分项复核

2026-09-24 在 `bbc186ed97fc6c6e8ea4f2b71189560679100caf` 上只读复核节装载与 WAMR 生命周期，并在仓外独立 C3 样例加入阶段采样。本轮没有新增运行期优化，也没有降低 guest、执行栈、host heap 或能力限额。固定输入为 IDF `578cf89c343e388db43ba1f4ddcd602fedcb763c`、lwIP `2758df4cd3666b3b2a5b53830148379326425c0d`、WAMR `a34d721b630213f59fde0b40cebbb980903660e8` 和上文 437 字节单页 counter；官方 C3 QEMU 为 `esp_develop_9.2.2_20260417`。探针仅在私有运行入口的分配阶段打印 8-bit heap，在同一进程串行建立三个 8 KiB pthread，分别运行完整生命周期并等待线程回收；没有 Base、网络或 Flash 包槽。

第二次和第三次生命周期的采样完全相同。下表的“本阶段占用”是相邻阶段空闲堆差，含分配器开销；它不是单个 `malloc` 的请求长度。

| 阶段 | 空闲堆 / 最大连续块（字节） | 本阶段占用（字节） |
| --- | ---: | ---: |
| 建线程前 | 334,356 / 188,416 | — |
| pthread 中，打开前 | 325,992 / 188,416 | 8,364 |
| runtime context | 325,604 / 188,416 | 388，结构体本身为 376 |
| 节描述符与代码副本准备完毕 | 325,368 / 188,416 | 236 |
| WAMR 初始化完成 | 325,368 / 188,416 | 0，已预热 |
| 模块已加载，节描述符已释放 | 324,644 / 188,416 | 724，含本阶段分配与描述符回收的净额 |
| 实例化完成 | 250,016 / 114,688 | 74,628 |
| exec env 创建完成 | 244,124 / 114,688 | 5,892 |
| init/event/stop 返回 | 244,124 / 114,688 | 0，事件结果为 3 |
| close 后，线程仍存活 | 325,992 / 188,416 | 恢复至该线程打开前 |
| join 与任务回收后 | 334,356 / 188,416 | 恢复至预热后的建线程前 |

从建线程前到 guest 存活时共占 **90,232 字节**；扣除 65,536 字节 guest 页后，额外占用为 **24,696 字节**。实例化的 74,628 字节包含一页 guest、4,096 字节 host-managed heap，其余 **4,996 字节**为本阶段实例、内存管理等净占用；不能把这部分归为第二份线性内存。exec env 的 5,892 字节包含 4,096 字节 Wasm 执行栈，其余包含 Classic 控制块地址缓存和执行环境。pthread 的 8,364 字节包含 8,192 字节原生栈。这三类空间职责不同，不能互相去重。首次冷启动的全过程较预热基线多留 **208 字节**；后两轮均恢复相同 free/largest，只证明这三轮未持续累积，不替代百次设备回收或 72 小时测试。

固定 Normal loader 的 `load_from_sections` 会复制导入/导出名称，保留函数代码与活动数据段指针；当前代码/数据副本因此须活到 `wasm_runtime_unload` 之后。实例化没有为这个无 start、无构造导出的 guest 再保留第二个 exec env。当前未找到可以不改变 ABI、业务配额或上游所有权而直接移除的完整常驻副本；尚不能通过缩短原生栈、Wasm 栈、host heap 或 Classic 缓存来声称容量闭环。

进一步只读复核锁定 WAMR 和当前 437 字节 counter：其 code 节仅 **98 字节**，没有 data 节。把 Classic 改为直接执行只读 Flash 代码，理论上最多去掉这次 98 字节代码副本申请，不会释放 64 KiB guest 页；实现还会同时触及 loader、解释器及指令计量。该收益不支持当前阶段扩大上游修改，本轮不实施，也不把它列为网络与 guest 同驻留的容量解决方案。

仓外探针位于 `/tmp/esp-container-memory-ledger.20260924`；三个生命周期的最终 app 为 **240,368 字节**，SHA-256 `888560306bc68ade3803b16f7e8eafa943244fe6f637053d4e4ed06aa5a214c6`；`qemu-ledger.log` 的 SHA-256 为 `f6d08f66dc3865aedbb25cec75d617116c45fd8135125e6292d575934324ec6f`。这是一份未签名的独立仿真探针，不是五组件可发布镜像。本轮原仓真实 WAMR CTest **8/8**、ASan/UBSan CTest **8/8**、SDK 锁检查和原样 C3 样例构建均通过；原样 app SHA-256 为 `f33751e02e15a0d61be5841147a6f6853968e5babc99d122b2b3ee4ad762d6d3`。没有改物理设备或生产分区；P6-03 继续未验收。

## 未验收条件

五组件 QEMU 此次节装载切片中，437 字节 guest 存活时的最大连续块为 **34,816 字节**，小于 FRP 单个 AEAD 接收记录所需的 **65,552 字节**；Base 后续 `fa4d` 切片的相应最大连续块为 **40,960 字节**，仍不足以容纳该记录。此前旧组合的 101,716 字节 Base READY 和 69,632 字节 WAMR 线性内存申请不能直接套算本候选的完整并发边界。保持 FRP 安全语义时，尚未证明 FRP 会话与 guest 同时驻留。物理板的 Wi-Fi/TLS/MQTT 峰值、完整包安装、三槽几何和保留数据迁移也仍待测量。上述 QEMU 观测与主机回归都不授权刷写设备。

## ABI 2 页内事件区与标准页边界

2026-09-24 核对固定 WAMR `a34d721` 及后续对齐修复时发现：一页初始/最大内存加 4,096 字节 host-managed heap 会使 `memory.size` 仍返回 1，却允许读取地址 65,536，`module_malloc(16)` 返回 65,544。相同输入禁用该 heap 后正确触发页外 trap。这是已复现的标准页边界缺口，不能将先前单页声明当作实际 64 KiB 隔离或配额证明。

本仓在原运行职责内完成一次 ABI 硬切。guest ABI 2 精确导出三个入口、`memory` 和不可变 `i32` global `econtainer_event_buffer`；SDK 通过 `guest-sdk/src/econtainer_guest.c` 保留 4,096 字节静态区，链接器导出该区地址，所有字节均计入 65,536 字节标准页。地址必须非零，完整事件区不得越界；主机打包、设备流式扫描和实际 WAMR 实例分别核对导出/类型/地址。运行期不再调用 `module_malloc/free` 传递事件，也删除 `heap_size_bytes` 参数；非空事件借用页内区，正常返回或 trap 后清零此次事件长度，空事件仍为 `(0,0)`。SDK 消费者重编并以 ABI 2 重新签名，不接受旧四导出包或旧 ABI 清单，不保留并行入口。

WAMR 实例化固定 `host_managed_heap_size=0`；样例及组件配置显式关闭 `WAMR_BUILD_SHRUNK_MEMORY`，源码编译守卫也拒绝启用 shrink。实例建立后核对当前/最大页数均为 1、每页恰为 65,536 字节。此修复完整保留满页读写能力；没有缩小 Wasm 页，也没有稀疏内存实现。动态 C/C++ 堆若产品确有需求，仍只能使用这份标准页内空间，当前 SDK 不提供 libc malloc。

组件同时将唯一 WAMR Git 依赖固定为公开 `26c235e53e29acd8b43abe7f3b524577bd4d1ae5`，继承既有 ESP-IDF 修复并加入 Classic 分支栈与 64 位 cell 对齐修复，来源见[许可记录](../design/source-provenance.md)。Component Manager 通过公开 Git 解析，样例 `dependencies.lock` 与下载组件 `.component_hash` 均为 `a799be27248cadffdee6f6fae988dbdf3f5d423baab0b1008d74b8581b5ed507`。修改 pin 后仅 build 曾保留旧 SHA；本轮执行官方 `idf.py update-dependencies` 后再次读取完整 SHA 和 hash 才接受构建结果，没有修改受管理源码。

主机验证使用 wasi-sdk 33：Python **20/20**；旧固定 WAMR 普通 CTest **9/9**；新公开 WAMR 来源的严格 `ASAN_OPTIONS=halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1` CTest **9/9**。测试包含全部 65,536 字节写入/读回、末字节读写、`memory.size=1`、`memory.grow(1)=-1`、地址 65,536 的 load/store 和从 65,533 开始的四字节跨页 load trap；另验证 4,096 字节完整事件、下次调用前清零、错误导出和实例失败后的关闭。签名 host/设备两端共同拒绝旧四导出、错误 global 索引/类型/可变性、零/负值/跨页地址和非法有符号 LEB，同时接受合法末端事件区与有效 padded LEB。独立复核另发现样例 checker 把 `i32.const` 当无符号 LEB 解码，已改为有符号读取并增加栈/事件区的 1/2/5 字节负值、合法 padded 正值和溢出测试；实际包扫描和运行期此前已正确拒绝该负值。原有预算、定时器、只读映射后输入释放、真实签名槽运行和百次生命周期仍通过。

ABI 2 counter 为 **469 字节**，SHA-256 `b9422cb4cb72983141988c4a9a59b602026d94729e2362403a04d1723f98a739`；边界 guest 为 **914 字节**，SHA-256 `047d76e8c2ac3383d57e0d498eca083a70e8fbd7d79bcca2a81e74f20b8fbf07`。固定 IDF `578cf89c343e388db43ba1f4ddcd602fedcb763c`、lwIP `2758df4cd3666b3b2a5b53830148379326425c0d` 的原样 C3 样例从公开依赖完整编译，app **234,176 字节**、SHA-256 `79446fccf137481eb2b92956a2134d1bcea00cb7c1295cd9ac3663c4f62b12a8`；该原样样例只执行底层预算探针，不单独证明私有 ABI 2 运行。

另在仓外独立工程中嵌入上述两个 guest，继续通过 Component Manager 读取相同公开 WAMR SHA，在官方 QEMU `esp_develop_9.2.2_20260417` 的 C3 上实际运行。16 KiB pthread owner 连续三轮完成满页验证、最大事件、增长拒绝和关闭，再分别验证三种页外 trap，最后完成 100 次 counter `open/init/event/stop/close`。采样如下：

| 时点 | 8-bit free / largest（字节） |
| --- | --- |
| pthread 创建前 | 334,564 / 196,608 |
| owner 进入、首次 open 前 | 317,904 / 180,224 |
| 三轮边界 guest 存活时 | 每轮 239,752 / 114,688 |
| 三轮关闭后 | 每轮 317,800 / 180,224 |
| counter 第 10/50/100 次关闭后 | 三次均 317,800 / 180,224 |
| join 回收 owner 后 | 334,356 / 180,224 |

冷启动与线程回收前后仍有固定差值，本记录不将其解释为零残留；预热后的重复关闭没有持续下降。公开依赖的实验 app **241,696 字节**，SHA-256 `aaa6d7a7a0f4055d5276a84d8003689f24e2f2c3dfc061de873fad97017dab45`；QEMU 日志 SHA-256 `c1bddfeb34bd0241c17433459acd7fdc5c8375d84c3f2a8bd4012fc6058ed516`，59 个源码/配置文件和制品输入收据 SHA-256 `b1993fefebfc6926dcf28faeae5f0291c8e202ea005e9f625a9d9a79ec309776`。实验工程位于 `/private/tmp/esp-container-page-abi-c3.20260924`，收据位于 `/private/tmp/esp-container-page-abi-receipt.20260924.json`；这些临时路径只用于本机复核。

该实验不含 Base、联网、OTA 或包槽，也没有物理设备写入；16 KiB owner 栈不能替代 Base 自有任务栈预算。取消附加 heap 修复了标准边界并消除对应分配，但仍保留完整 64 KiB 连续线性内存，不能据此宣布五仓组合容量达标。所有入口的计量继续有效；同步宿主调用不可抢占与完整停止期限仍未闭合，P6-03/P6-07/P6-09 总验收不变。
