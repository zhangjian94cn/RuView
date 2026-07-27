/**
 * @file controlled_probe.h
 * @brief ADR-152 fixed-transmitter probe task and device status packet.
 */

#ifndef CONTROLLED_PROBE_H
#define CONTROLLED_PROBE_H

#include "esp_err.h"
#include "nvs_config.h"

#define CONTROLLED_STATUS_MAGIC 0xC5110005
#define CONTROLLED_STATUS_VERSION 1

/**
 * Start the low-rate status task and, for TX nodes, the probe generator.
 *
 * The configuration is copied before the task starts. Existing passive nodes
 * emit status but do not inject traffic.
 */
esp_err_t controlled_probe_start(const nvs_config_t *cfg);

#endif /* CONTROLLED_PROBE_H */
