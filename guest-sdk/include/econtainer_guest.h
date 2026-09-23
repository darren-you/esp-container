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
/* 1..86400000 ms 后触发。period_ms=0 表示一次性事件，非零表示周期事件。
 * 返回实例私有的非零句柄；0 表示参数错误或配额已满。调用不等待事件执行。 */
uint64_t econtainer_timer_start(uint32_t delay_ms, uint32_t period_ms)
    ECONTAINER_HOST_IMPORT("timer_start");
/* 只取消当前实例仍有效的句柄。成功为 0，过期/其他实例句柄为 -1。 */
int32_t econtainer_timer_cancel(uint64_t handle)
    ECONTAINER_HOST_IMPORT("timer_cancel");
#undef ECONTAINER_HOST_IMPORT

/* 到期事件经普通 econtainer_on_event 串行投递。固定 16 字节：
 * [0..3] = 'E','C','T',1；[4..11] 为句柄 LE64；[12..15] 为本次合并跳过的周期数 LE32。
 * 一次性事件的 skipped 为 0。普通业务事件不得占用此前缀。 */
#define ECONTAINER_TIMER_EVENT_BYTES 16U
static inline int econtainer_timer_event_decode(const uint8_t *bytes, uint32_t size_bytes,
                                                uint64_t *handle, uint32_t *skipped)
{
    if (bytes == 0 || size_bytes != ECONTAINER_TIMER_EVENT_BYTES ||
        handle == 0 || skipped == 0 || bytes[0] != 'E' || bytes[1] != 'C' ||
        bytes[2] != 'T' || bytes[3] != 1) return 0;
    *handle = 0;
    *skipped = 0;
    for (uint32_t index = 0; index < 8; ++index)
        *handle |= (uint64_t)bytes[4 + index] << (index * 8);
    for (uint32_t index = 0; index < 4; ++index)
        *skipped |= (uint32_t)bytes[12 + index] << (index * 8);
    return *handle != 0;
}

#endif
