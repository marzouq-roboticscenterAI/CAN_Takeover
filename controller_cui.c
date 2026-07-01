/*
 * controller_cui.c — read a game controller (Linux evdev), draw it as a live
 * console UI, and forward inputs to the Arduino R4 over USB serial.
 *
 * Pairs with the snake_led sketch (serial). Pure C, no external libraries.
 *
 *   Build:  gcc -O2 -o controller_cui controller_cui.c
 *   Run :   ./controller_cui [/dev/ttyACM0]
 *           (auto-detects the controller; serial port optional / auto-found)
 *
 * Tokens sent (newline-terminated), matching the snake sketches:
 *   ^ v < >   directions (prefixed L/R/D by source: "L^", "D<", ...)
 *   A         A button (start / restart)
 *   STR       Start button
 *
 * Run from a NATIVE terminal (needs /dev/input + /dev/ttyACM* access; be in
 * the 'input' and 'dialout' groups).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <poll.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <linux/input.h>

#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x) - 1) / BITS_PER_LONG) + 1)
#define test_bit(bit, arr) (((arr)[(bit) / BITS_PER_LONG] >> ((bit) % BITS_PER_LONG)) & 1)

#define THRESH 0.6

static volatile int g_run = 1;
static void on_sigint(int s) { (void)s; g_run = 0; }

/* ---- controller state ---- */
typedef struct {
    int a, b, x, y, lb, rb, l3, r3, back, start, guide;
    double lx, ly, rx, ry;   /* -1..1 */
    double lt, rt;           /* 0..1 */
    int dpx, dpy;            /* -1/0/1 */
    const char *ldir, *rdir; /* last sent stick dir, for change detection */
} State;

/* ---- axis ranges ---- */
typedef struct { int min, max; int have; } Axis;
static Axis ax_x, ax_y, ax_rx, ax_ry, ax_z, ax_rz;

static double norm_c(Axis *a, int v) {           /* centered -1..1 */
    if (!a->have || a->max == a->min) return 0.0;
    double c = (a->max + a->min) / 2.0, h = (a->max - a->min) / 2.0;
    double r = (v - c) / h;
    if (r < -1) r = -1;
    if (r > 1) r = 1;
    return r;
}
static double norm_t(Axis *a, int v) {           /* 0..1 */
    if (!a->have || a->max == a->min) return 0.0;
    double r = (double)(v - a->min) / (a->max - a->min);
    if (r < 0) r = 0;
    if (r > 1) r = 1;
    return r;
}

/* ---- find a gamepad under /dev/input ---- */
static int find_gamepad(char *name, size_t namelen) {
    DIR *d = opendir("/dev/input");
    if (!d) return -1;
    struct dirent *e;
    int best = -1;
    while ((e = readdir(d))) {
        if (strncmp(e->d_name, "event", 5) != 0) continue;
        char path[300];
        snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        unsigned long keys[NBITS(KEY_MAX)];
        memset(keys, 0, sizeof(keys));
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) >= 0) {
            if (test_bit(BTN_GAMEPAD, keys) || test_bit(BTN_SOUTH, keys)) {
                if (name) { name[0] = 0; ioctl(fd, EVIOCGNAME(namelen), name); }
                best = fd;
                break;
            }
        }
        close(fd);
    }
    closedir(d);
    return best;
}

static void load_axis(int fd, int code, Axis *a) {
    struct input_absinfo info;
    if (ioctl(fd, EVIOCGABS(code), &info) >= 0) { a->min = info.minimum; a->max = info.maximum; a->have = 1; }
    else a->have = 0;
}

/* ---- serial ---- */
static int open_serial(const char *port) {
    int fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) return -1;
    struct termios t;
    if (tcgetattr(fd, &t) != 0) { close(fd); return -1; }
    cfmakeraw(&t);
    cfsetispeed(&t, B115200);
    cfsetospeed(&t, B115200);
    t.c_cflag |= (CLOCAL | CREAD);
    t.c_cflag &= ~CRTSCTS;
    tcsetattr(fd, TCSANOW, &t);
    return fd;
}
static void send_tok(int sfd, const char *tok) {
    if (sfd < 0) return;
    char buf[24];
    int n = snprintf(buf, sizeof(buf), "%s\n", tok);
    (void)!write(sfd, buf, n);
}

