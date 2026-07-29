# ADR-154: ESPectre 运动基线与官方 ESP-CSI 姿态数据面

- 日期: 2026-07-29
- 状态: proposed
- 范围: 固定房间、单人、三块 ESP32-S3 的分阶段运动与姿态研究
- 细化: [ADR-153](ADR-153-official-esp-csi-posture-poc.md)

## Context and Problem Statement

官方 ESP-CSI 三板姿态 PoC 已具备可编译的数据面和 Mac 工具，但真实房间
数据、盲测和稳定性证据尚未完成。ESPectre 已提供 ESP32-S3 的
`IDLE/MOTION` 检测，可在不先完成姿态模型的情况下快速建立独立运动基线。
不过 ESPectre 不提供空房/有人、坐/站/躺或跌倒结论，其 GPLv3 源码也不能
直接并入当前 ESP-CSI fork。

相关实现和证据：

- [ESPectre 2.8.0 上游工作区](../../../me-espectre/README.md)
- [Mac ESPectre 记录器](../../../me-esp-csi/tools/ruview_posture/espectre_recorder.py)
- [ESPectre A/B 验收器](../../../me-esp-csi/tools/ruview_posture/espectre_evaluate.py)
- [官方 ESP-CSI 三板应用](../../../me-esp-csi/examples/ruview-posture/README.md)
- [Mac 三头模型实现](../../../me-esp-csi/tools/ruview_posture/model.py)
- [中文执行记录](../../../../skills/my/repo/ruview/docs/2026-07-29-espectre-esp-csi-dual-stage-plan.md)

## Decision Drivers

- 尽快用真实房间证据回答“运动检测是否可靠”，同时不把运动等同于存在。
- 保持 GPLv3 ESPectre 与 ESP-CSI fork 的源码和许可证边界。
- 任何能力都必须经过独立盲测后才能从 `UNKNOWN` 激活。
- 保留三块板各自的 16 MB 完整镜像，错误板卡或错误产物不得被刷写。
- 姿态、存在、运动和跌倒需要分别训练、分别验收、分别发布。

## Considered Options

### 只使用 ESPectre

不采用。它适合作为运动二分类基准，但 `IDLE` 同时包含空房和有人静止，
无法满足存在和姿态目标。

### 将 ESPectre C++ 检测器复制到 ESP-CSI fork

不采用。这样会混合 GPLv3 与当前 fork 的代码边界，也会让临时基准成为
长期运行时的隐式依赖。

### 完全跳过 ESPectre，直接训练三板姿态模型

暂不采用。该路线无法先隔离验证“硬件和房间是否至少能稳定感知运动”，
会把链路问题、特征问题和姿态模型问题同时引入。

### 临时 ESPectre A/B 基线，再恢复并部署官方 ESP-CSI

采用。1 号板临时分别运行 ESPectre MVS 和 ML，Mac 仅通过 ESPHome
Native API 读取公开实体。完成后恢复 1 号板专属完整镜像，再按
1 发射、2 接收部署官方 ESP-CSI。

## Decision Outcome

ESPectre 固定为 release `2.8.0`、提交
`29e457a0cf4251d681905f0df60832988f2f7559`。MVS 和 ML 使用官方
ESP32-S3 工厂镜像，写入前必须同时验证官方资产摘要、节点 1 MAC
`28:84:85:92:81:3c` 和该节点专属回滚镜像。Mac 记录
`Motion Detected`、`Movement Score`、`Threshold`、连接状态和真实标签；
不要求 Home Assistant，也不保存 Wi-Fi 密码。

ESPectre 输出只拥有 `motion` 语义。`IDLE` 不能映射为 `ABSENT`，也不能
直接映射为已验证的 `PRESENT_STILL`。通过基准后，ESPectre 固件从 1 号板
移除，不进入 RuView 或 ESP-CSI 运行时。

长期数据面仍由 ADR-153 的官方 ESP-CSI fork 拥有。Mac 模型拆为
Presence、Motion、Posture 三个头；跌倒在姿态通过后使用独立数据和门禁。
模型、拓扑、协议或任一关键链路不匹配时统一输出 `UNKNOWN`。

## Consequences

- 正面影响：可以先得到独立运动基准，再逐层定位存在和姿态问题。
- 正面影响：临时基准与长期数据面职责清楚，许可证和运行依赖不混合。
- 代价：1 号板需要执行两次 ESPectre 刷写、一次完整恢复和后续 ESP-CSI
  刷写，现场时间增加。
- 代价：ESPectre 达标也不能证明有人静止、姿态或跌倒可用。
- 风险控制：当前串口未确认只连接 1 号板，因此写入门禁保持关闭。
- 后续验证：MVS/ML 各约 30 分钟 A/B、三板锁定盲测和两小时稳定性测试
  都属于发布前必需证据。
