#ifndef ECONTAINER_GUEST_H
#define ECONTAINER_GUEST_H

#include <stdint.h>

/* 初期 guest ABI 草案：均为显式入口，实例化时不执行用户代码。 */
/* init/stop 成功返回 0；on_event 返回非负业务结果，负数表示业务失败。 */
int32_t econtainer_init(void);
int32_t econtainer_on_event(const uint8_t *bytes, uint32_t size_bytes);
int32_t econtainer_stop(void);

#endif
