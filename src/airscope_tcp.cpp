#include "airscope_tcp.h"
#include "config.h"

#include <WiFi.h>
#include <WiFiClient.h>

static constexpr uint16_t AIRSCOPE_TCP_PORT = 8899;
static constexpr uint32_t RECONNECT_INTERVAL_MS = 5000;
static constexpr uint32_t MIN_COMMAND_INTERVAL_MS = 200;
static constexpr size_t RX_BUFFER_SIZE = 1024;

struct AirScopeTcpConnection {
  WiFiClient client;

  bool wasConnected = false;

  uint32_t lastConnectAttempt = 0;
  uint32_t lastRx = 0;
  uint32_t lastTx = 0;

  uint32_t rxPackets = 0;
  uint32_t txPackets = 0;
  uint32_t reconnects = 0;

  uint8_t rxBuffer[RX_BUFFER_SIZE];
  size_t rxLength = 0;
};

static AirScopeTcpConnection connections[MAX_ZONES];
static AirScopeTcpState tcpStates[MAX_ZONES];

static bool validAirScopeZone(int zone) {
  if (zone < 0 || zone >= MAX_ZONES) return false;

  const Zone& z = zones[zone];

  return z.enabled &&
         z.type == ZoneType::AirScope &&
         z.ip.length() > 0;
}

static void resetRx(AirScopeTcpConnection& c) {
  c.rxLength = 0;
}

static void disconnectZone(int zone) {
  AirScopeTcpConnection& c = connections[zone];

  if (c.client.connected()) {
    c.client.stop();
  }

  if (c.wasConnected) {
    Serial.printf(
      "[TCP 8899][airScope] DISCONNECTED zone=%d name=%s ip=%s\n",
      zone,
      zones[zone].name.c_str(),
      zones[zone].ip.c_str()
    );
  }

  c.wasConnected = false;
  resetRx(c);
}

static void connectZone(int zone) {
  if (!validAirScopeZone(zone)) {
    disconnectZone(zone);
    return;
  }

  AirScopeTcpConnection& c = connections[zone];

  if (c.client.connected()) {
    if (!c.wasConnected) {
      c.wasConnected = true;
    }
    return;
  }

  uint32_t now = millis();

  if (now - c.lastConnectAttempt < RECONNECT_INTERVAL_MS) {
    return;
  }

  c.lastConnectAttempt = now;

  c.client.stop();
  resetRx(c);

  Serial.printf(
    "[TCP 8899][airScope] CONNECT zone=%d name=%s ip=%s\n",
    zone,
    zones[zone].name.c_str(),
    zones[zone].ip.c_str()
  );

  c.client.setTimeout(250);

  if (c.client.connect(zones[zone].ip.c_str(), AIRSCOPE_TCP_PORT, 250)) {
    bool reconnect = c.reconnects > 0 || c.wasConnected;

    c.wasConnected = true;

    if (reconnect) {
      c.reconnects++;
    }

    Serial.printf(
      "[TCP 8899][airScope] CONNECTED zone=%d name=%s ip=%s\n",
      zone,
      zones[zone].name.c_str(),
      zones[zone].ip.c_str()
    );
  } else {
    c.client.stop();

    Serial.printf(
      "[TCP 8899][airScope] CONNECT FAILED zone=%d name=%s ip=%s\n",
      zone,
      zones[zone].name.c_str(),
      zones[zone].ip.c_str()
    );
  }
}

