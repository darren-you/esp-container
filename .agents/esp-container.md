# ESP Container 边界

- `esp_container` 负责业务包的静态验证、签名核对、实例生命周期、资源额度和独立包槽；`esp-base` 负责设备身份、安装授权、网络与组合调度，`esp-ota` 负责固件 OTA。业务包不能写入 app OTA 槽。
- 使用锁定的 WAMR Classic Interpreter 和标准 Wasm；首版关闭 AOT/JIT、Fast Interpreter、guest 线程、WASI 文件/socket 和自动 start。guest 不直接获取 IDF 结构、原生地址、凭据或任意 GPIO。
- 签名公钥只来自平台明确配置的信任锚，不采信包内公钥。主机 `product.pkg` 工具的通过结果不能替代设备从 Flash 回读后的验签与摘要核对。
- 每个 guest 入口须有正数指令额度；宿主调用另有期限和取消归属。停止失败时不释放仍被引用的资源，也不启动新实例。
- 4 MiB C3 的双固件、三包槽、RAM 峰值与分区迁移尚需真实容量原型。容量不成立时记录测量和约束，不以移除 TLS、签名、回退或设备身份保全来冒充验收。
- 上游 WAMR 只在其源码边界修改并固定精确提交；不得在普通构建中修改 `managed_components` 或自动注入补丁。
