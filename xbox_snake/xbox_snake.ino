/*
  xbox_snake — Arduino UNO R4 WiFi (BLE CENTRAL)

  Pairs DIRECTLY with an Xbox Wireless (BLE) controller using the patched
  ArduinoBLE (central-side LESC initiator) and plays Snake on the onboard 12x8
  LED matrix. No laptop, no ESP32 bridge — the R4 does the BLE pairing itself.

  Input: the Xbox streams 16-byte HID reports on GATT handle 0x1E (no discovery
  needed). We grab them via ATT's raw-notify hook and parse EVERYTHING:
    [0-1]=LX [2-3]=LY [4-5]=RX [6-7]=RY (center ~0x8000)
    [8-9]=LT [10-11]=RT (0..0x3FF)
    [12]=dpad hat (1=U,3=R,5=D,7=L; 2/4/6/8 diag; 0=none)
    [13]=A/B/X/Y/LB/RB   [14]=View/Menu/L3/R3   [15]=extra
  Snake uses the d-pad (or left stick) for direction and A to start/restart.

  Prereqs: patched ArduinoBLE (startPairing + raw-notify hook), ArduinoGraphics.
  If BLE.begin fails, fully power-cycle the R4 (USB unplug). If it fails across a
  power cycle, reflash the ESP32-S3 firmware (Arduino IDE -> Firmware Updater).
*/
#include <ArduinoBLE.h>
#include <utility/ATT.h>          // ATT.setRawNotifyHandler()
#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"

// ---------------- Xbox controller state ----------------
#define XBOX_REPORT_HANDLE 0x1E
// buttons byte 13
#define BTN_A  0x01
#define BTN_B  0x02
#define BTN_X  0x08
#define BTN_Y  0x10
#define BTN_LB 0x40
#define BTN_RB 0x80
// buttons byte 14 (bit positions vary by firmware; exposed for future use)
#define BTN_VIEW 0x04
#define BTN_MENU 0x08
#define BTN_L3   0x20
#define BTN_R3   0x40

struct XboxState {
  uint16_t lx, ly, rx, ry;   // sticks, center ~0x8000
  uint16_t lt, rt;           // triggers 0..0x3FF
  uint8_t  dpad;             // 0=none,1=U,2=UR,3=R,4=DR,5=D,6=DL,7=L,8=UL
  uint8_t  btn0, btn1, btn2; // button bitmasks
  volatile bool fresh;
};
volatile XboxState xb = {0x8000,0x8000,0x8000,0x8000,0,0,0,0,0,0,false};

// Raw-notify hook: parse the Xbox HID report. Runs inside BLE.poll().
void onXboxReport(uint16_t handle, const uint8_t* r, uint8_t n) {
  if (handle != XBOX_REPORT_HANDLE || n < 15) return;
  xb.lx = r[0] | (r[1] << 8);  xb.ly = r[2] | (r[3] << 8);
  xb.rx = r[4] | (r[5] << 8);  xb.ry = r[6] | (r[7] << 8);
  xb.lt = r[8] | (r[9] << 8);  xb.rt = r[10] | (r[11] << 8);
  xb.dpad = r[12];
  xb.btn0 = r[13];
  xb.btn1 = (n > 14) ? r[14] : 0;
  xb.btn2 = (n > 15) ? r[15] : 0;
  xb.fresh = true;
}

// Direction char (^v<>) from d-pad, else left stick. 0 = none.
char xboxDir() {
  switch (xb.dpad) {
    case 1: return '^';
    case 5: return 'v';
    case 7: return '<';
    case 3: return '>';
    case 8: case 2: return '^';   // up-diagonals -> up
    case 6: case 4: return 'v';   // down-diagonals -> down
  }
  int dx = (int)xb.lx - 0x8000;
  int dy = (int)xb.ly - 0x8000;
  const int TH = 12000;
  if (abs(dx) >= abs(dy)) { if (dx > TH) return '>'; if (dx < -TH) return '<'; }
  else                    { if (dy > TH) return 'v'; if (dy < -TH) return '^'; }  // stick: down = larger Y
  return 0;
}

// ---------------- Snake game (LED matrix) ----------------
ArduinoLEDMatrix matrix;
const int W = 12, H = 8, MAXLEN = W * H;
enum Mode { WAITING, TITLE, PLAYING };
Mode mode = WAITING;

int bodyCol[MAXLEN + 1], bodyRow[MAXLEN + 1];
int snakeLen, dirCol, dirRow, nextCol, nextRow, foodCol, foodRow;
unsigned long lastStep;
int stepMs;
bool gameOver;
uint8_t frame[H][W];

void clearFrame() { for (int r = 0; r < H; r++) for (int c = 0; c < W; c++) frame[r][c] = 0; }

