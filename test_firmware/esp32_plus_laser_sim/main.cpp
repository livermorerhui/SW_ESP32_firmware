#include <Arduino.h>
#include <BLE2902.h>
#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

namespace {

constexpr const char* kFirmwareVersion = "SW-HUB-LASER-SIM-1.0.4";
constexpr int kProtocolVersion = 2;
constexpr const char* kDeviceName = "SonicWave_SIM_PLUS";
constexpr const char* kServiceUuid = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* kRxUuid = "6E400002-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr const char* kTxUuid = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
constexpr uint8_t kLaserStatePin = 4;
constexpr uint32_t kDebounceMs = 80;
constexpr size_t kBleNotifyPayloadLimit = 180;
constexpr size_t kBleRxQueueCapacity = 8;
constexpr size_t kBleTxQueueCapacity = 16;

BLECharacteristic* g_tx = nullptr;
bool g_connected = false;
bool g_laserFault = false;
bool g_degradedStartEnabled = false;
bool g_leaveProtectionEnabled = true;
bool g_fallStopEnabled = true;
bool g_standing = true;
bool g_waveActive = false;
float g_frequencyHz = 20.0f;
int g_intensity = 80;
bool g_lastRawFault = false;
bool g_stableRawFault = false;
uint32_t g_rawChangedAtMs = 0;
String g_serialCommand;

String g_bleRxQueue[kBleRxQueueCapacity];
size_t g_bleRxHead = 0;
size_t g_bleRxTail = 0;
size_t g_bleRxCount = 0;

String g_bleTxQueue[kBleTxQueueCapacity];
size_t g_bleTxHead = 0;
size_t g_bleTxTail = 0;
size_t g_bleTxCount = 0;

bool enqueueBleRxLine(const String& line) {
  if (g_bleRxCount >= kBleRxQueueCapacity) {
    Serial.printf("[模拟器] 蓝牙接收队列已满，丢弃：%s\n", line.c_str());
    return false;
  }
  g_bleRxQueue[g_bleRxTail] = line;
  g_bleRxTail = (g_bleRxTail + 1) % kBleRxQueueCapacity;
  ++g_bleRxCount;
  return true;
}

bool dequeueBleRxLine(String& out) {
  if (g_bleRxCount == 0) return false;
  out = g_bleRxQueue[g_bleRxHead];
  g_bleRxQueue[g_bleRxHead] = "";
  g_bleRxHead = (g_bleRxHead + 1) % kBleRxQueueCapacity;
  --g_bleRxCount;
  return true;
}

bool enqueueBleTxLine(const String& line) {
  if (g_bleTxCount >= kBleTxQueueCapacity) {
    Serial.printf("[模拟器] 蓝牙发送队列已满，丢弃：%s\n", line.c_str());
    return false;
  }
  g_bleTxQueue[g_bleTxTail] = line;
  g_bleTxTail = (g_bleTxTail + 1) % kBleTxQueueCapacity;
  ++g_bleTxCount;
  return true;
}

bool dequeueBleTxLine(String& out) {
  if (g_bleTxCount == 0) return false;
  out = g_bleTxQueue[g_bleTxHead];
  g_bleTxQueue[g_bleTxHead] = "";
  g_bleTxHead = (g_bleTxHead + 1) % kBleTxQueueCapacity;
  --g_bleTxCount;
  return true;
}

void clearBleQueues() {
  for (size_t i = 0; i < kBleRxQueueCapacity; ++i) {
    g_bleRxQueue[i] = "";
  }
  for (size_t i = 0; i < kBleTxQueueCapacity; ++i) {
    g_bleTxQueue[i] = "";
  }
  g_bleRxHead = 0;
  g_bleRxTail = 0;
  g_bleRxCount = 0;
  g_bleTxHead = 0;
  g_bleTxTail = 0;
  g_bleTxCount = 0;
}

void handleCommand(String raw);

String boolFlag(bool value) {
  return value ? "1" : "0";
}

const char* zhBool(bool value) {
  return value ? "是" : "否";
}

const char* zhOnOff(bool value) {
  return value ? "开启" : "关闭";
}

const char* zhLaser() {
  return g_laserFault ? "故障" : "正常";
}

const char* topState() {
  if (g_waveActive) return "RUNNING";
  if (!g_laserFault && g_standing) return "ARMED";
  return "IDLE";
}

bool startReady() {
  return g_laserFault ? g_degradedStartEnabled : g_standing;
}

const char* waveStateName() {
  return g_waveActive ? "RUNNING" : "STOPPED";
}

float stableWeightKg() {
  return g_standing ? 60.0f : 0.0f;
}

String snapshotLine() {
  String s;
  s.reserve(260);
  s = "SNAPSHOT:";
  s += "top_state=";
  s += topState();
  s += " start_ready=";
  s += boolFlag(startReady());
  s += " baseline_ready=";
  s += boolFlag(g_standing);
  s += " user_present=";
  s += boolFlag(g_standing);
  s += " stable_weight=";
  s += String(stableWeightKg(), 2);
  s += " laser_available=";
  s += boolFlag(!g_laserFault);
  s += " measurement_health=";
  s += g_laserFault ? "FAULT" : "READY";
  s += " degraded_start_available=";
  s += boolFlag(g_laserFault);
  s += " degraded_start_enabled=";
  s += boolFlag(g_degradedStartEnabled);
  s += " leave_stop_supported=1";
  s += " leave_stop_enabled=";
  s += boolFlag(g_leaveProtectionEnabled);
  s += " fall_stop_enabled=";
  s += boolFlag(g_fallStopEnabled);
  s += " wave_output_active=";
  s += boolFlag(g_waveActive);
  s += " current_frequency=";
  s += String(g_frequencyHz, 0);
  s += " current_intensity=";
  s += String(g_intensity);
  s += " current_reason_code=";
  s += g_laserFault ? "MEASUREMENT_UNAVAILABLE" : "NONE";
  s += " current_safety_effect=";
  s += g_laserFault ? "WARNING_ONLY" : "NONE";
  return s;
}

void notifyLine(const String& line) {
  Serial.println(line);
  if (!g_connected || g_tx == nullptr) {
    return;
  }
  enqueueBleTxLine(line);
}

void sendBleLineNow(const String& line) {
  if (!g_connected || g_tx == nullptr) return;
  String framed = line;
  framed += "\n";
  size_t offset = 0;
  while (offset < framed.length()) {
    const size_t remaining = framed.length() - offset;
    const size_t chunkLen = remaining > kBleNotifyPayloadLimit ? kBleNotifyPayloadLimit : remaining;
    String chunk = framed.substring(offset, offset + chunkLen);
    g_tx->setValue(reinterpret_cast<uint8_t*>(const_cast<char*>(chunk.c_str())), chunk.length());
    g_tx->notify();
    offset += chunkLen;
    if (offset < framed.length()) {
      delay(1);
    }
  }
}

void processBleTxQueue() {
  if (!g_connected || g_tx == nullptr) return;
  String line;
  if (dequeueBleTxLine(line)) {
    sendBleLineNow(line);
  }
}

void processBleRxQueue() {
  String line;
  while (dequeueBleRxLine(line)) {
    handleCommand(line);
    processBleTxQueue();
  }
}

void notifySnapshot(const char* reason) {
  Serial.printf("[模拟器] 上报状态 reason=%s 激光=%s 站稳=%s 律动离开保护=%s 摔倒保护=%s 降级启动=%s 律动输出=%s\n",
                reason ? reason : "unknown",
                zhLaser(),
                zhBool(g_standing),
                zhOnOff(g_leaveProtectionEnabled),
                zhOnOff(g_fallStopEnabled),
                zhOnOff(g_degradedStartEnabled),
                zhOnOff(g_waveActive));
  notifyLine(snapshotLine());
}

void notifySafetyIfFault() {
  if (!g_laserFault) return;
  notifyLine("EVT:SAFETY reason=MEASUREMENT_UNAVAILABLE code=200 effect=WARNING_ONLY state=IDLE wave=STOPPED");
}

void notifyBaseline(const char* reason) {
  String line;
  line.reserve(90);
  line = "EVT:BASELINE start_ready=";
  line += boolFlag(startReady());
  line += " baseline_ready=";
  line += boolFlag(g_standing);
  line += " stable_weight=";
  line += String(stableWeightKg(), 2);
  Serial.printf("[模拟器] 基线 reason=%s 站稳=%s 开始可用=%s\n",
                reason ? reason : "unknown",
                zhBool(g_standing),
                zhBool(startReady()));
  notifyLine(line);
}

void notifyState(const char* reason) {
  Serial.printf("[模拟器] 设备状态 reason=%s top_state=%s\n", reason ? reason : "unknown", topState());
  String line = "EVT:STATE ";
  line += topState();
  notifyLine(line);
}

void notifySafetyClear(const char* reason) {
  Serial.printf("[模拟器] 清除保护事件 reason=%s\n", reason ? reason : "unknown");
  notifyLine("EVT:FAULT 0");
  String line = "EVT:SAFETY reason=NONE code=0 effect=NONE state=";
  line += topState();
  line += " wave=";
  line += waveStateName();
  notifyLine(line);
}

void setLaserFault(bool fault, const char* reason) {
  if (g_laserFault == fault) return;
  g_laserFault = fault;
  if (!g_laserFault) {
    g_degradedStartEnabled = false;
  }
  Serial.printf("[模拟器] 激光=%s reason=%s pin=GPIO%u\n",
                zhLaser(),
                reason ? reason : "unknown",
                static_cast<unsigned>(kLaserStatePin));
  notifyBaseline(reason);
  notifyState(reason);
  notifySnapshot(reason);
  if (g_laserFault) {
    notifySafetyIfFault();
  } else {
    notifySafetyClear(reason);
  }
}

void setStanding(bool standing, const char* reason) {
  if (g_standing == standing) {
    notifyBaseline(reason);
    notifySnapshot(reason);
    return;
  }
  g_standing = standing;
  Serial.printf("[模拟器] 站稳=%s reason=%s\n", zhBool(g_standing), reason ? reason : "unknown");
  notifyBaseline(reason);
  notifyState(reason);
  notifySnapshot(reason);
  if (g_standing) {
    notifySafetyClear(reason);
  }
}

void emitUserLeft(const char* reason) {
  Serial.printf("[模拟器] 触发律动离开 reason=%s 律动离开保护=%s 律动输出=%s\n",
                reason ? reason : "unknown",
                zhOnOff(g_leaveProtectionEnabled),
                zhOnOff(g_waveActive));
  g_standing = false;
  notifyBaseline(reason);

  if (g_leaveProtectionEnabled && g_waveActive) {
    g_waveActive = false;
    notifyLine("EVT:WAVE_OUTPUT active=0");
    notifyLine("EVT:STOP stop_reason=USER_LEFT_PLATFORM stop_source=FORMAL_SAFETY_OTHER code=100 effect=RECOVERABLE_PAUSE state=IDLE");
    notifyLine("EVT:FAULT 100 reason=USER_LEFT_PLATFORM");
    notifyLine("EVT:SAFETY reason=USER_LEFT_PLATFORM code=100 effect=RECOVERABLE_PAUSE state=IDLE wave=STOPPED");
  } else {
    String line = "EVT:SAFETY reason=USER_LEFT_PLATFORM code=100 effect=WARNING_ONLY state=";
    line += topState();
    line += " wave=";
    line += waveStateName();
    notifyLine(line);
  }
  notifyState(reason);
  notifySnapshot(reason);
}

void emitFallSuspected(const char* reason) {
  Serial.printf("[模拟器] 触发摔倒异常 reason=%s 摔倒保护=%s 律动输出=%s\n",
                reason ? reason : "unknown",
                zhOnOff(g_fallStopEnabled),
                zhOnOff(g_waveActive));
  if (g_fallStopEnabled && g_waveActive) {
    g_waveActive = false;
    notifyLine("EVT:WAVE_OUTPUT active=0");
    notifyLine("EVT:STOP stop_reason=FALL_SUSPECTED stop_source=FORMAL_SAFETY_OTHER code=101 effect=ABNORMAL_STOP state=IDLE");
    notifyLine("EVT:FAULT 101 reason=FALL_SUSPECTED");
    notifyLine("EVT:SAFETY reason=FALL_SUSPECTED code=101 effect=ABNORMAL_STOP state=IDLE wave=STOPPED");
  } else {
    String line = "EVT:SAFETY reason=FALL_SUSPECTED code=101 effect=WARNING_ONLY state=";
    line += topState();
    line += " wave=";
    line += waveStateName();
    notifyLine(line);
  }
  notifyState(reason);
  notifySnapshot(reason);
}

void printHelp() {
  Serial.println("[模拟器帮助] 可输入中文命令，也可继续使用 SIM:* 命令");
  Serial.println("  状态 / SIM:STATUS");
  Serial.println("  激光正常 / SIM:LASER ready");
  Serial.println("  激光故障 / SIM:LASER fault");
  Serial.println("  站稳 / SIM:STAND 1");
  Serial.println("  未站稳 / SIM:STAND 0");
  Serial.println("  离开 / 律动离开 / SIM:LEFT");
  Serial.println("  摔倒 / 摔倒异常 / SIM:FALL");
  Serial.println("  清除 / SIM:CLEAR");
  Serial.println("  快照 / SIM:SNAPSHOT");
}

void handleSimCommand(String raw) {
  if (raw == "帮助" || raw == "？" || raw == "?") {
    printHelp();
    return;
  }
  if (raw == "状态") {
    Serial.printf("[模拟器状态] 激光=%s 站稳=%s 律动离开保护=%s 摔倒保护=%s 降级启动=%s 律动输出=%s 频率=%.0f 强度=%d 蓝牙连接=%s\n",
                  zhLaser(),
                  zhBool(g_standing),
                  zhOnOff(g_leaveProtectionEnabled),
                  zhOnOff(g_fallStopEnabled),
                  zhOnOff(g_degradedStartEnabled),
                  zhOnOff(g_waveActive),
                  g_frequencyHz,
                  g_intensity,
                  zhBool(g_connected));
    Serial.println(snapshotLine());
    return;
  }
  if (raw == "激光正常") {
    setLaserFault(false, "serial_zh");
    return;
  }
  if (raw == "激光故障") {
    setLaserFault(true, "serial_zh");
    return;
  }
  if (raw == "站稳") {
    setStanding(true, "serial_zh");
    return;
  }
  if (raw == "未站稳" || raw == "不站稳") {
    setStanding(false, "serial_zh");
    return;
  }
  if (raw == "离开" || raw == "律动离开") {
    emitUserLeft("serial_zh");
    return;
  }
  if (raw == "摔倒" || raw == "摔倒异常") {
    emitFallSuspected("serial_zh");
    return;
  }
  if (raw == "清除") {
    notifySafetyClear("serial_zh");
    notifySnapshot("serial_zh_clear");
    return;
  }
  if (raw == "快照") {
    notifyBaseline("serial_zh_snapshot");
    notifyState("serial_zh_snapshot");
    notifySnapshot("serial_zh_snapshot");
    return;
  }

  String upper = raw;
  upper.toUpperCase();

  if (upper == "SIM:HELP" || upper == "SIM:?") {
    printHelp();
    return;
  }
  if (upper == "SIM:STATUS") {
    Serial.printf("[模拟器状态] 激光=%s 站稳=%s 律动离开保护=%s 摔倒保护=%s 降级启动=%s 律动输出=%s 频率=%.0f 强度=%d 蓝牙连接=%s\n",
                  zhLaser(),
                  zhBool(g_standing),
                  zhOnOff(g_leaveProtectionEnabled),
                  zhOnOff(g_fallStopEnabled),
                  zhOnOff(g_degradedStartEnabled),
                  zhOnOff(g_waveActive),
                  g_frequencyHz,
                  g_intensity,
                  zhBool(g_connected));
    Serial.println(snapshotLine());
    return;
  }
  if (upper.startsWith("SIM:LASER")) {
    const bool fault = upper.indexOf("FAULT") >= 0 ||
        upper.indexOf("1") >= 0 ||
        upper.indexOf("TRUE") >= 0;
    setLaserFault(fault, "serial");
    return;
  }
  if (upper.startsWith("SIM:STAND")) {
    const bool standing = upper.indexOf("0") < 0 &&
        upper.indexOf("FALSE") < 0 &&
        upper.indexOf("NO") < 0;
    setStanding(standing, "serial");
    return;
  }
  if (upper == "SIM:LEFT") {
    emitUserLeft("serial");
    return;
  }
  if (upper == "SIM:FALL") {
    emitFallSuspected("serial");
    return;
  }
  if (upper == "SIM:CLEAR") {
    notifySafetyClear("serial");
    notifySnapshot("serial_clear");
    return;
  }
  if (upper == "SIM:SNAPSHOT") {
    notifyBaseline("serial_snapshot");
    notifyState("serial_snapshot");
    notifySnapshot("serial_snapshot");
    return;
  }
  Serial.printf("[模拟器] 未识别命令：%s，输入“帮助”查看命令\n", raw.c_str());
}

void handleCommand(String raw) {
  raw.trim();
  if (raw.length() == 0) return;
  Serial.printf("[模拟器收到蓝牙指令] %s\n", raw.c_str());

  if (raw.startsWith("SIM:")) {
    handleSimCommand(raw);
    return;
  }

  if (raw.equalsIgnoreCase("CAP?")) {
    String cap;
    cap.reserve(120);
    cap = "ACK:CAP fw=";
    cap += kFirmwareVersion;
    cap += " proto=";
    cap += kProtocolVersion;
    cap += " platform_model=PLUS laser_installed=1 leave_stop_supported=1";
    notifyLine(cap);
    return;
  }

  if (raw.equalsIgnoreCase("SNAPSHOT?")) {
    notifySnapshot("query");
    return;
  }

  if (raw.startsWith("WAVE:SET")) {
    int fIndex = raw.indexOf("f=");
    int iIndex = raw.indexOf("i=");
    if (fIndex >= 0) {
      g_frequencyHz = raw.substring(fIndex + 2).toFloat();
    }
    if (iIndex >= 0) {
      g_intensity = raw.substring(iIndex + 2).toInt();
    }
    notifyLine("ACK:OK");
    notifySnapshot("wave_set");
    return;
  }

  if (raw.equalsIgnoreCase("WAVE:START")) {
    if (!startReady()) {
      notifyLine("NACK:NOT_ARMED");
      notifySnapshot("start_blocked");
      return;
    }
    g_waveActive = true;
    notifyLine("ACK:OK");
    notifyLine("EVT:WAVE_OUTPUT active=1");
    notifySnapshot("wave_start");
    return;
  }

  if (raw.equalsIgnoreCase("WAVE:STOP")) {
    g_waveActive = false;
    notifyLine("ACK:OK");
    notifyLine("EVT:WAVE_OUTPUT active=0");
    notifyLine("EVT:STOP stop_reason=MANUAL_STOP stop_source=APP_COMMAND code=0 effect=NORMAL_STOP state=IDLE");
    notifySnapshot("wave_stop");
    return;
  }

  if (raw.startsWith("DEBUG:DEGRADED_START")) {
    const bool requestedEnabled = raw.indexOf("enabled=1") >= 0 ||
        raw.indexOf("enabled=true") >= 0 ||
        raw.indexOf("mode=1") >= 0;
    g_degradedStartEnabled = requestedEnabled && g_laserFault;
    String ack = "ACK:DEGRADED_START enabled=";
    ack += boolFlag(g_degradedStartEnabled);
    ack += " available=";
    ack += boolFlag(g_laserFault);
    notifyLine(ack);
    notifySnapshot("degraded_start");
    return;
  }

  if (raw.startsWith("SAFETY:LEAVE_PROTECTION")) {
    const bool enabled = raw.indexOf("enabled=0") < 0 && raw.indexOf("enabled=false") < 0;
    g_leaveProtectionEnabled = enabled;
    String ack = "ACK:LEAVE_PROTECTION enabled=";
    ack += boolFlag(enabled);
    ack += " supported=1 effect=";
    ack += enabled ? "ENABLED_PAUSE" : "WARNING_ONLY";
    notifyLine(ack);
    notifySnapshot("leave_protection");
    return;
  }

  if (raw.startsWith("DEBUG:FALL_STOP")) {
    const bool enabled = raw.indexOf("enabled=0") < 0 && raw.indexOf("enabled=false") < 0;
    g_fallStopEnabled = enabled;
    String ack = "ACK:FALL_STOP enabled=";
    ack += boolFlag(enabled);
    ack += " mode=";
    ack += enabled ? "ENFORCED" : "DETECT_ONLY";
    notifyLine(ack);
    notifySnapshot("fall_stop");
    return;
  }

  notifyLine("NACK:UNKNOWN_CMD");
}

class SimServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    clearBleQueues();
    g_connected = true;
    Serial.println("[模拟器] 蓝牙已连接");
  }

  void onDisconnect(BLEServer* server) override {
    g_connected = false;
    g_waveActive = false;
    clearBleQueues();
    Serial.println("[模拟器] 蓝牙已断开，已重新开始广播");
    if (server != nullptr) {
      server->startAdvertising();
    }
  }
};

class SimRxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* characteristic) override {
    std::string value = characteristic->getValue();
    String line(value.c_str());
    line.trim();
    if (line.length() > 0) {
      enqueueBleRxLine(line);
    }
  }
};

void configureBle() {
  BLEDevice::init(kDeviceName);
  BLEDevice::setMTU(185);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new SimServerCallbacks());

  BLEService* service = server->createService(kServiceUuid);
  g_tx = service->createCharacteristic(kTxUuid, BLECharacteristic::PROPERTY_NOTIFY);
  g_tx->addDescriptor(new BLE2902());

  BLECharacteristic* rx = service->createCharacteristic(kRxUuid, BLECharacteristic::PROPERTY_WRITE);
  rx->setCallbacks(new SimRxCallbacks());

  service->start();

  BLEAdvertising* advertising = BLEDevice::getAdvertising();
  BLEAdvertisementData advData;
  advData.setFlags(0x06);
  advData.setCompleteServices(BLEUUID(kServiceUuid));

  BLEAdvertisementData scanRespData;
  scanRespData.setName(kDeviceName);
  scanRespData.setServiceData(BLEUUID(kServiceUuid), "proto=2;fw=SW-HUB-LASER-SIM-1.0.0;model=PLUS");

  advertising->setAdvertisementData(advData);
  advertising->setScanResponseData(scanRespData);
  advertising->addServiceUUID(kServiceUuid);
  advertising->setScanResponse(true);
  advertising->start();
}

