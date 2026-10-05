#pragma once
#include <Arduino.h>
bool airCloudSend(const String& ip,bool https,const String& command,String* response=nullptr);
String airCloudCurrentMode(const String& ip,bool https);
