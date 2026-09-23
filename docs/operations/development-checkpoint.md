# 开发检查点：2026-09-23

## 已核对的软件范围

| 检查 | 结果 | 边界 |
| --- | --- | --- |
| `python3 -m unittest discover -s tests -v` | 9 项通过 | 主机 RSA-3072/PSS、规范 JSON、受限 ustar、Wasm 初筛；测试键仅在临时目录产生 |
| CMake/CTest `wasm_scan` | 1 项通过 | 初始无 import/no start 扫描，不是完整 guest ABI 或设备验包 |
| `darren-you/esp-container` 公开 `master` 独立 checkout | 从首个第一方提交 `756244f18d66115eb17524707bf558063ef8216f` 重新拉取，Python 9 项与 CTest 1 项均通过 | 只证明公开仓主机工具可复现；不代表设备运行链路 |
| 固定 IDF `fff9895c82d744c7237be8847347bdd1b07c6643` + lwIP `2758df4cd3666b3b2a5b53830148379326425c0d` 构建 `examples/c3-runtime` | 原样官方 WAMR `8c18e3f68b16c4bcaf05996b2636f6ed2b4cf629` 失败 | 编译 `espidf_platform.c`、`espidf_file.c` 缺 POSIX stat 类型，以及 `espidf_memmap.c` 引用当前 C3 内存保护下不存在的 `MALLOC_CAP_EXEC`；这是修复前的失败基线 |
| 公开 WAMR fork `a34d721b630213f59fde0b40cebbb980903660e8` + 当前 `esp-container` 组件清单与锁 | 固定 IDF/C3 全部编译链接通过；bin 227168 字节，SHA-256 `092e3fc1d1df33e3c98859e34167098ddeb4815d19148ff117fbd758e9b8975f` | 锁文件精确指向公开 fork；`CONFIG_ESP_SYSTEM_MEMPROT=y`，35 个 WAMR 编译单元不含 `espidf_file.c`，计量与 Classic 编译标志已核对；未刷板、尚无实际运行/预算异常或堆峰值 |
| `esp-container` 公开 `master@ae52a774857eb7839481f012dcff03c8a22d1e3f` 全新 checkout | Python 9 项、CTest 1 项通过；固定 SDK/C3 全部编译链接通过，bin 227168 字节、SHA-256 `99e05d6e7e544a7978a912f07a0862f0d0e60e514c456382cdfbeb39f890df00` | 与本机原 checkout 镜像尺寸相同；构建时间等非源码输入会改变摘要，未声明位级可复现；未写板 |

公开 fork 从官方 WAMR-2.4.4 精确提交直接修正三处源码：无 WASI 时不编译文件适配、明确包含 `<sys/stat.h>`、在 IDF 无可执行堆能力时拒绝执行映射并保留普通映射。构建保留 `CONFIG_ESP_SYSTEM_MEMPROT=y`，未定义虚假的 `MALLOC_CAP_EXEC`，也未修改固定 SDK 或 `managed_components`。本仓 manifest 与 `dependencies.lock` 已精确指向该公开提交；P6-02 的 C3 构建条件已具备，实际最小运行、指令额度异常和实板资源结果仍未验收。

当前尚无 Flash 三包槽、设备流式验包、产品实例管理、真实签名包运行、C3 堆峰值和组合容量证据。主计划 P6-02 及后续运行、容量任务仍未验收。
