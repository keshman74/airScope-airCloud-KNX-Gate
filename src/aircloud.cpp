#include "aircloud.h"
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

bool airCloudSend(const String& ip,bool https,const String& command,String* response){
  String url=(https?"https://":"http://")+ip+(https?":8443/?Instruct=":":8000/?Instruct=")+command;
  HTTPClient h; h.setConnectTimeout(450); h.setTimeout(900);
  int code=-1;
  if(https){
    WiFiClientSecure c; c.setInsecure();
    if(!h.begin(c,url)) return false;
    code=h.GET();
  }else{
    if(!h.begin(url)) return false;
    code=h.GET();
  }
  String body=code>0?h.getString():"";
  h.end();
  if(response)*response=body;
  return code>0 && code<400;
}

static String field(const String&s,const String&key){
  int p=s.indexOf("\""+key+"\""); if(p<0)return "";
  p=s.indexOf(':',p); if(p<0)return "";
  p=s.indexOf('"',p); if(p<0)return "";
  int e=s.indexOf('"',p+1); if(e<0)return "";
  return s.substring(p+1,e);
}

String airCloudCurrentMode(const String& ip,bool https){
  String r;
  if(!airCloudSend(ip,https,"getStatusEx",&r)) return "";
  return field(r,"DevModel");
}
