/*
  controller_led  —  Arduino UNO R4 WiFi

  Receives a short newline-terminated label over USB serial and shows the last
  controller input on the 12x8 LED matrix.
    - Buttons (A, B, X, Y, L3, ...) -> letters (short = static, long = scroll)
    - Sticks/D-pad ("L^", "R<", "D^", ...) -> side letter, then an arrow
      e.g. left-stick-up  "L^"  ->  shows "L" then an up arrow.

  Requirements (install once in the Arduino IDE):
    - Boards Manager : "Arduino UNO R4 Boards"
    - Library Manager: "ArduinoGraphics"
  (Arduino_LED_Matrix ships with the R4 core.)
*/
#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"

ArduinoLEDMatrix matrix;
String buf;

uint8_t ARROW_UP[8][12] = {
  {0,0,0,0,0,1,1,0,0,0,0,0},
  {0,0,0,0,1,1,1,1,0,0,0,0},
  {0,0,0,1,1,1,1,1,1,0,0,0},
  {0,0,1,1,1,1,1,1,1,1,0,0},
  {0,0,0,0,1,1,1,1,0,0,0,0},
  {0,0,0,0,1,1,1,1,0,0,0,0},
  {0,0,0,0,1,1,1,1,0,0,0,0},
  {0,0,0,0,1,1,1,1,0,0,0,0},
};
uint8_t ARROW_DOWN[8][12] = {
  {0,0,0,0,1,1,1,1,0,0,0,0},
  {0,0,0,0,1,1,1,1,0,0,0,0},
  {0,0,0,0,1,1,1,1,0,0,0,0},
  {0,0,0,0,1,1,1,1,0,0,0,0},
  {0,0,1,1,1,1,1,1,1,1,0,0},
  {0,0,0,1,1,1,1,1,1,0,0,0},
  {0,0,0,0,1,1,1,1,0,0,0,0},
  {0,0,0,0,0,1,1,0,0,0,0,0},
};
uint8_t ARROW_LEFT[8][12] = {
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,1,0,0,0,0,0,0,0,0,0},
  {0,1,1,0,0,0,0,0,0,0,0,0},
  {1,1,1,1,1,1,1,1,1,1,1,0},
  {1,1,1,1,1,1,1,1,1,1,1,0},
  {0,1,1,0,0,0,0,0,0,0,0,0},
  {0,0,1,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
};
uint8_t ARROW_RIGHT[8][12] = {
  {0,0,0,0,0,0,0,0,0,0,0,0},
  {0,0,0,0,0,0,0,0,0,1,0,0},
  {0,0,0,0,0,0,0,0,0,1,1,0},
  {0,1,1,1,1,1,1,1,1,1,1,1},
  {0,1,1,1,1,1,1,1,1,1,1,1},
  {0,0,0,0,0,0,0,0,0,1,1,0},
  {0,0,0,0,0,0,0,0,0,1,0,0},
  {0,0,0,0,0,0,0,0,0,0,0,0},
};

bool isArrow(char c) { return c == '^' || c == 'v' || c == '<' || c == '>'; }

void drawArrow(char c) {
  switch (c) {
    case '^': matrix.renderBitmap(ARROW_UP, 8, 12); break;
    case 'v': matrix.renderBitmap(ARROW_DOWN, 8, 12); break;
    case '<': matrix.renderBitmap(ARROW_LEFT, 8, 12); break;
    case '>': matrix.renderBitmap(ARROW_RIGHT, 8, 12); break;
  }
}

void showText(const String &s) {
  int w = s.length() * 5 - 1;        // Font_4x6 ~ 5px per char
  matrix.beginDraw();
  matrix.clear();
  matrix.stroke(255, 255, 255);
  matrix.textFont(Font_4x6);
  if (w <= 12) {                     // fits -> static, centered
    int x = (12 - w) / 2;
    if (x < 0) x = 0;
    matrix.beginText(x, 1, 255, 255, 255);
    matrix.print(s.c_str());
    matrix.endText();
  } else {                           // too wide -> scroll once
    matrix.textScrollSpeed(70);
    matrix.beginText(12, 1, 255, 255, 255);
    matrix.print(s.c_str());
    matrix.endText(SCROLL_LEFT);
  }
  matrix.endDraw();
}

void handleLine(String line) {
  line.trim();
  if (line.length() == 0) return;
  char last = line.charAt(line.length() - 1);
  if (isArrow(last)) {
    if (line.length() > 1) {                       // side letter, e.g. "L"
      showText(line.substring(0, line.length() - 1));
      delay(450);
    }
    matrix.beginDraw(); matrix.clear(); matrix.endDraw();
    drawArrow(last);
  } else {
    showText(line);
  }
}

void setup() {
  Serial.begin(115200);
  matrix.begin();
  showText("..");                    // idle marker until first input
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n')        { handleLine(buf); buf = ""; }
    else if (c != '\r')   { buf += c; if (buf.length() > 16) buf = ""; }
  }
}
