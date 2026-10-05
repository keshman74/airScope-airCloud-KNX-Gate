#include "airscope.h"
#include <HTTPClient.h>

bool airScopeSend(const String& ip,const String& command,String* response){
  HTTPClient h;
  h.setConnectTimeout(350);
  h.setTimeout(700);
  String url="http://"+ip+"/httpapi.asp?command="+command;

  uint32_t t0=millis();
  Serial.printf("[HTTP TX][airScope] %s  %s\n",ip.c_str(),command.c_str());

  if(!h.begin(url)){
    Serial.printf("[HTTP ERR][airScope] begin failed  %s\n",url.c_str());
    return false;
  }

  int code=h.GET();
  String body = code>0 ? h.getString() : "";
  uint32_t dt=millis()-t0;
  h.end();

  Serial.printf("[HTTP RX][airScope] code=%d time=%lums body=%s\n",
                code,(unsigned long)dt,body.c_str());

  if(response) *response=body;
  return code>0 && code<400;
}

static String jsonish(const String&s,const String&key){
  int p=s.indexOf("\""+key+"\"");
  if(p<0) return "";
  p=s.indexOf(':',p); if(p<0)return "";
  p=s.indexOf('"',p); if(p<0)return "";
  int e=s.indexOf('"',p+1); if(e<0)return "";
  return s.substring(p+1,e);
}

bool airScopePlayerStatus(const String& ip,String& mode,String& status,String* raw){
  String r;
  if(!airScopeSend(ip,"getPlayerStatus",&r)) return false;
  mode=jsonish(r,"mode");
  status=jsonish(r,"status");
  if(raw) *raw=r;
  return true;
}

String airScopeCurrentMode(const String& ip){
  String mode,status;
  if(!airScopePlayerStatus(ip,mode,status,nullptr)) return "";
  return mode;
}
