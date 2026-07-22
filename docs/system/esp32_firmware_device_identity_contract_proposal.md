# ESP32 固件设备身份合同（拟议）

状态：`proposed`。固件、Android、backend 的基础代码和 focused tests 已完成；尚缺真实设备 readback、数据库 migration 和 iOS 实现/联调证据，因此还不能写成已全面生效的正式 OTA 合同。

## 1. 目的与边界

目标：同一台 SonicWave ESP32 在 Android、iPhone、iPad 上始终返回同一个 `firmware_device_id`，backend 以它执行固件灰度、白名单、黑名单和审计。

它不是：

- 手机 APP 的安装 ID、登录用户 ID 或 CoreBluetooth UUID。
- Android 蓝牙地址或 iOS `CBPeripheral.identifier`。
- 认证凭据、配对密钥或防伪证明。OTA 包可信性仍由 manifest 签名和 SHA-256 决定。
- 对现有 `CAP?`、`SNAPSHOT?`、`WAVE:*` 语义的修改。

## 2. 固件事实源

| 项 | 合同 |
|---|---|
| 字段 | `firmware_device_id` |
| 输出格式 | `sw1_` 加 32 个小写十六进制字符，例如 `sw1_b2de0cafc15d6c4e6c1bb70db34fafaf` |
| 稳定性 | 同一物理 ESP32 跨重启、APP 重装、Android/iOS 平台必须不变 |
| 唯一性 | v1 取 SHA-256 的前 128 bits；不能以名称、型号、蓝牙会话 UUID 或本地存储替代 |
| 生成源 | `esp_efuse_mac_get_default` 返回的 6 个 base-MAC 原始字节；不得把整数按主机字节序转换后再哈希 |
| 算法 | `SHA-256(UTF-8("SW-FIRMWARE-DEVICE-ID-V1:") || base_mac_bytes[6])` 的前 16 bytes，编码为小写 hex 并加 `sw1_` 前缀 |
| 隐私 | 不在协议、页面、日志或后台管理端直接展示 raw MAC；日志默认仅保留 ID 后 8 位 |
| 安全边界 | 此派生是稳定伪标识，不是不可伪造的硬件证明；若未来需要防伪，另开设备证书 / attestation 专题 |

### 固定测试向量

```text
base_mac_bytes: 24:6f:28:12:34:56
sha256:         b2de0cafc15d6c4e6c1bb70db34fafaf6f00d194a54ec4dcb4b5c4218a216bce
device_id:      sw1_b2de0cafc15d6c4e6c1bb70db34fafaf
```

固件、Android、iOS、backend 的 fixture 必须使用同一向量。实现不允许使用显示字符串 `24:6F:...` 代替六个原始字节。

## 3. BLE 协议

为保护现有 `ACK:CAP` 的单帧预算，身份使用独立的正常业务 BLE 文本查询，不扩充 `CAP?`。

请求：

```text
DEVICE:IDENTITY?
```

成功响应：

```text
ACK:DEVICE_ID id=sw1_b2de0cafc15d6c4e6c1bb70db34fafaf version=1
```

失败响应：

```text
NACK:DEVICE_ID_UNAVAILABLE
```

规则：

1. 这是只读查询，不改变训练、波形、连接、校准或 OTA 状态。
2. `id` 必须满足 `^sw1_[a-f0-9]{32}$`；`version` 当前必须为 `1`。
3. 旧固件不认识请求时，客户端只把既有未知命令响应映射为“设备身份暂不支持（IDENTITY_UNSUPPORTED_LEGACY）”，不得把 raw NACK 作为业务主状态。
4. identity 读取失败、格式错误或版本未知时，客户端不得向 `firmware_device_id` 策略的发布接口发送替代 ID。

## 4. 跨端与后台合同

### 后台请求与发布策略

新的 manifest 请求参数：

```text
firmwareDeviceId=sw1_<32-lowercase-hex>
firmwareIdentityVersion=1
```

发布记录新增稳定字段 `identity_scheme`：

