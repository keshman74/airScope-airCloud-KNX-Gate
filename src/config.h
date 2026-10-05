#pragma once
#include <Arduino.h>

constexpr int MAX_ZONES = 10;
constexpr int MAX_MAPPINGS = 40;
constexpr int INPUT_COUNT = 8;

enum class ZoneType : uint8_t { AirScope=0, AirCloud=1, Custom=2 };

struct Zone {
  String name;
  ZoneType type=ZoneType::AirScope;
  String ip;
  bool enabled=false;
  bool useHttps=false;
  uint8_t inputMask=0x06;
  String fbPlay;
  String fbPause;
  String fbMute;
  String fbNetwork;
  String fbLineIn;
  String fbBluetooth;
};

struct Mapping {
  String ga;
  int zone=0;
  String action="NONE";
  String customUrl;
  String dataType="BIT";
  bool enabled=false;
};

struct NetConfig {
  String ssid;
  String password;
  bool dhcp=true;
  String ip="192.168.0.110";
  String gateway="192.168.0.1";
  String subnet="255.255.255.0";
  String dns="192.168.0.1";
};

extern Zone zones[MAX_ZONES];
extern Mapping mappings[MAX_MAPPINGS];
extern NetConfig netConfig;

bool configBegin();
bool configSave();
