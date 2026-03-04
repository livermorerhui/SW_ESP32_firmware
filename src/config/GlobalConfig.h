#pragma once
#include <Arduino.h>

// ===== Versions =====
#define FW_VER        "SW-HUB-1.0.0"
#define PROTO_VER     1

// ===== I2S =====
#define I2S_PORT        I2S_NUM_0
#define SAMPLE_RATE     16000
#define BUFFER_LEN      128

#define I2S_BCLK_PIN    4
#define I2S_LRCK_PIN    5
#define I2S_DOUT_PIN    6

// ===== Laser / Modbus =====
#define RX_PIN 15
#define TX_PIN 14
#define MODBUS_BAUD 9600
#define MODBUS_SLAVE_ID 1
#define REG_DISTANCE 0x0064

// ===== Scale Algo =====
#define WINDOW_N 10
#define MIN_WEIGHT 5.0f
#define LEAVE_TH 3.0f
#define STD_TH 0.20f
#define LOG_TH 1.0f

// 开启实时流（用于校准/调试）
#define DEBUG_STREAM true

// ===== Safety =====
#define FALL_DW_DT_SUSPECT_TH 25.0f   // kg/s（先用保守默认，后续真机调）
#define FAULT_COOLDOWN_MS     3000
#define CLEAR_CONFIRM_MS      1000

// ===== BLE =====
#define BLE_DEVICE_NAME "SonicWave_Hub"

#define SERVICE_UUID           "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_RX           "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_UUID_TX           "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"

static const bool STOP_ON_DISCONNECT = true;

// ===== WDT (optional) =====
#define ENABLE_WDT 1
static const int WDT_TIMEOUT_S = 5;