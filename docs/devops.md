# SonicWave DevOps / CI-CD Guide

## 1. Workflow 总览

本仓库提供四类 GitHub Actions 流水线：

1. CI（`.github/workflows/ci.yml`）
- 触发：`push(main)`、`pull_request`
- 作用：固件构建 + Python BLE test runner 健康检查 + artifact 上传

2. Nightly（`.github/workflows/nightly.yml`）
- 触发：每天 UTC `00:00`、手动 `workflow_dispatch`
- 作用：每日定时构建并上传固件，附 nightly build report

3. Release（`.github/workflows/release.yml`）
- 触发：`push tag v*`
- 作用：构建固件、生成 `sha256`、创建 GitHub Release 并上传资产

4. Hardware Integration（`.github/workflows/hw_integration.yml`）
- 触发：手动、每周定时（可选）
- 运行器：`self-hosted`
- 作用：运行安全联锁类硬件集成测试，上传测试报告

## 2. 固件产物位置

本地或 Actions 构建后的固件产物路径：

- `.pio/build/<env>/firmware.bin`

工作流中的 artifact 收集逻辑会从 `.pio/build/*/firmware.bin` 自动收集。

## 3. CI 说明（无硬件也可通过）

CI 包含两部分：
1. `pio run` 固件构建
2. `tools/test_runner` 的 BLE sanity check

sanity check 使用示例配置 `targets.example.yaml`，按 dry-run 模式执行：
- 不依赖真实 BLE 设备
- 生成测试报告目录
- 返回成功（用于保证 CI 可稳定通过）

## 4. Nightly 说明

Nightly 会上传：
- 固件 artifact（包含日期与 commit sha）
- `reports/nightly/build_report.md`（commit、环境、时间戳、产物路径）

适用于追踪每日构建稳定性与固件留档。

## 5. Release 说明

Tag 命名规则：`v*`（例如 `v1.0.0`）

Release 流程会自动：
1. 构建固件
2. 收集 `firmware.bin`
3. 生成 `firmware.sha256`
4. 创建 GitHub Release
5. 上传资产：
   - `firmware.bin`
   - `firmware.sha256`

Release notes 模板包含：
- 变更摘要
- 兼容性
- 已知问题
- 测试状态
- `docs/test_plan.md` 引用（若存在）

## 6. Self-hosted Runner 启用指南（硬件集成）

### 6.1 在 GitHub 仓库创建 runner
1. 进入仓库 `Settings -> Actions -> Runners`
2. 添加 self-hosted runner（Linux）
3. 按 GitHub 指引下载安装并注册

### 6.2 建议标签
- `self-hosted`
- `linux`
- （可选）`ble-lab`、`sonicwave`

### 6.3 设备接入建议
- 连接真实 BLE 目标设备与传感器硬件
- 维护本地私有 `tools/test_runner/config/targets.yaml`
- 确保 runner 用户具备 BLE 访问权限

## 7. 常见问题

1. PlatformIO 缓存导致异常
- 处理：清理 `~/.platformio` 后重试构建。

2. 依赖下载慢
- 处理：利用 Actions cache；必要时切换镜像源。

3. `firmware.bin` 找不到
- 检查 `pio run` 是否成功。
- 检查 `platformio.ini` 的 env 名称与 `.pio/build/<env>/` 路径。
- 查看 workflow 的 artifact 收集步骤日志。

4. BLE sanity check 在 CI 失败
- 检查是否使用 `targets.example.yaml`。
- 检查 `run_tests.py` dry-run 路径是否被改动。

## 8. 扩展建议

- 增加静态检查（YAML lint、Markdown lint、Python lint）
- 增加 SBOM/依赖漏洞扫描
- 将 nightly 报告汇总为趋势面板
