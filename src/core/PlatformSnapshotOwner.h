#pragma once

#include "PlatformSnapshot.h"

class PlatformSnapshotOwner {
public:
  virtual PlatformSnapshot snapshot() const = 0;
  virtual ~PlatformSnapshotOwner() = default;
};