/* ---- stick direction -> token on change ---- */
static double fabsd(double v) { return v < 0 ? -v : v; }
static const char *dir_of(double x, double y) {
    if (fabsd(y) >= fabsd(x) && fabsd(y) > THRESH) return y < 0 ? "^" : "v";
    if (fabsd(x) > THRESH) return x < 0 ? "<" : ">";
    return NULL;
}

static void stick_token(int sfd, State *st, char side) {
    double x = side == 'L' ? st->lx : st->rx;
    double y = side == 'L' ? st->ly : st->ry;
    const char *d = dir_of(x, y);
    const char **slot = side == 'L' ? &st->ldir : &st->rdir;
    if (d != *slot) {
        *slot = d;
        if (d) { char t[3] = {side, d[0], 0}; send_tok(sfd, t); }
    }
}

/* ---- rendering ---- */
#define ON  "\033[7m"
#define OFF "\033[0m"
static void btn(char *out, const char *lbl, int pressed) {
    if (pressed) sprintf(out, ON "(%s)" OFF, lbl);
    else         sprintf(out, "(%s)", lbl);
}
static void bar(char *out, double v) {
    int n = (int)(v * 10 + 0.5), i;
    out[0] = '[';
    for (i = 0; i < 10; i++) out[1 + i] = i < n ? '#' : ' ';
    out[11] = ']'; out[12] = 0;
}
static const char *stickglyph(double x, double y) {
    const char *d = dir_of(x, y);
    return d ? d : "o";
}

static void render(State *st, const char *dev, const char *sport, int slink) {
    char bY[32], bX[32], bB[32], bA[32], bLB[32], bRB[32], bBk[32], bSt[32], lt[16], rt[16];
    btn(bY, "Y", st->y); btn(bX, "X", st->x); btn(bB, "B", st->b); btn(bA, "A", st->a);
    btn(bLB, "LB", st->lb); btn(bRB, "RB", st->rb);
    btn(bBk, "Back", st->back); btn(bSt, "Start", st->start);
    bar(lt, st->lt); bar(rt, st->rt);
    const char *dpu = st->dpy < 0 ? ON "^" OFF : "^";
    const char *dpd = st->dpy > 0 ? ON "v" OFF : "v";
    const char *dpl = st->dpx < 0 ? ON "<" OFF : "<";
    const char *dpr = st->dpx > 0 ? ON ">" OFF : ">";

    printf("\033[H");   /* cursor home; \033[K clears each line to EOL */
    printf("\033[K CAN_Takeover — Controller (CUI)      serial: %s %s\n",
           slink ? "OK" : "--", sport);
    printf("\033[K Device: %s\n\033[K\n", dev[0] ? dev : "(none)");
    printf("\033[K   LT %s %s         %s RT %s\n", lt, bLB, bRB, rt);
    printf("\033[K\n");
    printf("\033[K                       %s\n", bY);
    printf("\033[K                 %s     %s        %s   %s\n", bX, bB, bBk, bSt);
    printf("\033[K                       %s\n", bA);
    printf("\033[K\n");
    printf("\033[K   Left Stick:%s%s%s     D-Pad: %s %s %s %s     Right Stick:%s%s%s\n",
           st->l3 ? ON : "", stickglyph(st->lx, st->ly), st->l3 ? OFF : "",
           dpu, dpd, dpl, dpr,
           st->r3 ? ON : "", stickglyph(st->rx, st->ry), st->r3 ? OFF : "");
    printf("\033[K\n\033[K Ctrl+C to quit.\033[K\n");
    fflush(stdout);
}

