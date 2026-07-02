/*
  snake_flipper  —  Arduino UNO R4 WiFi  (BLE CENTRAL)

  Connects to the Flipper Zero (BLE peripheral named "ARD_Snake", running the
  flipper_snake FAP) and runs the snake game on the LED matrix. Input arrives as
  NOTIFY tokens on characteristic 9a1e0002:
     "^ v < >"  directions (Flipper d-pad)
     "A"        OK (start / restart)

  USB-C is power only; the link to the Flipper is BLE.

  Requires: "Arduino UNO R4 Boards" core + "ArduinoGraphics" + "ArduinoBLE".
  BLE firmware must be enabled (IDE -> Tools -> Firmware Updater).
*/
#include <ArduinoBLE.h>
#include "ArduinoGraphics.h"
#include "Arduino_LED_Matrix.h"

// The Flipper advertises this 16-bit service UUID (it overrides the adv name
// with its own device name, so match by UUID, not name).
#define ADV_UUID "0000f00d-0000-1000-8000-00805f9b34fb"
#define IN_UUID  "9a1e0002-1b2c-4f3d-8e5a-0123456789ab"  // notify char (found after connect)

ArduinoLEDMatrix  matrix;
BLEDevice         peripheral;
BLECharacteristic inChar;
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

void clearFrame(){ for(int r=0;r<H;r++) for(int c=0;c<W;c++) frame[r][c]=0; }
void spawnFood(){
  while(true){
    int c=random(W), r=random(H); bool on=false;
    for(int i=0;i<snakeLen;i++) if(bodyCol[i]==c&&bodyRow[i]==r){on=true;break;}
    if(!on){ foodCol=c; foodRow=r; return; }
  }
}
void resetGame(){
  snakeLen=3; bodyCol[0]=5;bodyRow[0]=4; bodyCol[1]=4;bodyRow[1]=4; bodyCol[2]=3;bodyRow[2]=4;
  dirCol=1;dirRow=0; nextCol=1;nextRow=0; stepMs=350; gameOver=false;
  spawnFood(); lastStep=millis(); mode=PLAYING;
}
void setDir(char a){
  int nc=dirCol,nr=dirRow;
  if(a=='^'){nc=0;nr=-1;} else if(a=='v'){nc=0;nr=1;}
  else if(a=='<'){nc=-1;nr=0;} else if(a=='>'){nc=1;nr=0;} else return;
  if(nc==-dirCol&&nr==-dirRow) return;
  nextCol=nc; nextRow=nr;
}
void step(){
  dirCol=nextCol; dirRow=nextRow;
  int hc=bodyCol[0]+dirCol, hr=bodyRow[0]+dirRow;
  if(hc<0||hc>=W||hr<0||hr>=H){ gameOver=true; return; }
  bool grow=(hc==foodCol&&hr==foodRow);
  for(int i=0;i<snakeLen;i++) if(bodyCol[i]==hc&&bodyRow[i]==hr){
    if(i==snakeLen-1&&!grow) continue; gameOver=true; return;
  }
  int top=grow?snakeLen:(snakeLen-1);
  for(int i=top;i>0;i--){ bodyCol[i]=bodyCol[i-1]; bodyRow[i]=bodyRow[i-1]; }
  bodyCol[0]=hc; bodyRow[0]=hr;
  if(grow){ if(snakeLen<MAXLEN) snakeLen++; spawnFood(); if(stepMs>120) stepMs-=12; }
}
void renderGame(){
  clearFrame();
  for(int i=0;i<snakeLen;i++) frame[bodyRow[i]][bodyCol[i]]=1;
  if((millis()/200)%2==0) frame[foodRow][foodCol]=1;
  matrix.renderBitmap(frame,H,W);
}
void renderTitle(){
  clearFrame(); frame[4][2]=1;frame[4][3]=1;frame[4][4]=1;frame[3][4]=1;
  if((millis()/300)%2==0) frame[2][9]=1;
  matrix.renderBitmap(frame,H,W);
}
void renderWaiting(){
  clearFrame();
  if((millis()/250)%2==0){ frame[0][0]=1;frame[0][W-1]=1;frame[H-1][0]=1;frame[H-1][W-1]=1; }
  matrix.renderBitmap(frame,H,W);
}
void handleToken(const char* tok){
  int n=strlen(tok); if(n==0) return;
  if(mode==TITLE){ if(!strcmp(tok,"A")||!strcmp(tok,"STR")) resetGame(); }
  else { if(gameOver){ if(!strcmp(tok,"A")||!strcmp(tok,"STR")) resetGame(); } else setDir(tok[n-1]); }
}

