// SPDX-License-Identifier: Apache-2.0
#include "esp_container_product.h"

#include "runtime_internal.h"
#include "slot_runtime_internal.h"

econtainer_slot_runtime_result_t econtainer_product_open(
    const econtainer_slots_io_t *io, const econtainer_slots_geometry_t *geometry,
    const econtainer_slot_selection_request_t *request,
    const econtainer_package_slot_validation_t *validation,
    const econtainer_runtime_limits_t *runtime_limits,
    econtainer_runtime_t **out)
{
    return econtainer_slot_runtime_open(io, geometry, request, validation,
                                        runtime_limits, out);
}

econtainer_runtime_result_t econtainer_product_init(econtainer_runtime_t *runtime)
{
    return econtainer_runtime_init(runtime);
}

econtainer_runtime_result_t econtainer_product_on_event(econtainer_runtime_t *runtime,
                                                       const uint8_t *event,
                                                       size_t event_size_bytes,
                                                       int32_t *guest_result)
{
    return econtainer_runtime_on_event(runtime, event, event_size_bytes, guest_result);
}

econtainer_runtime_result_t econtainer_product_stop(econtainer_runtime_t *runtime)
{
    return econtainer_runtime_stop(runtime);
}

econtainer_runtime_result_t econtainer_product_take_log(econtainer_runtime_t *runtime,
                                                       uint8_t *output,
                                                       size_t output_capacity,
                                                       size_t *log_size_bytes)
{
    return econtainer_runtime_take_log(runtime, output, output_capacity, log_size_bytes);
}

econtainer_runtime_result_t econtainer_product_poll_timer(econtainer_runtime_t *runtime,
                                                         econtainer_timer_event_t *event,
                                                         int32_t *guest_result)
{
    return econtainer_runtime_poll_timer(runtime, event, guest_result);
}

econtainer_runtime_result_t econtainer_product_next_timer_deadline(
    econtainer_runtime_t *runtime, uint64_t *deadline_ms)
{
    return econtainer_runtime_next_timer_deadline(runtime, deadline_ms);
}

econtainer_runtime_result_t econtainer_product_close(econtainer_runtime_t **runtime)
{
    return econtainer_runtime_close(runtime);
}
