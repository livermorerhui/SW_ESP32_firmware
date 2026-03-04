#include "SystemStateMachine.h"

bool SystemStateMachine::isFaultLocked() const {
  if (st != TopState::FAULT_STOP) return false;
  return (millis() - fault_ms) < FAULT_COOLDOWN_MS;
}

void SystemStateMachine::emitState() {
  if (!bus) return;

  Event e{};
  e.type = EventType::STATE;
  e.state = st;
  e.ts_ms = millis();

  bus->publish(e);
}

void SystemStateMachine::emitFault(FaultCode code) {
  if (!bus) return;

  Event e{};
  e.type = EventType::FAULT;
  e.fault = code;
  e.ts_ms = millis();

  bus->publish(e);
}

void SystemStateMachine::setState(TopState s) {
  if (st == s) return;

  st = s;
  emitState();
}

void SystemStateMachine::onUserOn() {
  if (st == TopState::IDLE) {
    setState(TopState::ARMED);
  }
}

void SystemStateMachine::onUserOff() {
  fault_ms = millis();
  clear_window_active = false;

  setState(TopState::FAULT_STOP);
  emitFault(FaultCode::USER_OFF);
}

void SystemStateMachine::onFallSuspected() {
  fault_ms = millis();
  clear_window_active = false;

  setState(TopState::FAULT_STOP);
  emitFault(FaultCode::FALL_SUSPECTED);
}

void SystemStateMachine::onSensorErr() {
  fault_ms = millis();
  clear_window_active = false;

  setState(TopState::FAULT_STOP);
  emitFault(FaultCode::SENSOR_ERR);
}

bool SystemStateMachine::requestStart(FaultCode& reason) {
  if (st == TopState::FAULT_STOP) {
    reason = FaultCode::FAULT_LOCKED;
    return false;
  }

  if (st != TopState::ARMED) {
    reason = FaultCode::NOT_ARMED;
    return false;
  }

  setState(TopState::RUNNING);
  reason = FaultCode::NONE;
  return true;
}

void SystemStateMachine::requestStop() {
  setState(TopState::IDLE);
}

void SystemStateMachine::onWeightSample(float w) {
  if (st != TopState::FAULT_STOP) return;

  if (isFaultLocked()) {
    clear_window_active = false;
    return;
  }

  if (w < LEAVE_TH) {
    if (!clear_window_active) {
      clear_window_active = true;
      clear_candidate_ms = millis();
    }
    else if (millis() - clear_candidate_ms >= CLEAR_CONFIRM_MS) {
      clear_window_active = false;
      setState(TopState::IDLE);
    }
  } else {
    clear_window_active = false;
  }
}