bool tryConnect(){
  BLEDevice dev = BLE.available();
  if(!dev) return false;
  int cnt = dev.advertisedServiceUuidCount();
  bool match = false;
  for(int i=0;i<cnt;i++){ String u=dev.advertisedServiceUuid(i); u.toLowerCase(); if(u.indexOf("f00d")>=0){match=true;break;} }
  Serial.print("saw "); Serial.print(dev.address());
  Serial.print(" name='"); Serial.print(dev.localName());
  Serial.print("' uuids="); Serial.print(cnt);
  if(cnt>0){ Serial.print(" ["); Serial.print(dev.advertisedServiceUuid(0)); Serial.print("]"); }
  Serial.println(match?"  <-- MATCH":"");
  if(!match) return false;
  BLE.stopScan();
  Serial.println("connecting...");
  if(dev.connect()){
    Serial.println("connected; discovering...");
    if(dev.discoverAttributes()){
      inChar = dev.characteristic(IN_UUID);
      if(inChar && inChar.canSubscribe() && inChar.subscribe()){
        Serial.println("SUBSCRIBED -> linked");
        peripheral=dev; linked=true; mode=TITLE; return true;
      } else Serial.println("char/subscribe FAIL");
    } else Serial.println("discoverAttributes FAIL");
  } else Serial.println("connect FAIL");
  dev.disconnect(); BLE.scan(); return false;
}

void setup(){
  Serial.begin(115200);
  matrix.begin();
  randomSeed(analogRead(A0));
  mode=TITLE;
  unsigned long t0=millis(); while(!Serial && millis()-t0<2500){}
  Serial.println("boot: snake_flipper");
  bool ok=false;
  for(int i=1;i<=10 && !ok;i++){
    ok = BLE.begin();
    Serial.print("BLE.begin try "); Serial.print(i); Serial.println(ok?" OK":" fail");
    if(!ok){ BLE.end(); delay(800); }
  }
  if(!ok){
    Serial.println("BLE DEAD after retries -> power-cycle needed");
    matrix.beginDraw(); matrix.stroke(255,255,255); matrix.textFont(Font_4x6);
    matrix.beginText(0,1,255,255,255); matrix.println("BLE?"); matrix.endText(); matrix.endDraw();
    while(1) delay(1000);
  }
  Serial.println("scan started");
  BLE.scan();
}

void loop(){
  static unsigned long hb=0;
  if(millis()-hb>1000){ hb=millis(); Serial.print("hb linked="); Serial.println(linked); }
  if(!linked){ tryConnect(); renderWaiting(); delay(20); return; }
  if(!peripheral.connected()){ linked=false; BLE.scan(); return; }

  if(inChar.valueUpdated()){
    uint8_t b[24]; int n=inChar.readValue(b,sizeof(b)-1);
    if(n<0)n=0; if(n>23)n=23; b[n]=0; handleToken((char*)b);
  }

  if(mode==TITLE){ renderTitle(); delay(20); }
  else {
    if(!gameOver){
      if(millis()-lastStep>=(unsigned long)stepMs){ lastStep=millis(); step(); }
      renderGame(); delay(20);
    } else delay(50);
  }
}
