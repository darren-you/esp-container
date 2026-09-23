# 宿主导入检查点

## 当前私有 ABI

固定 wasi-sdk 33 编译的 guest 可从 `econtainer` 模块导入 `monotonic_ms() -> i64`、`log(i32 offset, i32 size_bytes) -> i32`、`timer_start(i32 delay_ms, i32 period_ms) -> i64` 和 `timer_cancel(i64 handle) -> i32`。guest SDK 声明对应的 `econtainer_` 函数。C 扫描器与主机包工具均拒绝其他模块、函数、种类、函数签名和重复导入。`counter.c` 仍为无导入样例；`tests/host_api_guest.c` 与 `tests/timer_guest.c` 分别运行时钟/日志和定时器导入。

`components/esp_container/src/runtime_internal.h` 仍是组件私有接口。调用方必须显式给 `allowed_capabilities` 授予对应的时钟、日志和/或定时器能力；未授权的必需导入在 WAMR 加载前拒绝。包工具还要求实际导入对应的 `monotonic-time`、`log`、`timer` 位于已签名 manifest 的 `required_capabilities`，但这只核对包声明，不授予设备权限。当前没有公开安装 API，Base 不能直接消费裸 Wasm 私有入口。

日志导入在当前实例中只复制一条待取记录，长度上限由 `max_log_bytes` 设置且不超过 256 字节；零长度和超限返回 `-1`，上一条尚未取走返回 `-2`，有效日志返回 `0`。WAMR 验证偏移和长度属于**当前实例**的 guest 地址空间；越界地址触发引擎异常并使该实例失败。宿主在 guest 调用返回后通过 `econtainer_runtime_take_log` 复制并取走日志；目标缓冲太小时记录不丢失。`close` 释放缓冲，新实例没有旧日志或可复用句柄。原生导入使用 WAMR 执行环境与实例身份核对，不暴露宿主地址。

四个导入只执行单调时钟读取、最多 256 字节的内存复制或固定八槽内的有限操作，不发起网络、Flash、等待或用户回调；guest 调用链中没有同步反向调用 guest 的入口。`init/on_event/stop/take_log/poll_timer/close` 使用单实例调用占用位拒绝同一时段的第二次进入。ESP-IDF 上仍要求同一 `pthread_create` 宿主线程串行调用；占用位不是跨线程实例生命周期管理。日志输出、敏感值过滤和速率限制尚未连接，待取字节不能直接当作安全的产品日志发布。完整宿主墙钟期限机制未验收。

## 2026-09-24 定时器软件切片

平台在运行限制中独立授予 `ECONTAINER_CAP_TIMER`，并设置 `max_timers` 为 1–8；未授权模块在 WAMR 加载前被拒绝。guest 可创建 1–86400000 毫秒后的一次或周期事件，周期为 0 时只触发一次。返回的 64 位句柄包含实例代次、不可回绕复用的分配序号和槽编号；0 表示参数错误或配额已满。取消只接受本实例仍有效的句柄，旧实例和已到期的一次性句柄返回 `-1`。停止先撤销全部定时器，再调用受预算约束的 guest stop，stop 中不能创建新定时器；关闭实例不留下原生计时器回调或后台任务。

定时器不运行自己的线程。唯一 owner 在 guest 入口之外通过 `econtainer_runtime_next_timer_deadline` 获取下一次唤醒点，并在到期时调用 `econtainer_runtime_poll_timer`；每次最多投递一条到普通 `econtainer_on_event`，使用同一条事件指令预算。宿主导入只登记或取消，不同步重入 guest。事件为 16 字节固定编码：`ECT`、版本 1、LE64 句柄、LE32 已合并跳过的周期数；普通事件入口拒绝这一保留格式，避免伪造。周期超时只投递一条，下一期限沿原节奏推进；guest 内存分配失败时保留原到期状态，供 owner 再试。此机制不承诺硬实时精度，若 owner 不轮询就不会自动投递。

锁定 WAMR 真 guest 回归覆盖一次/周期事件、周期超期合并、配额满、非法期限、取消、伪造事件拒绝、stop 撤销和跨实例旧句柄拒绝。平台尚未提供公开安装入口或 Base 装配，因此此切片不能被解释成设备上的产品定时任务已可发布。

隔离 checkout 使用 wasi-sdk 33 的 Python unittest 18/18、锁定 WAMR 的普通与 ASan/UBSan CTest 各 8/8 通过。定时器 guest 连续 100 次创建、挂起计时、stop、close；macOS 校准后的第 10/50/100 次 malloc 在三个采样点均为 24144 字节，虚拟地址均为 500517535744 字节，区域数均为 62。固定 ESP-IDF `855937c` 与 lwIP `2758df4` 的 C3 样例普通构建通过，app 为 `0x39260` 字节，SHA-256 为 `e6cdd8063882cadcdd2c63b32677a720c1dc4297efd7dc217f9f23fb3bfd9d6d`；样例仍未在 C3 上执行定时器 guest，也没有刷板。

## 已验证的软件证据

在独立 checkout 中，固定 wasi-sdk 33 的 Python unittest 18/18 通过；锁定 WAMR `a34d721b630213f59fde0b40cebbb980903660e8` 的普通及 ASan/UBSan CTest 均为 3/3 通过。真实 guest 覆盖逐项拒绝未授权导入、日志复制后原事件缓冲改写、短目标缓冲保留、队列满、长度超限、非法地址异常、单调时间以及关闭重开不继承日志；包工具用测试键验证导入需求必须进入签名 manifest。

当前 SDK 锁是公开 ESP-IDF fork `855937cf9dcee13ee9c423fb0319238cdc8d53fd`、esp-lwip `2758df4cd3666b3b2a5b53830148379326425c0d`。独立 checkout 在这组源码与 WAMR fork 下完成 C3 原样样例构建，镜像大小 `0x39170` 字节、SHA-256 为 `d70de72efbdb9438299f3ce4d8d2877ab29f9fbf640a8f30fef148533e3bb204`；原样样例尚未强制链接或执行新私有宿主导入。

此前在官方 ESP-IDF `fff9895c82d744c7237be8847347bdd1b07c6643` 加同一 lwIP/WAMR 的软件检查中，临时于样例 `main.c` 嵌入 690 字节测试 guest，仿真后恢复原文件；测试镜像 SHA-256 为 `2a6536da56db7466ff6eadbf9d77c08294dec8b82150c86fc40ac2f81583ac22`。官方 Espressif QEMU 9.2.2 的 C3 串口记录为 `host-api open=0 init=0 log=0 log_size=4 event=0 result=0 stop=0`，旧指令预算探针仍为 `normal=1 instruction_limit=1`。日志保存在仓外 `/tmp/esp-container-host-abi-qemu-final.log`；该运行结果属于原 SDK 基线，不能替代当前 fork SDK 的私有导入真运行。

## 未闭合边界

当前只有时钟、单条待取日志和实例私有定时器。消息、设备、业务命令与持久存储授权，其他异步操作的完成/取消，产品日志过滤、完整宿主墙钟期限和组合资源配额仍未实现。签名包设备验包、Flash 三槽、Base 装配、五组件同时运行的动态 RAM、真实 C3 板和失败恢复均未由本次测试证明。P6-04/P6-07 保持进行中。
