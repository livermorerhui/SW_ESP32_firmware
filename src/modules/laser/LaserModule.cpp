#include "LaserModule.h"
#include <math.h>

void LaserModule::begin(EventBus* eb, SystemStateMachine* fsm) {
  bus = eb;
  sm = fsm;

  preferences.begin("scale_cal", false);
  zeroDistance = preferences.getFloat("zero", -22.0f);
  scaleFactor  = preferences.getFloat("factor", 1.0f);

  Serial.printf("\n=== LaserModule boot ===\nZero=%.2f K=%.4f\n", zeroDistance, scaleFactor);

  Serial1.begin(MODBUS_BAUD, SERIAL_8N1, RX_PIN, TX_PIN);
  node.begin(MODBUS_SLAVE_ID, Serial1);

  lastMs = millis();
  needSendParams = true;
}

void LaserModule::startTask() {
  xTaskCreatePinnedToCore(taskThunk, "LaserTask", 4096, this, 2, NULL, 1);
  Serial.println("[OK] LaserModule started");
}

void LaserModule::triggerZero() {
  needZero = true;
}

void LaserModule::setParams(float zero, float factor) {
  zeroDistance = zero;
  scaleFactor = factor;
  preferences.putFloat("zero", zeroDistance);
  preferences.putFloat("factor", scaleFactor);

  bufCount = 0; reportedStable = false;
  needSendParams = true;
}

void LaserModule::getParams(float &zero, float &factor) const {
  zero = zeroDistance;
  factor = scaleFactor;
}

float LaserModule::getMean() const {
  float sum = 0;
  for (int i = 0; i < bufCount; i++) sum += weightBuffer[i];
  return (bufCount == 0) ? 0 : (sum / bufCount);
}

float LaserModule::getStdDev() const {
  if (bufCount < 2) return 999.0f;
  float mean = getMean();
  float sumSqDiff = 0;
  for (int i = 0; i < bufCount; i++) sumSqDiff += pow(weightBuffer[i] - mean, 2);
  return sqrt(sumSqDiff / bufCount);
}

void LaserModule::taskThunk(void* arg) {
  static_cast<LaserModule*>(arg)->taskLoop();
}

void LaserModule::taskLoop() {
  uint32_t lastRead = 0;

  while (true) {
    vTaskDelay(pdMS_TO_TICKS(20));

    if (needSendParams) {
      // 发布参数事件（由 BLE Transport 发给 App）
      float z, k; getParams(z, k);
      Event e{}; e.type = EventType::PARAMS; e.v1 = z; e.v2 = k; e.ts_ms = millis();
      if (bus) bus->publish(e);
      needSendParams = false;
    }

    if (millis() - lastRead < 200) continue;
    lastRead = millis();

    uint8_t result = node.readInputRegisters(REG_DISTANCE, 1);
    if (result != node.ku8MBSuccess) {
      static uint32_t lastErr = 0;
      if (millis() - lastErr > 1000) {
        Serial.printf("❌ Modbus read fail (0x%02X)\n", result);
        lastErr = millis();
      }
      // 安全起见：可触发传感器故障停机
      if (sm) sm->onSensorErr();
      continue;
    }

    float dist = (int16_t)node.getResponseBuffer(0) / 100.0f;

    if (needZero) {
      zeroDistance = dist;
      preferences.putFloat("zero", zeroDistance);
      bufCount = 0; reportedStable = false;
      needZero = false;
      Serial.printf("🔘 ZERO done: %.2f\n", zeroDistance);
      lastLogDist = -999.0f;
      needSendParams = true;
    }

    float weight = (dist - zeroDistance) * scaleFactor;
    if (weight < 0) weight = 0.0f;

    // ===== Safety supervisor: user on/off =====
    if (sm) {
      if (weight < LEAVE_TH) sm->onUserOff();
      else if (weight > MIN_WEIGHT) sm->onUserOn();
    }

    // ===== Fall suspected (rate) =====
    uint32_t now = millis();
    float dt = (now - lastMs) / 1000.0f;
    if (dt <= 0) dt = 0.001f;
    float rate = fabsf(weight - lastWeight) / dt;
    if (sm && sm->state() == TopState::RUNNING && rate > FALL_DW_DT_SUSPECT_TH) {
      sm->onFallSuspected();
    }
    lastWeight = weight;
    lastMs = now;

    // ===== Fault clear feed (Gemini rule) =====
    if (sm) sm->onWeightSample(weight);

    // ===== Original stable-weight logic =====
    if (weight < LEAVE_TH) {
      if (bufCount > 0 || reportedStable) {
        Serial.println("🔄 reset (user leave)");
        lastLogDist = -999.0f;
      }
      bufCount = 0; bufHead = 0; reportedStable = false;
    } else if (weight > MIN_WEIGHT) {
      weightBuffer[bufHead] = weight;
      bufHead = (bufHead + 1) % WINDOW_N;
      if (bufCount < WINDOW_N) bufCount++;

      if (bufCount == WINDOW_N && getStdDev() < STD_TH && !reportedStable) {
        float finalWeight = getMean();
        reportedStable = true;
        Serial.printf("✅ STABLE: %.2f kg\n", finalWeight);

        Event e{}; e.type = EventType::STABLE_WEIGHT; e.v1 = finalWeight; e.ts_ms = millis();
        if (bus) bus->publish(e);
      }
    }

    // ===== DEBUG_STREAM =====
    if (DEBUG_STREAM) {
      Event e{}; e.type = EventType::STREAM; e.v1 = dist; e.v2 = weight; e.ts_ms = millis();
      if (bus) bus->publish(e);
    }

    // ===== Silent log =====
    if (abs(dist - lastLogDist) > LOG_TH) {
      Serial.printf(">> Dist %6.2f -> %6.2f | W %5.2f\n",
        lastLogDist == -999.0f ? 0 : lastLogDist, dist, weight);
      lastLogDist = dist;
    }
  }
}