static void handleStatePayload(int zone, const String& payload) {
  if (zone < 0 || zone >= MAX_ZONES) return;

  AirScopeTcpState& state = tcpStates[zone];
  bool changed = false;

  if (payload.startsWith("AXX+PLY+")) {
    int value = payload.substring(8, 11).toInt();

    if (value == 0 || value == 1) {
      state.playKnown = true;
      state.playing = (value == 1);
      state.playUpdatedAt = millis();
      changed = true;

      Serial.printf(
        "[TCP STATE][airScope] zone=%d PLAY=%u\n",
        zone,
        state.playing ? 1 : 0
      );
    }
  }
  else if (payload.startsWith("AXX+MUT+")) {
    int value = payload.substring(8, 11).toInt();

    if (value == 0 || value == 1) {
      state.muteKnown = true;
      state.muted = (value == 1);
      state.muteUpdatedAt = millis();
      changed = true;

      Serial.printf(
        "[TCP STATE][airScope] zone=%d MUTE=%u\n",
        zone,
        state.muted ? 1 : 0
      );
    }
  }
  else if (payload.startsWith("AXX+VOL+")) {
    int value = payload.substring(8, 11).toInt();

    if (value >= 0 && value <= 100) {
      state.volumeKnown = true;
      state.volume = static_cast<uint8_t>(value);
      state.volumeUpdatedAt = millis();
      changed = true;

      Serial.printf(
        "[TCP STATE][airScope] zone=%d VOL=%u\n",
        zone,
        state.volume
      );
    }
  }
  else if (payload.startsWith("AXX+PLM+")) {
    int value = payload.substring(8, 11).toInt();

    if (value >= 0 && value <= 999) {
      state.modeKnown = true;
      state.mode = value;
      state.modeUpdatedAt = millis();
      changed = true;

      Serial.printf(
        "[TCP STATE][airScope] zone=%d MODE=%d\n",
        zone,
        state.mode
      );
    }
  }

  if (changed) {
    state.updatedAt = millis();
  }
}

static void printPayload(int zone, const uint8_t* data, size_t len) {
  String payload;
  payload.reserve(len + 1);

  for (size_t i = 0; i < len; i++) {
    char ch = static_cast<char>(data[i]);

    if (ch >= 32 && ch <= 126) {
      payload += ch;
    } else {
      payload += '.';
    }
  }

  Serial.printf(
    "[TCP RX][airScope] zone=%d name=%s payload=%s\n",
    zone,
    zones[zone].name.c_str(),
    payload.c_str()
  );

  handleStatePayload(zone, payload);
}

static void parseRxBuffer(int zone) {
  AirScopeTcpConnection& c = connections[zone];

  static const uint8_t header[4] = {
    0x18, 0x96, 0x18, 0x20
  };

  while (c.rxLength >= 20) {
    size_t start = 0;

    while (
      start + 4 <= c.rxLength &&
      memcmp(c.rxBuffer + start, header, 4) != 0
    ) {
      start++;
    }

    if (start > 0) {
      if (start >= c.rxLength) {
        resetRx(c);
        return;
      }

      memmove(
        c.rxBuffer,
        c.rxBuffer + start,
        c.rxLength - start
      );

      c.rxLength -= start;

      if (c.rxLength < 20) {
        return;
      }
    }

    uint32_t payloadLength =
      static_cast<uint32_t>(c.rxBuffer[4]) |
      (static_cast<uint32_t>(c.rxBuffer[5]) << 8) |
      (static_cast<uint32_t>(c.rxBuffer[6]) << 16) |
      (static_cast<uint32_t>(c.rxBuffer[7]) << 24);

    if (payloadLength > RX_BUFFER_SIZE - 20) {
      Serial.printf(
        "[TCP RX][airScope] INVALID LENGTH zone=%d len=%lu\n",
        zone,
        static_cast<unsigned long>(payloadLength)
      );

      resetRx(c);
      return;
    }

    size_t packetLength = 20 + payloadLength;

    if (c.rxLength < packetLength) {
      return;
    }

    const uint8_t* payload = c.rxBuffer + 20;

    c.rxPackets++;
    c.lastRx = millis();

    printPayload(zone, payload, payloadLength);

    size_t remaining = c.rxLength - packetLength;

    if (remaining > 0) {
      memmove(
        c.rxBuffer,
        c.rxBuffer + packetLength,
        remaining
      );
    }

    c.rxLength = remaining;
  }
}

static void readZone(int zone) {
  AirScopeTcpConnection& c = connections[zone];

  if (!c.client.connected()) {
    if (c.wasConnected) {
      disconnectZone(zone);
    }
    return;
  }

  while (c.client.available() > 0) {
    if (c.rxLength >= RX_BUFFER_SIZE) {
      Serial.printf(
        "[TCP RX][airScope] BUFFER OVERFLOW zone=%d\n",
        zone
      );

      resetRx(c);
    }

    int room = RX_BUFFER_SIZE - c.rxLength;

    int n = c.client.read(
      c.rxBuffer + c.rxLength,
      room
    );

    if (n <= 0) break;

    c.rxLength += static_cast<size_t>(n);

    parseRxBuffer(zone);
  }
}

