#include "webui.h"
#include "config.h"
#include "ota.h"
#include <WebServer.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>
#include "logo_data.h"

static WebServer server(80);
static String esc(String s){s.replace("&","&amp;");s.replace("<","&lt;");s.replace(">","&gt;");s.replace("\"","&quot;");s.replace("'","&#39;");return s;}
static const char* acts[]={"NONE","PLAY_PAUSE","PLAY","PAUSE","NEXT","PREVIOUS","VOL_UP","VOL_DOWN","VOLUME","MUTE",
"PRESET_1","PRESET_2","PRESET_3","PRESET_4","PRESET_5","PRESET_6","PRESET_7","PRESET_8","PRESET_9","PRESET_10",
"INPUT_NEXT","INPUT_LINE_IN","INPUT_BLUETOOTH","INPUT_OPTICAL","INPUT_COAXIAL","INPUT_LINE_IN_2","INPUT_USB_DISK","INPUT_PC_USB","CUSTOM_GET"};
static const char* inLab[]={"Network/WiFi","Line/RCA","Bluetooth","Optical","Coaxial","Line2","USB Disk","PC USB"};

static String typeOpt(ZoneType t){
 String x="<option value='0'";if(t==ZoneType::AirScope)x+=" selected";x+=">airScope A31/A97/A98</option>";
 x+="<option value='1'";if(t==ZoneType::AirCloud)x+=" selected";x+=">airCloud A33</option>";
 x+="<option value='2'";if(t==ZoneType::Custom)x+=" selected";x+=">Custom HTTP/S</option>";return x;
}
static String actionOpt(const String&cur){
 String x; for(auto a:acts)x+="<option"+String(cur==a?" selected":"")+">"+String(a)+"</option>";return x;
}
static String dataTypeOpt(const String& cur){
 String x;
 x+="<option value='BIT'"+String(cur=="BIT"?" selected":"")+">1-bit Button</option>";
 x+="<option value='DIM'"+String(cur=="DIM"?" selected":"")+">4-bit Dimming (DPT 3.007)</option>";
 x+="<option value='PERCENT'"+String(cur=="PERCENT"?" selected":"")+">1-byte Percent (DPT 5.001)</option>";
 return x;
}
static String zoneOpt(int cur){
 String x;for(int z=0;z<MAX_ZONES;z++)if(zones[z].enabled)x+="<option value='"+String(z)+"'"+String(cur==z?" selected":"")+">"+esc(zones[z].name)+"</option>";return x;
}
static String wifiOptions(){
 String x="<option value='"+esc(netConfig.ssid)+"'>"+esc(netConfig.ssid)+" (saved)</option>";
 int n=WiFi.scanNetworks(false,true);
 if(n<=0){x+="<option disabled>No networks found</option>";return x;}
 for(int i=0;i<n;i++){
   String ss=WiFi.SSID(i); if(!ss.length())continue;
   x+="<option value='"+esc(ss)+"'>"+esc(ss)+" ("+String(WiFi.RSSI(i))+" dBm"+(WiFi.encryptionType(i)==WIFI_AUTH_OPEN?", open":"")+")</option>";
 }
 WiFi.scanDelete(); return x;
}
static void chunk(const String& s){ server.sendContent(s); }
static void logo(){
 server.sendHeader("Cache-Control","public, max-age=86400");
 server.send_P(200,"image/png",(const char*)AIRSCOPE_LOGO_PNG,AIRSCOPE_LOGO_PNG_LEN);
}
static void root(){
 // Stream the page in small chunks. Never build the complete HTML in RAM.
 server.setContentLength(CONTENT_LENGTH_UNKNOWN);
 server.send(200,"text/html; charset=utf-8","");
 chunk(R"HTML(<html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width'>
 <style>body{font-family:-apple-system,Arial;background:#101012;color:#eee;margin:0}.w{max-width:1120px;margin:auto;padding:20px}.c{background:#1d1d20;border:1px solid #333;border-radius:12px;padding:18px;margin:14px 0}
 input,select{background:#29292d;color:#fff;border:1px solid #48484d;border-radius:7px;padding:9px;margin:4px}.wide{width:min(420px,85%)}button{background:#fff;color:#111;border:0;border-radius:8px;padding:10px 16px;font-weight:700;margin:5px;cursor:pointer}
 .row{padding:10px 0;border-bottom:1px solid #333}.muted{color:#999}.save{width:100%;font-size:18px;margin:12px 0}.hidden{display:none}.danger{background:#522;color:#fff}
 .fb{margin:10px 0;padding:10px 12px;background:#171719;border-radius:8px}.fb summary{cursor:pointer;font-weight:700}.fbgrid{display:grid;grid-template-columns:repeat(2,minmax(220px,1fr));gap:6px 14px;margin-top:8px}.fbgrid label{display:flex;align-items:center;justify-content:space-between;gap:8px}.fbgrid input{width:120px}
 .otaOverlay{display:none;position:fixed;inset:0;background:rgba(0,0,0,.88);z-index:9999;align-items:center;justify-content:center;text-align:center}.otaBox{background:#1d1d20;border:1px solid #444;border-radius:14px;padding:28px;min-width:280px;max-width:520px}.spin{width:42px;height:42px;border:5px solid #444;border-top-color:#fff;border-radius:50%;margin:0 auto 18px;animation:sp .8s linear infinite}@keyframes sp{to{transform:rotate(360deg)}}.warn{font-weight:800;margin-top:12px}.progress{height:12px;background:#333;border-radius:8px;overflow:hidden;margin:16px 0}.bar{height:100%;width:0;background:#fff;transition:width .2s} .head{display:flex;align-items:center;justify-content:space-between;gap:24px}.headtext{min-width:0}.brand{width:220px;max-width:34vw;height:auto;object-fit:contain}@media(max-width:650px){.fbgrid{grid-template-columns:1fr}.brand{width:150px;max-width:38vw}.head{gap:10px}}
 </style><script>
 function addNext(prefix,enPrefix,max){for(let i=0;i<max;i++){let e=document.getElementById(prefix+i);if(e&&e.classList.contains('hidden')){e.classList.remove('hidden');document.querySelector('[name='+enPrefix+i+']').value='1';return;}}alert('No free slots');}
 function del(id,en){document.querySelector('[name='+en+']').value='0';document.getElementById(id).classList.add('hidden')}
 function copySSID(){var s=document.getElementById('scan');if(s.value)document.getElementById('ssid').value=s.value}
 function otaShow(msg){document.getElementById('otaMsg').textContent=msg||'Updating firmware...';document.getElementById('otaOverlay').style.display='flex'}
 function localOta(e){e.preventDefault();var f=document.getElementById('fwfile');if(!f.files.length){alert('Choose firmware.bin first');return false;}otaShow('Uploading firmware...');var x=new XMLHttpRequest(),d=new FormData(e.target);x.upload.onprogress=function(ev){if(ev.lengthComputable){var p=Math.round(ev.loaded*100/ev.total);document.getElementById('otaBar').style.width=p+'%';document.getElementById('otaPct').textContent=p+'%';}};x.onload=function(){document.getElementById('otaMsg').textContent=x.status==200?'Update complete. Gateway is restarting...':'UPDATE FAILED';if(x.status==200)setTimeout(function(){location.href='/'},7000);};x.onerror=function(){document.getElementById('otaMsg').textContent='UPDATE FAILED';};x.open('POST','/update');x.send(d);return false;}
 function checkOnline(){otaShow('Checking for online update...');fetch('/ota/check').then(r=>r.json()).then(j=>{document.getElementById('otaOverlay').style.display='none';if(j.error){alert('Online update check failed: '+j.error);return;}if(!j.available){alert('Firmware is up to date. Installed: '+j.installed+'  Latest: '+j.latest);return;}if(confirm('New firmware available\nInstalled: '+j.installed+'\nAvailable: '+j.latest+'\n\nUpdate now?')){otaShow('Downloading and installing '+j.latest+'...');location.href='/ota/install';}}).catch(()=>{document.getElementById('otaOverlay').style.display='none';alert('Online update check failed');});}
 </script></head><body><div class='w'><div class='head'><div class='headtext'><h1>KNX → airScope / airCloud Gateway</h1>
 <div class='muted'>Firmware v0.8.12 · KNXnet/IP · airScope (A31/A97/A98 chip) / airCloud (A33 chip)</div></div><img class='brand' src='/airscope-logo.png' alt='airScope'></div>
 <form method='POST' action='/save'><div class='c'><h2>NETWORK</h2><b>Available Wi-Fi networks</b><br><select id='scan' onchange='copySSID()'>)HTML");
 chunk(wifiOptions());
 chunk("</select> <button type='button' onclick='location.reload()'>RESCAN WI-FI</button><br>");
 chunk("SSID <input id='ssid' class='wide' name='ssid' value='"+esc(netConfig.ssid)+"'><br>Password <input class='wide' type='password' name='pass' placeholder='leave blank to keep current password'><br>");
 chunk("IP mode <select name='dhcp'><option value='1'"+String(netConfig.dhcp?" selected":"")+">DHCP</option><option value='0'"+String(!netConfig.dhcp?" selected":"")+">STATIC</option></select><br>");
 chunk("IP <input name='nip' value='"+esc(netConfig.ip)+"'> Gateway <input name='gw' value='"+esc(netConfig.gateway)+"'><br>Subnet <input name='mask' value='"+esc(netConfig.subnet)+"'> DNS <input name='dns' value='"+esc(netConfig.dns)+"'></div>");
 chunk("<button class='save' type='submit'>SAVE & RESTART</button><div class='c'><h2>ZONES</h2>");
 for(int i=0;i<MAX_ZONES;i++){
  String p=String(i); bool on=zones[i].enabled; String r;
  r.reserve(3500);
  r+="<div class='row "+String(on?"":"hidden")+"' id='z"+p+"'><input type='hidden' name='ze"+p+"' value='"+String(on?"1":"0")+"'><b>"+esc(on?zones[i].name:String("New Zone ")+String(i+1))+"</b><br>";
  r+="Name <input name='zn"+p+"' value='"+esc(on?zones[i].name:String(""))+"' placeholder='Zone "+String(i+1)+"'> IP <input name='zi"+p+"' value='"+esc(on?zones[i].ip:String(""))+"' placeholder='192.168.0.x'> <select name='zt"+p+"'>"+typeOpt(on?zones[i].type:ZoneType::AirScope)+"</select>";
  r+=" A33 HTTPS <input type='checkbox' name='zh"+p+"' "+String(on&&zones[i].useHttps?"checked":"")+"><br><span class='muted'>INPUT NEXT physical inputs:</span>";
  for(int q=1;q<INPUT_COUNT;q++){bool checked=on?(zones[i].inputMask&(1<<q)):(q==1||q==2);r+=" <label><input type='checkbox' name='in"+p+"_"+String(q)+"' "+String(checked?"checked":"")+">"+String(inLab[q])+"</label>";}
  r+="<br><span class='muted'>airScope Network/Wi-Fi is entered by PRESET and is not cycled by INPUT NEXT.</span>";
  r+="<details class='fb'><summary>KNX FEEDBACK (airScope)</summary><div class='muted'>Empty GA = disabled. Telegram is sent only when status changes.</div><div class='fbgrid'>";
  r+="<label>PLAY <input name='fp"+p+"' value='"+esc(on?zones[i].fbPlay:String(""))+"' placeholder='3/4/0'></label>";
  r+="<label>PAUSE <input name='fz"+p+"' value='"+esc(on?zones[i].fbPause:String(""))+"' placeholder='3/4/1'></label>";
  r+="<label>MUTE <input name='fm"+p+"' value='"+esc(on?zones[i].fbMute:String(""))+"' placeholder='3/4/5'></label>";
  r+="<label>NETWORK <input name='fn"+p+"' value='"+esc(on?zones[i].fbNetwork:String(""))+"' placeholder='3/4/4'></label>";
  r+="<label>LINE-IN <input name='fl"+p+"' value='"+esc(on?zones[i].fbLineIn:String(""))+"' placeholder='3/4/2'></label>";
  r+="<label>BLUETOOTH <input name='fb"+p+"' value='"+esc(on?zones[i].fbBluetooth:String(""))+"' placeholder='3/4/3'></label></div></details>";
  r+="<br><button type='button' class='danger' onclick=\"del('z"+p+"','ze"+p+"')\">DELETE</button></div>"; chunk(r);
 }
 chunk("<button type='button' onclick=\"addNext('z','ze',"+String(MAX_ZONES)+")\">+ ADD ZONE</button></div><div class='c'><h2>KNX MAPPINGS</h2>");
 for(int i=0;i<MAX_MAPPINGS;i++){
  String p=String(i); bool on=mappings[i].enabled; String r; r.reserve(3000);
  r+="<div class='row "+String(on?"":"hidden")+"' id='m"+p+"'><input type='hidden' name='me"+p+"' value='"+String(on?"1":"0")+"'><input name='mg"+p+"' value='"+esc(on?mappings[i].ga:String(""))+"' placeholder='3/0/0'><select name='mz"+p+"'>";
  for(int z=0;z<MAX_ZONES;z++){String label=zones[z].enabled?zones[z].name:String("Zone ")+String(z+1);int cur=on?mappings[i].zone:0;r+="<option value='"+String(z)+"'"+String(cur==z?" selected":"")+">"+esc(label)+"</option>";}
  r+="</select><select name='ma"+p+"'>"+actionOpt(on?mappings[i].action:String("NONE"))+"</select>";
  r+="<select name='md"+p+"'>"+dataTypeOpt(on?mappings[i].dataType:String("BIT"))+"</select>";
  r+="<input class='wide' name='mu"+p+"' value='"+esc(on?mappings[i].customUrl:String(""))+"' placeholder='Custom http(s):// URL'><button type='button' class='danger' onclick=\"del('m"+p+"','me"+p+"')\">DELETE</button></div>"; chunk(r);
 }
 chunk("<button type='button' onclick=\"addNext('m','me',"+String(MAX_MAPPINGS)+")\">+ ADD MAPPING</button></div><button class='save' type='submit'>SAVE & RESTART</button></form>");
 chunk("<div class='c'><h2>SYSTEM</h2>Firmware <b>v0.8.12</b><br><b>OTA Online Test: v0.8.12</b><br>Wi-Fi <b>"+String(WiFi.status()==WL_CONNECTED?"Connected":"Recovery AP")+"</b><br>");
 chunk("SSID <b>"+esc(WiFi.status()==WL_CONNECTED?WiFi.SSID():String("airScope-Gateway-Setup"))+"</b><br>Gateway IP <b>"+(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():WiFi.softAPIP().toString())+"</b><br>");
 chunk("Free Heap <b>"+String(ESP.getFreeHeap())+" bytes</b><br>Minimum Free Heap <b>"+String(ESP.getMinFreeHeap())+" bytes</b><br>Largest Free Block <b>"+String(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT))+" bytes</b><br>LittleFS <b>"+String(LittleFS.usedBytes())+" / "+String(LittleFS.totalBytes())+" bytes</b></div>");
 chunk("<div class='c'><h2>FIRMWARE UPDATE</h2><div>Installed: <b>v0.8.12</b></div><p><button type='button' onclick='checkOnline()'>CHECK & UPDATE ONLINE</button></p><p class='muted'>Online update checks the official firmware channel. Configuration in LittleFS is preserved.</p><hr style='border:0;border-top:1px solid #333;margin:18px 0'><b>Local firmware</b><form method='POST' action='/update' enctype='multipart/form-data' onsubmit='return localOta(event)'><input id='fwfile' type='file' name='firmware' accept='.bin'><button>INSTALL LOCAL FIRMWARE</button></form><p class='muted'>Do not disconnect power while firmware is being installed.</p></div><div id='otaOverlay' class='otaOverlay'><div class='otaBox'><div class='spin'></div><h2 id='otaMsg'>Updating firmware...</h2><div class='progress'><div id='otaBar' class='bar'></div></div><div id='otaPct'>Please wait...</div><div class='warn'>DO NOT POWER OFF</div></div></div></div></body></html>");
 server.sendContent("");
 Serial.printf("WEB: streamed; freeHeap=%u minHeap=%u largest=%u\n",(unsigned)ESP.getFreeHeap(),(unsigned)ESP.getMinFreeHeap(),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT));
}
static void save(){
 netConfig.ssid=server.arg("ssid");if(server.arg("pass").length())netConfig.password=server.arg("pass");
 netConfig.dhcp=server.arg("dhcp")=="1";netConfig.ip=server.arg("nip");netConfig.gateway=server.arg("gw");netConfig.subnet=server.arg("mask");netConfig.dns=server.arg("dns");
 for(int i=0;i<MAX_ZONES;i++){
  String p=String(i);zones[i].enabled=server.hasArg("ze"+p)&&server.arg("ze"+p)=="1";
  if(zones[i].enabled){
   zones[i].name=server.arg("zn"+p);zones[i].ip=server.arg("zi"+p);zones[i].type=(ZoneType)server.arg("zt"+p).toInt();zones[i].useHttps=server.hasArg("zh"+p);zones[i].inputMask=0;
   for(int q=0;q<INPUT_COUNT;q++)if(server.hasArg("in"+p+"_"+String(q)))zones[i].inputMask|=(1<<q);
   zones[i].fbPlay=server.arg("fp"+p);zones[i].fbPause=server.arg("fz"+p);zones[i].fbMute=server.arg("fm"+p);zones[i].fbNetwork=server.arg("fn"+p);zones[i].fbLineIn=server.arg("fl"+p);zones[i].fbBluetooth=server.arg("fb"+p);
  }
 }
 for(int i=0;i<MAX_MAPPINGS;i++){
  String p=String(i);mappings[i].enabled=server.hasArg("me"+p)&&server.arg("me"+p)=="1";
  if(mappings[i].enabled){mappings[i].ga=server.arg("mg"+p);mappings[i].zone=server.arg("mz"+p).toInt();mappings[i].action=server.arg("ma"+p);mappings[i].customUrl=server.arg("mu"+p);mappings[i].dataType=server.arg("md"+p);if(!mappings[i].dataType.length())mappings[i].dataType="BIT";}
 }
 if(!configSave()){server.send(500,"text/plain","Configuration save failed");return;}
 server.send(200,"text/html; charset=utf-8","<html><body style='font-family:Arial;background:#111;color:#fff;padding:30px'><h2>Configuration saved</h2><p>Gateway is restarting...</p></body></html>");
 delay(700);ESP.restart();
}
void webBegin(){server.on("/",HTTP_GET,root);server.on("/airscope-logo.png",HTTP_GET,logo);server.on("/save",HTTP_POST,save);otaAttach(server);server.begin();Serial.println("WEB server: started");}
void webLoop(){server.handleClient();}
