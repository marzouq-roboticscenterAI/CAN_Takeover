/*
  xbox_probe — R4 BLE central experiment against an Xbox Wireless controller.

  Determines exactly where stock ArduinoBLE stalls: does it connect? find the
  HID service (0x1812)? can it subscribe to the report characteristic, or does
  it fail on authentication? does BLE.paired() ever go true? do input reports
  arrive? Prints everything over USB serial.

  Prereqs to actually run:
    - R4 BLE radio ALIVE (fully power-cycle the board if BLE.begin fails).
    - Xbox controller disconnected from the laptop and in PAIRING mode
      (hold the top pair button until the Xbox logo fast-blinks).
*/
#include <ArduinoBLE.h>

static void dumpAttrs(BLEDevice& d) {
  if(!d.discoverAttributes()) { Serial.println("discoverAttributes FAIL"); return; }
  Serial.print("services="); Serial.println(d.serviceCount());
  for(int s=0; s<d.serviceCount(); s++) {
    BLEService sv = d.service(s);
    Serial.print("  svc "); Serial.println(sv.uuid());
    for(int c=0; c<sv.characteristicCount(); c++) {
      BLECharacteristic ch = sv.characteristic(c);
      Serial.print("    chr "); Serial.print(ch.uuid());
      Serial.print(" props=0x"); Serial.print(ch.properties(), HEX);
      if(ch.canSubscribe()) {
        bool ok = ch.subscribe();
        Serial.print(ok ? "  [subscribed]" : "  [subscribe FAIL]");
      }
      Serial.println();
    }
  }
  Serial.print("BLE.paired()="); Serial.println(BLE.paired());
}

void setup() {
  Serial.begin(115200);
  unsigned long t0=millis(); while(!Serial && millis()-t0<2500){}
  Serial.println("xbox_probe boot");

  // Give the ESP32-S3 radio time to finish its own boot before the first begin.
  delay(3000);
  bool ok=false;
  for(int i=1;i<=15 && !ok;i++){
    ok=BLE.begin();
    Serial.print("BLE.begin try "); Serial.print(i); Serial.println(ok?" OK":" fail");
    if(!ok){ BLE.end(); delay(1500); }
  }
  if(!ok){ Serial.println("BLE DEAD -> power-cycle the R4 (unplug USB ~30s, plug direct)"); while(1) delay(1000); }

  BLE.setPairable(true);                 // ask ArduinoBLE to allow bonding
  Serial.println("scanning for Xbox / HID(0x1812)...");
  BLE.scan();
}

void loop() {
  BLEDevice d = BLE.available();
  if(!d) return;

  String n = d.localName();
  bool isXbox = (n.indexOf("Xbox") >= 0);
  for(int i=0;i<d.advertisedServiceUuidCount();i++){ String u=d.advertisedServiceUuid(i); u.toLowerCase(); if(u.indexOf("1812")>=0) isXbox=true; }
  if(n.length()) { Serial.print("saw '"); Serial.print(n); Serial.print("' "); Serial.println(d.address()); }
  if(!isXbox) return;

  BLE.stopScan();
  Serial.print(">>> Xbox candidate "); Serial.print(d.address()); Serial.println(" connecting...");
  if(!d.connect()) { Serial.println("connect FAIL"); BLE.scan(); return; }
  Serial.println("CONNECTED - pumping events for SMP (20s)...");
  unsigned long tp=millis();
  while(d.connected() && millis()-tp<20000){
    BLE.poll();
    if(BLE.paired()){ Serial.println("*** PAIRED ***"); break; }
    delay(3);
  }
  Serial.print("connected="); Serial.print(d.connected());
  Serial.print(" paired="); Serial.println(BLE.paired());

  // Discovery requires an encrypted link. Only attempt it once paired, and
  // NEVER iterate services on a failed discovery (that path bus-faults).
  if(!d.connected()){ Serial.println("dropped before pairing; rescan"); BLE.scan(); return; }
  if(!BLE.paired()){
    Serial.println("not paired -> skipping discovery. holding link 8s then rescan.");
    unsigned long th=millis();
    while(d.connected() && millis()-th<8000){ BLE.poll(); delay(5); }
    if(d.connected()) d.disconnect();
    BLE.scan(); return;
  }

  Serial.println("*** PAIRED! ***");
  // discoverAttributes()/discoverService() HANG on the Xbox (poll() drains a
  // continuous packet stream). Skip discovery entirely: just pump events and let
  // the patched handleNotifyOrInd dump raw HID notifications. If the Xbox streams
  // reports on the bonded link, we'll see them here as "NOTIFY h=... : ..".
  Serial.println("NO discovery — pumping 60s for raw NOTIFY (press buttons / move sticks)...");
  unsigned long t=millis();
  while(d.connected() && millis()-t<60000){
    BLE.poll();
    delay(2);
  }
  Serial.print("done; connected="); Serial.println(d.connected());
  if(d.connected()) d.disconnect();
  BLE.scan();
}
