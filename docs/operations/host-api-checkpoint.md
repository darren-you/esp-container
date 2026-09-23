# 宿主导入检查点：2026-09-23

## 当前私有 ABI

固定 wasi-sdk 33 编译的 guest 可从 `econtainer` 模块导入 `monotonic_ms() -> i64` 和 `log(i32 offset, i32 size_bytes) -> i32`。guest SDK 的 C 声明分别为 `econtainer_monotonic_ms()` 与 `econtainer_log(bytes, size_bytes)`。C 扫描器与主机包工具均拒绝其他模块、函数、种类、函数签名和重复导入。`counter.c` 仍为无导入样例；`tests/host_api_guest.c` 才是当前真实调用两项导入的测试 guest。

`components/esp_container/src/runtime_internal.h` 仍是组件私有接口。调用方必须显式给 `allowed_capabilities` 授予 `ECONTAINER_CAP_MONOTONIC_TIME` 和/或 `ECONTAINER_CAP_LOG`；未授权的必需导入在 WAMR 加载前拒绝。包工具还要求导入对应的 `monotonic-time`、`log` 位于已签名 manifest 的 `required_capabilities`，但这只核对包声明，不授予设备权限。当前没有公开安装 API，Base 不能直接消费裸 Wasm 私有入口。

日志导入在当前实例中只复制一条待取记录，长度上限由 `max_log_bytes` 设置且不超过 256 字节；零长度和超限返回 `-1`，上一条尚未取走返回 `-2`，有效日志返回 `0`。WAMR 验证偏移和长度属于**当前实例**的 guest 地址空间；越界地址触发引擎异常并使该实例失败。宿主在 guest 调用返回后通过 `econtainer_runtime_take_log` 复制并取走日志；目标缓冲太小时记录不丢失。`close` 释放缓冲，新实例没有旧日志或可复用句柄。原生导入使用 WAMR 执行环境与实例身份核对，不暴露宿主地址。

两个导入仅执行单调时钟读取和最多 256 字节的内存复制，不发起网络、Flash、等待或用户回调；guest 调用链中没有同步反向调用 guest 的入口。`init/on_event/stop/take_log/close` 使用单实例调用占用位拒绝同一时段的第二次进入。ESP-IDF 上仍要求同一 `pthread_create` 宿主线程串行调用；占用位不是跨线程实例生命周期管理。日志输出、敏感值过滤和速率限制尚未连接，待取字节不能直接当作安全的产品日志发布。当前也没有可取消的异步宿主操作，完整墙钟期限机制未验收。

## 已验证的软件证据

在独立 checkout 中，固定 wasi-sdk 33 的 Python unittest 18/18 通过；锁定 WAMR `a34d721b630213f59fde0b40cebbb980903660e8` 的普通及 ASan/UBSan CTest 均为 3/3 通过。真实 guest 覆盖逐项拒绝未授权导入、日志复制后原事件缓冲改写、短目标缓冲保留、队列满、长度超限、非法地址异常、单调时间以及关闭重开不继承日志；包工具用测试键验证导入需求必须进入签名 manifest。

当前 SDK 锁是公开 ESP-IDF fork `855937cf9dcee13ee9c423fb0319238cdc8d53fd`、esp-lwip `2758df4cd3666b3b2a5b53830148379326425c0d`。独立 checkout 在这组源码与 WAMR fork 下完成 C3 原样样例构建，镜像大小 `0x39170` 字节、SHA-256 为 `d70de72efbdb9438299f3ce4d8d2877ab29f9fbf640a8f30fef148533e3bb204`；原样样例尚未强制链接或执行新私有宿主导入。

此前在官方 ESP-IDF `fff9895c82d744c7237be8847347bdd1b07c6643` 加同一 lwIP/WAMR 的软件检查中，临时于样例 `main.c` 嵌入 690 字节测试 guest，仿真后恢复原文件；测试镜像 SHA-256 为 `2a6536da56db7466ff6eadbf9d77c08294dec8b82150c86fc40ac2f81583ac22`。官方 Espressif QEMU 9.2.2 的 C3 串口记录为 `host-api open=0 init=0 log=0 log_size=4 event=0 result=0 stop=0`，旧指令预算探针仍为 `normal=1 instruction_limit=1`。日志保存在仓外 `/tmp/esp-container-host-abi-qemu-final.log`；该运行结果属于原 SDK 基线，不能替代当前 fork SDK 的私有导入真运行。

## 未闭合边界

此切片只有时钟和单条待取日志。消息、设备、事件命令与持久存储授权，带代次的资源句柄，异步完成/取消，产品日志过滤，完整宿主墙钟期限和资源配额仍未实现。签名包设备验包、Flash 三槽、Base 装配、五组件同时运行的动态 RAM、真实 C3 板和失败恢复均未由本次测试证明。P6-04/P6-07 保持进行中。
