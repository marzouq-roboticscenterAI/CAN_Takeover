#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Callback fired when the Arduino writes a status packet (16 bytes:
 * [state, score, foodCol, foodRow, 12-byte snake bitmap]). */
typedef void (*SnakeStatusCb)(const uint8_t* data, size_t len, void* ctx);

/* Start a BLE peripheral advertising `name` (e.g. "ARD_Snake") with:
 *   input  characteristic (NOTIFY) 9a1e0002 : Flipper -> Arduino tokens
 *   status characteristic (WRITE)  9a1e0003 : Arduino -> Flipper status
 * Returns true on success. */
bool ble_snake_start(const char* name, SnakeStatusCb cb, void* ctx);

/* Send a short token ("^","v","<",">","A","STR") to the Arduino via NOTIFY. */
void ble_snake_send(const char* token);

/* Stop advertising / drop the link and restore the Flipper's default BLE.
 * This is the "forget the Arduino on exit" behaviour. */
void ble_snake_stop(void);

/* True while an Arduino (central) is connected. */
bool ble_snake_linked(void);
