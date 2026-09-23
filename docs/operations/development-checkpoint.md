# 开发检查点：2026-09-23

## 已核对的软件范围

| 检查 | 结果 | 边界 |
| --- | --- | --- |
| `python3 -m unittest discover -s tests -v` | 9 项通过 | 主机 RSA-3072/PSS、规范 JSON、受限 ustar、Wasm 初筛；测试键仅在临时目录产生 |
| CMake/CTest `wasm_scan` | 1 项通过 | 初始无 import/no start 扫描，不是完整 guest ABI 或设备验包 |
| `darren-you/esp-container` 公开 `master` 独立 checkout | 从首个第一方提交 `756244f18d66115eb17524707bf558063ef8216f` 重新拉取，Python 9 项与 CTest 1 项均通过 | 只证明公开仓主机工具可复现；不代表设备运行链路 |
| 固定 IDF `fff9895c82d744c7237be8847347bdd1b07c6643` + lwIP `2758df4cd3666b3b2a5b53830148379326425c0d` 构建 `examples/c3-runtime` | 原样 WAMR `8c18e3f68b16c4bcaf05996b2636f6ed2b4cf629` 失败 | 编译上游 `espidf_platform.c`、`espidf_file.c` 缺 POSIX stat 类型，以及 `espidf_memmap.c` 引用当前 C3 内存保护下不存在的 `MALLOC_CAP_EXEC` |
| 仓外临时上游源码修正原型 | C3 全部编译链接通过；bin 227168 字节，SHA-256 `f6dde4da9aabb56a0e10ea80fdc3d9a9de9abbf83cb9881629917c3e5680cf32` | 仅证明最小无网络样例可构建；该临时源码不是本仓锁定的正式上游提交，未刷板，尚无运行时堆/预算结果 |

上游修正原型只调整三处：无 WASI 时不编译文件适配、明确包含 `<sys/stat.h>`、在 IDF 无可执行堆能力时拒绝执行映射并保留普通映射。实验保留 `CONFIG_ESP_SYSTEM_MEMPROT=y`，未定义虚假的 `MALLOC_CAP_EXEC`，也未修改固定 SDK 或 `managed_components`。[上游 PR #5116](https://github.com/wasm-micro-runtime/wasm-micro-runtime/pull/5116)已提交该修正；本仓正式依赖仍指向未修正的 WAMR SHA，上游源码修复并取得可消费的精确提交前，P6-02 的 C3 构建条件未满足。

当前尚无 Flash 三包槽、设备流式验包、产品实例管理、真实签名包运行、C3 堆峰值和组合容量证据。第 9、12、13 节的对应任务均未验收。
