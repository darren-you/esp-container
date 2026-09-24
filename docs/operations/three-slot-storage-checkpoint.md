# 三包槽存储软件检查点

本检查点实现 `esp_container_slots.h` 的受控原始 Flash 包槽、单 blob 记录与双固件集合对账。它服务于跨仓计划 P6-08/P6-10 的软件切片，**尚不是可发布的包安装或双固件联合升级**。没有写入设备或修改真实分区表。

## 事实与边界

- 调用方从 Base 的实际分区发现中提供独立 data 分区边界、三个互不重叠的槽、擦写粒度；本仓没有冻结物理 offset、槽长或 4 MiB 布局。几何不完整、未对齐、越界或重叠会在调用任何回调前拒绝。
- `read_blob` 必须精确区分 NVS key 不存在、完整读取和读取失败，且只能从已提交的持久视图读取，不得返回同一 NVS handle 的未提交缓存。`write_blob` 只有对**同一个 key**完成 `set_blob`、`commit` 才能返回 true；组件随后精确读回 288 字节。commit 返回 false 即使读到新字节仍视为结果不明，不据此擦槽。CRC32 检测意外损坏，不承担数据真实性。记录非法、读回未知或序号不符时不擦槽。
- `lock` 必须覆盖所有包槽 Flash/NVS 写入者和运行切换者，且在整个操作期间保持独占。组件不包含第二套操作账本；Base 对外仍负责跨多轮 operation_id 的去重、权限与串行化。单 blob 仅保留最近一次操作 ID 与相位。
- 迁移入口 `initialize` 仅接受缺失记录，并对现有包引用全量回读 SHA-256；两份绑定还必须与调用方报告的实际可启动固件摘要集合精确相等。调用方必须事先核对实际 app/otadata、包签名、授权、ABI 与产品归属。读取失败、CRC 错误和真实空记录严格不同，不能以初始化覆盖损坏记录。
- 任一擦除前重新读取记录、核对预期序号、全量计算两份已确认包摘要，并先把唯一候选槽写为 `WRITING`，完成 NVS commit/精确读回。一个包槽被任一已确认绑定或未决候选引用时不能被选为擦写目标。
- 当前固件已有业务包时，product-only 更新要求候选 `data_schema_version` 精确相同；尚无包时可首次安装。产品 ID、签名密钥、ABI 与所需能力仍由验包/授权回调精确核对。
- 写包时只擦已持久保留的槽，按提供的写粒度写入，不足末块补 `0xff`。写完从 Flash 重新计算**精确包长** SHA-256，再让调用方的只读回调完成签名、Wasm、产品授权与宿主 grant 检查；现有 `econtainer_package_slot_validate()` 可承担这段回读准入。回调区分校验期间的 Flash 读失败 `IO_FAILED` 和完整读取后的信任拒绝 `UNTRUSTED`。任何失败留在 `WRITING`，不能自动试运行；校验通过且 blob 读回一致后才进入 `PREPARED`。
- `begin_trial` 在旧实例已停止回收之后、启动新实例之前持久记录 boot ID；`mark_healthy` 仅接受同一 boot ID；`confirm` 再读回受保护引用，并在单 blob 内替换该固件绑定。当前 API 仅处理同一实际固件下的 product-only 更新，不读写 `otadata`，不推断 OTA 固件 VALID。
- 启动对账 `reconcile` 先重新计算两份确认包摘要，仅对运行固件摘要命中的绑定给出选择。`WRITING`、`PREPARED`、`TRIAL_STARTED`、`HEALTH_VERIFIED` 在新 boot 均只返回旧确认绑定，不会重启候选或自动提交。完整候选摘要或读取失败时，保留旧确认包选择和状态，同时返回原始 `UNTRUSTED`/`IO_FAILED` 与 `BOOT_RECOVER_CONFIRMED_CANDIDATE_INVALID`；调用方只能启动另行验证过的旧确认包，必须记录候选错误，不能自动推进或擦槽。确认包错误仍是 `BOOT_BLOCKED`。
- `abandon` 为显式取消。不同 boot 可直接证明 RAM 中不存在前次试运行实例；同一 boot 必须由唯一执行器 owner 在持有存储锁期间调用 `trial_stopped_fn`，核对该 operation 的候选已停止、回调/原生引用已收敛。取消只依赖确认包完整性，被抛弃的候选字节即使损坏也不妨碍持久取消。取消后的新操作仍须取得不同 operation ID，并重新执行授权/验包。调用方在启动旧包前仍须验证签名、ABI、授权、数据 schema 和运行能力。

