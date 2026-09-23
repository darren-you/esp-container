# 开发检查点：2026-09-23

## 已核对的软件范围

| 检查 | 结果 | 边界 |
| --- | --- | --- |
| `python3 -m unittest discover -s tests -v` | 9 项通过 | 主机 RSA-3072/PSS、规范 JSON、受限 ustar、Wasm 初筛；测试键仅在临时目录产生 |
| CMake/CTest `wasm_scan` | 1 项通过 | 初始无 import/no start 扫描，不是完整 guest ABI 或设备验包 |
| `darren-you/esp-container` 公开 `master` 独立 checkout | 从首个第一方提交 `756244f18d66115eb17524707bf558063ef8216f` 重新拉取，Python 9 项与 CTest 1 项均通过 | 只证明公开仓主机工具可复现；不代表设备运行链路 |
| 固定 IDF `fff9895c82d744c7237be8847347bdd1b07c6643` + lwIP `2758df4cd3666b3b2a5b53830148379326425c0d` 构建 `examples/c3-runtime` | 原样官方 WAMR `8c18e3f68b16c4bcaf05996b2636f6ed2b4cf629` 失败 | 编译 `espidf_platform.c`、`espidf_file.c` 缺 POSIX stat 类型，以及 `espidf_memmap.c` 引用当前 C3 内存保护下不存在的 `MALLOC_CAP_EXEC`；这是修复前的失败基线 |
| 公开 WAMR fork `a34d721b630213f59fde0b40cebbb980903660e8` + 当前 `esp-container` 组件清单与锁 | 固定 IDF/C3 全部编译链接通过；bin 227168 字节，SHA-256 `092e3fc1d1df33e3c98859e34167098ddeb4815d19148ff117fbd758e9b8975f` | 锁文件精确指向公开 fork；`CONFIG_ESP_SYSTEM_MEMPROT=y`，35 个 WAMR 编译单元不含 `espidf_file.c`，计量与 Classic 编译标志已核对；未刷板、尚无实际运行/预算异常或堆峰值 |
| 精确 WAMR fork 的主机 Classic 执行与 C3 探针异常判定收紧 | 锁文件 WAMR `a34d721...`、`.component_hash=e7a23b...` 一致；主机 CTest 2/2 通过，正常函数 `call_ok=1/result=0`，死循环 `call_ok=0/Exception: instruction limit exceeded`；固定 SDK/C3 重新完整链接，bin 227232 字节，SHA-256 `b78ce257e17ef505a20c8c8b0f2af550ca3e1fa897494faabab5dabd5b55b8e1` | 这是本机真实解释器执行与 C3 构建证据；尚未刷板，不能声称 C3 实际执行、堆峰值或全负载预算通过 |
| 公开 `master@e27b5dc5cdb5979c7d83dc09833d21643604498c` 全新 checkout | 独立目录 `/tmp/esp-container-public-e27` 从 GitHub 克隆；Python 9/9、真实 WAMR host CTest 2/2、固定 SDK/C3 完整链接均通过；锁文件和下载组件 `.component_hash` 同为 `e7a23b...`，bin 227232 字节，SHA-256 `effcfec54778d734b2bd1b12a3242fdfeeccb7db6db184bca044e3879eb92f65` | 两次 C3 构建路径不同，SHA 不相等；不宣称逐字节可复现，也不以主机结果替代实板异常与堆峰值 |
| `esp-container` 公开 `master@ae52a774857eb7839481f012dcff03c8a22d1e3f` 全新 checkout | Python 9 项、CTest 1 项通过；固定 SDK/C3 全部编译链接通过，bin 227168 字节、SHA-256 `99e05d6e7e544a7978a912f07a0862f0d0e60e514c456382cdfbeb39f890df00` | 与本机原 checkout 镜像尺寸相同；构建时间等非源码输入会改变摘要，未声明位级可复现；未写板 |
| wasi-sdk 33 freestanding counter guest | 官方 macOS arm64 资产 SHA-256 `85c997a2665ead91673b5bb88b7d0df3fc8900df3bfa244f720d478187bbdc78`；`WASI_SDK_ROOT` 下 Python 13/13，counter 437 字节、SHA-256 `1896ae9ed2390bd4fb71af9d50965533a679245806c9c07675d65b81be42c91c`；两种输出路径字节相同，静态 ABI/profile 检查通过，锁定 WAMR 主机实际调用 init/event/stop 通过 | 只验证 counter 样例的编译、输出形状和主机 Classic 执行；无实板、宿主授权、句柄、包安装或完整 P6-04 结论 |
| 五组件仓外 C3 容量原型 | [独立报告](five-component-capacity-probe.md)：固定五仓及 SDK/WAMR 提交的实际 ELF/map 保留 FRP TLS/Yamux、MQTT 发布订阅、OTA HTTPS 与 WAMR Classic 调用；RSA-3072 签名镜像 `0x121000`，官方分区工具验证两组 4 MiB 布局 | P6-03 未验收：Base 旧 MQTT 试验组件仍冲突；极限布局双 app 零余量且 512 KiB Wasm 打包超槽；留余量布局尚无真实业务包、动态 RAM、迁移和实板数据 |
| 官方 Espressif QEMU ESP32-C3 最小运行 | 公开 `4a37af82b44a8a4e4444a64652cee6c6e29e08eb` 的 `xTaskCreate` 探针仿真时在 `pthread_self` 断言；后续 `f013c6a8ae60e4232f1815782e72e8b174d9f790` 仍保留该入口。改用与锁定 WAMR ESP-IDF 示例一致的 joinable pthread 后，固定 IDF/lwIP 与官方 `qemu-riscv32` 9.2.2 (`esp_develop_9.2.2_20260417`) 仿真启动成功。真实 WAMR C3 日志：`before free=326416 largest=188416`；`run call_ok=1 result=0 exception=none`；`looping call_ok=0 ... Exception: instruction limit exceeded`；`after free=326312 largest=188416`；`normal=1 instruction_limit=1`。Python 9 项通过、counter 编译测试因未设置 `WASI_SDK_ROOT` 跳过，固定 WAMR 主机 CTest 2/2 通过；C3 bin 231488 字节，SHA-256 `2e93e4f42debb306224f7550929fe2cf9b67d110cfed82e35e80e8d04cd9115f` | 仅为 QEMU 仿真与本机软件证据；样例没有 Wi-Fi、FRP、MQTT、OTA、生产包和真实 Flash 布局。未刷板，不是实板堆峰值、时限或掉电行为验收 |
| [组件私有单实例运行切片](single-instance-runtime-checkpoint.md) | 固定 wasi-sdk 33 + 锁定 WAMR host CTest 3/3；counter、事件复制与分配失败、ABI 和原始内存页拒绝、init/event/stop 各自额度异常、关闭重开通过。固定 IDF/C3 组件源码编译、原样样例链接与仓外私有 API 强制引用链接通过 | 原样样例没有调用新 API；本项仍缺 C3 新链路真运行、签名包、宿主能力、Base 装配、RAM/实板结果，P6-04/P6-07 未验收 |

公开 fork 从官方 WAMR-2.4.4 精确提交直接修正三处源码：无 WASI 时不编译文件适配、明确包含 `<sys/stat.h>`、在 IDF 无可执行堆能力时拒绝执行映射并保留普通映射。构建保留 `CONFIG_ESP_SYSTEM_MEMPROT=y`，未定义虚假的 `MALLOC_CAP_EXEC`，也未修改固定 SDK 或 `managed_components`。本仓 manifest 与 `dependencies.lock` 已精确指向该公开提交；P6-02 的 C3 最小运行和指令额度异常目前已有 QEMU 仿真证据，实板运行与资源结果仍未验收。

当前已有只读组合链接与分区几何证据，但仍无实际 Flash 三包槽、设备流式验包、产品实例管理、真实签名包运行、C3 堆峰值和分区迁移。主计划 P6-02 及后续运行、容量任务仍未验收。
