/*
 * ble_snake.c — custom BLE peripheral for flipper_snake.
 *
 * Advertises as "ARD_Snake" with one service (9a1e0001) and one NOTIFY
 * characteristic (9a1e0002). The Flipper pushes button tokens ("^ v < >","A")
 * by updating/notifying that characteristic; the Arduino (central) subscribes.
 * No writes / no flow control -> simple and central-friendly.
 *
 * bt_profile_start() handles root keys + GAP; bt_profile_restore_default()
 * restores the Flipper's normal BLE on exit ("forgets" the Arduino).
 */
#include <furi.h>
#include <string.h>
#include <bt/bt_service/bt.h>
#include <furi_ble/gatt.h>
#include <furi_ble/profile_interface.h>
#include <gap.h>
#include <furi_hal_version.h>
#include <ble/core/ble_defs.h>
#include "ble_snake.h"

#define TAG "flipper_snake_ble"

// ---- state ----
typedef struct {
    FuriHalBleProfileBase base;
    uint16_t svc_handle;
    BleGattCharacteristicInstance in_char;
} SnakeProfile;

static const FuriHalBleProfileTemplate snake_profile_template;  // fwd decl

static Bt* s_bt = NULL;
static SnakeProfile* s_prof = NULL;
static volatile bool s_linked = false;

static uint8_t s_tok[20] = {0};
static uint16_t s_tok_len = sizeof(s_tok);  // report MAX at char-creation time

// Characteristic value provider: at creation returns max len; later the current token.
static bool tok_data_cb(const void* context, const uint8_t** data, uint16_t* data_len) {
    UNUSED(context);
    // At characteristic creation this is called with data==NULL just to fetch
    // the (max) length, so guard the pointer.
    if(data) *data = s_tok;
    if(data_len) *data_len = s_tok_len;
    return false;  // no ownership transfer
}

// Characteristic descriptor (input, notify), UUID 9a1e0002 in little-endian.
static const BleGattCharacteristicParams in_char_params = {
    .name = "snake_in",
    .descriptor_params = NULL,
    .data.callback = {.fn = tok_data_cb, .context = NULL},
    .uuid.Char_UUID_128 =
        {0xab, 0x89, 0x67, 0x45, 0x23, 0x01, 0x5a, 0x8e, 0x3d, 0x4f, 0x2c, 0x1b, 0x02, 0x00, 0x1e, 0x9a},
    .data_prop_type = FlipperGattCharacteristicDataCallback,
    .is_variable = 1,
    .uuid_type = UUID_TYPE_128,
    .char_properties = CHAR_PROP_READ | CHAR_PROP_NOTIFY,
    .security_permissions = ATTR_PERMISSION_NONE,
    .gatt_evt_mask = GATT_DONT_NOTIFY_EVENTS,
};

// ---- profile template ----
static FuriHalBleProfileBase* snake_profile_start(FuriHalBleProfileParams params) {
    UNUSED(params);
    SnakeProfile* p = malloc(sizeof(SnakeProfile));
    memset(p, 0, sizeof(SnakeProfile));
    p->base.config = &snake_profile_template;  // required: identifies the profile

    Service_UUID_t svc_uuid;
    const uint8_t svc_le[16] =
        {0xab, 0x89, 0x67, 0x45, 0x23, 0x01, 0x5a, 0x8e, 0x3d, 0x4f, 0x2c, 0x1b, 0x01, 0x00, 0x1e, 0x9a};
    memcpy(svc_uuid.Service_UUID_128, svc_le, 16);

    ble_gatt_service_add(UUID_TYPE_128, &svc_uuid, PRIMARY_SERVICE, 8, &p->svc_handle);
    ble_gatt_characteristic_init(p->svc_handle, &in_char_params, &p->in_char);

    s_prof = p;
    return &p->base;
}

static void snake_profile_stop(FuriHalBleProfileBase* base) {
    SnakeProfile* p = (SnakeProfile*)base;
    ble_gatt_characteristic_delete(p->svc_handle, &p->in_char);
    ble_gatt_service_delete(p->svc_handle);
    free(p);
    s_prof = NULL;
}

static void snake_profile_gap_config(GapConfig* cfg, FuriHalBleProfileParams params) {
    UNUSED(params);
    // Mirror the firmware's serial profile config (known to advertise). A 128-bit
    // UUID here overflows the advert and silently kills advertising, so advertise
    // a small 16-bit UUID; the real 128-bit GATT service is found after connect.
    memset(cfg, 0, sizeof(GapConfig));
    strncpy(cfg->adv_name, "ARD_Snake", sizeof(cfg->adv_name) - 1);
    cfg->adv_service.UUID_Type = UUID_TYPE_16;
    cfg->adv_service.Service_UUID_16 = 0xF00D;
    cfg->appearance_char = 0x8600;
    cfg->bonding_mode = false;
    cfg->pairing_method = GapPairingNone;
    memcpy(cfg->mac_address, furi_hal_version_get_ble_mac(), GAP_MAC_ADDR_SIZE);
    cfg->conn_param.conn_int_min = 0x06;
    cfg->conn_param.conn_int_max = 0x24;
    cfg->conn_param.slave_latency = 0;
    cfg->conn_param.supervisor_timeout = 0;
}

static const FuriHalBleProfileTemplate snake_profile_template = {
    .start = snake_profile_start,
    .stop = snake_profile_stop,
    .get_gap_config = snake_profile_gap_config,
};

// ---- status changes ----
static void status_changed_cb(BtStatus status, void* ctx) {
    UNUSED(ctx);
    s_linked = (status == BtStatusConnected);
}

// ---- public API ----
bool ble_snake_start(const char* name, SnakeStatusCb cb, void* ctx) {
    UNUSED(name);
    UNUSED(cb);   // no Arduino->Flipper channel in this design
    UNUSED(ctx);
    s_linked = false;
    s_bt = furi_record_open(RECORD_BT);
    bt_set_status_changed_callback(s_bt, status_changed_cb, NULL);
    if(!bt_profile_start(s_bt, &snake_profile_template, NULL)) {
        FURI_LOG_E(TAG, "profile start failed");
        return false;
    }
    return true;
}

void ble_snake_send(const char* token) {
    if(!s_prof || !token) return;
    uint16_t n = (uint16_t)strlen(token);
    if(n > sizeof(s_tok)) n = sizeof(s_tok);
    memcpy(s_tok, token, n);
    s_tok_len = n;
    ble_gatt_characteristic_update(s_prof->svc_handle, &s_prof->in_char, NULL);
}

void ble_snake_stop(void) {
    if(s_bt) {
        bt_set_status_changed_callback(s_bt, NULL, NULL);
        bt_profile_restore_default(s_bt);
        furi_record_close(RECORD_BT);
        s_bt = NULL;
    }
    s_linked = false;
}

bool ble_snake_linked(void) {
    return s_linked;
}
