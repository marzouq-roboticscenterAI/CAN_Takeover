/*
  esp32_xbox_bridge — ESP32-C6 + Bluepad32

  Pairs an Xbox Wireless (BLE) controller DIRECTLY (Bluepad32 does the bonding +
  HID that ArduinoBLE on the R4 cannot), and forwards our snake tokens to the
  Arduino UNO R4 over a UART. The R4 runs snake_uart on its LED matrix.

  Tokens (newline-terminated), same protocol as the rest of the project:
     "^ v < >"  directions (d-pad or left stick)
     "A"        A button (start / restart)

  --- Install (Arduino IDE) ---
  Bluepad32 ships as its own board package (a Bluepad32-enabled ESP32 core):
    File > Preferences > Additional Boards Manager URLs, add:
      https://raw.githubusercontent.com/ricardoquesada/esp32-arduino-lib-builder/master/bluepad32_files/package_esp32_bluepad32_index.json
    Boards Manager > install "ESP32 Bluepad32 boards" (by Ricardo Quesada)
    Tools > Board > select your "ESP32-C6 ... (Bluepad32)" board.

  --- Wiring (ESP32-C6 -> R4) ---
    ESP32 UART_TX_PIN  ->  R4 D0 (RX / Serial1)
    ESP32 GND          ->  R4 GND
    Power each board over its own USB (or the ESP32 from R4 3V3).
    NOTE: only drive ESP32(3.3V) -> R4. Do NOT wire R4 D1/TX (5V) to the ESP32
    RX (3.3V) — it can damage the ESP32.

  --- Pair the controller ---
    Put the Xbox pad in pairing mode (hold the top pair button until the Xbox
    logo fast-blinks). First run, uncomment BP32.forgetBluetoothKeys().
*/
#include <Bluepad32.h>

#define UART_TX_PIN 5      // ESP32-C6 GPIO -> R4 D0  (pick a free GPIO on your board)
#define UART_RX_PIN 4      // unused (one-way link), but Serial1 needs a pin assigned
#define STICK_THRESH 350   // Bluepad32 axes are ~ -512..511

static ControllerPtr myCtl = nullptr;
static const char* lastDir = nullptr;
static bool lastA = false;

static void onConnected(ControllerPtr ctl) {
  if(myCtl == nullptr) { myCtl = ctl; Serial.println("controller connected"); }
}
static void onDisconnected(ControllerPtr ctl) {
  if(myCtl == ctl) { myCtl = nullptr; lastDir = nullptr; lastA = false; Serial.println("controller disconnected"); }
}

static void sendTok(const char* t) {
  Serial1.print(t);
  Serial1.print('\n');
  Serial.print("-> "); Serial.println(t);   // debug on USB
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200, SERIAL_8N1, UART_RX_PIN, UART_TX_PIN);
  BP32.setup(&onConnected, &onDisconnected);
  BP32.enableNewBluetoothConnections(true);
  // BP32.forgetBluetoothKeys();   // uncomment ONCE to clear old pairings, then re-comment
}

void loop() {
  if(BP32.update() && myCtl && myCtl->isConnected() && myCtl->isGamepad()) {
    // direction: d-pad first, else left stick
    const char* dir = nullptr;
    uint8_t dp = myCtl->dpad();
    if(dp & DPAD_UP)         dir = "^";
    else if(dp & DPAD_DOWN)  dir = "v";
    else if(dp & DPAD_LEFT)  dir = "<";
    else if(dp & DPAD_RIGHT) dir = ">";
    if(!dir) {
      int x = myCtl->axisX(), y = myCtl->axisY();
      if(abs(y) >= abs(x) && abs(y) > STICK_THRESH) dir = (y < 0) ? "^" : "v";
      else if(abs(x) > STICK_THRESH)                dir = (x < 0) ? "<" : ">";
    }
    if(dir != lastDir) { lastDir = dir; if(dir) sendTok(dir); }   // send on change

    bool a = myCtl->a();
    if(a && !lastA) sendTok("A");
    lastA = a;
  }
  delay(10);
}
