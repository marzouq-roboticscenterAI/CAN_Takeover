/*
  snake_led  —  Arduino UNO R4 WiFi

  Snake on the 12x8 LED matrix. Boots into a TITLE / "setup" screen and waits
  for START before the game begins.

  Direction comes over USB serial from the controller bridge
  (controller_to_arduino.py): tokens whose LAST character is an arrow steer:
      ^ = up   v = down   < = left   > = right
  so the d-pad ("D^"...) AND either analog stick ("L^"/"R>"...) all work.

  Controls:
    START or A  -> begin a game (from the title screen, or after game over)

  Requirements (already installed): "Arduino UNO R4 Boards" core + "ArduinoGraphics".
*/
#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"

ArduinoLEDMatrix matrix;

const int W = 12;        // columns
const int H = 8;         // rows
const int MAXLEN = W * H;

enum Mode { TITLE, PLAYING };
Mode mode;
bool titleIntro;         // scroll the name once when entering the title

int  bodyCol[MAXLEN + 1];
int  bodyRow[MAXLEN + 1];
int  snakeLen;
int  dirCol, dirRow;
int  nextCol, nextRow;
int  foodCol, foodRow;
unsigned long lastStep;
int  stepMs;
bool gameOver;

String buf;
uint8_t frame[H][W];

void clearFrame() {
  for (int r = 0; r < H; r++) for (int c = 0; c < W; c++) frame[r][c] = 0;
}

void spawnFood() {
  while (true) {
    int c = random(W), r = random(H);
    bool onSnake = false;
    for (int i = 0; i < snakeLen; i++)
      if (bodyCol[i] == c && bodyRow[i] == r) { onSnake = true; break; }
    if (!onSnake) { foodCol = c; foodRow = r; return; }
  }
}

void resetGame() {
  snakeLen = 3;
  bodyCol[0] = 5; bodyRow[0] = 4;
  bodyCol[1] = 4; bodyRow[1] = 4;
  bodyCol[2] = 3; bodyRow[2] = 4;
  dirCol = 1; dirRow = 0;
  nextCol = 1; nextRow = 0;
  stepMs = 350;
  gameOver = false;
  spawnFood();
  lastStep = millis();
  mode = PLAYING;
}

void setDir(char a) {
  int nc = dirCol, nr = dirRow;
  if      (a == '^') { nc = 0;  nr = -1; }
  else if (a == 'v') { nc = 0;  nr = 1;  }
  else if (a == '<') { nc = -1; nr = 0;  }
  else if (a == '>') { nc = 1;  nr = 0;  }
  else return;
  if (nc == -dirCol && nr == -dirRow) return;   // no 180-degree reversal
  nextCol = nc; nextRow = nr;
}

void step() {
  dirCol = nextCol; dirRow = nextRow;
  int hc = bodyCol[0] + dirCol;
  int hr = bodyRow[0] + dirRow;

  if (hc < 0 || hc >= W || hr < 0 || hr >= H) { gameOver = true; return; }  // wall

  bool grow = (hc == foodCol && hr == foodRow);

  for (int i = 0; i < snakeLen; i++) {            // self collision
    if (bodyCol[i] == hc && bodyRow[i] == hr) {
      if (i == snakeLen - 1 && !grow) continue;   // tail moves out of the way
      gameOver = true; return;
    }
  }

  int shiftTop = grow ? snakeLen : (snakeLen - 1);
  for (int i = shiftTop; i > 0; i--) { bodyCol[i] = bodyCol[i - 1]; bodyRow[i] = bodyRow[i - 1]; }
  bodyCol[0] = hc; bodyRow[0] = hr;

  if (grow) {
    if (snakeLen < MAXLEN) snakeLen++;
    spawnFood();
    if (stepMs > 120) stepMs -= 12;
  }
}

void renderGame() {
  clearFrame();
  for (int i = 0; i < snakeLen; i++) frame[bodyRow[i]][bodyCol[i]] = 1;
  if ((millis() / 200) % 2 == 0) frame[foodRow][foodCol] = 1;   // blink food
  matrix.renderBitmap(frame, H, W);
}

void scrollText(const char *s) {
  matrix.beginDraw();
  matrix.clear();
  matrix.stroke(255, 255, 255);
  matrix.textScrollSpeed(70);
  matrix.textFont(Font_4x6);
  matrix.beginText(12, 1, 255, 255, 255);
  matrix.print(s);
  matrix.endText(SCROLL_LEFT);
  matrix.endDraw();
}

// Static title screen: a little preview snake + a blinking "food" pixel.
void renderTitle() {
  clearFrame();
  frame[4][2] = 1; frame[4][3] = 1; frame[4][4] = 1; frame[3][4] = 1;  // preview snake
  if ((millis() / 300) % 2 == 0) frame[2][9] = 1;                      // blink food
  matrix.renderBitmap(frame, H, W);
}

void showScore() {
  char sc[8];
  snprintf(sc, sizeof(sc), "%d", snakeLen - 3);
  scrollText(sc);
}

void gameOverFx() {
  for (int k = 0; k < 3; k++) {
    for (int r = 0; r < H; r++) for (int c = 0; c < W; c++) frame[r][c] = 1;
    matrix.renderBitmap(frame, H, W); delay(150);
    clearFrame();
    matrix.renderBitmap(frame, H, W); delay(150);
  }
  showScore();
}

void enterTitle() { mode = TITLE; titleIntro = true; }

void setup() {
  Serial.begin(115200);
  matrix.begin();
  randomSeed(analogRead(A0));
  enterTitle();                 // boot into the title / setup screen
}

void loop() {
  // --- input ---
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n') {
      buf.trim();
      if (buf.length() > 0) {
        if (mode == TITLE) {
          if (buf == "A" || buf == "STR") resetGame();
        } else {                         // PLAYING
          if (gameOver) { if (buf == "A" || buf == "STR") resetGame(); }
          else            setDir(buf.charAt(buf.length() - 1));
        }
      }
      buf = "";
    } else if (c != '\r') {
      buf += c;
      if (buf.length() > 16) buf = "";
    }
  }

  // --- render / update ---
  if (mode == TITLE) {
    if (titleIntro) { scrollText("SNAKE"); titleIntro = false; }
    renderTitle();
    delay(20);
  } else {                               // PLAYING
    if (!gameOver) {
      if (millis() - lastStep >= (unsigned long)stepMs) {
        lastStep = millis();
        step();
        if (gameOver) gameOverFx();      // flash + show score, then wait for A
      }
      renderGame();
      delay(20);
    } else {
      delay(50);                         // frozen board; press A for a new game
    }
  }
}