static size_t buildPacket(
  const String& command,
  uint8_t* output,
  size_t outputSize
) {
  const size_t payloadLength = command.length();
  const size_t totalLength = 20 + payloadLength;

  if (totalLength > outputSize) {
    return 0;
  }

  output[0] = 0x18;
  output[1] = 0x96;
  output[2] = 0x18;
  output[3] = 0x20;

  uint32_t len = payloadLength;

  output[4] = len & 0xFF;
  output[5] = (len >> 8) & 0xFF;
  output[6] = (len >> 16) & 0xFF;
  output[7] = (len >> 24) & 0xFF;

  uint32_t checksum = 0;

  for (size_t i = 0; i < payloadLength; i++) {
    checksum += static_cast<uint8_t>(command[i]);
  }

  output[8]  = checksum & 0xFF;
  output[9]  = (checksum >> 8) & 0xFF;
  output[10] = (checksum >> 16) & 0xFF;
  output[11] = (checksum >> 24) & 0xFF;

  memset(output + 12, 0, 8);

  memcpy(
    output + 20,
    command.c_str(),
    payloadLength
  );

  return totalLength;
}


// ------------------------------------------------------------
// Pending relative volume control.
//
// KNX may generate volume commands faster than the Arylic TCP
// API permits. We accumulate requested +/- changes here and let
// airScopeTcpLoop() send them at a safe rate.
// ------------------------------------------------------------

static int pendingVolumeDelta[MAX_ZONES] = {};
static portMUX_TYPE volumeMux = portMUX_INITIALIZER_UNLOCKED;
static bool volumeGetRequested[MAX_ZONES] = {};

// Last absolute volume successfully sent to the device.
// This prevents fast KNX commands from being calculated from
// an old AXX+VOL state while the new state is still in flight.
static bool optimisticVolumeKnown[MAX_ZONES] = {};
static uint8_t optimisticVolume[MAX_ZONES] = {};

static void servicePendingVolume(int zone) {
  if (!validAirScopeZone(zone)) return;

  AirScopeTcpConnection& c = connections[zone];

  if (!c.client.connected()) {
    volumeGetRequested[zone] = false;
    return;
  }

  portENTER_CRITICAL(&volumeMux);
  int pending = pendingVolumeDelta[zone];
  portEXIT_CRITICAL(&volumeMux);

  if (pending == 0) return;

  uint32_t now = millis();

  // Respect Arylic minimum interval between TCP commands.
  if (c.lastTx != 0 &&
      now - c.lastTx < MIN_COMMAND_INTERVAL_MS) {
    return;
  }

  AirScopeTcpState state{};

  if (!airScopeTcpGetState(zone, state) ||
      !state.volumeKnown) {

    // We have a pending KNX volume command but do not yet know
    // the current A31 volume. Ask once and wait for AXX+VOL.
    if (!volumeGetRequested[zone]) {
      if (airScopeTcpSend(zone, "MCU+VOL+GET")) {
        volumeGetRequested[zone] = true;

        Serial.printf(
          "[VOL][airScope][TCP] zone=%d initial volume requested\n",
          zone
        );
      }
    }

    return;
  }

  volumeGetRequested[zone] = false;

  portENTER_CRITICAL(&volumeMux);
  int delta = pendingVolumeDelta[zone];
  portEXIT_CRITICAL(&volumeMux);

  if (delta == 0) return;

  int baseVolume =
    optimisticVolumeKnown[zone]
      ? (int)optimisticVolume[zone]
      : (int)state.volume;

  int target = baseVolume + delta;

  if (target < 0) target = 0;
  if (target > 100) target = 100;

  // Nothing left to do at the boundary.
  if (target == baseVolume) {
    portENTER_CRITICAL(&volumeMux);
    pendingVolumeDelta[zone] -= delta;
    portEXIT_CRITICAL(&volumeMux);
    return;
  }

  char cmd[20];
  snprintf(cmd, sizeof(cmd), "MCU+VOL+%03d", target);

  if (airScopeTcpSend(zone, String(cmd))) {
    portENTER_CRITICAL(&volumeMux);
    pendingVolumeDelta[zone] -= delta;
    portEXIT_CRITICAL(&volumeMux);

    // Immediately advance our local expected state.
    // A later AXX+VOL push remains the authoritative confirmation.
    optimisticVolume[zone] = static_cast<uint8_t>(target);
    optimisticVolumeKnown[zone] = true;

    Serial.printf(
      "[VOL][airScope][TCP QUEUE] zone=%s current=%d delta=%d target=%d\n",
      zones[zone].name.c_str(),
      baseVolume,
      delta,
      target
    );
  }
}

