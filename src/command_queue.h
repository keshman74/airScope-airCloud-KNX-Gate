#pragma once
#include <Arduino.h>
void commandQueueBegin();
bool commandEnqueue(int zone,const String& action,uint8_t value,const String& customUrl="");
void commandQueueLoop();