void spawnFood() {
  while (true) {
    int c = random(W), r = random(H); bool on = false;
    for (int i = 0; i < snakeLen; i++) if (bodyCol[i] == c && bodyRow[i] == r) { on = true; break; }
    if (!on) { foodCol = c; foodRow = r; return; }
  }
}
void resetGame() {
  snakeLen = 3;
  bodyCol[0] = 5; bodyRow[0] = 4; bodyCol[1] = 4; bodyRow[1] = 4; bodyCol[2] = 3; bodyRow[2] = 4;
  dirCol = 1; dirRow = 0; nextCol = 1; nextRow = 0;
  stepMs = 350; gameOver = false; spawnFood(); lastStep = millis(); mode = PLAYING;
}
void setDir(char a) {
  int nc = dirCol, nr = dirRow;
  if      (a == '^') { nc = 0;  nr = -1; }
  else if (a == 'v') { nc = 0;  nr = 1;  }
  else if (a == '<') { nc = -1; nr = 0;  }
  else if (a == '>') { nc = 1;  nr = 0;  }
  else return;
  if (nc == -dirCol && nr == -dirRow) return;  // no 180
  nextCol = nc; nextRow = nr;
}
void step() {
  dirCol = nextCol; dirRow = nextRow;
  int hc = bodyCol[0] + dirCol, hr = bodyRow[0] + dirRow;
  if (hc < 0 || hc >= W || hr < 0 || hr >= H) { gameOver = true; return; }
  bool grow = (hc == foodCol && hr == foodRow);
  for (int i = 0; i < snakeLen; i++)
    if (bodyCol[i] == hc && bodyRow[i] == hr) {
      if (i == snakeLen - 1 && !grow) continue;
      gameOver = true; return;
    }
  int top = grow ? snakeLen : (snakeLen - 1);
  for (int i = top; i > 0; i--) { bodyCol[i] = bodyCol[i - 1]; bodyRow[i] = bodyRow[i - 1]; }
  bodyCol[0] = hc; bodyRow[0] = hr;
  if (grow) { if (snakeLen < MAXLEN) snakeLen++; spawnFood(); if (stepMs > 120) stepMs -= 12; }
}
void renderGame() {
  clearFrame();
  for (int i = 0; i < snakeLen; i++) frame[bodyRow[i]][bodyCol[i]] = 1;
  frame[foodRow][foodCol] = 1;   // food stays solid (no blink)
  matrix.renderBitmap(frame, H, W);
}
void renderTitle() {
  clearFrame();
  frame[4][2] = 1; frame[4][3] = 1; frame[4][4] = 1; frame[3][4] = 1;   // little snake
  if ((millis() / 300) % 2 == 0) frame[2][9] = 1;                        // blinking food
  matrix.renderBitmap(frame, H, W);
}
void renderWaiting() {
  clearFrame();
  if ((millis() / 250) % 2 == 0) { frame[0][0]=1; frame[0][W-1]=1; frame[H-1][0]=1; frame[H-1][W-1]=1; }
  matrix.renderBitmap(frame, H, W);
}
void scrollText(const char* s) {
  matrix.beginDraw(); matrix.clear(); matrix.stroke(255,255,255);
  matrix.textScrollSpeed(70); matrix.textFont(Font_4x6);
  matrix.beginText(12, 1, 255, 255, 255); matrix.print(s); matrix.endText(SCROLL_LEFT);
  matrix.endDraw();
}
void gameOverFx() {
  for (int k = 0; k < 3; k++) {
    for (int r=0;r<H;r++) for (int c=0;c<W;c++) frame[r][c]=1; matrix.renderBitmap(frame,H,W); delay(150);
    clearFrame(); matrix.renderBitmap(frame,H,W); delay(150);
  }
  char sc[8]; snprintf(sc, sizeof(sc), "%d", snakeLen - 3); scrollText(sc);
}

// ---------------- BLE central / Xbox link ----------------
BLEDevice xbox;
bool linked = false;

bool isXboxAdv(BLEDevice& d) {
  String n = d.localName();
  if (n.indexOf("Xbox") >= 0) return true;
  for (int i = 0; i < d.advertisedServiceUuidCount(); i++) {
    String u = d.advertisedServiceUuid(i); u.toLowerCase();
    if (u.indexOf("1812") >= 0) return true;
  }
  return false;
}

void setup() {
  Serial.begin(115200);
  matrix.begin();
  randomSeed(analogRead(A0));

  bool ok = false;
  for (int i = 1; i <= 10 && !ok; i++) { ok = BLE.begin(); if (!ok) { BLE.end(); delay(1200); } }
  if (!ok) { scrollText("BLE?"); while (1) delay(1000); }

  BLE.setPairable(true);                 // allow LESC bonding
  ATT.setRawNotifyHandler(onXboxReport); // HID reports bypass GATT discovery
  BLE.scan();
  mode = WAITING;
}

void loop() {
  if (!linked) {
    renderWaiting();
    BLEDevice d = BLE.available();
    if (d && isXboxAdv(d)) {
      BLE.stopScan();
      if (d.connect()) {
        unsigned long tp = millis();
        while (d.connected() && millis() - tp < 9000) { BLE.poll(); if (BLE.paired()) break; delay(3); }
        if (BLE.paired()) { xbox = d; linked = true; mode = TITLE; }
        else { if (d.connected()) d.disconnect(); BLE.scan(); }
      } else { BLE.scan(); }
    }
    return;
  }

  BLE.poll();                                  // pumps HID reports -> onXboxReport
  if (!xbox.connected()) { linked = false; mode = WAITING; BLE.scan(); return; }

  // A button starts / restarts (edge-triggered)
  static bool prevA = false;
  bool a = (xb.btn0 & BTN_A) != 0;
  if (a && !prevA && (mode == TITLE || gameOver)) resetGame();
  prevA = a;

  if (mode == PLAYING && !gameOver) {
    char dir = xboxDir();
    if (dir) setDir(dir);
  }

  if (mode == TITLE) { renderTitle(); delay(15); }
  else if (!gameOver) {
    if (millis() - lastStep >= (unsigned long)stepMs) {
      lastStep = millis(); step();
      if (gameOver) gameOverFx();
    }
    renderGame(); delay(15);
  } else {
    renderGame(); delay(40);   // frozen board until A
  }
}
