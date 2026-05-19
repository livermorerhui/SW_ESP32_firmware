# ESP32 BLE Firmware OTA Contract

状态：当前正式
文档类型：合同
适用范围：SW APP 通过 BLE 更新 SonicWave ESP32-S3 N16R8 固件
Owner：SW_ESP3_Firmware OTA / SW Android 设备接入
更新日期：2026-05-18
真相源：本文件、`src/ota/FirmwareOtaManager.*`、`src/transport/ble/BleTransport.*`
证据源：host-side evaluator tests、PlatformIO build、Android JVM tests、OTA capture 产物

This document is the formal v1 contract for updating SonicWave ESP32-S3 N16R8 firmware from the SW APP over BLE. It is a cross-repo contract between `SW_ESP3_Firmware` and `SW/apps/android`.

## Scope

Target link:

`SW APP firmware package -> BLE OTA transfer -> ESP32 inactive OTA app partition -> image validation -> boot partition switch -> ESP32 reboot -> SW APP reconnect -> version confirmation`

Related links:

- Existing business BLE service: `CAP?`, `SNAPSHOT?`, `WAVE:*`, `EVT:*`.
- Android product route and training start gates.
- Firmware safety and wave output state machine.
- Firmware release artifact generation and sha256 manifest.

Truth source:

- ESP32 OTA state, target partition, image validation, boot partition selection, post-reboot firmware identity.

Evidence sources:

- ESP32 serial logs.
- OTA status characteristic notifications.
- Android OTA logs.
- Post-reboot `CAP?` / snapshot firmware identity.

Out of scope for this contract:

- Wi-Fi OTA.
- Replacing bootloader or partition table over BLE.
- Training session semantic changes.
- Changing existing `CAP? / SNAPSHOT? / WAVE:* / EVT:*` meanings.
- App APK update flow.
- Backend firmware manifest distribution and forced update policy.
- Signed manifest and rollback confirmation hardening; these are reserved for v2.

## Mature Mechanism Baseline

The OTA write/switch mechanism must use Espressif OTA APIs:

- `esp_ota_get_next_update_partition`
- `esp_ota_begin`
- `esp_ota_write`
- `esp_ota_end`
- `esp_ota_set_boot_partition`
- `esp_restart`

The required flash layout is a dual OTA app partition layout with `otadata`, `app0`, and `app1`. The current N16R8 build target uses `default_16MB.csv`, with `app0` and `app1` each at `6400K`.

## Existing BLE Service Freeze

The existing business BLE service remains frozen:

| Item | Value |
| --- | --- |
| Service UUID | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX characteristic | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX characteristic | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |
| Payload model | UTF-8 line protocol |
| Existing command families | `CAP?`, `SNAPSHOT?`, `WAVE:*`, `DEVICE:*`, `CAL:*` |
| Existing event families | `ACK:*`, `NACK:*`, `SNAPSHOT:*`, `EVT:*` |

The OTA feature must not send firmware binary bytes through this service. The current firmware transport has short text buffers (`ControlMsg.line[128]`, `TxMsg.line[512]`) and control/stream queue semantics optimized for runtime commands, not binary transfer.

## OTA GATT Service

OTA must use a separate GATT service.

Frozen v1 UUIDs:

| Item | Direction | UUID |
| --- | --- | --- |
| OTA service | n/a | `7D2A0001-3F6A-4F1D-9B2C-0D9A5E7C0001` |
| OTA control characteristic | APP -> ESP32 | `7D2A0002-3F6A-4F1D-9B2C-0D9A5E7C0001` |
| OTA data characteristic | APP -> ESP32 | `7D2A0003-3F6A-4F1D-9B2C-0D9A5E7C0001` |
| OTA status characteristic | ESP32 -> APP | `7D2A0004-3F6A-4F1D-9B2C-0D9A5E7C0001` |

Characteristic properties:

| Characteristic | Properties | Notes |
| --- | --- | --- |
| control | write with response | Small structured control frames only. |
| data | write with response in v1 | Binary chunks. Write-with-response keeps sequencing explicit. |
| status | notify | Progress, error, and reboot-ready state. |

