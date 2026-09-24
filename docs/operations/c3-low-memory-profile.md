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

运行期另有独立 RAM 上界：`econtainer_runtime_open` 对完整 Wasm 先执行 `malloc(wasm_size_bytes)` 和复制，然后交给 Classic/Normal `wasm_runtime_load`；锁定 WAMR 的接口要求输入缓冲可写，并持续有效至 `wasm_runtime_unload`。现有私有入口没有直接从 Flash 分段加载或只读映射的路径。当前五组件 Base READY 仅有 101,716 字节空闲堆；Base 的 C3 小内存分支复测也只有 124,924 字节，且这些数值还未建立网络会话或支付完整 guest 初始化内存。因此即使某个大包在 Flash 三槽几何上放得下，也不能据此宣称同大小 Wasm 可被当前运行期加载。可接受 Wasm 字节上限须由真实模块的加载、运行和完整网络峰值共同确定。

## 本分支验证

从公开 `esp-container master@77155349795f3b6564e6f6fbbacb61e884e583ad` 创建独立 `codex/c3-low-memory` 分支。固定 wasi-sdk 33 下，Python unittest **19/19** 通过；公开 WAMR fork `a34d721b630213f59fde0b40cebbb980903660e8` 的 Classic/Normal loader、指令计量主机 CTest **8/8** 通过，`runtime_instance` 在一次期限负例的宿主耗时波动被修正后额外连续通过三次。固定 ESP-IDF fork `578cf89c343e388db43ba1f4ddcd602fedcb763c` 与 esp-lwip `2758df4cd3666b3b2a5b53830148379326425c0d` 的 C3 独立样例完成 SDK 锁检查及 `idf.py build`，生成 app `0x39360` 字节。这个原样样例没有读取新 counter，也没有执行本分支私有运行期；实际一页 guest 调用由锁定 WAMR 主机测试和既有仓外 QEMU 切片分别覆盖。没有在本分支刷写物理设备。

## 未验收条件

新版五组件 QEMU 切片中，FRP 单个 AEAD 接收记录仍需连续 **65,552 字节**，已经大于一页 guest 存活时的剩余最大连续块；仅这块和 WAMR 的 69,632 字节申请之和就比 Base READY 的 101,716 字节空闲堆多 33,468 字节。保持 FRP 安全验收语义时，单页 profile 不能证明 FRP 会话与 guest 同时驻留。物理板的 Wi-Fi/TLS/MQTT 峰值、完整包安装、三槽几何和保留数据迁移也仍待测量。上述 QEMU 观测与本分支主机回归都不授权刷写设备。
