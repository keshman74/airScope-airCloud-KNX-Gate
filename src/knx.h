#pragma once
#include <Arduino.h>
void knxBegin();
void knxLoop();
bool knxSendBit(const String& groupAddress,bool value);
