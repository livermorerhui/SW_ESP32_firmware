#include "FirmwareDiagnosticSink.h"
#include <Arduino.h>
#if !ARDUINO_USB_CDC_ON_BOOT
#include <driver/uart.h>
#endif
namespace FirmwareDiagnostics {
void prepareSink() {
#if ARDUINO_USB_CDC_ON_BOOT
  // 限制诊断后台的单次内部等待；不改变 USB/UART 路由。
  Serial.setTxTimeoutMs(1);
#endif
}
const char* sinkName() {
#if ARDUINO_USB_CDC_ON_BOOT
#if ARDUINO_USB_MODE
  return "usb_serial_jtag";
#else
  return "usb_cdc";
#endif
#else
  return "uart0";
#endif
}
WriteResult writeSome(const uint8_t* bytes, size_t length) {
  if (!length) return {};
  const size_t chunk = length < 64 ? length : 64;
#if ARDUINO_USB_CDC_ON_BOOT
  if (!Serial) return {0, false, true};
  const int available = Serial.availableForWrite();
  if (available <= 0) return {0, false, true};
  const size_t count = chunk < static_cast<size_t>(available) ? chunk : static_cast<size_t>(available);
  return {Serial.write(bytes, count), false, true};
#else
  // 默认 Arduino UART0 txBufferSize=0。该 API 不等 FIFO 空间，但可等驱动互斥锁；
  // 因此只在后台调用，绝不把它当作业务线程的 wait-free API。
  const int count = uart_tx_chars(UART_NUM_0, reinterpret_cast<const char*>(bytes), chunk);
  return count < 0 ? WriteResult{0, true, false} : WriteResult{static_cast<size_t>(count), false, false};
#endif
}
}
