#pragma once

#include <Arduino.h>

class FirmwareRollbackConfirmation {
public:
  static void confirmIfPending();
  static void resetEvidence();
  static bool hasEvidence();
  static String evidenceJson();
};
