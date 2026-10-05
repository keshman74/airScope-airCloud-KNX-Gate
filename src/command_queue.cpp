#include "command_queue.h"
#include "config.h"
#include "airscope.h"
#include "aircloud.h"
#include "knx.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

struct Cmd {
  int zone;
  char action[32];
  uint8_t value;
  char customUrl[256];
};

static QueueHandle_t commandQueue=nullptr;
static TaskHandle_t worker=nullptr;

bool commandEnqueue(int zone,const String&a,uint8_t v,const String&u){
  if(!commandQueue) return false;
  Cmd c{};
  c.zone=zone;
  c.value=v;
  a.substring(0,sizeof(c.action)-1).toCharArray(c.action,sizeof(c.action));
  u.substring(0,sizeof(c.customUrl)-1).toCharArray(c.customUrl,sizeof(c.customUrl));
  BaseType_t ok=xQueueSend(commandQueue,&c,0);
  Serial.printf("[QUEUE] %s zone=%d action=%s value=%u\n",
                ok==pdTRUE?"ADD":"FULL",zone,c.action,v);
  return ok==pdTRUE;
}
void commandQueueLoop(){}

static const char* scopeInput[]={"wifi","line-in","bluetooth","optical","co-axial","line-in2","udisk","PCUSB"};
static const char* cloudInput[]={"Network","LineIn","Bluetooth","OpticalIn","CoaxialIn","LineIn2","USBDisk","PCUSB"};

static bool customGet(const String&url){
  HTTPClient h; h.setConnectTimeout(450); h.setTimeout(900); int code=-1;
  uint32_t t0=millis();
  Serial.printf("[HTTP TX][CUSTOM] %s\n",url.c_str());
  if(url.startsWith("https://")){
    WiFiClientSecure c; c.setInsecure(); if(!h.begin(c,url))return false; code=h.GET();
  }else{ if(!h.begin(url))return false; code=h.GET(); }
  String body=code>0?h.getString():"";
  h.end();
  Serial.printf("[HTTP RX][CUSTOM] code=%d time=%lums body=%s\n",code,(unsigned long)(millis()-t0),body.c_str());
  return code>0&&code<400;
}
static bool sendNative(Zone&z,const String&cmd){
  if(z.type==ZoneType::AirScope)return airScopeSend(z.ip,cmd);
  if(z.type==ZoneType::AirCloud)return airCloudSend(z.ip,z.useHttps,cmd);
  return false;
}

// A31/A97/A98 getPlayerStatus mode -> physical input index.
// Network modes intentionally map to -1: Wi-Fi is entered by PRESET, never by INPUT NEXT.
static int airScopePhysicalInputFromMode(const String&m){
  int mode=m.toInt();
  if(mode==40) return 1; // line-in
  if(mode==41) return 2; // bluetooth
  if(mode==43) return 3; // optical
  if(mode==47) return 5; // line-in2
  if(mode==51) return 7; // PC USB
  if(mode==11) return 6; // USB disk
  return -1;             // network/AirPlay/DLNA/Spotify/idle/etc.
}

static int airCloudInputIndex(String m){
  m.toLowerCase();
  if(m.indexOf("network")>=0||m=="wifi")return 0;
  if(m.indexOf("line")>=0||m.indexOf("rca")>=0||m.indexOf("phono")>=0)return 1;
  if(m.indexOf("bluetooth")>=0)return 2;
  if(m.indexOf("optical")>=0)return 3;
  if(m.indexOf("co-ax")>=0||m.indexOf("coax")>=0)return 4;
  if(m.indexOf("usb disk")>=0||m=="udisk")return 6;
  if(m.indexOf("pcusb")>=0)return 7;
  return -1;
}

