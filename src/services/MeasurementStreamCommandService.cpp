#include "MeasurementStreamCommandService.h"

#include "HubAckBuilder.h"
#include "modules/laser/LaserModule.h"

MeasurementStreamCommandService::MeasurementStreamCommandService(
    LaserModule* laserModule)
    : laserModule_(laserModule) {}

bool MeasurementStreamCommandService::handles(CmdType type) const {
  return type == CmdType::STREAM_SET;
}

bool MeasurementStreamCommandService::handle(
    const Command& command,
    String& outAck) const {
  if (!laserModule_) {
    outAck = HubAckBuilder::simpleNack("STREAM_SERVICE_UNAVAILABLE");
    return false;
  }
  String reason;
  if (!laserModule_->setStreamSubscription(
          command.streamSubscription.enabled,
          command.streamSubscription.rateHz,
          reason)) {
    outAck = HubAckBuilder::simpleNack(reason);
    return false;
  }
  outAck = HubAckBuilder::streamSubscription(
      laserModule_->streamSubscriptionEnabled(),
      laserModule_->streamSubscriptionRateHz());
  return true;
}
