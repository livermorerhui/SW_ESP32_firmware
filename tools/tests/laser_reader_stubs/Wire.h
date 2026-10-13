#pragma once

struct TwoWire {
  int endCount = 0;
  void end() { ++endCount; }
};

extern TwoWire Wire;
