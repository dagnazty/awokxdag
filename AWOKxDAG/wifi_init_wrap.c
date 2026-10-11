// Linker wraps that shrink the Wi-Fi and BLE-controller footprints. Arduino's
// wifiLowLevelInit hardcodes dynamic RX/TX = 32 and leaves CSI and AMPDU on,
// which reserves a large DMA block; the dual-radio views time-share the radio
// (see RadioScheduler) so only one is DMA-resident at a time, but trimming
// Wi-Fi still keeps startup headroom healthy. Linked with
// -Wl,--wrap=esp_wifi_init -Wl,--wrap=esp_bt_controller_init.
#include <stdio.h>
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_wifi.h"
#include "esp_bt.h"

extern esp_err_t __real_esp_wifi_init(const wifi_init_config_t *config);

static void log_internal_heap(const char *tag) {
  const uint32_t caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  printf("[wifi] %s: internal free=%u largest=%u\n", tag,
         (unsigned)heap_caps_get_free_size(caps),
         (unsigned)heap_caps_get_largest_free_block(caps));
}

esp_err_t __wrap_esp_wifi_init(const wifi_init_config_t *config) {
  if (!config) return __real_esp_wifi_init(config);
  wifi_init_config_t cfg = *config;
  // AWOKxDAG only ever scans APs and injects the occasional mgmt frame -- it
  // never sustains a throughput connection -- so trim Arduino's oversized STA
  // buffer pool and turn off AMPDU. Under the time-multiplex scheduler
  // Wi-Fi no longer has to leave room for a resident BLE controller, so these
  // are moderate (scan-friendly) caps rather than the earlier hard floor.
  if (cfg.dynamic_rx_buf_num > 8) cfg.dynamic_rx_buf_num = 8;
  if (cfg.dynamic_tx_buf_num > 8) cfg.dynamic_tx_buf_num = 8;
  if (cfg.mgmt_sbuf_num > 16) cfg.mgmt_sbuf_num = 16;
  if (cfg.static_rx_buf_num > 4) cfg.static_rx_buf_num = 4;
  cfg.csi_enable = 0;
  cfg.ampdu_rx_enable = 0;
  cfg.ampdu_tx_enable = 0;
  cfg.amsdu_tx_enable = 0;
  cfg.rx_ba_win = 0;
  cfg.dump_hesigb_enable = false;
  if (cfg.tx_hetb_queue_num > 1) cfg.tx_hetb_queue_num = 1;
  // Arduino sets cache_tx_buf_num = 4 when SPIRAM is compiled in. Do not set
  // CONFIG_FEATURE_CACHE_TX_BUF_BIT: the prebuilt C5 wifi lib then checks the
  // sdkconfig cache count (0) and rejects init.
  if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0 && cfg.cache_tx_buf_num == 0) {
    cfg.cache_tx_buf_num = 4;
  }
  log_internal_heap("wrap before");
  printf("[wifi] init wrap rx=%d tx=%d static_rx=%d mgmt=%d cache=%d csi=%d\n",
         cfg.dynamic_rx_buf_num, cfg.dynamic_tx_buf_num, cfg.static_rx_buf_num,
         cfg.mgmt_sbuf_num, cfg.cache_tx_buf_num, cfg.csi_enable);
  esp_err_t err = __real_esp_wifi_init(&cfg);
  log_internal_heap("wrap after");
  if (err == ESP_OK) return err;
  if (cfg.dynamic_rx_buf_num > 4) cfg.dynamic_rx_buf_num = 4;
  if (cfg.dynamic_tx_buf_num > 4) cfg.dynamic_tx_buf_num = 4;
  if (cfg.mgmt_sbuf_num > 8) cfg.mgmt_sbuf_num = 8;
  printf("[wifi] init wrap retry rx=%d tx=%d mgmt=%d err=0x%x\n",
         cfg.dynamic_rx_buf_num, cfg.dynamic_tx_buf_num, cfg.mgmt_sbuf_num,
         (unsigned)err);
  return __real_esp_wifi_init(&cfg);
}

extern esp_err_t __real_esp_bt_controller_init(esp_bt_controller_config_t *cfg);

esp_err_t __wrap_esp_bt_controller_init(esp_bt_controller_config_t *cfg) {
#if defined(CONFIG_IDF_TARGET_ESP32C5)
  if (cfg) {
    cfg->rxbuf_reserved = 0;
    cfg->enhanced_mem_resv = 0;
    if (cfg->ble_acl_buf_count > 6) cfg->ble_acl_buf_count = 6;
    // App-level dedup (wardriveMacSeen) already covers correctness, so the
    // controller's 100-entry hardware duplicate lists are unnecessary bulk.
    if (cfg->ble_ll_rsp_dup_list_count > 16) cfg->ble_ll_rsp_dup_list_count = 16;
    if (cfg->ble_ll_adv_dup_list_count > 16) cfg->ble_ll_adv_dup_list_count = 16;
    printf("[ble] controller wrap acl=%u conn=%u dup=%u/%u\n",
           (unsigned)cfg->ble_acl_buf_count, (unsigned)cfg->nimble_max_connections,
           (unsigned)cfg->ble_ll_rsp_dup_list_count,
           (unsigned)cfg->ble_ll_adv_dup_list_count);
  }
#endif
  return __real_esp_bt_controller_init(cfg);
}
