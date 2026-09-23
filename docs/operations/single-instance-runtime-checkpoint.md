# 单实例运行切片检查点：2026-09-23

## 组件内合同

`components/esp_container/src/runtime_internal.h` 是组件私有边界。它接收内存中的 Wasm 与显式资源限制，仅用于当前可复现的软件检查点；公开头 `include/esp_container.h` 没有裸 Wasm 安装入口，Base 不能直接装配这套函数。

`open` 要求输出指针预先为 `NULL`，拒绝第二个实例，并复制一份供 WAMR 持有的可写模块字节。加载前先做现有无 import、无 start、无隐式构造入口扫描，再从原始 Wasm memory section 核对单块非共享、有限最大页数；WAMR 加载后核对四个且仅四个导出：`memory`、`econtainer_init() -> i32`、`econtainer_on_event(i32, i32) -> i32`、`econtainer_stop() -> i32`。WAMR 会规范化加载后的内存页数，所以不能拿其导出类型中的页数替代原始 section 上限检查。

成功路径是 `open → init → on_event* → stop → close`。`init`、`on_event`、`stop` 每次调用都设置各自的正数指令预算；额度耗尽与其他引擎异常分别返回。`init` 和 `stop` 要求 guest 返回 0；`on_event` 的 guest 返回值只在宿主状态为成功时写给调用者，允许负数。事件先通过 `wasm_runtime_module_malloc` 分配 guest 地址、复制 payload，再在调用后释放；分配失败返回独立的内存不足状态，不改变运行态。`stop` 成功后可重复调用；一旦 guest trap 或 init/stop 返回失败，实例进入不可继续执行的失败态，`close` 仍释放 exec env、实例、模块、可写字节和 WAMR 全局所有权。`close` 也可用作未停止实例的立即释放；当前 guest 无宿主能力和异步回调。

ESP-IDF 调用方必须在**同一个由 `pthread_create` 创建的宿主线程**串行执行这些方法。当前锁定 WAMR 的 `espidf` 平台实现将 `os_self_thread` 映射到 `pthread_self`；在普通 FreeRTOS `xTaskCreate` 任务中调用会触发断言。全局原子占用只拒绝并行打开第二实例，不提供同一实例方法的并发保护。

`max_wasm_bytes`、guest 声明的 `max_memory_pages`、stack、host-managed heap、最大单事件字节数和三个指令预算目前都是调用方输入，尚无产品冻结值。页数上限不是 C3 总 RAM 峰值；模块副本、WAMR 内部元数据和 host-managed heap 还会占用内存。

## 已完成验证

使用固定 wasi-sdk 33 生成真实 counter 与五个临时 guest，使用精确锁定的 WAMR Classic 源码运行 `ctest`，三项全部通过。`runtime_instance` 逐项覆盖 counter 事件计数、guest payload 读取、空事件负返回值、过大事件拒绝、guest 堆分配不足后恢复、单实例占用、错误 ABI、超过原始内存页上限、三个入口分别死循环触发精确指令额度异常、失败释放、重复 stop/close 与八轮关闭后重开。输出 Wasm 位于 CMake 构建目录，不进入 Git。

固定 IDF `fff9895c82d744c7237be8847347bdd1b07c6643`、lwIP `2758df4cd3666b3b2a5b53830148379326425c0d`、WAMR `a34d721b630213f59fde0b40cebbb980903660e8` 的 C3 样例完整构建通过，新 `runtime.c` 被编译进组件 archive。原样样例只调用先前底层探针，其最终 ELF 不保留这个新 API。另在仓外复制的临时工程强制引用私有 `open`，完整链接通过，最终 ELF 的符号表保留 `open/init/on_event/stop/close` 和 WAMR `module_malloc`；这证明新 API 的 C3 链接链路，尚不能声称 C3 已实际执行单实例链路。临时工程和生成镜像不进入仓库。

## 未闭合边界

当前没有宿主能力授权、guest 句柄、异步回调、签名 `product.pkg` 设备验包、Flash 三槽、Base 装配与实例持久状态；指令计量也不能单独限制未来宿主函数内部的耗时。没有此 API 的 C3 真运行、动态 RAM 峰值、实板运行和掉电恢复证据。P6-04 与 P6-07 仍未验收。
