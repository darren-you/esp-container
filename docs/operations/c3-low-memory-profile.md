# ESP32-C3 单页 guest 开发切片：2026-09-24

## 目标与实际约束

本分支为 ESP32-C3／4 MiB 保留现有签名、四导出 ABI、导入授权、指令预算、入口期限、单实例隔离和三包槽恢复合同，将业务 guest 的 Wasm 线性内存固定为一页（64 KiB）。这是一项候选资源上限，尚未完成五能力并发或真实设备验收。

此前[五组件 QEMU 容量切片](five-component-qemu-capacity-probe.md)在同一旧组合中测得：固定两页 counter 使 WAMR 单次连续分配请求达到 **135,168 字节**（128 KiB 线性内存加 4 KiB 宿主管理 heap），因连续块不足无法实例化；将同一源码改成一页后，该请求为 **69,632 字节**，减少 **65,536 字节**，并完成两次 guest 生命周期。新版精确锁的独立复测也让一页 guest 在 Base READY 后完成 `open → init → event → stop → close`，但存活时只余 **11,328 字节**空闲堆、**7,680 字节**最大连续块，且没有 FRPS、Broker、Wi-Fi 实际连接或 OTA 下载。

锁定 WAMR 构建已经显示 `Shrunk memory enabled`。其 Normal loader 仅在同时找到数据结束、堆基址和栈顶全局导出且无可能的 memory grow 时收缩声明内存；当前 ABI 精确要求三个函数加 `memory` 共四个导出，counter 也只有一个栈指针全局，故不能靠重复打开该开关节省这份 guest 的 128 KiB。额外导出这些全局会改变既有 ABI，本分支没有采用。

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

目前设备 `slots_idf.c` 的 `product.pkg` provider 通过 `esp_partition_read` 提供有界回调；验签和静态准入从包槽回读，但没有 `esp_partition_mmap` 将已验签 Wasm 映射到本入口，也没有完整的映射/解除映射与实例启动接线。只读 `mmap` 测试验证装载器输入合同，不证明真实包槽直接运行。后续接线必须在已验签包和槽状态受保护期间取得映射，并在 `open` 返回后解除映射；还需用真实代码和数据模块、FRP/TLS/MQTT 同时运行及物理板测峰值。此前 **373 KiB** 三槽 Flash 几何上界与本表 RAM 边界是两个不同约束。

## 本分支验证

从公开 `esp-container master@77155349795f3b6564e6f6fbbacb61e884e583ad` 创建独立 `codex/c3-low-memory` 分支。固定 wasi-sdk 33 下，Python unittest **19/19** 通过；公开 WAMR fork `a34d721b630213f59fde0b40cebbb980903660e8` 的 Classic/Normal loader、指令计量主机 CTest **8/8** 通过，`runtime_instance` 在一次期限负例的宿主耗时波动被修正后额外连续通过三次。固定 ESP-IDF fork `578cf89c343e388db43ba1f4ddcd602fedcb763c` 与 esp-lwip `2758df4cd3666b3b2a5b53830148379326425c0d` 的 C3 独立样例完成 SDK 锁检查及 `idf.py build`，生成 app `0x39360` 字节。这个原样样例没有读取新 counter，也没有执行本分支私有运行期；实际一页 guest 调用由锁定 WAMR 主机测试和既有仓外 QEMU 切片分别覆盖。没有在本分支刷写物理设备。

只读节装载候选另以固定 WAMR 和 wasi-sdk 运行主机 CTest **8/8**、AddressSanitizer/UndefinedBehaviorSanitizer CTest **8/8**、Python unittest **19/19**；`runtime_instance` 增加只读映射、非空数据节、输入解除映射后的入口调用及畸形节负例。固定 IDF 的独立 C3 样例重新构建通过；另以 `-DWAMR_BUILD_CUSTOM_NAME_SECTION=1` 独立重配置，确认组件 CMake 守卫拒绝该 profile。该样例不调用私有运行期；上表实际 C3 运行来自仓外五组件 QEMU。QEMU 使用测试签名键与 ADC2 专用空校准桩，未建立 Wi-Fi、FRPS 或 Broker 连接，也未刷写物理板。

## 未验收条件

五组件 QEMU 此次节装载切片中，437 字节 guest 存活时的最大连续块为 **34,816 字节**，小于 FRP 单个 AEAD 接收记录所需的 **65,552 字节**；Base 后续 `fa4d` 切片的相应最大连续块为 **40,960 字节**，仍不足以容纳该记录。此前旧组合的 101,716 字节 Base READY 和 69,632 字节 WAMR 线性内存申请不能直接套算本候选的完整并发边界。保持 FRP 安全语义时，尚未证明 FRP 会话与 guest 同时驻留。物理板的 Wi-Fi/TLS/MQTT 峰值、完整包安装、三槽几何和保留数据迁移也仍待测量。上述 QEMU 观测与主机回归都不授权刷写设备。
