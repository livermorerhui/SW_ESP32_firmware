# DevOps Diff Summary

## 1. Workflow 新增

- `ci.yml`
  - push(main)/PR 触发
  - 平台构建 + 固件 artifact 上传
  - Python BLE runner sanity check（example config dry-run）

- `nightly.yml`
  - 每日 UTC 00:00 + 手动触发
  - 构建固件并上传 nightly artifact
  - 生成 `reports/nightly/build_report.md`

- `release.yml`
  - `v*` tag 触发
  - 构建固件、生成 SHA256
  - 创建 Release 并上传资产

- `hw_integration.yml`
  - self-hosted runner
  - 运行 safety suite
  - 上传测试报告

## 2. 文档与入口更新

- `docs/devops.md`：新增 DevOps 使用说明
- `README.md`：新增 CI badge 与 DevOps / CI 链接

## 3. 工具最小调整

- `tools/test_runner/run_tests.py`
  - 当配置文件为 `targets.example.yaml` 时，执行 dry-run 并返回 0
  - 目的：保证无硬件时 CI 也能稳定通过，并输出 SKIPPED 报告

## 4. 非固件源码变更声明

- 未修改 `src/` 下固件业务逻辑源码。
