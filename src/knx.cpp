#include "knx.h"
#include "config.h"
#include "command_queue.h"
#include <WiFi.h>
#include <WiFiUdp.h>

static WiFiUDP udp;
static IPAddress mc(224,0,23,12);
static bool knxStarted=false;

static String ga(uint16_t a){
  return String((a>>11)&31)+"/"+String((a>>8)&7)+"/"+String(a&255);
}

void knxBegin(){
  knxStarted=false;
  if(WiFi.status()!=WL_CONNECTED){
    Serial.println("KNX: not started (Wi-Fi STA not connected)");
    return;
  }
  if(udp.beginMulticast(mc,3671)){
    knxStarted=true;
    Serial.println("KNX: multicast 224.0.23.12:3671 started");
  }else{
    Serial.println("KNX: ERROR starting multicast");
  }
}

static bool parseGa(const String&s,uint16_t&out){
  int p1=s.indexOf('/'); int p2=s.indexOf('/',p1+1);
  if(p1<=0||p2<=p1+1) return false;
  int main=s.substring(0,p1).toInt();
  int middle=s.substring(p1+1,p2).toInt();
  int sub=s.substring(p2+1).toInt();
  if(main<0||main>31||middle<0||middle>7||sub<0||sub>255) return false;
  out=(uint16_t)((main<<11)|(middle<<8)|sub); return true;
}

bool knxSendBit(const String&groupAddress,bool value){
  if(!knxStarted || WiFi.status()!=WL_CONNECTED || !groupAddress.length()) return false;
  uint16_t dst=0; if(!parseGa(groupAddress,dst)){Serial.printf("[KNX TX] invalid GA %s\n",groupAddress.c_str());return false;}
  // KNXnet/IP Routing Indication + cEMI L_Data.ind, source IA 1.1.250.
  uint8_t b[17]={0x06,0x10,0x05,0x30,0x00,0x11, 0x29,0x00, 0xBC,0xE0, 0x11,0xFA, 0,0, 0x01,0x00,0x80};
  b[12]=(dst>>8)&0xff; b[13]=dst&0xff; b[16]=0x80|(value?1:0);
  if(!udp.beginPacket(mc,3671)){Serial.printf("[KNX TX] %s=%d begin failed\n",groupAddress.c_str(),value);return false;}
  udp.write(b,sizeof(b)); bool ok=udp.endPacket()==1;
  Serial.printf("[KNX TX] %s = %d %s\n",groupAddress.c_str(),value,ok?"OK":"FAILED");
  return ok;
}

void knxLoop(){
  if(!knxStarted || WiFi.status()!=WL_CONNECTED) return;

  int n=udp.parsePacket();
  if(n<=0) return;

  uint8_t b[512];
  n=udp.read(b,min(n,512));
  if(n<17||b[0]!=0x06||b[1]!=0x10||b[2]!=0x05||b[3]!=0x30) return;

  int cs=6;
  if(b[cs]!=0x29) return;
  int fs=cs+2+b[cs+1];
  if(fs+9>n) return;
  if(!(b[fs+1]&0x80)) return;

  uint16_t dst=((uint16_t)b[fs+4]<<8)|b[fs+5];
  uint8_t apci=((b[fs+7]&3)<<2)|((b[fs+8]>>6)&3);
  if(apci!=2) return;

  uint8_t val=b[fs+8]&0x3f;
  if(b[fs+6]>1) return;

  String g=ga(dst);
  Serial.printf("KNX %s value=%u\n",g.c_str(),val);

  for(int i=0;i<MAX_MAPPINGS;i++){
    if(mappings[i].enabled && mappings[i].ga==g){
      if(!commandEnqueue(mappings[i].zone,mappings[i].action,val,mappings[i].customUrl))
        Serial.println("QUEUE FULL");
      return;
    }
  }
}