The v1 contract uses write-with-response for data chunks. v2 may explicitly opt in to write-without-response via `transfer_mode=write_command`; v1 remains the default.

v2 characteristic update:

| Characteristic | Additional property | Compatibility |
| --- | --- | --- |
| data | write without response | Optional. Android must request `transfer_mode=write_command`; otherwise the data path behaves as v1 `write_request`. |

## Firmware OTA State Machine

Firmware OTA states:

| State | Meaning |
| --- | --- |
| `IDLE` | No OTA session active. |
| `PREPARED` | Metadata accepted, inactive partition selected, OTA handle opened. |
| `RECEIVING` | Firmware bytes are being written sequentially. |
| `VERIFYING` | `esp_ota_end` and sha256 validation are running. |
| `READY_TO_REBOOT` | Boot partition has been switched; reboot is allowed. |
| `REBOOTING` | ESP32 is rebooting. |
| `FAILED` | OTA session failed and must be aborted/cleared before retry. |

Only firmware may advance the authoritative OTA state. Android may request state transitions but must not infer success without firmware confirmation.

## Preconditions

OTA begin must be rejected unless all conditions are true:

- Firmware target board is `sonicwave_esp32s3_n16r8`.
- Current partition table has at least two OTA app slots.
- New image size is greater than zero and no larger than the inactive app partition.
- No training session is running.
- Wave output is stopped.
- Firmware is not in blocking safety/fault handling.
- One BLE client owns the OTA session.
- Requested image metadata includes `version`, `build_id`, `size`, `sha256`, `board`, and `protocol`.

If a training command arrives during OTA, firmware must reject it with a stable reason code rather than starting output.

## Control Frames

Control frames are UTF-8 JSON objects in the OTA control characteristic. They are not part of the existing line protocol.

Required requests:

```json
{"type":"ota_begin","protocol":1,"version":"1.0.1","build_id":"20260422.1","board":"sonicwave_esp32s3_n16r8","size":1016032,"sha256":"<64 hex>","chunk_size":244}
```

v2 optional begin fields:

```json
{"type":"ota_begin","p":1,"v":"1.0.1","b":"20260422.1","bd":"sonicwave_esp32s3_n16r8","s":1016032,"h":"<64 hex>","c":234,"m":"wc","w":4,"i":4,"a":16,"ap":"wa","bp":"rsc"}
```

Defaults:

- `transfer_mode`: `wr`
- `window_size`: `4`
- `max_inflight_chunks`: `window_size`
- `ack_interval_chunks`: `16`
- `ack_policy`: `window_ack`
- `busy_policy`: `retry_same_chunk`

Runtime short aliases:

- `ap=wa` means `ack_policy=window_ack`
- `bp=rsc` means `busy_policy=retry_same_chunk`

```json
{"type":"ota_end","total_chunks":4165,"size":1016032,"sha256":"<64 hex>"}
```

```json
{"type":"ota_abort","reason":"USER_CANCEL"}
```

```json
{"type":"ota_reboot"}
```

```json
{"type":"ota_query"}
```

Required status notifications:

```json
{"type":"ota_status","st":"PREPARED","r":0,"s":1016032}
```

```json
{"type":"ota_progress","st":"RECEIVING","r":524288,"s":1016032,"p":51}
```

v2 window acknowledgement:

```json
{"type":"ota_window_ack","m":"wc","as":15,"n":16,"r":3744,"e":3744,"w":4,"i":4,"a":16,"ap":"window_ack","bp":"retry_same_chunk"}
```

```json
{"type":"ota_status","st":"VERIFYING","r":1016032,"s":1016032}
```

```json
{"type":"ota_status","st":"READY_TO_REBOOT","r":1016032,"s":1016032}
```

```json
{"type":"ota_error","code":"VERIFY_FAILED","message":"sha256 mismatch"}
```

Control JSON should stay below 244 bytes on the negotiated control characteristic path. Runtime `ota_status` notify frames must also stay below the negotiated ATT payload budget, normally 244 bytes at MTU 247. `ota_status` is therefore a compact phase/status frame; detailed maintenance evidence belongs to `ota_query`. Unknown control fields must be ignored. Unknown required `type` values must return `UNSUPPORTED_COMMAND`.