## 双固件集合对账软件切片

`initialize`、`reconcile` 和 `reserve` 接受同一 `econtainer_slot_firmware_set_t`：一或两份实际可启动固件 SHA-256，以及其中正在运行的一份。集合不区分数组顺序；零摘要、重复、越界计数、运行固件不在集合均拒绝。已提交 blob 中的固件绑定必须与集合完全一致；旧固件已被 OTA 覆盖却仍留在绑定中，或新固件出现却没有绑定，启动选择返回 `BOOT_BLOCKED`，擦槽预留返回 `CONFLICT`。这不会自动删除旧包或把新固件判作已确认。摘要校验继续覆盖两份绑定，因此从当前固件启动也不能忽略回退固件的包损坏。

这个参数必须由 Base 从实际 app 分区与 `otadata` 读取和校验；Base 还须把固件切换与包操作放在同一串行操作所有权下，确保集合快照在调用期间不变。Container 目前只核对调用方给出的值，无法自行证明物理可启动性、app 签名、OTA 状态或旧槽已经失去回退资格。联合更新需要另行实现受 OTA 状态约束的 prepared/trial/health/VALID/confirmed 转换；现有 `reserve`、`begin_trial` 和 `confirm` 仍只处理 product-only。物理分区几何没有因此确定，当前 Base 分区仍不能绑定包区。

## 三槽引用序列

| 时点 | 旧固件确认包 | 当前固件确认包 | 未决/新包 | 下一个可擦槽 |
| --- | --- | --- | --- | --- |
| 初始 | P0：槽 0 | P1：槽 1 | 无 | 槽 2 |
| P2 写入/试运行 | P0：槽 0 | P1：槽 1 | P2：槽 2 | 无 |
| P2 确认 | P0：槽 0 | P2：槽 2 | 无 | 槽 1 |
| P3 写入/试运行 | P0：槽 0 | P2：槽 2 | P3：槽 1 | 无 |
| P3 确认 | P0：槽 0 | P3：槽 1 | 无 | 槽 2 |

相同包摘要的共享槽可以留在两份固件绑定中；该槽的摘要、长度、ABI 与 schema 必须完全一致。本切片没有做共享包的下载去重。记录只保留两个固件摘要，且不会自行加入、淘汰或验证实际 OTA 槽的固件身份；联合升级时必须由 Base/esp-ota 扩展该状态转换并按计划第 9.7 节验证，不能直接复用 product-only `confirm`。

## 已验证与待验

主机 `slots` CTest 使用假 Flash/NVS 覆盖 P0→P3 引用变化、两份固件各自重启对账、错误/缺失/重复固件集合的启动与擦槽拒绝、超过实际单槽容量的候选拒绝、先提交保留再擦除、两种 commit 返回错误的保守裁决、blob 读回失败/CRC 损坏、部分写入、候选回读摘要损坏/断读、旧确认包可恢复但不可推进候选、同 boot 无停止证明拒绝取消/停止证明后取消、错序号/错固件与几何拒绝。原包解析和 Wasm 扫描 CTest 保持独立运行。

新增 `package_slot` 主机测试把真实签名包、三槽假 Flash 回读和独立授权组合，并拒绝错误产品、schema、key ID、内存/队列/指令/期限及读故障。仍缺已冻结的真实包分区与 Base provider 装配、同 Flash 写入者独占、实际两份固件身份/状态对账、联合 OTA 阶段、产品操作账本、真实断电与 C3 实板的容量/时延/磨损验证。P6-06、P6-08、P6-10、P7-01 因此保持未验收。

## ESP-IDF provider 接线（2026-09-24）