void readLaserPinDebounced() {
  const bool rawFault = digitalRead(kLaserStatePin) == LOW;
  const uint32_t now = millis();
  if (rawFault != g_lastRawFault) {
    g_lastRawFault = rawFault;
    g_rawChangedAtMs = now;
  }
  if (rawFault != g_stableRawFault && now - g_rawChangedAtMs >= kDebounceMs) {
    g_stableRawFault = rawFault;
    setLaserFault(g_stableRawFault, "gpio");
  }
}

void readSerialCommands() {
  while (Serial.available() > 0) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') continue;
    if (ch == '\b' || ch == 127) {
      if (g_serialCommand.length() > 0) {
        g_serialCommand.remove(g_serialCommand.length() - 1);
        Serial.print("\b \b");
      }
      continue;
    }
    if (ch == '\n') {
      Serial.println();
      g_serialCommand.trim();
      if (g_serialCommand.length() > 0) {
        handleSimCommand(g_serialCommand);
      }
      g_serialCommand = "";
      continue;
    }
    Serial.write(ch);
    g_serialCommand += ch;
    if (g_serialCommand.length() > 120) {
      g_serialCommand = "";
      Serial.println("[模拟器] 串口命令过长，已清空。输入“帮助”查看命令");
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(kLaserStatePin, INPUT_PULLUP);
  g_lastRawFault = digitalRead(kLaserStatePin) == LOW;
  g_stableRawFault = g_lastRawFault;
  g_laserFault = g_stableRawFault;
  configureBle();
  Serial.printf("[模拟器] ESP32-Plus 假设备固件已启动，版本=%s，激光故障输入脚=GPIO%u\n",
                kFirmwareVersion,
                static_cast<unsigned>(kLaserStatePin));
  Serial.printf("[模拟器] 当前激光状态=%s，GPIO%u 接 GND 表示激光故障\n",
                zhLaser(),
                static_cast<unsigned>(kLaserStatePin));
  printHelp();
}

void loop() {
  readSerialCommands();
  readLaserPinDebounced();
  processBleRxQueue();
  processBleTxQueue();
  delay(20);
}
