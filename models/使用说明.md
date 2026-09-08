# 离线 AI 模型

## 当前 Windows 7 交付配置

- 模型：Qwen3.5-0.8B Q4_0
- 软件内文件名：`Qwen3.5-0.8B-Q4_0.gguf`
- 固定来源：[Hugging Face 模型文件](https://huggingface.co/ggml-org/Qwen3.5-0.8B-GGUF/blob/9447f74101aeb4e93621884dfa36ee8effb8831b/Qwen3.5-0.8B-Q4_0.gguf)
- 上游版本：`9447f74101aeb4e93621884dfa36ee8effb8831b`
- 文件大小：563,036,064 bytes
- SHA-256：`57d1997790d1744fba5b40a7317df71ea5e2acee28c47e78f0cce39c0703f8cf`
- 许可：Apache-2.0，许可文本保存在 `models/qwen/Qwen3.5-LICENSE`
- 推理运行时：项目固定的 Windows 7 `llama.cpp` 适配源码，服务只监听 `127.0.0.1`

模型权重不进入 GitHub 源码仓库或工程师源码 ZIP。客户使用的 Windows 安装程序和 macOS DMG 已包含经过本项目校验的模型、运行时及所需动态库；工程师只在重新制作安装包时按上面的固定链接下载模型并核对文件大小与 SHA-256。

没有模型时，程序仍可编译，采集、分析、保存、复核和报告等确定性功能仍须工作；深度自然语言问答不可用属于预期状态。

当前交付只启用文本、手册问答和受控界面操作。未提供视觉投影器 `mmproj`，因此不声明模型能够读取截图或检测图像；图谱数值与鉴定结论始终由确定性 C++ 引擎和人工复核产生。

模型文件不进入科学计算链。`AiCommandRouter` 先把低风险自然语言操作路由到 C++ `QAction` 白名单；普通问题由 `LocalKnowledgeStore` 检索本地操作手册，再由 `LocalAiBridge` 把手册片段与确定性引擎证据交给模型解释。服务绑定本机回环地址、禁用 Web UI，并为每次 App 进程生成临时 API Token。模型缺失或启动失败时，软件核心流程保持可用。

替换模型时优先使用环境变量：

```text
QITEST_AI_SERVER=/absolute/path/to/llama-server
QITEST_AI_MODEL=/absolute/path/to/model.gguf
QITEST_AI_LOW_MEMORY=1              # 触发低内存档位（更低 ctx、输出、队列、n-gpu-layers）
QITEST_AI_PROFILE=low               # 与上面同义，任选其一
```

新模型必须重新执行离线响应、数字忠实性、拒绝编造、提示注入和长证据压力测试；仅凭模型更大或更新不能自动进入正式版本。