`esp_container_slots_idf.h` 现提供对现有三槽回调的 ESP-IDF 实现。绑定必须由调用方给出已经冻结的**独立**包 data 分区标签、精确地址/大小、三个槽的绝对边界、NVS 分区标签和精确地址/大小、namespace/key，以及与同一设备全部包槽写入者共享的锁。provider 从当前 IDF 分区表发现并读回这些事实；包区只接受可写的 `data/undefined` 分区，NVS 只接受可写的 `data/nvs` 分区，两者不得重叠。错类型、缺失、地址/大小不符、槽未对齐/越界或重叠均在创建回调前拒绝。擦除粒度取实际分区的 `erase_size`；普通写入按 4 字节、加密分区按 IDF 要求的 16 字节对齐，所有 Flash 回调再次检查绝对边界再转为分区相对地址。

锁由调用方创建并覆盖所有包槽操作及运行切换；provider 只做非阻塞获取，不另建私有锁。NVS 必须由产品先初始化，provider 不自动 erase、重建或迁移 NVS；`write_blob` 对一个 key 执行 `set_blob → commit`，`read_blob` 关闭写 handle 后另开只读 handle，精确读取 288 字节并区分 key 缺失、长度错误与 I/O 错误。commit 返回失败即使底层实际写入也仍由三槽引擎按不确定结果裁决。provider 不保存另一份状态，也不负责签名、授权、业务账本或固件身份。

当前 Base 的 4 MiB 分区表没有独立包 data 分区，只有双 app、NVS/otadata/phy/coredump 与末尾 `base_store` NVS；把 `base_store` 当包区会被类型检查拒绝。Container C3 样例只编译本 provider，没有调用它。host 的合成分区/NVS 假件证明缺失/错类型/只读/越界拒绝、绝对到相对 Flash 地址转换、NVS commit/独立读回和三槽初始化/加载；它不是实际 4 MiB 布局证据。P6-03 尚须冻结真实容量与目标分区，P7-01 才能经授权迁移并由 Base 装配共享锁、真实签名/授权和运行绑定。在此之前没有启用安装或运行，P6-08 继续未验收。

## 安装槽到私有运行时的同步连接（2026-09-24）

本轮在 `codex/c3-low-memory` 的 `bbc186ed97fc6c6e8ea4f2b71189560679100caf` 候选上，按已授权的软件范围增加 `src/slot_runtime_internal.h` 的 `econtainer_slot_runtime_open`。它仍为组件私有入口，真实消费者是新增签名 counter 集成测试；没有对外开放裸 Wasm 安装接口，没有接入 Base 物理分区，也没有新增持久相位、活动指针、租约或后台任务。

调用方在同一个 pthread 执行器上先停止回收旧实例，使用 Base 已有的外层 firmware/storage owner 保持固件集合稳定；三槽内层继续使用原有 `io.lock`。请求带实际可启动固件集合、预期 sequence 和选择类型。`CONFIRMED` 只从当前运行固件的实际绑定取包；`TRIAL` 必须精确匹配已提交的 `TRIAL_STARTED`、当前 running firmware、operation ID 和本 boot ID。PREPARED、HEALTH_VERIFIED 或另一 boot 的记录均不能当作新 trial 启动。确认启动仍核对两份可启动固件的所有确认引用，但不要求被抛弃候选完整，保留旧包恢复合同。

包槽引擎在同一锁内读回单 blob、检查 sequence 与固件集合、选择记录中的 slot/整包摘要/长度/ABI/schema，再调用同步装载函数。装载先映射精确整包，使用该只读映射的 ≤512 字节回调完成签名、完整包和 Wasm 摘要、产品、ABI/schema、静态 profile 及独立授权检查，再把同一映射中的精确 Wasm 区间交给私有 `runtime_open`。安装时遗留的 `verified_info` 不作为输入，且无需提供它；确认包复用验证核心，不伪造候选 operation。即使 provider 错误地映射了另一份完整、签名有效的包，持久摘要绑定也会拒绝它。