`ota_query` must include current transfer mode, supported transfer modes, window settings, `max_inflight_chunks`, `ack_policy`, `busy_policy`, `next_seq`, `received`, and `expected_offset` so Android can distinguish v1 fallback from v2 high-throughput mode. Short keys may be used for the runtime status path as long as Android accepts both aliases. v2 does not support byte-level resume; interrupted transfers are aborted and retried from the beginning.

For v2 high-throughput mode, `ota_window_ack` is a credit confirmation, not only a UI progress event. Android may only advance the next window after the expected `next_seq` and `expected_offset` are acknowledged. If Android receives a local GATT busy / not accepted result, it must retry the same chunk and must not advance `seq` or `offset`.

## Data Frames

OTA data characteristic value format v1:

| Offset | Size | Type | Meaning |
| --- | ---: | --- | --- |
| 0 | 4 | `uint32_le` | `seq` |
| 4 | 4 | `uint32_le` | byte `offset` |
| 8 | 2 | `uint16_le` | payload length |
| 10 | n | bytes | firmware binary payload |

Rules:

- `seq` starts at `0`.
- `offset` must match total bytes already accepted.
- Payload length must be `> 0`.
- Final chunk may be shorter than configured chunk size.
- Duplicate chunk with the already accepted offset may be idempotently ACKed only if payload hash matches the last accepted chunk; otherwise fail.
- Out-of-order chunks are rejected in v1.

Recommended initial chunk size: `min(244, negotiated_mtu - 13)`.

## Release Manifest

v1 firmware package source is a fixed Release URL, not backend distribution.

Each firmware release must publish:

- `firmware.bin`
- `firmware.sha256`
- `firmware_manifest.json`

Minimum manifest:

```json
{"board":"sonicwave_esp32s3_n16r8","version":"v1.0.1","build_id":"<git sha>","size":1016032,"sha256":"<64 hex>","url":"firmware.bin","protocol":1}
```

Android must verify manifest schema and sha256 before starting BLE transfer.

## Error Codes

All user-facing Android text must be Chinese first, with the code in parentheses when useful.

| Code | Meaning | User-facing text |
| --- | --- | --- |
| `BUSY_RUNNING` | Training or wave output is active. | 设备正在运行，无法升级（BUSY_RUNNING） |
| `UNSUPPORTED_BOARD` | Firmware package target board does not match. | 固件型号不匹配（UNSUPPORTED_BOARD） |
| `IMAGE_TOO_LARGE` | Image does not fit inactive app partition. | 固件包过大（IMAGE_TOO_LARGE） |
| `INVALID_METADATA` | Begin metadata is missing or malformed. | 固件信息不完整（INVALID_METADATA） |
| `INVALID_STATE` | Request is invalid for current OTA state. | 升级状态不正确（INVALID_STATE） |
| `SEQ_MISMATCH` | Chunk sequence is not expected. | 固件分片顺序错误（SEQ_MISMATCH） |
| `OFFSET_MISMATCH` | Chunk offset is not expected. | 固件分片偏移错误（OFFSET_MISMATCH） |
| `WRITE_FAILED` | `esp_ota_write` failed. | 写入固件失败（WRITE_FAILED） |
| `VERIFY_FAILED` | `esp_ota_end` or sha256 validation failed. | 固件校验未通过（VERIFY_FAILED） |
| `BOOT_SWITCH_FAILED` | `esp_ota_set_boot_partition` failed. | 设置启动分区失败（BOOT_SWITCH_FAILED） |
| `BLE_DISCONNECTED` | BLE disconnected during OTA. | 蓝牙连接中断（BLE_DISCONNECTED） |
| `USER_CANCEL` | User canceled OTA. | 用户已取消升级（USER_CANCEL） |
| `UNSUPPORTED_TRANSFER_MODE` | Requested transfer mode is not supported. | 升级模式不支持（UNSUPPORTED_TRANSFER_MODE） |
| `GATT_BUSY` | Android local GATT write queue is busy. | 蓝牙写入繁忙（GATT_BUSY） |
| `RX_QUEUE_FULL` | ESP32 OTA receive queue is full. | 设备接收队列已满（RX_QUEUE_FULL） |
| `SEQ_DUPLICATE_UNSUPPORTED` | Duplicate chunk cannot be idempotently confirmed. | 固件分片重复且无法确认（SEQ_DUPLICATE_UNSUPPORTED） |
| `WINDOW_TIMEOUT` | Android did not receive a required window ACK in time. | 窗口确认超时（WINDOW_TIMEOUT） |
| `WINDOW_OVERFLOW` | Window settings or received chunks exceed allowed limits. | 窗口数据超限（WINDOW_OVERFLOW） |
| `VERSION_CONFIRM_FAILED` | Android could not confirm post-reboot identity. | 版本确认失败（VERSION_CONFIRM_FAILED） |
| `MANIFEST_SIGNATURE_INVALID` | Release manifest signature is invalid. | 固件签名无效（MANIFEST_SIGNATURE_INVALID） |
| `MANIFEST_EXPIRED` | Release manifest is expired. | 固件签名已过期（MANIFEST_EXPIRED） |
| `ROLLBACK_CONFIRM_FAILED` | New firmware failed rollback confirmation. | 回退确认失败（ROLLBACK_CONFIRM_FAILED） |
| `INTERNAL_ERROR` | Unexpected firmware error. | 设备内部错误（INTERNAL_ERROR） |

