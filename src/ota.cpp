#include "ota.h"
#include <Update.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

static WebServer* s=nullptr;
static const char* FW_VERSION="0.8.11";
static const char* MANIFEST_URL="https://raw.githubusercontent.com/keshman74/airScope-airCloud-KNX-Gate/main/latest.json";

static String jsonValue(const String& body,const char* key){
  String needle=String("\"")+key+"\"";
  int p=body.indexOf(needle); if(p<0)return "";
  p=body.indexOf(':',p+needle.length()); if(p<0)return "";
  p=body.indexOf('"',p+1); if(p<0)return "";
  int e=body.indexOf('"',p+1); if(e<0)return "";
  return body.substring(p+1,e);
}
static int verCmp(String a,String b){
  a.replace("v",""); b.replace("v","");
  int ai=0,bi=0;
  for(int n=0;n<3;n++){
    int ad=a.indexOf('.',ai), bd=b.indexOf('.',bi);
    String as=ad<0?a.substring(ai):a.substring(ai,ad);
    String bs=bd<0?b.substring(bi):b.substring(bi,bd);
    int av=as.toInt(),bv=bs.toInt();
    if(av<bv)return -1; if(av>bv)return 1;
    if(ad<0&&bd<0)break;
    ai=ad<0?a.length():ad+1; bi=bd<0?b.length():bd+1;
  }
  return 0;
}
static bool getManifest(String& version,String& url,String& err){
  WiFiClientSecure c; c.setInsecure();
  HTTPClient h;
  if(!h.begin(c,MANIFEST_URL)){err="manifest connection";return false;}
  h.setTimeout(12000);
  int code=h.GET();
  if(code!=HTTP_CODE_OK){err="manifest HTTP "+String(code);h.end();return false;}
  String body=h.getString(); h.end();
  version=jsonValue(body,"version"); url=jsonValue(body,"firmware");
  if(!version.length()||!url.length()){err="invalid manifest";return false;}
  return true;
}
static bool installUrl(const String& url,String& err){
  WiFiClientSecure c; c.setInsecure();
  HTTPClient h;
  h.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  h.setTimeout(20000);
  if(!h.begin(c,url)){err="firmware connection";return false;}
  int code=h.GET();
  if(code!=HTTP_CODE_OK){err="firmware HTTP "+String(code);h.end();return false;}
  int len=h.getSize();
  if(len<=0){err="invalid firmware size";h.end();return false;}
  if(!Update.begin((size_t)len)){err="Update.begin failed";h.end();return false;}
  NetworkClient* stream=h.getStreamPtr();
  size_t written=Update.writeStream(*stream);
  bool ok=(written==(size_t)len)&&Update.end(true)&&!Update.hasError();
  h.end();
  if(!ok){err="firmware write failed";return false;}
  return true;
}
void otaAttach(WebServer&server){
  s=&server;
  server.on("/ota/check",HTTP_GET,[](){
    String v,u,e;
    if(!getManifest(v,u,e)){s->send(200,"application/json",String("{\"error\":\"")+e+"\"}");return;}
    bool avail=verCmp(String(FW_VERSION),v)<0;
    s->send(200,"application/json",String("{\"installed\":\"v")+FW_VERSION+"\",\"latest\":\""+v+"\",\"available\":"+(avail?"true":"false")+"}");
  });
  server.on("/ota/install",HTTP_GET,[](){
    String v,u,e;
    if(!getManifest(v,u,e)){s->send(500,"text/plain","Online update failed: "+e);return;}
    if(verCmp(String(FW_VERSION),v)>=0){s->sendHeader("Location","/");s->send(302,"text/plain","");return;}
    s->setContentLength(CONTENT_LENGTH_UNKNOWN);
    s->send(200,"text/html; charset=utf-8","");
    s->sendContent("<html><body style='font-family:Arial;background:#111;color:#fff;padding:30px'><h2>Downloading and installing "+v+"...</h2><p><b>DO NOT POWER OFF</b></p>");
    bool ok=installUrl(u,e);
    if(!ok){s->sendContent("<h2>UPDATE FAILED</h2><p>"+e+"</p></body></html>");s->sendContent("");return;}
    s->sendContent("<h2>UPDATE COMPLETE</h2><p>Gateway is restarting...</p><script>setTimeout(()=>location.href='/',7000)</script></body></html>");
    s->sendContent(""); delay(700); ESP.restart();
  });
  server.on("/update",HTTP_POST,[](){
    bool ok=!Update.hasError();
    s->send(200,"text/html",ok?"<h2>Update OK. Rebooting...</h2>":"<h2>Update FAILED</h2>");
    if(ok){delay(700);ESP.restart();}
  },[](){
    HTTPUpload&u=s->upload();
    if(u.status==UPLOAD_FILE_START){Serial.printf("OTA: %s\n",u.filename.c_str()); Update.begin(UPDATE_SIZE_UNKNOWN);}
    else if(u.status==UPLOAD_FILE_WRITE)Update.write(u.buf,u.currentSize);
    else if(u.status==UPLOAD_FILE_END)Update.end(true);
  });
}
