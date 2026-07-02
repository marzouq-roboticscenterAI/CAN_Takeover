#include <WiFiS3.h>
void setup(){ Serial.begin(115200); unsigned long t0=millis(); while(!Serial&&millis()-t0<3000){} Serial.print("FWVER="); Serial.println(WiFi.firmwareVersion()); Serial.print("LATEST="); Serial.println(WIFI_FIRMWARE_LATEST_VERSION); }
void loop(){}