static void inputNext(Zone&z){
  if(z.type==ZoneType::AirScope){
    String mode=airScopeCurrentMode(z.ip);
    int cur=airScopePhysicalInputFromMode(mode);

    // airScope: index 0 (Wi-Fi) is NEVER part of INPUT NEXT.
    // If currently Network/unknown, select the first enabled physical input.
    int start = cur>=1 ? cur : 0;
    for(int s=1;s<=INPUT_COUNT;s++){
      int n=(start+s)%INPUT_COUNT;
      if(n==0) continue;
      if(z.inputMask&(1<<n)){
        String cmd="setPlayerCmd:switchmode:";
        cmd+=scopeInput[n];
        Serial.printf("[INPUT NEXT][airScope] mode=%s current=%d next=%d\n",mode.c_str(),cur,n);
        sendNative(z,cmd);
        return;
      }
    }
    Serial.println("[INPUT NEXT][airScope] no enabled physical inputs");
    return;
  }

  if(z.type==ZoneType::AirCloud){
    String mode=airCloudCurrentMode(z.ip,z.useHttps);
    int cur=airCloudInputIndex(mode);
    for(int s=1;s<=INPUT_COUNT;s++){
      int n=(cur+s+INPUT_COUNT)%INPUT_COUNT;
      if(z.inputMask&(1<<n)){
        String cmd="setPlayerCmd:switchmode:";
        cmd+=cloudInput[n];
        sendNative(z,cmd);
        return;
      }
    }
  }
}

static void airScopePlayPause(Zone&z){
  String mode,status;
  if(!airScopePlayerStatus(z.ip,mode,status,nullptr)){
    Serial.println("[PLAY_PAUSE][airScope] status read failed; fallback onepause");
    airScopeSend(z.ip,"setPlayerCmd:onepause");
    return;
  }

  status.toLowerCase();
  Serial.printf("[PLAY_PAUSE][airScope] mode=%s status=%s\n",mode.c_str(),status.c_str());

  if(status=="play"){
    airScopeSend(z.ip,"setPlayerCmd:pause");
  }else if(status=="pause"){
    airScopeSend(z.ip,"setPlayerCmd:resume");
  }else{
    // Do not guess when A31 reports stop/idle. We observed a real A31
    // playing a preset while reporting mode=0,status=stop after switchmode:wifi.
    Serial.printf("[PLAY_PAUSE][airScope] ABNORMAL/STOP state: no command sent (mode=%s status=%s)\n",
                  mode.c_str(),status.c_str());
  }
}

static void execute(const Cmd&c){
  if(c.zone<0||c.zone>=MAX_ZONES||!zones[c.zone].enabled)return;
  Zone&z=zones[c.zone];
  String a(c.action);
  String customUrl(c.customUrl);

  Serial.printf("[EXEC] zone=%s action=%s value=%u\n",z.name.c_str(),a.c_str(),c.value);

  if(a=="CUSTOM_GET"){ if(customUrl.length())customGet(customUrl); return; }
  if(z.type==ZoneType::Custom)return;

  if(a=="MUTE"){ sendNative(z,String("setPlayerCmd:mute:")+(c.value?"1":"0")); return; }

  if(a=="PLAY_PAUSE"){
    if(z.type==ZoneType::AirScope) airScopePlayPause(z);
    else sendNative(z,"setPlayerCmd:onepause");
    return;
  }

  if(c.value!=1)return;

  if(a=="PLAY")sendNative(z,"setPlayerCmd:resume");
  else if(a=="PAUSE")sendNative(z,"setPlayerCmd:pause");
  else if(a=="NEXT")sendNative(z,"setPlayerCmd:next");
  else if(a=="PREVIOUS")sendNative(z,"setPlayerCmd:prev");
  else if(a=="VOL_UP") sendNative(z,z.type==ZoneType::AirCloud?"setPlayerCmd:RemoteVol++":"setPlayerCmd:vol--");
  else if(a=="VOL_DOWN") sendNative(z,z.type==ZoneType::AirCloud?"setPlayerCmd:RemoteVol--":"setPlayerCmd:vol%2b%2b");
  else if(a=="INPUT_NEXT")inputNext(z);
  else if(a.startsWith("PRESET_") && z.type==ZoneType::AirScope) sendNative(z,"MCUKeyShortClick:"+a.substring(7));
  else if(a.startsWith("INPUT_")){
    int n=-1;
    // INPUT_WIFI intentionally disabled for airScope: Network is entered by PRESET.
    if(a=="INPUT_WIFI" && z.type==ZoneType::AirCloud)n=0;
    else if(a=="INPUT_LINE_IN")n=1;
    else if(a=="INPUT_BLUETOOTH")n=2;
    else if(a=="INPUT_OPTICAL")n=3;
    else if(a=="INPUT_COAXIAL")n=4;
    else if(a=="INPUT_LINE_IN_2")n=5;
    else if(a=="INPUT_USB_DISK")n=6;
    else if(a=="INPUT_PC_USB")n=7;

    if(n>=0){
      String cmd="setPlayerCmd:switchmode:";
      cmd+=(z.type==ZoneType::AirCloud?cloudInput[n]:scopeInput[n]);
      sendNative(z,cmd);
    }else if(a=="INPUT_WIFI" && z.type==ZoneType::AirScope){
      Serial.println("[BLOCKED][airScope] INPUT_WIFI / switchmode:wifi is disabled; use PRESET for Network");
    }
  }
}