## Android Ownership Rules

Android must treat OTA as a dedicated device mode:

- OTA client owns GATT during transfer.
- Existing product BLE client must not concurrently write business commands.
- UI/ViewModel must not directly parse raw OTA code into business state.
- Repository/domain layer maps OTA state to UI state.
- Training start controls must be disabled while OTA is active.
- If OTA is active, Android must not start or resume a training session.

The existing Android APK update code can be reused only for artifact download and sha256 verification patterns. It must not be coupled to ESP32 BLE transfer state.

## Firmware Ownership Rules

Firmware must treat OTA as a dedicated safety mode:

- OTA module owns OTA state and write handle.
- Existing BLE transport remains responsible for business text commands.
- System state machine remains owner of training/wave runtime state.
- OTA precheck consumes system state snapshots but must not redefine training state.
- OTA failure must abort the OTA handle and leave the current running firmware bootable.

## Version Confirmation

After reboot, Android must reconnect through the normal business BLE path and call `CAP?` or snapshot query.

`CAP?` should eventually include:

```text
ACK:CAP fw=<version> build=<build_id> board=sonicwave_esp32s3_n16r8 proto=<proto> platform_model=<...> laser_installed=<0|1> leave_stop_supported=1
```

`ota_slot` must not be added to `ACK:CAP`; it is mutable maintenance/runtime evidence, not bootstrap identity. Slot evidence belongs to OTA `ota_query`, OTA status notify, or a future dedicated runtime field. OTA acceptance cannot be marked complete without post-reboot firmware identity evidence and OTA state evidence.

## Validation Matrix

Minimum validation before release:

| Case | Expected result |
| --- | --- |
| USB first-flash N16R8 build | Boot succeeds, business BLE still works. |
| Existing business commands before OTA | `CAP?`, `SNAPSHOT?`, `WAVE:SET`, `WAVE:START`, `WAVE:STOP` still pass. |
| OTA happy path | Image transfers, validates, switches boot partition, reboots. |
| Reconnect after reboot | APP reconnects and confirms new firmware identity. |
| OTA while running | Firmware rejects with `BUSY_RUNNING`; wave output unaffected. |
| BLE disconnect mid-transfer | OTA aborts, old firmware remains bootable. |
| Wrong board manifest | Rejects with `UNSUPPORTED_BOARD`. |
| Wrong sha256 | Rejects with `VERIFY_FAILED`; boot partition is unchanged. |
| Oversized image | Rejects with `IMAGE_TOO_LARGE`. |
| APP process restart during OTA | Device reports recoverable/failed OTA state via `ota_query`. |

## v2 Hardening

- Optional write-without-response transfer window with `ota_window_ack`; v1 remains the default fallback.
- Rollback confirmation: new firmware marks itself valid only after startup self-check has completed.
- Signed manifest verification.
- Backend manifest distribution / gray release.
