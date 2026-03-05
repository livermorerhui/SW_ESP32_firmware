# 文档增强 Diff 摘要

## 1. 同步动作
- 基于现有 `reports/dev_docs/developer_guide.md` 进行增强。
- 将增强后的内容同步到 `docs/firmware_developer_guide.md`。
- 当前两份文档保持一致。

## 2. 文档增强内容
在原有结构不破坏的前提下，新增以下章节：
- `11. 开发者接手 Checklist`
  - 编译、烧录、BLE/I2S/激光测重验证流程
  - Safety Interlock 必测场景：
    - `RUNNING` 状态
    - 用户离开平台
    - 蓝牙断开
    - Modbus 读取失败
    - 判定标准：系统必须停止振动
- `12. 参数与阈值说明`
  - `LEAVE_TH`
  - `MIN_WEIGHT`
  - `STD_TH`
  - `WINDOW_N`
  - `FALL_DW_DT_SUSPECT_TH`
  - 每项给出作用、默认值、调整建议
- `13. 常见维护任务`
  - 修改 BLE 协议需改文件
  - 新增硬件模块接入 EventBus 的步骤
  - 修改 I2S 参数需改文件
  - 新增状态机事件需改文件

## 3. 新增配套 Artifact
- `reports/dev_docs/repo_structure.md`
- `reports/dev_docs/doc_generation_summary.md`
- `reports/dev_docs/diff_summary.md`
- `reports/dev_docs/patch.diff`

## 4. 代码改动说明
- 本次仅文档改动，无源码逻辑改动。
