#include <Arduino.h>
#include <WiFi.h>
#include "config.h"
#include "knx.h"
#include "webui.h"
#include "command_queue.h"
#include "airscope_tcp.h"
#include <esp_heap_caps.h>

static bool connectWiFi(){
  if(!netConfig.ssid.length() || !netConfig.password.length()){
    Serial.println("WiFi: credentials not configured");
    return false;
  }

  WiFi.mode(WIFI_STA);

  if(!netConfig.dhcp){
    IPAddress ip,gw,mask,dns;
    if(ip.fromString(netConfig.ip) && gw.fromString(netConfig.gateway) &&
       mask.fromString(netConfig.subnet) && dns.fromString(netConfig.dns)){
      WiFi.config(ip,gw,mask,dns);
      Serial.println("WiFi: static IP configuration applied");
    }else{
      Serial.println("WiFi: invalid static configuration; using DHCP");
    }
  }

  Serial.printf("WiFi: connecting to %s",netConfig.ssid.c_str());
  WiFi.begin(netConfig.ssid.c_str(),netConfig.password.c_str());

  unsigned long t=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-t<12000){
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if(WiFi.status()==WL_CONNECTED){
    Serial.printf("WiFi: CONNECTED, IP=%s\n",WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("WiFi: connection failed");
  return false;
}

static void recovery(){
  WiFi.mode(WIFI_AP_STA);
  if(WiFi.softAP("airScope-Gateway-Setup","airScope123")){
    Serial.println("Recovery AP: STARTED");
    Serial.println("Recovery SSID: airScope-Gateway-Setup");
    Serial.printf("Recovery WEB: http://%s\n",WiFi.softAPIP().toString().c_str());
  }else{
    Serial.println("Recovery AP: ERROR");
  }
}

void setup(){
  Serial.begin(115200);
  delay(400);
  Serial.println("\n======================================");
  Serial.println("KNX -> airScope / airCloud Gateway");
  Serial.println("Firmware v0.8.10");
  Serial.println("======================================");

  configBegin();

  bool wifiOK=connectWiFi();
  if(!wifiOK) recovery();

  commandQueueBegin();

  if(wifiOK){
    airScopeTcpBegin();
    knxBegin();
  }

  Serial.printf("Memory: Free Heap=%u, Min Free Heap=%u, Largest Free Block=%u bytes\n", ESP.getFreeHeap(), ESP.getMinFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));

  webBegin();

  if(wifiOK)
    Serial.printf("WEB: http://%s\n",WiFi.localIP().toString().c_str());
  else
    Serial.printf("WEB: http://%s (Recovery AP)\n",WiFi.softAPIP().toString().c_str());
}

void loop(){
  webLoop();
  knxLoop();
  commandQueueLoop();
  airScopeTcpLoop();
  delay(1);
}