struct FeedbackState {
  bool valid=false;
  bool play=false,pause=false,mute=false,network=false,lineIn=false,bluetooth=false;
};
static FeedbackState fbState[MAX_ZONES];
static int fbZone=0;

static String jsonField(const String&s,const String&key){
  int p=s.indexOf("\""+key+"\""); if(p<0)return "";
  p=s.indexOf(':',p); if(p<0)return "";
  p=s.indexOf('"',p); if(p<0)return "";
  int e=s.indexOf('"',p+1); if(e<0)return "";
  return s.substring(p+1,e);
}
static void sendChanged(const String&ga,bool now,bool old,bool force){
  if(ga.length() && (force||now!=old)) knxSendBit(ga,now);
}
static void pollOneFeedback(){
  for(int tries=0;tries<MAX_ZONES;tries++){
    int i=fbZone; fbZone=(fbZone+1)%MAX_ZONES;
    Zone&z=zones[i];
    if(!z.enabled || z.type!=ZoneType::AirScope) continue;
    if(!(z.fbPlay.length()||z.fbPause.length()||z.fbMute.length()||z.fbNetwork.length()||z.fbLineIn.length()||z.fbBluetooth.length())) continue;
    String mode,status,raw;
    if(!airScopePlayerStatus(z.ip,mode,status,&raw)) return;
    String st=status; st.toLowerCase(); int m=mode.toInt();
    bool play=(st=="play"||st=="load");
    bool pause=(st=="pause");
    bool mute=(jsonField(raw,"mute")=="1");
    bool network=(m==1||m==2||m==10||m==20||m==31);
    bool lineIn=(m==40);
    bool bluetooth=(m==41);
    FeedbackState &old=fbState[i]; bool force=!old.valid;
    sendChanged(z.fbPlay,play,old.play,force);
    sendChanged(z.fbPause,pause,old.pause,force);
    sendChanged(z.fbMute,mute,old.mute,force);
    sendChanged(z.fbNetwork,network,old.network,force);
    sendChanged(z.fbLineIn,lineIn,old.lineIn,force);
    sendChanged(z.fbBluetooth,bluetooth,old.bluetooth,force);
    old.valid=true; old.play=play; old.pause=pause; old.mute=mute; old.network=network; old.lineIn=lineIn; old.bluetooth=bluetooth;
    Serial.printf("[FEEDBACK] zone=%s mode=%s status=%s mute=%d N=%d L=%d B=%d\n",z.name.c_str(),mode.c_str(),status.c_str(),mute,network,lineIn,bluetooth);
    return;
  }
}

static void task(void*){
  Cmd c{}; uint32_t lastPoll=0;
  for(;;){
    if(xQueueReceive(commandQueue,&c,pdMS_TO_TICKS(50))==pdTRUE) execute(c);
    if(millis()-lastPoll>=1000){lastPoll=millis();pollOneFeedback();}
  }
}

void commandQueueBegin(){
  commandQueue=xQueueCreate(32,sizeof(Cmd));
  if(!commandQueue){
    Serial.println("[QUEUE] ERROR: xQueueCreate failed");
    return;
  }
  xTaskCreatePinnedToCore(task,"httpWorker",7168,nullptr,1,&worker,0);
  Serial.println("[QUEUE] FreeRTOS queue started (32 commands)");
}
