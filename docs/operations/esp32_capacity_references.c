#include <stdbool.h>
#include <stddef.h>

#include "esp_frp.h"
#include "emqtt.h"
#include "eota.h"
#include "esp_container.h"
#include "esp_container_package.h"
#include "esp_container_slots.h"
#include "esp_container_slots_idf.h"
#include "wasm_export.h"

/* This source is a link probe only. Its volatile gate stays zero; never flash. */
static volatile bool capacity_probe_never_run;

void capacity_references(void)
{
    if (!capacity_probe_never_run) {
        return;
    }
    efrp_client_t *frp = NULL;
    efrp_status_t frp_status;
    (void)efrp_create(NULL, &frp);
    (void)efrp_start(frp);
    (void)efrp_get_status(frp, &frp_status);
    (void)efrp_stop(frp, 1000);
    (void)efrp_destroy(&frp, 1000);

    emqtt_runtime_t *mqtt = NULL;
    emqtt_event_t mqtt_event;
    int message_id;
    (void)emqtt_create(NULL, &mqtt);
    (void)emqtt_start(mqtt, true, true);
    (void)emqtt_subscribe(mqtt, "device/events", 1);
    (void)emqtt_enqueue(mqtt, "device/events", "x", 1, 1, false, &message_id);
    (void)emqtt_poll(mqtt, &mqtt_event);
    (void)emqtt_stop(mqtt);
    (void)emqtt_destroy(mqtt);

    (void)eota_preflight(NULL, 0, NULL);
    (void)eota_prepare(NULL, NULL, NULL, NULL, NULL);
    (void)eota_select(NULL, NULL);
    (void)eota_confirm_pending(NULL);

    uint8_t wasm[] = {0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00};
    char error[128];
    (void)econtainer_wasm_check(wasm, sizeof(wasm));
    (void)econtainer_package_verify(NULL, NULL, 0, 0, NULL, 0, NULL, NULL, NULL);
    (void)econtainer_package_wasm_check(NULL, NULL, NULL, NULL, NULL);
    (void)econtainer_slots_geometry_valid(NULL);
    (void)econtainer_slots_idf_bind(NULL, NULL);
    (void)econtainer_slots_reconcile(NULL, NULL, NULL, NULL, NULL);
    (void)econtainer_slots_write_and_prepare(NULL, NULL, 0, NULL, NULL, NULL, NULL, NULL);
    (void)wasm_runtime_init();
    wasm_module_t module = wasm_runtime_load(wasm, sizeof(wasm), error, sizeof(error));
    wasm_module_inst_t instance = wasm_runtime_instantiate(module, 4096, 0, error, sizeof(error));
    wasm_exec_env_t env = wasm_runtime_create_exec_env(instance, 4096);
    wasm_function_inst_t function = wasm_runtime_lookup_function(instance, "econtainer_on_event");
    uint32_t args[1] = {0};
    wasm_runtime_set_instruction_count_limit(env, 1000);
    (void)wasm_runtime_call_wasm(env, function, 0, args);
    wasm_runtime_destroy_exec_env(env);
    wasm_runtime_deinstantiate(instance);
    wasm_runtime_unload(module);
}
