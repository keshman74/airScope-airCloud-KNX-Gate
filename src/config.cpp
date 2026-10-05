#include "config.h"
#include <LittleFS.h>

Zone zones[MAX_ZONES];
Mapping mappings[MAX_MAPPINGS];
NetConfig netConfig;

static void defaults(){
  netConfig.ssid="Lab2G";
  netConfig.password="";
  zones[0].name="Zone 1";
  zones[0].type=ZoneType::AirScope;
  zones[0].ip="192.168.0.35";
  zones[0].enabled=true;
  zones[0].inputMask=(1<<0)|(1<<1)|(1<<2)|(1<<6);

  const char* ga[]={"3/0/0","3/1/0","3/1/1","3/1/6","3/2/0","3/2/1","3/2/2","3/2/3"};
  const char* ac[]={"PLAY_PAUSE","VOL_UP","VOL_DOWN","MUTE","PRESET_1","PRESET_2","PRESET_3","PRESET_4"};
  for(int i=0;i<8;i++){mappings[i].ga=ga[i];mappings[i].action=ac[i];mappings[i].zone=0;mappings[i].enabled=true;}
}
static String enc(String s){s.replace("%","%25");s.replace("\n","%0A");s.replace("\t","%09");return s;}
static String dec(String s){s.replace("%09","\t");s.replace("%0A","\n");s.replace("%25","%");return s;}
static String getv(const String&line,int n){
  int start=0;
  for(int i=0;i<n;i++){start=line.indexOf('\t',start);if(start<0)return "";start++;}
  int end=line.indexOf('\t',start); if(end<0)end=line.length();
  return dec(line.substring(start,end));
}

bool configBegin(){
  defaults();
  if(!LittleFS.begin(true)){Serial.println("LittleFS: mount FAILED");return false;}
  Serial.printf("LittleFS: OK, total=%u used=%u\n",(unsigned)LittleFS.totalBytes(),(unsigned)LittleFS.usedBytes());
  if(!LittleFS.exists("/config.txt")){Serial.println("Config: defaults");return true;}
  File f=LittleFS.open("/config.txt","r"); if(!f)return false;
  while(f.available()){
    String l=f.readStringUntil('\n'); l.trim(); if(!l.length())continue;
    String tag=getv(l,0);
    if(tag=="NET"){
      netConfig.ssid=getv(l,1); netConfig.password=getv(l,2); netConfig.dhcp=getv(l,3)=="1";
      netConfig.ip=getv(l,4);netConfig.gateway=getv(l,5);netConfig.subnet=getv(l,6);netConfig.dns=getv(l,7);
    }else if(tag=="Z"){
      int i=getv(l,1).toInt(); if(i>=0&&i<MAX_ZONES){
        zones[i].enabled=getv(l,2)=="1";zones[i].name=getv(l,3);zones[i].type=(ZoneType)getv(l,4).toInt();
        zones[i].ip=getv(l,5);zones[i].useHttps=getv(l,6)=="1";zones[i].inputMask=(uint8_t)getv(l,7).toInt();
        zones[i].fbPlay=getv(l,8);zones[i].fbPause=getv(l,9);zones[i].fbMute=getv(l,10);
        zones[i].fbNetwork=getv(l,11);zones[i].fbLineIn=getv(l,12);zones[i].fbBluetooth=getv(l,13);
      }
    }else if(tag=="M"){
      int i=getv(l,1).toInt(); if(i>=0&&i<MAX_MAPPINGS){
        mappings[i].enabled=getv(l,2)=="1";mappings[i].ga=getv(l,3);mappings[i].zone=getv(l,4).toInt();
        mappings[i].action=getv(l,5);mappings[i].customUrl=getv(l,6);
        String dt=getv(l,7);
        mappings[i].dataType=dt.length()?dt:"BIT";
      }
    }
  }
  f.close(); Serial.println("Config: loaded from LittleFS"); return true;
}

bool configSave(){
  File f=LittleFS.open("/config.tmp","w"); if(!f){Serial.println("Config: write FAILED");return false;}
  f.printf("NET\t%s\t%s\t%d\t%s\t%s\t%s\t%s\n",enc(netConfig.ssid).c_str(),enc(netConfig.password).c_str(),netConfig.dhcp,
    enc(netConfig.ip).c_str(),enc(netConfig.gateway).c_str(),enc(netConfig.subnet).c_str(),enc(netConfig.dns).c_str());
  for(int i=0;i<MAX_ZONES;i++) if(zones[i].enabled)
    f.printf("Z\t%d\t1\t%s\t%d\t%s\t%d\t%u\t%s\t%s\t%s\t%s\t%s\t%s\n",i,enc(zones[i].name).c_str(),(int)zones[i].type,enc(zones[i].ip).c_str(),zones[i].useHttps,zones[i].inputMask,
      enc(zones[i].fbPlay).c_str(),enc(zones[i].fbPause).c_str(),enc(zones[i].fbMute).c_str(),enc(zones[i].fbNetwork).c_str(),enc(zones[i].fbLineIn).c_str(),enc(zones[i].fbBluetooth).c_str());
  for(int i=0;i<MAX_MAPPINGS;i++) if(mappings[i].enabled)
    f.printf("M\t%d\t1\t%s\t%d\t%s\t%s\t%s\n",i,enc(mappings[i].ga).c_str(),mappings[i].zone,enc(mappings[i].action).c_str(),enc(mappings[i].customUrl).c_str(),enc(mappings[i].dataType).c_str());
  f.close();
  LittleFS.remove("/config.txt");
  if(!LittleFS.rename("/config.tmp","/config.txt")){Serial.println("Config: rename FAILED");return false;}
  Serial.println("Config: saved to LittleFS"); return true;
}
