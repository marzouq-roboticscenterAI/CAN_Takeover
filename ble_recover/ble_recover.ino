#include <WiFiS3.h>
#include <ArduinoBLE.h>
void setup(){
  Serial.begin(115200);
  unsigned long t0=millis(); while(!Serial && millis()-t0<3000){}
  Serial.println("=== ble_recover boot ===");
  // Fully exercise the ESP via WiFi to force a clean co-processor state
  Serial.print("wifi fw: "); Serial.println(WiFi.firmwareVersion());
  Serial.print("wifi scan: "); Serial.println(WiFi.scanNetworks());
  WiFi.end();
  Serial.println("WiFi.end() done; settling 3s");
  delay(3000);
  bool ok=false;
  for(int i=1;i<=12 && !ok;i++){ ok=BLE.begin(); Serial.print("BLE.begin try "); Serial.print(i); Serial.println(ok?" OK":" fail"); if(!ok){ BLE.end(); delay(1200);} }
  Serial.println(ok?"=== BLE RECOVERED ===":"=== BLE STILL DEAD ===");
}
void loop(){ delay(1000); }
