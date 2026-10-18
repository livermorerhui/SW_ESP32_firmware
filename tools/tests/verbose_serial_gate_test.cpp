// 用实际宏验证开启/关闭时的参数求值与 sink 调用，不替换生产宏。
#include <cstdio>

#include "core/VerboseSerial.h"
namespace FirmwareDiagnostics {
int sinkCalls = 0;
bool detailEnabled(const char*) { return true; }
Admission detailf(const char*, const char*, ...) { ++sinkCalls; return Admission::Accepted; }
}

namespace {
int fieldCalls = 0;
int failures = 0;
const char* field() { ++fieldCalls; return "值"; }
void expect(const char* name, int invocation) {
  const int expected = invocation * SERIAL_VERBOSE_DIAGNOSTICS;
  const bool passed = fieldCalls == expected && FirmwareDiagnostics::sinkCalls == expected;
  if (!passed) ++failures;
  std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
}
}

int main() {
  SW_VERBOSE_SERIAL_PRINTF("%s", field());
  expect("printf_lazy_gate", 1);
  SW_VERBOSE_SERIAL_PRINTLN(field());
  expect("println_lazy_gate", 2);
  SW_VERBOSE_SERIAL_PRINT(field());
  expect("print_lazy_gate", 3);
  std::printf("assertions=3 failures=%d verbose=%d\n", failures, SERIAL_VERBOSE_DIAGNOSTICS);
  return failures ? 1 : 0;
}
