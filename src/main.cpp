#include <Arduino.h>
#include "config/GlobalConfig.h"
#include "core/EventBus.h"
#include "core/CommandBus.h"
#include "core/SystemStateMachine.h"
#include "modules/wave/WaveModule.h"
#include "modules/laser/LaserModule.h"
#include "transport/ble/BleTransport.h"

static EventBus g_eventBus;
static CommandBus g_cmdBus;
static SystemStateMachine g_fsm;

static WaveModule g_wave;
static LaserModule g_laser;
static BleTransport g_ble;

class HubHandler : public CommandHandler {
public:
  HubHandler(SystemStateMachine* fsm, WaveModule* wave, LaserModule* laser, BleTransport* ble)
    : sm(fsm), w(wave), l(laser), bt(ble) {}

  bool handle(const Command& c, String& outAck) override {
    switch (c.type) {
      case CmdType::CAP_QUERY:
        outAck = String("ACK:CAP fw=") + FW_VER + " proto=" + String(PROTO_VER);
        return true;

      case CmdType::WAVE_SET:
        w->setFreq(c.wave.freqHz);
        w->setIntensity(c.wave.intensity);
        outAck = "ACK:OK";
        return true;

      case CmdType::WAVE_START: {
        FaultCode reason = FaultCode::NONE;
        if (!sm->requestStart(reason)) {
          outAck = (reason == FaultCode::FAULT_LOCKED) ? "NACK:FAULT_LOCKED" : "NACK:NOT_ARMED";
          return false;
        }
        w->setEnable(true);
        outAck = "ACK:OK";
        return true;
      }

      case CmdType::WAVE_STOP:
        sm->requestStop();
        w->setEnable(false);
        outAck = "ACK:OK";
        return true;

      case CmdType::SCALE_ZERO:
        l->triggerZero();
        outAck = "ACK:OK";
        return true;

      case CmdType::SCALE_CAL:
        l->setParams(c.p1, c.p2);
        outAck = "ACK:OK";
        return true;

      case CmdType::LEGACY_FIE: {
        // 兼容旧命令：只改触碰到的字段
        // freqHz = -1 表示没提供
        if (c.wave.freqHz >= 0) w->setFreq(c.wave.freqHz);
        if (c.wave.intensity >= 0) w->setIntensity(c.wave.intensity);

        // 旧协议里 E:0/1 表示 enable；若没提供 E 则 parseCommand 没法区分
        // 这里建议你旧端若要改 enable 就必须带 E: 字段
        // 简化：只要字符串包含 E: 我们就使用 out.wave.enable（parseCommand 已设置）
        // 由于这里拿不到原串，保守做法：让旧端总是带 E
        w->setEnable(c.wave.enable);

        outAck = "ACK:OK";
        return true;
      }
    }
    outAck = "NACK:UNSUPPORTED";
    return false;
  }

private:
  SystemStateMachine* sm;
  WaveModule* w;
  LaserModule* l;
  BleTransport* bt;
};

static HubHandler g_handler(&g_fsm, &g_wave, &g_laser, &g_ble);

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println("\n=== SonicWave Hub FW (Integrated) ===");

  g_fsm.begin(&g_eventBus);

  g_ble.begin(&g_cmdBus);
  g_eventBus.setSink(&g_ble);
  g_cmdBus.setHandler(&g_handler);

  g_wave.begin();
  g_laser.begin(&g_eventBus, &g_fsm);
  g_laser.startTask();

  Serial.println("Ready ✅");
  Serial.println("Try: CAP?");
  Serial.println("Try: WAVE:SET freq=40 amp=80");
  Serial.println("Try: WAVE:START");
  Serial.println("Try: WAVE:STOP");
}

void loop() {
  // 断连安全策略：断连 -> 停机（并进入 FAULT_STOP，避免误启动）
  static bool lastConn = false;
  bool conn = g_ble.isConnected();

  if (lastConn && !conn && STOP_ON_DISCONNECT) {
    Serial.println("!! disconnect -> safety stop");
    g_fsm.onUserOff();
    g_wave.setEnable(false);
  }
  lastConn = conn;

  delay(20);
}