/*
 * flipper_snake — Flipper Zero app that controls the Arduino snake over BLE.
 *
 * The Flipper can only be a BLE *peripheral*, so it advertises as "ARD_Snake"
 * and the Arduino (snake_flipper.ino, a BLE central) connects to it.
 *   - Flipper buttons -> tokens ("^ v < >", "A") sent via NOTIFY to the Arduino.
 *   - Arduino -> Flipper: 16-byte game status (state/score/food/snake bitmap),
 *     shown as a live mini board on screen.
 *   - Exiting the app stops advertising / drops the link ("forgets" the Arduino).
 *
 * GUI/input here use stable APIs and work on stock or Momentum firmware.
 * The BLE peripheral bits live in ble_snake.c (finalize against your SDK).
 */
#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <string.h>
#include "ble_snake.h"

typedef struct {
    FuriMutex* mutex;
    bool ble_ok;                      // did bt_profile_start() succeed?
    bool up, down, left, right, ok;   // currently-held buttons
    uint8_t gstate;                   // 0 title, 1 play, 2 over
    uint8_t gscore, food_c, food_r;
    uint8_t bmp[12];                  // snake bitmap (96 cells)
} AppState;

static void on_status(const uint8_t* d, size_t len, void* ctx) {
    if(len < 16) return;
    AppState* s = ctx;
    furi_mutex_acquire(s->mutex, FuriWaitForever);
    s->gstate = d[0];
    s->gscore = d[1];
    s->food_c = d[2];
    s->food_r = d[3];
    for(int i = 0; i < 12; i++) s->bmp[i] = d[4 + i];
    furi_mutex_release(s->mutex);
}

static void draw_cb(Canvas* c, void* ctx) {
    AppState* s = ctx;
    furi_mutex_acquire(s->mutex, FuriWaitForever);
    canvas_clear(c);

    canvas_set_font(c, FontPrimary);
    canvas_draw_str(c, 2, 10, "ARD Snake");
    canvas_set_font(c, FontSecondary);
    const char* bstr = !s->ble_ok ? "BLE: start FAILED"
                       : (ble_snake_linked() ? "BLE: linked" : "BLE: advertising");
    canvas_draw_str(c, 2, 21, bstr);

    // D-pad + OK, highlighted while held
    int cx = 24, cy = 46;
    if(s->up)    canvas_draw_box(c, cx - 4, cy - 15, 8, 10);  else canvas_draw_frame(c, cx - 4, cy - 15, 8, 10);
    if(s->down)  canvas_draw_box(c, cx - 4, cy + 5, 8, 10);   else canvas_draw_frame(c, cx - 4, cy + 5, 8, 10);
    if(s->left)  canvas_draw_box(c, cx - 19, cy - 4, 10, 8);  else canvas_draw_frame(c, cx - 19, cy - 4, 10, 8);
    if(s->right) canvas_draw_box(c, cx + 9, cy - 4, 10, 8);   else canvas_draw_frame(c, cx + 9, cy - 4, 10, 8);
    if(s->ok)    canvas_draw_box(c, cx - 4, cy - 4, 8, 8);    else canvas_draw_frame(c, cx - 4, cy - 4, 8, 8);

    // Mini board (12x8) mirroring the LED matrix
    int bx = 62, by = 24, cs = 4;
    canvas_draw_frame(c, bx - 1, by - 1, 12 * cs + 2, 8 * cs + 2);
    for(int i = 0; i < 96; i++) {
        if(s->bmp[i >> 3] & (1 << (i & 7))) {
            canvas_draw_box(c, bx + (i % 12) * cs, by + (i / 12) * cs, cs - 1, cs - 1);
        }
    }
    if(s->gstate == 1) canvas_draw_frame(c, bx + s->food_c * cs, by + s->food_r * cs, cs, cs);

    char line[24];
    const char* st = s->gstate == 0 ? "title" : (s->gstate == 2 ? "over" : "play");
    snprintf(line, sizeof(line), "%s  sc %d", st, s->gscore);
    canvas_draw_str(c, 62, 63, line);

    furi_mutex_release(s->mutex);
}

static void input_cb(InputEvent* e, void* ctx) {
    FuriMessageQueue* q = ctx;
    furi_message_queue_put(q, e, FuriWaitForever);
}

int32_t flipper_snake_app(void* p) {
    UNUSED(p);
    AppState* s = malloc(sizeof(AppState));
    memset(s, 0, sizeof(AppState));
    s->mutex = furi_mutex_alloc(FuriMutexTypeNormal);

    FuriMessageQueue* q = furi_message_queue_alloc(8, sizeof(InputEvent));
    ViewPort* vp = view_port_alloc();
    view_port_draw_callback_set(vp, draw_cb, s);
    view_port_input_callback_set(vp, input_cb, q);
    Gui* gui = furi_record_open(RECORD_GUI);

    s->ble_ok = ble_snake_start("ARD_Snake", on_status, s);  // advertise; Arduino connects
    gui_add_view_port(gui, vp, GuiLayerFullscreen);

    InputEvent e;
    bool running = true;
    while(running) {
        if(furi_message_queue_get(q, &e, 100) == FuriStatusOk) {
            const char* tok = NULL;
            if(e.type == InputTypePress || e.type == InputTypeRelease) {
                bool on = (e.type == InputTypePress);
                furi_mutex_acquire(s->mutex, FuriWaitForever);
                switch(e.key) {
                    case InputKeyUp:    s->up = on;    if(on) tok = "^"; break;
                    case InputKeyDown:  s->down = on;  if(on) tok = "v"; break;
                    case InputKeyLeft:  s->left = on;  if(on) tok = "<"; break;
                    case InputKeyRight: s->right = on; if(on) tok = ">"; break;
                    case InputKeyOk:    s->ok = on;    if(on) tok = "A"; break;
                    default: break;
                }
                furi_mutex_release(s->mutex);
                if(tok) ble_snake_send(tok);
            } else if(e.type == InputTypeShort && e.key == InputKeyBack) {
                running = false;  // Back exits
            }
        }
        view_port_update(vp);  // also refreshes the board as status arrives
    }

    ble_snake_stop();  // stop advertising / drop link -> "forget" the Arduino

    gui_remove_view_port(gui, vp);
    furi_record_close(RECORD_GUI);
    view_port_free(vp);
    furi_message_queue_free(q);
    furi_mutex_free(s->mutex);
    free(s);
    return 0;
}
