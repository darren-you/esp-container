# 来源与许可

本仓第一方 `esp_container` 检查器、guest SDK、主机打包工具和测试采用 Apache-2.0。没有复制旧 ESP Service、ESP Tool 或第三方运行时代码。

| 来源 | 精确版本 | 用途 | 本仓保留范围 | 许可 |
| --- | --- | --- | --- | --- |
| [WAMR](https://github.com/wasm-micro-runtime/wasm-micro-runtime) | `8c18e3f68b16c4bcaf05996b2636f6ed2b4cf629`（`WAMR-2.4.4`） | ESP-IDF 组件依赖、Classic Interpreter 与指令计量 | 不复制源码；由 IDF Component Manager 按完整 SHA 获取 | Apache-2.0 WITH LLVM-exception |
| [ESP-IDF](https://github.com/espressif/esp-idf) | `fff9895c82d744c7237be8847347bdd1b07c6643` | C3 工具链与固件 SDK | 不复制源码；独立 SDK checkout | Apache-2.0 等，依上游各文件 |
| [esp-lwip](https://github.com/darren-you/esp-lwip) | `2758df4cd3666b3b2a5b53830148379326425c0d` | 目标组合的 SDK lwIP 修正 | 不复制源码；随锁定 IDF checkout | 见该仓许可 |

上游 WAMR 的 C3 示例仅作为组件装配和 API 行为依据。本仓 `examples/c3-runtime` 的两份最小 Wasm v1 字节由对应函数体手工构造，用于预算与死循环原型，不是上游示例字节的复制。WAMR 的默认 IDF 配置含 Fast Interpreter、AOT 和 WASI；本仓样例显式关闭并构建检查。WAMR v2.4.4 的实例化路径会调用特定导出入口，所以本仓在加载前拒绝 start 段及 `__post_instantiate`、`__wasm_call_ctors`、`_initialize` 导出；这项检查的安全性仍须真实运行与异常输入验证。
