#pragma once
#include <Arduino.h>

bool airScopeSend(const String& ip,const String& command,String* response=nullptr);
String airScopeCurrentMode(const String& ip);
bool airScopePlayerStatus(const String& ip,String& mode,String& status,String* raw=nullptr);
