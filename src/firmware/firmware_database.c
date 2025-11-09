/**
 * Firmware Database Registry - Auto-generated
 * DO NOT EDIT
 */

#include "firmware_database.h"
#include "firmware_registry.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>

// Firmware registry table
typedef struct {
    const char *processor;
    const uint8_t* (*get_spl)(size_t *size);
    const uint8_t* (*get_uboot)(size_t *size);
} firmware_registry_entry_t;

static const firmware_registry_entry_t firmware_registry[] = {
    {"a1_n_ne_x", firmware_a1_n_ne_x_get_spl, firmware_a1_n_ne_x_get_uboot},
    {"a1_nt_a", firmware_a1_nt_a_get_spl, firmware_a1_nt_a_get_uboot},
    {"t20", firmware_t20_get_spl, firmware_t20_get_uboot},
    {"t21", firmware_t21_get_spl, firmware_t21_get_uboot},
    {"t23", firmware_t23_get_spl, firmware_t23_get_uboot},
    {"t30", firmware_t30_get_spl, firmware_t30_get_uboot},
    {"t30a", firmware_t30a_get_spl, firmware_t30a_get_uboot},
    {"t30nl", firmware_t30nl_get_spl, firmware_t30nl_get_uboot},
    {"t30x", firmware_t30x_get_spl, firmware_t30x_get_uboot},
    {"t31", firmware_t31_get_spl, firmware_t31_get_uboot},
    {"t31a", firmware_t31a_get_spl, firmware_t31a_get_uboot},
    {"t31nl", firmware_t31nl_get_spl, firmware_t31nl_get_uboot},
    {"t31x", firmware_t31x_get_spl, firmware_t31x_get_uboot},
    {"t40", firmware_t40_get_spl, firmware_t40_get_uboot},
    {"t41", firmware_t41_get_spl, firmware_t41_get_uboot},
};

const firmware_binary_t* firmware_get(const char *processor) {
    if (!processor) return NULL;

    static firmware_binary_t result;

    for (size_t i = 0; i < sizeof(firmware_registry) / sizeof(firmware_registry[0]); i++) {
        if (strcasecmp(firmware_registry[i].processor, processor) == 0) {
            result.processor = firmware_registry[i].processor;
            result.spl_data = firmware_registry[i].get_spl(&result.spl_size);
            result.uboot_data = firmware_registry[i].get_uboot(&result.uboot_size);
            return &result;
        }
    }

    return NULL;
}

const firmware_binary_t* firmware_list(size_t *count) {
    if (count) {
        *count = sizeof(firmware_registry) / sizeof(firmware_registry[0]);
    }

    // Build array of firmware_binary_t on first call
    static firmware_binary_t *list = NULL;
    static size_t list_size = 0;

    if (!list) {
        list_size = sizeof(firmware_registry) / sizeof(firmware_registry[0]);
        list = malloc(list_size * sizeof(firmware_binary_t));

        for (size_t i = 0; i < list_size; i++) {
            list[i].processor = firmware_registry[i].processor;
            list[i].spl_data = firmware_registry[i].get_spl(&list[i].spl_size);
            list[i].uboot_data = firmware_registry[i].get_uboot(&list[i].uboot_size);
        }
    }

    return list;
}

int firmware_available(const char *processor) {
    return firmware_get(processor) != NULL;
}
