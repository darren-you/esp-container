#ifndef ECONTAINER_GUEST_H
#define ECONTAINER_GUEST_H

#include <stdint.h>

/* 初期 guest ABI 草案：均为显式入口，实例化时不执行用户代码。 */
/* 业务返回 0 表示本次调用完成；其他值由宿主记录为业务失败。 */
int32_t econtainer_init(void);
int32_t econtainer_on_event(const uint8_t *bytes, uint32_t size_bytes);
int32_t econtainer_stop(void);

#endif
