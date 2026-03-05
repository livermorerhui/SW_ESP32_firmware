# 改动摘要

本次已做最小可落地修改（代码 + 配置）：

1. `.gitignore`
- 补全常见本地文件忽略项：`.idea/`、`.fleet/`、`*.swp`、`*.swo`、`*~`、`*.bak`、`compile_commands.json`。

2. `src/transport/ble/BleTransport.h`
- 增加 `friend class MyServerCallbacks;` 与 `friend class MyRxCallbacks;`，修复回调访问私有成员导致的编译失败。

3. `src/core/SystemStateMachine.cpp`
- 新增 `begin(EventBus*)` 与 `state() const` 实现，补齐接口定义。

4. `src/core/Types.h`
- 在 `WaveParams` 中新增 `hasEnable`，用于标识旧协议 `E:` 字段是否被触碰。

5. `src/core/ProtocolCodec.h`
- 旧协议解析中对 `E:` 字段写入 `hasEnable`，避免“未携带 E 也被当作 false”的歧义。

6. `src/main.cpp`
- LEGACY 命令路径改为通过状态机联锁处理启停，避免绕过安全状态。
- 在主循环增加“非 RUNNING 态强制停波”兜底联锁。

7. 构建验证
- 使用 `~/.platformio/penv/bin/pio run` 验证通过。
