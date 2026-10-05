#pragma once

#include <Arduino.h>

// airScope / classic LinkPlay TCP API on port 8899.
// Experimental transport for A31.
// HTTP remains the fallback transport.

void airScopeTcpBegin();
void airScopeTcpLoop();

// Returns true when a persistent TCP connection is established.
bool airScopeTcpConnected(int zone);

// Send an Arylic TCP command such as:
// MCU+MUT+001
// MCU+PLY+PUS
bool airScopeTcpSend(int zone, const String& command);

// Diagnostics.
uint32_t airScopeTcpRxPackets(int zone);
uint32_t airScopeTcpTxPackets(int zone);
uint32_t airScopeTcpReconnects(int zone);
uint32_t airScopeTcpLastRx(int zone);