IDF provider 的 `flash_map/flash_unmap` 是可选的存储回调、必需的私有装载能力。它先检查真实专用分区边界，再使用 `esp_partition_mmap(DATA | BLOCKS_WRITE)`；SDK 负责非页对齐地址和返回指针调整。成功映射无论后续授权或 WAMR 是否成功，均在解锁前解除；映射失败不留下句柄，成功但 NULL 指针也会清理。映射活跃时不执行 NVS commit 或包擦写，避免 `BLOCKS_WRITE` 阻塞自己。`runtime_open` 不执行 guest，并在返回前拥有 WAMR 所需 code/data 副本，因此映射不延长到 `init/event/stop/close` 生命周期。

签名请求与独立 runtime policy 在连接处共同限制 Wasm 大小、内存页、执行栈、三个入口指令预算；能力取 signed requested、静态独立 grant 与运行 policy 的交集，签名不能自行授权。非法 policy 不产生实例。host heap、单事件字节、日志长度、定时器个数和整个入口的 `max_entry_duration_ms` 仍是独立平台参数。事件队列、持久存储和单次 `host_call_timeout_ms` 没有在当前 runtime 中实现；其签名上限继续经过准入，不能称为运行期已执行，更不能把单次宿主期限偷换成整个 guest 入口期限。

返回结构分别保留 `slots` 和 `runtime`：装载前的存储/身份/授权失败使 runtime 保持 `INVALID_STATE`；存储与授权成功但 WAMR 忙或装载失败时，slots 可为 OK、runtime 为具体错误。只有两者均为 OK 且输出实例非 NULL 才能继续 `init`。失败不会给调用方留下部分实例；实际 runtime 内部失败统一回收其资源，连接层负责映射和槽锁，不建立第二套 close 所有权。

普通真实 WAMR CTest **9/9** 通过。新增 `slot_runtime` 使用临时 RSA-3072 测试键签署真实 wasi-sdk 33 counter，执行两个固件绑定下的 P0→P3 更新、确认包恢复、即时 `munmap` 后的 guest 调用、签名配额和独立授权、映射错误与 WAMR 失败清理。两个条件变量同步的 pthread 在 map 后及 open 后分别竞争预留和擦写，均返回 BUSY，写入计数不变；测试没有假定未确认的并发工作模式。具体负例见[测试入口](../../tests/README.md)。测试使用合成 Flash/NVS 和真实 WAMR，不等于真实 Flash 包槽或设备掉电验证。

固定 IDF `578cf89c343e388db43ba1f4ddcd602fedcb763c`、lwIP `2758df4cd3666b3b2a5b53830148379326425c0d`、WAMR `a34d721b630213f59fde0b40cebbb980903660e8` 的独立 C3 构建通过；链接参数额外强制引用 `econtainer_slot_runtime_open`，最终 ELF 中该函数及 `econtainer_slots_with_selected_package`、`econtainer_package_slot_check`、`econtainer_runtime_open` 均为已定义代码符号，避免样例未调用而被回收。app 为 **328,928 字节**（`0x504e0`），SHA-256 `673d896a6e693099c87962c09a89f3b610633a7de527b8a142e52173e55ed0b1`；构建日志在仓外 `/private/tmp/esp-container-slot-runtime.20260924/c3-build.log`。样例没有实际调用新入口，该项只证明完整编译链接，没有写板。

严格 UBSan 复核修正了旧 `slots_test` 假 Flash 的指针表达式：必须先计算 `offset - FLASH_BASE` 再形成数组指针，相关测试假件同步修正。首次 ASan/UBSan 未开启遇错停止时 CTest 虽返回 9/9，但完整日志含恢复性 UB 告警，因此不能作为 UBSan 无告警证据。开启 `UBSAN_OPTIONS=halt_on_error=1` 后，新槽测试和原有裸 `runtime_instance` 均在锁定 WAMR `wasm_interp_classic.c:6835` 的 `WASMBranchBlock` 8 字节对齐处失败；原路径不调用新增槽连接，证明该问题既有。新旧严格失败日志分别保存在 `/private/tmp/esp-container-slot-runtime.20260924/asan-ubsan-strict-tests.log` 与 `/private/tmp/esp-container-slot-audit-baseline-test.20260924.log`，首次恢复性日志为同目录 `asan-recovering-first.log`。本轮不修改 WAMR 依赖或屏蔽对齐检查，完整 UBSan 验证继续未通过。

