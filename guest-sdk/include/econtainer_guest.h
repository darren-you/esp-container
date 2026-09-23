#ifndef ECONTAINER_GUEST_H
#define ECONTAINER_GUEST_H

#include <stdint.h>

/* 初期 guest ABI 草案：均为显式入口，实例化时不执行用户代码。 */
/* init/stop 成功返回 0；on_event 返回非负业务结果，负数表示业务失败。 */
int32_t econtainer_init(void);
int32_t econtainer_on_event(const uint8_t *bytes, uint32_t size_bytes);
int32_t econtainer_stop(void);

/* 仅由宿主逐项授权的导入。guest 不获得宿主地址：log 在调用期间复制
 * [bytes, bytes + size_bytes)，当前只保留一条待取日志。 */
#if defined(__wasm32__)
#define ECONTAINER_HOST_IMPORT(name) \
    __attribute__((import_module("econtainer"), import_name(name)))
#else
#define ECONTAINER_HOST_IMPORT(name)
#endif
uint64_t econtainer_monotonic_ms(void) ECONTAINER_HOST_IMPORT("monotonic_ms");
/* 0=已接收，-1=非法长度/地址，-2=上一条日志尚未取走；非法地址也可导致 trap。 */
int32_t econtainer_log(const uint8_t *bytes, uint32_t size_bytes)
    ECONTAINER_HOST_IMPORT("log");
#undef ECONTAINER_HOST_IMPORT

#endif