void airScopeTcpBegin() {
  for (int i = 0; i < MAX_ZONES; i++) {
    connections[i].lastConnectAttempt =
      millis() - RECONNECT_INTERVAL_MS;
  }

  Serial.printf(
    "[TCP 8899][airScope] manager started, max zones=%d\n",
    MAX_ZONES
  );
}

void airScopeTcpLoop() {
  for (int i = 0; i < MAX_ZONES; i++) {
    if (!validAirScopeZone(i)) {
      if (connections[i].client.connected() ||
          connections[i].wasConnected) {
        disconnectZone(i);
      }

      continue;
    }

    if (!connections[i].client.connected()) {
      connectZone(i);
    }

    readZone(i);
    servicePendingVolume(i);
  }
}

bool airScopeTcpConnected(int zone) {
  if (!validAirScopeZone(zone)) return false;

  return connections[zone].client.connected();
}

bool airScopeTcpQueueVolumeStep(int zone, int delta) {
  if (!validAirScopeZone(zone)) return false;

  AirScopeTcpConnection& c = connections[zone];

  if (!c.client.connected()) {
    return false;
  }

  // Accumulate fast KNX volume commands.
  // This function runs on the command worker core while
  // airScopeTcpLoop() consumes the accumulator on another core.
  portENTER_CRITICAL(&volumeMux);

  int next = pendingVolumeDelta[zone] + delta;

  if (next > 100) next = 100;
  if (next < -100) next = -100;

  pendingVolumeDelta[zone] = next;
  int pendingNow = pendingVolumeDelta[zone];

  portEXIT_CRITICAL(&volumeMux);

  Serial.printf(
    "[VOL][airScope][TCP QUEUE] zone=%s add=%d pending=%d\n",
    zones[zone].name.c_str(),
    delta,
    pendingNow
  );

  return true;
}

bool airScopeTcpSend(int zone, const String& command) {
  if (!validAirScopeZone(zone)) return false;

  AirScopeTcpConnection& c = connections[zone];

  if (!c.client.connected()) {
    return false;
  }

  uint32_t now = millis();

  // Arylic documentation requires >= 200 ms between commands.
  if (c.lastTx != 0 &&
      now - c.lastTx < MIN_COMMAND_INTERVAL_MS) {
    Serial.printf(
      "[TCP TX][airScope] RATE LIMIT zone=%d command=%s\n",
      zone,
      command.c_str()
    );

    return false;
  }

  uint8_t packet[512];

  size_t packetLength =
    buildPacket(command, packet, sizeof(packet));

  if (packetLength == 0) {
    Serial.printf(
      "[TCP TX][airScope] PACKET TOO LARGE zone=%d command=%s\n",
      zone,
      command.c_str()
    );

    return false;
  }

  size_t written = c.client.write(packet, packetLength);

  if (written != packetLength) {
    Serial.printf(
      "[TCP TX][airScope] WRITE FAILED zone=%d wrote=%u/%u\n",
      zone,
      static_cast<unsigned>(written),
      static_cast<unsigned>(packetLength)
    );

    return false;
  }

  c.client.flush();

  c.lastTx = millis();
  c.txPackets++;

  Serial.printf(
    "[TCP TX][airScope] zone=%d name=%s command=%s\n",
    zone,
    zones[zone].name.c_str(),
    command.c_str()
  );

  return true;
}

uint32_t airScopeTcpRxPackets(int zone) {
  if (zone < 0 || zone >= MAX_ZONES) return 0;
  return connections[zone].rxPackets;
}

uint32_t airScopeTcpTxPackets(int zone) {
  if (zone < 0 || zone >= MAX_ZONES) return 0;
  return connections[zone].txPackets;
}

uint32_t airScopeTcpReconnects(int zone) {
  if (zone < 0 || zone >= MAX_ZONES) return 0;
  return connections[zone].reconnects;
}

uint32_t airScopeTcpLastRx(int zone) {
  if (zone < 0 || zone >= MAX_ZONES) return 0;
  return connections[zone].lastRx;
}


bool airScopeTcpGetState(int zone, AirScopeTcpState& state) {
  if (zone < 0 || zone >= MAX_ZONES) return false;

  state = tcpStates[zone];

  return state.playKnown ||
         state.muteKnown ||
         state.volumeKnown ||
         state.modeKnown;
}
