#include <ArduinoBLE.h>
void setup(){
  Serial.begin(115200);
  unsigned long t0=millis(); while(!Serial && millis()-t0<3000){}
  Serial.println("=== stock scan boot ===");
  delay(2000);
  bool ok=false;
  for(int i=1;i<=10 && !ok;i++){ ok=BLE.begin(); Serial.print("BLE.begin try "); Serial.print(i); Serial.println(ok?" OK":" fail"); if(!ok){ BLE.end(); delay(1200);} }
  if(!ok){ Serial.println("STOCK BLE DEAD"); while(1) delay(1000); }
  Serial.println("STOCK BLE OK -> scanning");
  BLE.scan();
}
void loop(){
  BLEDevice d=BLE.available();
  if(d){ Serial.print("saw "); Serial.print(d.address()); Serial.print(" "); Serial.println(d.localName()); }
}
