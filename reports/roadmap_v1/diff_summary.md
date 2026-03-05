# Roadmap v1 Diff Summary

## 1. 文件清单

- 新增 `docs/roadmap_v1.md`
- 新增 `.github/workflows/lint.yml`
- 新增 `.markdownlint-cli2.jsonc`
- 新增 `tools/test_runner/pyproject.toml`
- 更新 `README.md`
- 新增 `reports/roadmap_v1/audit_report.md`
- 新增 `reports/roadmap_v1/diff_summary.md`
- 新增 `reports/roadmap_v1/patch.diff`

## 2. 改动摘要

1. 路线图文档
- 定义 v1.0 Scope Freeze
- 规划 F1–F5 里程碑与 DoD
- 增加风险矩阵、Release 约定、文档关联、变更控制机制

2. Lint workflow
- 触发：push(main) / pull_request / workflow_dispatch
- Markdown：`markdownlint-cli2` 检查 `README.md` 与 `docs/**/*.md`
- YAML：`yamllint` 检查 `.github/workflows/*.yml`
- Python：`ruff + black --check` 检查 `tools/test_runner/**/*.py`

3. README
- 增加 Lint badge
- 增加 Roadmap 小节链接
- 文档导航补充 `docs/roadmap_v1.md`

## 3. 非源码改动说明

- 未修改固件业务逻辑源码（`src/` 下无改动）。
