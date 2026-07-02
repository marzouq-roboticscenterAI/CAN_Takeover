#include <WiFiS3.h>
void setup(){
  Serial.begin(115200);
  unsigned long t0=millis(); while(!Serial && millis()-t0<3000){}
  Serial.println("=== wifi_check boot ===");
  Serial.print("firmwareVersion: ");
  Serial.println(WiFi.firmwareVersion());
  int st = WiFi.status();
  Serial.print("status: "); Serial.println(st);
  Serial.println("scanning networks...");
  int n = WiFi.scanNetworks();
  Serial.print("networks found: "); Serial.println(n);
  Serial.println("=== co-processor RESPONDS -> radio hardware OK ===");
}
void loop(){ delay(1000); }
