#pragma once

#include <Arduino.h>

// airScope / classic LinkPlay TCP API on port 8899.
// Experimental transport for A31.
// HTTP remains the fallback transport.

void airScopeTcpBegin();
void airScopeTcpLoop();

struct AirScopeTcpState {
  bool playKnown = false;
  bool playing = false;
  uint32_t playUpdatedAt = 0;

  bool muteKnown = false;
  bool muted = false;
  uint32_t muteUpdatedAt = 0;

  bool volumeKnown = false;
  uint8_t volume = 0;
  uint32_t volumeUpdatedAt = 0;

  bool modeKnown = false;
  int mode = -1;
  uint32_t modeUpdatedAt = 0;

  // Timestamp of the most recent state event of any type.
  uint32_t updatedAt = 0;
};

// Latest state learned directly from TCP push messages.
bool airScopeTcpGetState(int zone, AirScopeTcpState& state);

// Returns true when a persistent TCP connection is established.
bool airScopeTcpConnected(int zone);

// Send an Arylic TCP command such as:
// MCU+MUT+001
// MCU+PLY+PUS
bool airScopeTcpSend(int zone, const String& command);

// Queue a relative volume change for TCP 8899.
// Multiple fast changes are accumulated and sent respecting
// the Arylic >=200 ms command interval.
bool airScopeTcpQueueVolumeStep(int zone, int delta);


// Diagnostics.
uint32_t airScopeTcpRxPackets(int zone);
uint32_t airScopeTcpTxPackets(int zone);
uint32_t airScopeTcpReconnects(int zone);
uint32_t airScopeTcpLastRx(int zone);
