#include "esp_container_slots.h"

#include <stdio.h>

int main(void)
{
    const econtainer_slots_geometry_t geometry = {
        .partition_offset_bytes = 0x260000,
        .partition_size_bytes = 0x186000,
        .erase_unit_bytes = 0x1000,
        .write_unit_bytes = 4,
        .slots = {
            {0x260000, 0x82000},
            {0x2e2000, 0x82000},
            {0x364000, 0x82000},
        },
    };
    const int valid = econtainer_slots_geometry_valid(&geometry);
    const unsigned partition_end = geometry.partition_offset_bytes +
                                   geometry.partition_size_bytes;
    const unsigned slots_end = geometry.slots[2].offset_bytes +
                               geometry.slots[2].size_bytes;
    printf("geometry_valid=%d partition_end=0x%x slots_end=0x%x\n",
           valid, partition_end, slots_end);
    return valid && partition_end == 0x3e6000 && slots_end == partition_end ? 0 : 1;
}
