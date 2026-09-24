#include "econtainer_guest.h"

/* 由链接器保留完整页内空间；导出的是地址，不在实例化时执行代码。 */
uint8_t econtainer_event_buffer[ECONTAINER_EVENT_BUFFER_BYTES];