int main(int argc, char **argv) {
    char name[256] = {0};
    int evfd = find_gamepad(name, sizeof(name));
    if (evfd < 0) { fprintf(stderr, "No controller found (connect it; be in 'input' group).\n"); return 1; }

    load_axis(evfd, ABS_X, &ax_x);  load_axis(evfd, ABS_Y, &ax_y);
    load_axis(evfd, ABS_RX, &ax_rx); load_axis(evfd, ABS_RY, &ax_ry);
    load_axis(evfd, ABS_Z, &ax_z);  load_axis(evfd, ABS_RZ, &ax_rz);

    const char *port = (argc > 1) ? argv[1] : "/dev/ttyACM0";
    int sfd = open_serial(port);

    signal(SIGINT, on_sigint);
    printf("\033[2J\033[?25l");   /* clear screen, hide cursor */

    State st; memset(&st, 0, sizeof(st));
    struct pollfd pfd = { .fd = evfd, .events = POLLIN };

    while (g_run) {
        int pr = poll(&pfd, 1, 33);   /* ~30 fps */
        if (pr > 0 && (pfd.revents & POLLIN)) {
            struct input_event ev;
            while (read(evfd, &ev, sizeof(ev)) == (ssize_t)sizeof(ev)) {
                if (ev.type == EV_KEY) {
                    int v = ev.value ? 1 : 0;
                    switch (ev.code) {
                        case BTN_SOUTH: st.a = v; if (ev.value == 1) send_tok(sfd, "A"); break;
                        case BTN_EAST:  st.b = v; break;
                        case BTN_NORTH: st.y = v; break;
                        case BTN_WEST:  st.x = v; break;
                        case BTN_TL:    st.lb = v; break;
                        case BTN_TR:    st.rb = v; break;
                        case BTN_THUMBL: st.l3 = v; break;
                        case BTN_THUMBR: st.r3 = v; break;
                        case BTN_SELECT: st.back = v; break;
                        case BTN_START: st.start = v; if (ev.value == 1) send_tok(sfd, "STR"); break;
                        case BTN_MODE:  st.guide = v; break;
                        case BTN_DPAD_UP:    st.dpy = v ? -1 : 0; if (v) send_tok(sfd, "D^"); break;
                        case BTN_DPAD_DOWN:  st.dpy = v ?  1 : 0; if (v) send_tok(sfd, "Dv"); break;
                        case BTN_DPAD_LEFT:  st.dpx = v ? -1 : 0; if (v) send_tok(sfd, "D<"); break;
                        case BTN_DPAD_RIGHT: st.dpx = v ?  1 : 0; if (v) send_tok(sfd, "D>"); break;
                    }
                } else if (ev.type == EV_ABS) {
                    switch (ev.code) {
                        case ABS_X:  st.lx = norm_c(&ax_x, ev.value);  stick_token(sfd, &st, 'L'); break;
                        case ABS_Y:  st.ly = norm_c(&ax_y, ev.value);  stick_token(sfd, &st, 'L'); break;
                        case ABS_RX: st.rx = norm_c(&ax_rx, ev.value); stick_token(sfd, &st, 'R'); break;
                        case ABS_RY: st.ry = norm_c(&ax_ry, ev.value); stick_token(sfd, &st, 'R'); break;
                        case ABS_Z:  st.lt = norm_t(&ax_z, ev.value); break;
                        case ABS_RZ: st.rt = norm_t(&ax_rz, ev.value); break;
                        case ABS_HAT0X:
                            st.dpx = ev.value; if (ev.value < 0) send_tok(sfd, "D<"); else if (ev.value > 0) send_tok(sfd, "D>"); break;
                        case ABS_HAT0Y:
                            st.dpy = ev.value; if (ev.value < 0) send_tok(sfd, "D^"); else if (ev.value > 0) send_tok(sfd, "Dv"); break;
                    }
                }
            }
        }
        render(&st, name, port, sfd >= 0);
    }

    printf("\033[?25h\033[2J\033[H");   /* show cursor, clear */
    if (sfd >= 0) close(sfd);
    close(evfd);
    return 0;
}