最终容量与三槽物理几何、生产信任锚/授权、Base 的 pthread 执行器和安装命令、健康窗口、联合 OTA 转换及实板恢复仍未接线。P6-06/P6-08/P6-10 的软件连接取得进展，整项验收状态不变。

独立只读复核未发现新同步连接的阻断缺陷；单独纯 ASan 的 `slots/package_slot/slots_idf/slot_runtime` **4/4** 通过，日志 `/private/tmp/esp-container-slot-audit-address-test.20260924.log`。修正假件指针后，前三项严格 ASan/UBSan **3/3** 通过，日志 `/private/tmp/esp-container-slot-audit-fixture-test.20260924.log`；本轮严格全套为 **6/9**，三个使用 WAMR 的目标均因上述既有对齐 UB 失败。报告分别保留这些结果，不把关闭 UBSan 的 ASan 通过称为联合 sanitizer 通过。

## C3 QEMU 真实 Flash/NVS 装载切片

同轮按明确授权在仓外 `/private/tmp/esp-container-slot-runtime.20260924/qemu-lab` 建立独立 C3 探针，使用上述固定 SDK/WAMR 和官方 QEMU `esp_develop_9.2.2_20260417`。虚拟 4 MiB Flash 中专用 `package_store` 为 `0x110000/0x18000`，划三个 `0x8000` 槽；只有一个 factory app，传入的两个 firmware SHA 是明确标记的 fixture，**并非 Base 从 app/otadata 发现的双固件集合**。这些地址和大小仅为仿真实验，不改变正式分区表，也不冻结业务包容量。虚拟 NVS 初次为空，不使用自动 erase 或失败后重建。

探针从临时 RSA-3072 测试键签署的 **10,240 字节** counter 包写入实际 provider，运行 `reserve → Flash erase/write → readback verify → PREPARED(sequence=3) → durable TRIAL_STARTED → slot_runtime_open → init/event/stop/close → mark_healthy → confirm(sequence=6)`；随后两次从确认绑定重新验签和运行。三个生命周期的事件均返回 `3`。provider 仅由薄计数包装调用真实 `esp_partition_mmap/munmap`；五次成功映射全部解除，每次进入 `init` 前 `active_map=0`。错误产品在真实映射后被拒绝、注入 map 失败不创建映射、已有实例时 WAMR BUSY 不影响旧实例；三条失败之后均可继续运行和关闭。新源码不含这些计数或注入代码。

| 阶段 | 8-bit free / largest（字节） | 8 KiB pthread 最小剩余栈（字节） |
| --- | ---: | ---: |
| pthread 开始，尚未首次建记录/验包 | 310,500 / 172,032 | 6,804 |
| 包准入及错误产品/map 故障检查后 | 309,040 / 172,032 | 3,240 |
| 第一次装载完成 | 227,076 / 114,688 | 3,216 |
| 第一次关闭 | 308,936 / 172,032 | 3,216 |
| 第二、三次确认包装载 | 227,084 / 114,688 | 3,104 |
| 第二、三次关闭 | 308,936 / 172,032 | 3,104 |

包含 guest 存活时第二次验签以触发 BUSY 的全过程最小 free 为 **221,120 字节**；8 KiB pthread 的测得最大已用栈为 **5,088 字节**。三次 close 后 free/largest 相同，首次装载前后仍有 104 字节冷启动差，不能宣称完全零差值；join 后 free 为 **317,388 字节**。该探针没有 Wi-Fi/TLS/FRP/MQTT，也没有 Base 控制链、实板栈保证或跨断电恢复验证，不能外推五组件容量。

