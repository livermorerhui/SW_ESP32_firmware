# DevOps Automation Audit Report

## 1. 本次新增/修改文件

### Workflows
- `.github/workflows/ci.yml`
- `.github/workflows/nightly.yml`
- `.github/workflows/release.yml`
- `.github/workflows/hw_integration.yml`

### 文档
- `docs/devops.md`
- `README.md`（新增 CI badge 与 DevOps 链接）

### 工具最小改动（为满足 CI dry-run DoD）
- `tools/test_runner/run_tests.py`
  - 新增 example 配置检测逻辑：当使用 `targets.example.yaml` 时，生成 `SKIPPED` 报告并返回 `0`。

### 审阅报告
- `reports/devops_automation/audit_report.md`
- `reports/devops_automation/diff_summary.md`
- `reports/devops_automation/patch.diff`

## 2. 关键设计选择

1. CI 设计
- 仅 Ubuntu runner，降低环境差异。
- `pio run` 后统一收集 `.pio/build/*/firmware.bin`。
- 即使找不到固件也会上传提示文件，确保 artifact 步骤可见。
- Python test runner 使用 example 配置做 sanity check，避免硬件依赖导致 CI 不稳定。

2. Nightly 设计
- UTC 00:00 定时（北京时间 08:00）。
- 附带 `reports/nightly/build_report.md`，记录 commit、环境、时间戳与产物路径。

3. Release 设计
- `v*` tag 触发。
- 自动生成并上传 `firmware.bin` + `firmware.sha256`。
- Release notes 模板包含变更摘要、兼容性、已知问题、测试状态，并引用 `docs/test_plan.md`（若存在）。

4. 硬件集成测试设计（可选）
- 运行在 `self-hosted`，不阻塞普通 CI。
- 支持手动与每周定时触发。
- 自动上传最新 test runner 报告目录。

## 3. Soft-fail / Skipped 策略

- CI 中 test runner 使用 `targets.example.yaml` 进入 dry-run：
  - 不连接真实设备
  - 结果标记 `SKIPPED`
  - exit code `0`（CI 通过）
- 真实硬件验证放在 `hw_integration.yml`（self-hosted）。

## 4. 扩展建议

1. 在 CI 增加 markdown/yaml lint。
2. Nightly 可追加构建尺寸趋势统计。
3. Release 可附加 changelog 自动生成（按 PR labels）。
4. hw_integration 可增加 runner 标签（如 `ble-lab`）细分硬件实验室。
