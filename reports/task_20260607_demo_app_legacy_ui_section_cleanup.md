# Demo APP Legacy UI Section Cleanup

状态：已完成，轻量真机 smoke 通过
日期：2026-06-07
范围：`tools/android_demo/app-demo`

## 1. 本轮判型

本轮属于直接实现 / 低风险 UI 重构收口。

目标链路：

- Demo APP 当前信息架构入口。
- Compose UI section 文件边界。

相关链路：

- 顶栏搜索 / 断开。
- `型号 / 校准 / 采样 / 运行 / 日志` Tab。
- 底部 `WaveControlBottomBar`。

真相源：

- `MainScreen.kt` 当前调用链。
- `rg` 调用点审计。
- Gradle 编译 / 单测 / assemble。

不该动的层：

- ESP32 BLE wire payload。
- `DemoViewModel` business owner。
- `SonicWaveClient` / BLE transport。
- 固件协议、校准算法、运行控制状态机。
- 当前可见 UI 文案、布局和交互。

## 2. 成熟方案对齐

本包按 Android Compose 当前结构做死代码清理：

- 保留当前已上线的信息架构主入口。
- 删除已经无调用点的旧 UI section，避免后续误接回归。
- 不为了行数继续拆大组件；只清理确定不再参与当前 UI tree 的文件。

## 3. 改动点

删除以下旧堆叠首页遗留 section：

- `DeviceConnectSection.kt`
- `ScaleSection.kt`
- `StableWeightSection.kt`
- `WaveSection.kt`

当前入口保持：

- 连接入口：顶栏 `搜索 / 断开`。
- 主页面：固定顶部 `型号 / 校准 / 采样 / 运行 / 日志`。
- 运行控制：底部 `WaveControlBottomBar`。
- 校准归零：当前校准页确认弹窗路径。

## 4. 抽象点

- 删除旧 section 后，当前 UI tree 的 owner 更清晰。
- 不再保留旧 `ScaleSection` / `WaveSection` 这种与当前校准页、运行底栏重复的入口。
- 不再保留旧 `DeviceConnectSection`，避免型号页连接状态卡片被误接回来。

## 5. 参数点

- 本包不新增参数。
- 不改 `SectionPresentationModels`。
- 不改 `MainScreen` actions DTO。
- 不改任何 ViewModel API。

## 6. 验证结果

本地门禁已通过：

```bash
git diff --check
cd tools/android_demo
./gradlew :app-demo:compileDebugKotlin --no-daemon --stacktrace
./gradlew :sonicwave-protocol:test :app-demo:testDebugUnitTest --no-daemon --stacktrace
./gradlew :app-demo:assembleDebug --no-daemon --stacktrace
```

调用点复核：

```bash
rg -n "ScaleSection\(|WaveSection\(|StableWeightSection\(|DeviceConnectSection\(" \
  tools/android_demo/app-demo/src/main \
  tools/android_demo/app-demo/src/test -S
```

结果为空，证明旧 section 只有定义、没有当前调用点。

已知非 blocker 提示：

- Gradle 仍提示本机 `org.gradle.java.installations.paths` 中的旧 JDK 路径不存在；不影响本包构建结果。

轻量真机 smoke：

- 安装最新 demo APK。
- 打开 Demo APP。
- 确认默认进入 `型号` 页。
- 确认顶部 Tab 显示 `型号 / 校准 / 采样 / 运行 / 日志`。
- 确认顶栏 `搜索` 可打开扫描入口。
- 确认底部运行控制仍显示。

结果：

- 用户完成轻量安装打开 smoke 并反馈通过。

## 7. 不覆盖范围

- 不验证 ESP32 start / stop 设备闭环。
- 不重复完整 Demo APP quality baseline。
- 不验证 device config 写入。
- 不验证校准模型写入。

原因：本包只删除无调用点 UI 文件，不改当前可见 UI tree 或设备命令链。