未签名的实验 app 为 **376,768 字节**，SHA-256 `ba97686242a7aa74b60baaf35c49c7f3bb165a10ed81c36cbf0cf88310fc2f51`；它嵌入的是已签名测试业务包，二者签名身份不可混称。测试包 SHA-256 为 `2d68842b936826d288e6e922a5fc68af44ed18ff92d1c1ab88d9e9b115d5aff2`，最终串口日志 `qemu-slot-runtime.log` 为 4,369 字节、SHA-256 `51b7025fc06cc759ddaeb3235ed3fe0fd50412aeb36a46b45cf3cfd67511cc6b`。第一次运行也通过，但采集器在 join 行未收全时提前退出，原始输出保留为 `qemu-slot-runtime-first.log`；修正采集器为等待完整行后，从原始空虚拟 Flash 镜像重跑，以上数据来自完整第二次日志。

仓外 `source-freeze.json` 及 `source-snapshot/` 绑定完整组件与测试源码，冻结清单 SHA-256 为 `e30db241d77021f5c8a7729673fa1dee558f398db9ea68e720f6ef5bc43ff75c`；`qemu-receipt.json` 另绑定探针、分区、sdkconfig、依赖锁、包、公钥、app、ELF、初始虚拟 Flash 和日志，SHA-256 为 `241e77e57cc306c1842b5065be583b2432d965e594315888bb55301fcf6bd0b9`。包私钥仅在仓外临时测试目录，生产凭据未使用；没有设备写入、提交或推送。

### 保留虚拟 NVS 的跨 boot 验证

保留上述完整制品，另建 `qemu-reboot-lab` 和 `qemu-trial-seed-lab`。重启 reader 只挂载既有 NVS、调用 `slots_load` 与私有装载接口，不调用 `slots_initialize`、擦除、写包或持久转换；boot fixture 从 `0x55` 改为 `0x66`。宿主只替换虚拟 factory app 区间，NVS、包槽、分区表与 bootloader 原样保留，再启动一个新的 QEMU 进程。因此这是实际虚拟 Flash 跨 boot 读取，不是同一 RAM 结构的再次调用。

第一条路径从前次 `confirmed sequence=6` 的虚拟 Flash 重启，读回同一状态，重新验签装载后 `init/event(result=3)/stop/close` 全部成功，map/unmap 各一次，active map 为零；最后再次读取状态仍为 sequence 6。第二条路径先在明确的 `TRIAL_STARTED sequence=4` commit/readback 后暂停实验线程，再保留虚拟 Flash 重启 reader；新 boot 的 trial 请求返回 CONFLICT，确认选择返回 EMPTY，映射次数为零，持久状态仍为 sequence 4。本种子此前无确认包，因此证明候选不会被自动提升，不能写成已有旧确认包的实板恢复证明。

两条重启路径前后的 **整个 NVS 区和三个包槽区逐字节一致**。确认路径 NVS SHA-256 为 `bf5a9ff0acf12e20652ba6c44e25bef291f61c99889e6663c357f6ff6310e8e2`；各区域和全部输入/输出 Flash 的摘要保存在 `qemu-reboot-receipt.json`，该清单 SHA-256 为 `d96a22c57d2b47f2f137ecbf80a5098a8dec8356dc642f3bc22656010bed54a2`。reader app 为 344,736 字节、SHA-256 `2e23ce10c77fda43f922e9c6215e0bb2fa2b44221a502bc86333efd134368629`；`qemu-confirmed-reboot.log` SHA-256 为 `c2179d7e1642743d7b2dcf28a7520e79e0b78da6afa6cb08aa30f51f1d76c883`，`qemu-trial-reboot.log` 为 `b33ca726316da58358d5236038cc988f85e33ed54b254633bc7aee1f995a9d18`。两个 variant 最初并行配置时曾触发 IDF Component Manager 缓存 `index.lock` 冲突；未删除他方锁，待另一配置完成后串行重试通过，首次环境错误日志保留为 `qemu-trial-seed-build-first.log`。

原始源码冻结点、固定 SDK/WAMR 和测试签名键均未变化。这些新 boot 的固件摘要仍是实验 fixture；没有验证 Base 的真实签名固件身份集合、真实 OTA 回滚、物理板掉电或完整五组件并发。
