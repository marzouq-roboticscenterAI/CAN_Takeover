/*
  snake_ble  —  Arduino UNO R4 WiFi  (BLE PERIPHERAL)

  The R4 uses its onboard ESP32 BLE (via ArduinoBLE) to advertise as "SnakeR4"
  with one writable characteristic. The laptop (controller_to_ble.py, a BLE
  central) reads the wired 8BitDo and WRITES short tokens to it:
      ^ v < >   directions (d-pad or either stick)
      A / STR   start / restart

  USB-C is power + flashing only; all input arrives over BLE.

  Requires: "Arduino UNO R4 Boards" core + "ArduinoGraphics" + "ArduinoBLE".
  If the matrix shows scrolling "BLE?" at boot, update the R4's WiFi/BLE
  firmware once via the IDE (Tools -> Firmware Updater).
*/
#include <ArduinoBLE.h>
#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"

#define SVC_UUID "9a1e0001-1b2c-4f3d-8e5a-0123456789ab"
#define CHR_UUID "9a1e0002-1b2c-4f3d-8e5a-0123456789ab"

ArduinoLEDMatrix matrix;
BLEService        snakeSvc(SVC_UUID);
BLECharacteristic inputChar(CHR_UUID, BLEWrite | BLEWriteWithoutResponse, 20);

bool linked = false;

const int W = 12, H = 8;
const int MAXLEN = W * H;
enum Mode { TITLE, PLAYING };
Mode mode;

int  bodyCol[MAXLEN + 1], bodyRow[MAXLEN + 1];
int  snakeLen, dirCol, dirRow, nextCol, nextRow, foodCol, foodRow;
unsigned long lastStep;
int  stepMs;
bool gameOver;
uint8_t frame[H][W];

void clearFrame() { for (int r = 0; r < H; r++) for (int c = 0; c < W; c++) frame[r][c] = 0; }

void spawnFood() {
  while (true) {
    int c = random(W), r = random(H);
    bool on = false;
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
  if (nc == -dirCol && nr == -dirRow) return;
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
  if ((millis() / 200) % 2 == 0) frame[foodRow][foodCol] = 1;
  matrix.renderBitmap(frame, H, W);
}
void renderTitle() {
  clearFrame();
  frame[4][2] = 1; frame[4][3] = 1; frame[4][4] = 1; frame[3][4] = 1;
  if ((millis() / 300) % 2 == 0) frame[2][9] = 1;
  matrix.renderBitmap(frame, H, W);
}
void renderWaiting() {
  clearFrame();
  if ((millis() / 250) % 2 == 0) { frame[0][0] = 1; frame[0][W-1] = 1; frame[H-1][0] = 1; frame[H-1][W-1] = 1; }
  matrix.renderBitmap(frame, H, W);
}
void scrollText(const char *s) {
  matrix.beginDraw(); matrix.clear(); matrix.stroke(255, 255, 255);
  matrix.textScrollSpeed(70); matrix.textFont(Font_4x6);
  matrix.beginText(12, 1, 255, 255, 255); matrix.print(s); matrix.endText(SCROLL_LEFT);
  matrix.endDraw();
}
void showScore() { char sc[8]; snprintf(sc, sizeof(sc), "%d", snakeLen - 3); scrollText(sc); }
void gameOverFx() {
  for (int k = 0; k < 3; k++) {
    for (int r = 0; r < H; r++) for (int c = 0; c < W; c++) frame[r][c] = 1;
    matrix.renderBitmap(frame, H, W); delay(150);
    clearFrame(); matrix.renderBitmap(frame, H, W); delay(150);
  }
  showScore();
}

void handleToken(const char *tok) {
  int n = strlen(tok);
  if (n == 0) return;
  if (mode == TITLE) { if (!strcmp(tok, "A") || !strcmp(tok, "STR")) resetGame(); }
  else {
    if (gameOver) { if (!strcmp(tok, "A") || !strcmp(tok, "STR")) resetGame(); }
    else setDir(tok[n - 1]);
  }
}

void onConnect(BLEDevice d)    { linked = true; mode = TITLE; }
void onDisconnect(BLEDevice d) { linked = false; }

void setup() {
  matrix.begin();
  randomSeed(analogRead(A0));
  mode = TITLE;
  if (!BLE.begin()) { scrollText("BLE?"); while (1) delay(1000); }
  BLE.setLocalName("SnakeR4");
  BLE.setDeviceName("SnakeR4");
  BLE.setAdvertisedService(snakeSvc);
  snakeSvc.addCharacteristic(inputChar);
  BLE.addService(snakeSvc);
  BLE.setEventHandler(BLEConnected, onConnect);
  BLE.setEventHandler(BLEDisconnected, onDisconnect);
  BLE.advertise();
}

void loop() {
  BLE.poll();                                   // service BLE + fire handlers

  if (inputChar.written()) {
    uint8_t b[24];
    int n = inputChar.valueLength();
    if (n > 23) n = 23;
    memcpy(b, inputChar.value(), n);
    b[n] = 0;
    handleToken((char *)b);
  }

  if (!linked) { renderWaiting(); delay(20); return; }

  if (mode == TITLE) { renderTitle(); delay(20); }
  else {
    if (!gameOver) {
      if (millis() - lastStep >= (unsigned long)stepMs) {
        lastStep = millis(); step();
        if (gameOver) gameOverFx();
      }
      renderGame(); delay(20);
    } else delay(50);
  }
}
