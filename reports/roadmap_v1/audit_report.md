# Roadmap v1 + Lint Workflow Audit Report

## 1. 新增内容概览

### Roadmap 文档
- `docs/roadmap_v1.md`

### Lint Workflow
- `.github/workflows/lint.yml`

### Lint 配置（为稳定执行）
- `.markdownlint-cli2.jsonc`
- `tools/test_runner/pyproject.toml`

### README 更新
- `README.md`
  - 新增 Lint badge
  - 新增 Roadmap 小节
  - 文档导航加入 `docs/roadmap_v1.md`

### 审阅报告
- `reports/roadmap_v1/audit_report.md`
- `reports/roadmap_v1/diff_summary.md`
- `reports/roadmap_v1/patch.diff`

## 2. 关键设计选择

1. 为什么选 `markdownlint-cli2`
- GitHub hosted runner 可稳定安装（Node 生态成熟）。
- 对 README/docs 的批量检查速度快。

2. 为什么选 `yamllint`
- 对 workflow YAML 语法与常见格式问题有明确提示。
- 通过内联规则禁用 `truthy/document-start/line-length`，适配 GitHub Actions YAML 习惯（如 `on:`）。

3. 为什么选 `ruff + black`
- 轻量、执行快、社区广泛使用。
- 仅检查 `tools/test_runner/**/*.py`，避免影响固件构建链路。

4. 为什么将 Python lint 配置放在 `tools/test_runner/pyproject.toml`
- 将 lint 作用域限定在测试工具子目录。
- 不引入对固件工程（PlatformIO/Arduino）的额外配置干扰。

5. 为什么新增 `.markdownlint-cli2.jsonc`
- 现有文档历史风格差异较大，默认规则会导致大量非功能性失败。
- 采用“必要规则 + 降噪规则”方案，优先保证 workflow 稳定可执行。

## 3. 可运行性验证结果

本地已验证：
- `markdownlint-cli2 "README.md" "docs/**/*.md"` 通过
- `yamllint`（带 workflow 兼容配置）通过
- `ruff check tools/test_runner` 通过
- `black --check tools/test_runner` 通过

## 4. 影响范围与约束符合性

- 未修改 `src/` 下固件业务逻辑源码。
- 仅变更 `docs/`、`.github/workflows/`、`README.md`、`tools/test_runner` 配置与 `reports/`。
