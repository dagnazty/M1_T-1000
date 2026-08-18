/* See COPYING.txt for license details. */

/*
 *
 * m1_esp_link.h
 *
 * M1 Link over ESP32 (ESP-NOW) — host (T-1000) side of the remote-trigger PoC.
 *
 * Drives the ESP32-C6's AT+M1ESPNOW* commands over SPI (spi_AT_send_recv) and
 * parses the +ESPNOWRX: / +ESPNOWPEER: unsolicited events. Second transport for
 * the remote-trigger feature already shipped over the Si4463 sub-GHz radio
 * (see m1_link.c "M1 Link"). Phase 0 payload target: BadUSB.
 *
 * Design + wire format: esp32-at-monstatek-m1/docs/ESPNOW_LINK_DESIGN.md
 *
 * M1 Project
 *
 */

#ifndef M1_ESP_LINK_H_
#define M1_ESP_LINK_H_

#include <stdint.h>
#include <stdbool.h>

/* Payload types — MUST match ENOW_PTYPE_* on the ESP and M1_LINK_TRIG_* here. */
#define M1_ESPNOW_PTYPE_SUB     1u
#define M1_ESPNOW_PTYPE_BADUSB  2u   /* Phase 0 target */
#define M1_ESPNOW_PTYPE_BADBT   3u   /* excluded from PoC */
#define M1_ESPNOW_PTYPE_IR      4u

/* Bring ESP-NOW up on a Wi-Fi channel (1-13) / tear it down. Prints result. */
void m1_esp_link_enable(uint8_t channel);
void m1_esp_link_disable(void);

/* Query ESP-NOW state (enabled, channel, own MAC, key-set). Prints response. */
void m1_esp_link_info(void);

/* Set the shared passphrase used for encrypted peers. */
void m1_esp_link_key(const char *passphrase);

/* Broadcast HELLO for `seconds` and print discovered peers (+ESPNOWPEER). */
void m1_esp_link_scan(uint32_t seconds);

/* Send an ACK'd trigger to a peer MAC ("xx:xx:xx:xx:xx:xx"). Prints OK/ERROR. */
void m1_esp_link_trigger(const char *mac, uint8_t ptype, const char *name);

/* Pairing allowlist: add a peer MAC / list paired MACs. When any peer is
 * paired, the ESP only honors triggers from listed MACs. */
void m1_esp_link_pair(const char *mac);
void m1_esp_link_pairs(void);

/* Listen for `seconds`, polling the ESP for +ESPNOWRX: events. On a BADUSB
 * trigger, runs 0:/BadUSB/<name> via badusb_execute_file(). This is the peer
 * side of the Phase 0 gate. */
void m1_esp_link_listen(uint32_t seconds);

/* ---- UI-facing API (return values instead of printing) -------------------- */

typedef struct {
    char   mac[18];   /* "xx:xx:xx:xx:xx:xx" */
    char   name[16];  /* peer callsign from HELLO */
    int8_t rssi;
} m1_esp_peer_t;

/* Bring ESP-NOW up on a channel. Returns true on success. */
bool m1_esp_link_enable_ok(uint8_t channel);

/* Scan for `seconds`, fill `out` with up to `max_peers` unique peers (by MAC).
 * Returns the number of peers found. */
int  m1_esp_link_scan_peers(m1_esp_peer_t *out, int max_peers, uint32_t seconds);

/* Set passphrase / pair a MAC / send a trigger — return true on ESP "OK". */
bool m1_esp_link_key_ok(const char *passphrase);
bool m1_esp_link_pair_ok(const char *mac);
bool m1_esp_link_trigger_ok(const char *mac, uint8_t ptype, const char *name);

/* Send the actual payload: read `path`, split into fragments, and transfer them
 * to the peer, which reassembles + runs it as `remote_name`. Returns true if
 * every fragment was ACKed. */
bool m1_esp_link_send_file_ok(const char *mac, uint8_t ptype,
                              const char *path, const char *remote_name);

/* Poll the ESP once for a pending inbound trigger/fragment; reassemble file
 * transfers and, when complete, write 0:/BadUSB/<name> and run it. Returns true
 * if a payload was executed this call. MUST be called from the main UI task so
 * the USB->HID switch works. */
bool m1_esp_link_rx_poll(void);

/* One receive poll: query the ESP trigger queue and run any pending trigger
 * (in the caller's task context). Returns true if one was dispatched. Call from
 * the UI/main task so the BadUSB USB-HID switch works. */
bool m1_esp_link_rx_poll(void);

/* On-device app entry point (menu: Sub-GHz -> "ESP Link"). */
void m1_esp_link_app_run(void);

#endif /* M1_ESP_LINK_H_ */