| 值 | 含义 | 可接受请求 |
|---|---|---|
| `legacy_ble_address` | 过渡期旧 Android 地址策略 | 仅旧 `deviceId` / 地址路径 |
| `firmware_device_id` | 正式跨平台策略 | 必须有格式正确的 `firmwareDeviceId` |

一个 release record 只能有一种 identity scheme。禁止在同一灰度、白名单或黑名单中混用地址和 `firmware_device_id`。

对 `firmware_device_id` 发布记录，缺失 / 非法 / 不支持身份的请求返回可读错误：

```json
{"code":"FIRMWARE_IDENTITY_REQUIRED","message":"当前设备暂不支持跨平台固件升级身份"}
```

建议 HTTP `409`。它不是“已是最新” (`204`)；客户端必须给出迁移提示而非静默显示最新。

### Android 与 iOS

- Android：完成 `DEVICE:IDENTITY?` 成功 readback 后，使用 `firmwareDeviceId` 请求新策略。旧策略仍只传现有地址，直到迁移期结束。
- iOS/iPadOS：只使用 `firmwareDeviceId`；`CBPeripheral.identifier` 仍只用于本机连接和历史记录，绝不进入发布策略。
- backend：只由 service 层根据 `identity_scheme` 选择 stable hash、allowlist 与 denylist；controller 和页面不自行解释身份。

## 5. 旧设备迁移

```text
旧固件（无 DEVICE:IDENTITY?）
-> Android 既有 legacy_ble_address OTA
-> 身份基础固件（支持 DEVICE:IDENTITY?）
-> Android/iOS 读取 firmware_device_id
-> firmware_device_id 发布策略
```

约束：

1. iOS OTA 不负责把旧设备迁移到身份基础固件，避免在无稳定身份时绕过灰度。
2. 迁移发布必须使用 `legacy_ble_address`，并保留 Android 真机成功链证据。
3. 迁移完成前，`firmware_device_id` 策略遇到旧设备只给“需先完成设备身份升级”的明确结果。
4. 旧 release record 的含义保持不变；不得把既有地址白名单静默重解释为新 ID 白名单。

## 6. Owner 与禁止事项

| 层 | Owner | 责任 |
|---|---|---|
| 固件 | `FirmwareDeviceIdentityProvider` | eFuse bytes -> v1 ID；只读命令响应 |
| Android repository | `FirmwareDeviceIdentityReader` | 请求、解析、格式校验、fresh readback |
| iOS repository | `FirmwareDeviceIdentityReader`（待 I4） | 请求、解析、格式校验、fresh readback |
| backend service | `FirmwareReleaseIdentityPolicy` | 根据 release identity scheme 执行发布策略 |
| UI | 已有设备页 / presentation owner | 显示已投影的“可升级 / 需身份升级 / 无更新” |

禁止：View 生成或拼接 ID；客户端用蓝牙地址或 UUID 伪造 ID；固件把 identity query 放进 OTA binary service；任何层把 ID 当登录身份或授权证明。

## 7. 验收合同

| 场景 | 必须证据 |
|---|---|
| 固件 unit | 测试向量、格式、重复读取一致、无 raw MAC 日志 |
| Android parser | 请求/ACK 正确；旧固件 unknown response -> legacy unsupported |
| iOS parser | 同上；UUID 不进入 backend query |
| backend policy | 同 ID 的 Android/iOS 命中同一 hash、allowlist、denylist；缺 ID 得 `FIRMWARE_IDENTITY_REQUIRED` |
| 迁移 | Android 用 legacy 策略更新基础固件，更新后读到固定 ID |
| 真机 | R.W.iPad 与 Android 分别连接同一 ESP32，采到相同 `firmware_device_id` 后 8 位与相同发布决策 |

## 8. 生效条件

本提案只有在以下全部通过后才改为正式合同：固件单元测试和构建、Android/iOS focused tests、backend 策略测试、迁移真机证据、跨平台同设备发布决策证据。
