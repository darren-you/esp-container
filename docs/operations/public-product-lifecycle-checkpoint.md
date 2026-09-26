# 公开产品生命周期接线检查点

## 接口与边界

`components/esp_container/include/esp_container_product.h` 向 Base 产品装配公开受限的槽选择与 `open/init/on_event/stop/close`，以及现有日志、定时器导入的 owner 驱动读取/投递。组件没有公开 `econtainer_runtime_open` 裸 Wasm 入口、任意包字节入口或第二份活动包状态。`product_open` 直接复用既有私有槽装载：在共享槽锁内读取单 blob、核对实际可启动固件集合和预期序号，只选择运行固件的确认绑定或同一 boot 且处于 `TRIAL_STARTED` 的精确 operation；随后从选中槽映射完整包，对映射中的字节重新验签、验摘要、核对产品/ABI/schema/独立授权和限额，再打开 WAMR。映射在解锁及返回实例前解除。无包绑定返回 `EMPTY`，不会虚构 guest。

调用方必须提供如下真实输入：

- 由 Base 在 app/otadata 串行 owner 下核对的已签名可启动固件 SHA-256 集合；该集合从调用开始到结束不得变化。Container 只核对传入值与持久绑定，不证明底层 app 身份。
- 经冻结分区表验证的三槽 `io/geometry`，包括精确专用包区、独立 NVS 单 blob 及覆盖所有包写入者的锁。这个槽锁与 Base 外层 owner 不得是同一个非递归锁。
- 持久记录的 `expected_sequence`；试运行还需当前 boot 的原始 `operation_id` 与 `boot_id`，并先完成 `TRIAL_STARTED` 持久提交及读回。确认选择必须把两个 ID 字节数组置零。
- 独立可信的产品 ID、RSA 公钥及 key ID、设备能力授权、包资源限额、同步验证工作区；签名清单不能自行授予能力。运行限额与签名值取更严格边界。若允许 LOG/TIMER 导入，同一个执行 owner 还须读取日志与轮询期限，不由本组件从后台进入 guest。

同一实例只由唯一串行 owner 调用。ESP-IDF 的执行 owner 必须是 `pthread_create` 线程；原始 FreeRTOS `xTaskCreate` 任务没有 WAMR 所需的 pthread 身份。`open` 成功后才调用 `init`；失败、trap 或停止失败均不得再投递事件。调用方先证明自身原生引用已回收，再调用 `close` 释放组件资源；`close` 的成功不能冒充业务健康或持久 `CONFIRMED`。本 API 不替 Base 管理 MQTT/FRP 回调队列、网络期限、外部动作、升级授权或持久操作回执。

## 软件验证与尚缺

`slot_runtime` CTest 使用公开头和公开入口运行真实 RSA-3072 测试签名包与锁定 WAMR，覆盖双固件 P0→P3、确认/试运行、错误固件/operation/boot/序号、错误签名映射、独立授权、引擎失败、单实例 BUSY、并发写锁、map/unmap 配对、宿主日志和定时器投递。测试密钥只存在临时目录。CMake 的公开头消费者不再取得组件私有 include 目录。测试结果以本次执行收据为准；此处不把 host 通过扩展为实板结论。

2026-09-27 在隔离副本执行：锁定 WAMR `26c235e53e29acd8b43abe7f3b524577bd4d1ae5` 与校验过 SHA-256 的官方 wasi-sdk 33 下，主机 CTest 9/9、Python unittest 20/20 通过。固定 ESP-IDF `578cf89c343e388db43ba1f4ddcd602fedcb763c`、lwIP `2758df4cd3666b3b2a5b53830148379326425c0d` 上，C3 与 ESP32 独立样例均完成构建，两个组件归档均编译 `product.c`；镜像 SHA-256 分别为 `c154de762f87123c636942e7c0dc0e14783be3baea802385e91da2df612c5a4b`、`53e3d76561aebf3243d129258f61af2a2787875fb4b71ed7b1566bb5a19a4bf8`。独立样例没有调用公开产品 API，也不含 Base/FRP/MQTT/OTA 产品组合，故它们的构建不能证明最终产品 ELF 保留该链路。

Base 仍未具备可刷产品接线：C3 现行分区没有专用包区；ESP32 候选表尚未受控迁移；Base 当前固件集合观察拒绝 pending 运行槽，启动 owner 与现有适配会重复 claim。两个 target 的实际产品签名链接、容量、物理 provider 读写、联合 OTA 与断电恢复都需继续验证。公开本入口不改变 P6-03、P7-01 或 P7-02 的验收状态。
