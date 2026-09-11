# 第三方组件与发布许可边界

本文档是 2026-08-31 的工程核查记录，不是法律意见。产品对外、商业或海外发布前，必须由公司许可/法务负责人签字确认。

## 可以随内部评估包保留的组件

| 组件 | 版本 | 许可 | 随包义务 | 官方来源 |
|---|---|---|---|---|
| Qwen3.5-0.8B Q4_0 GGUF | 当前轻量部署模型，ggml-org 量化 | Apache-2.0 | 保留 Apache-2.0 许可文本和模型校验值 | <https://huggingface.co/ggml-org/Qwen3.5-0.8B-GGUF> |
| llama.cpp | b10752 | MIT | 保留版权与 MIT 许可文本 | <https://github.com/ggml-org/llama.cpp/releases/tag/b10752> |
| Qt | macOS / Windows 统一为 5.12.12 | 商业或 LGPLv3/GPLv3，按实际模块复核 | 见下方决策；随包保留许可文本和实际使用模块记录 | <https://www.qt.io/licensing/> |

模型许可位于运行资源 `ai/Qwen3.5-LICENSE`；其他组件许可位于 `notices`。Mac 运行资源在 `Contents/Resources`，Windows 在 `resources`。

## Qt 发布决策

当前 App 动态链接 Qt Framework。对外发布前二选一：

1. 记录公司有效的 Qt 商业许可、覆盖版本和发布主体；或
2. 完整执行 LGPLv3，包括许可文本、Qt 对应源码或有效书面获取方式、用户替换/重链接库的权利与安装信息、不限制这些权利的发布条款，并复核 macOS 签名和分发渠道是否与之兼容。

未完成其中一条时，不得对外分发。

## SWGDRUG 参考谱库

SWGDRUG 官网将 3.14 EI 谱库提供下载，并对使用/误用声明免责；同一页页脚标注 `All rights reserved`。官网页面没有提供足以支持商业再分发的明确授权文本。

因此：

- 当前内含谱库的 App 仅是内部评估候选；
- 对外发布前必须从 SWGDRUG/DEA 获得允许将谱图转换为 SQLite 并随商业软件再分发的书面许可；
- 如未获许可，正式包必须不内置该数据库，改为由获得授权的用户/管理员在本地导入。

官方页面：<https://swgdrug.org/ms.htm>。

## 发布结论

| 用途 | 当前状态 |
|---|---|
| 本项目内部开发与流程评估 | 可用，保持模拟/参考标识 |
| 向公司内部指定评审人交付 | 可作候选包，同时交付本文档 |
| 商业、海外或客户外发 | **不可**，直到 Qt 许可路线和 SWGDRUG 书面授权完成 |
