/**
 * @file controlled_probe.c
 * @brief ADR-152 controlled-link probe generation and topology evidence.
 */

#include "controlled_probe.h"

#include <stdbool.h>
#include <string.h>
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

#include "csi_collector.h"
#include "stream_sender.h"

#define PROBE_BROADCAST_MAGIC 0xC5110006
#define PROBE_BROADCAST_PORT 5006
#define STATUS_INTERVAL_MS 1000

#define STATUS_FLAG_FILTER_SET (1u << 0)
#define STATUS_FLAG_CONFIG_VALID (1u << 1)
#define STATUS_FLAG_TX_ACTIVE (1u << 2)

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t version;
    uint8_t node_id;
    uint8_t role;
    uint8_t transport;
    uint16_t probe_interval_ms;
    uint16_t flags;
    uint32_t uptime_ms;
    uint32_t sequence;
    uint8_t filter_mac[6];
    uint8_t station_mac[6];
    char firmware_version[16];
} controlled_status_packet_t;

_Static_assert(sizeof(controlled_status_packet_t) == 48,
               "controlled status packet wire size must remain 48 bytes");

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint8_t node_id;
    uint8_t reserved;
    uint16_t sequence;
    uint64_t uptime_us;
} controlled_broadcast_probe_t;

static const char *TAG = "controlled_probe";
static nvs_config_t s_cfg;
static uint32_t s_status_sequence;
static uint16_t s_probe_sequence;
static int s_broadcast_socket = -1;
static struct sockaddr_in s_broadcast_addr;

static bool config_is_valid(void)
{
    if (s_cfg.probe_role > PROBE_ROLE_RX ||
        s_cfg.probe_transport > PROBE_TRANSPORT_UDP_BROADCAST ||
        s_cfg.probe_interval_ms < 20 ||
        s_cfg.probe_interval_ms > 1000) {
        return false;
    }
    return s_cfg.probe_role != PROBE_ROLE_RX || s_cfg.filter_mac_set;
}

static void send_status(void)
{
    controlled_status_packet_t packet;
    memset(&packet, 0, sizeof(packet));
    packet.magic = CONTROLLED_STATUS_MAGIC;
    packet.version = CONTROLLED_STATUS_VERSION;
    packet.node_id = s_cfg.node_id;
    packet.role = s_cfg.probe_role;
    packet.transport = s_cfg.probe_transport;
    packet.probe_interval_ms = s_cfg.probe_interval_ms;
    packet.uptime_ms = (uint32_t)(esp_timer_get_time() / 1000);
    packet.sequence = s_status_sequence++;
    if (s_cfg.filter_mac_set) {
        packet.flags |= STATUS_FLAG_FILTER_SET;
        memcpy(packet.filter_mac, s_cfg.filter_mac, sizeof(packet.filter_mac));
    }
    if (config_is_valid()) {
        packet.flags |= STATUS_FLAG_CONFIG_VALID;
    }
    if (s_cfg.probe_role == PROBE_ROLE_TX) {
        packet.flags |= STATUS_FLAG_TX_ACTIVE;
    }
    (void)esp_wifi_get_mac(WIFI_IF_STA, packet.station_mac);

    const esp_app_desc_t *desc = esp_app_get_description();
    strncpy(packet.firmware_version, desc->version, sizeof(packet.firmware_version) - 1);
    (void)stream_sender_send((const uint8_t *)&packet, sizeof(packet));
}

static esp_err_t open_broadcast_socket(void)
{
    if (s_broadcast_socket >= 0) {
        return ESP_OK;
    }
    s_broadcast_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s_broadcast_socket < 0) {
        return ESP_FAIL;
    }
    int enabled = 1;
    if (setsockopt(s_broadcast_socket, SOL_SOCKET, SO_BROADCAST,
                   &enabled, sizeof(enabled)) != 0) {
        close(s_broadcast_socket);
        s_broadcast_socket = -1;
        return ESP_FAIL;
    }
    memset(&s_broadcast_addr, 0, sizeof(s_broadcast_addr));
    s_broadcast_addr.sin_family = AF_INET;
    s_broadcast_addr.sin_port = htons(PROBE_BROADCAST_PORT);
    s_broadcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    return ESP_OK;
}

static esp_err_t send_broadcast_probe(void)
{
    if (open_broadcast_socket() != ESP_OK) {
        return ESP_FAIL;
    }
    controlled_broadcast_probe_t packet = {
        .magic = PROBE_BROADCAST_MAGIC,
        .node_id = s_cfg.node_id,
        .reserved = 0,
        .sequence = s_probe_sequence++,
        .uptime_us = (uint64_t)esp_timer_get_time(),
    };
    int sent = sendto(s_broadcast_socket, &packet, sizeof(packet), 0,
                      (struct sockaddr *)&s_broadcast_addr, sizeof(s_broadcast_addr));
    return sent == (int)sizeof(packet) ? ESP_OK : ESP_FAIL;
}

static void controlled_probe_task(void *arg)
{
    (void)arg;
    TickType_t last_status = 0;
    TickType_t last_probe = 0;
    const TickType_t status_ticks = pdMS_TO_TICKS(STATUS_INTERVAL_MS);
    const TickType_t probe_ticks = pdMS_TO_TICKS(s_cfg.probe_interval_ms);

    while (true) {
        TickType_t now = xTaskGetTickCount();
        if ((now - last_status) >= status_ticks) {
            send_status();
            last_status = now;
        }
        if (s_cfg.probe_role == PROBE_ROLE_TX && (now - last_probe) >= probe_ticks) {
            esp_err_t err = s_cfg.probe_transport == PROBE_TRANSPORT_RAW_NULL
                ? csi_inject_ndp_frame()
                : send_broadcast_probe();
            if (err != ESP_OK && (s_probe_sequence % 100u) == 0u) {
                ESP_LOGW(TAG, "Probe transport failed: %s", esp_err_to_name(err));
            }
            last_probe = now;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

esp_err_t controlled_probe_start(const nvs_config_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memcpy(&s_cfg, cfg, sizeof(s_cfg));
    if (!config_is_valid()) {
        ESP_LOGE(TAG, "Invalid controlled-link config: role=%u transport=%u interval=%u filter=%u",
                 (unsigned)s_cfg.probe_role, (unsigned)s_cfg.probe_transport,
                 s_cfg.probe_interval_ms, (unsigned)s_cfg.filter_mac_set);
    }
    BaseType_t created = xTaskCreate(
        controlled_probe_task, "controlled_probe", 4096, NULL, 5, NULL);
    if (created != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Controlled probe started: role=%u transport=%u interval=%u ms",
             (unsigned)s_cfg.probe_role, (unsigned)s_cfg.probe_transport,
             s_cfg.probe_interval_ms);
    return ESP_OK;
}
