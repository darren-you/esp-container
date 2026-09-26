#include "esp_container_slots.h"

#include <stdio.h>

int main(void)
{
    const econtainer_slots_geometry_t geometry = {
        .partition_offset_bytes = 0x258000,
        .partition_size_bytes = 0x186000,
        .erase_unit_bytes = 0x1000,
        .write_unit_bytes = 4,
        .slots = {
            {0x258000, 0x82000},
            {0x2da000, 0x82000},
            {0x35c000, 0x82000},
        },
    };
    const int valid = econtainer_slots_geometry_valid(&geometry);
    printf("geometry_valid=%d package_end=0x%x slots_end=0x%x\n",
           valid, geometry.partition_offset_bytes + geometry.partition_size_bytes,
           geometry.slots[2].offset_bytes + geometry.slots[2].size_bytes);
    return valid ? 0 : 1;
}
