# SCIENTZ 4S Qt/C++ 参考绑定

- 参考文档：`/Users/zhouzhiyuan/SCIENTZ_4S_QT_CPP_DESIGN_SYSTEM_v1.1.md`
- 版本：v1.1
- SHA-256：`c03cd6b45f75685b20b070d8cda8976381c6b366d22f54485e52fc5edcc2e25d`
- 录入日期：2026-08-31

## 本项目采用的工程路线

```text
Fusion Base Style
  -> C++ Theme Tokens
  -> Application-level QSS
  -> Custom QWidget + QPainter
  -> QAbstractItemModel + View + Delegate
```

- Qt 6.10.2 + C++17 + CMake，Qt Widgets-first。
- 颜色、字体、间距、圆角和状态语义由 `ScientzTheme` 统一管理。
- 通用控件使用应用级 QSS；谱图、读数、报警和 AI 证据等专业组件使用 Custom QWidget/QPainter。
- 全部用户命令注册到 `ActionRegistry`，同一命令可被顶部命令栏、菜单、右键和快捷键复用。
- 大量条目优先使用 Model/View/Delegate，不为每条数据构造复杂子 Widget。

## Instrument Console 项目差异

- Word 文档 2.3 对登录、加载、主页、设置和三级功能的要求高于通用视觉参考。
- 主框架使用顶部命令、中央 TIC/质谱图、右侧遥测、底部状态的 Instrument Console，不照搬分析工作台的左侧导航。
- 仪器实际值、设定值、通讯状态和报警必须语义分离。
- 当前模拟仪器和演示谱库必须明示为演示；未接入正式协议前不发送真实仪器指令。

## 不采用

- 不采用 Web/React/Ant Design/CSS Component 技术路线。
- 不在页面散落颜色、随机尺寸和局部 `setStyleSheet()`。
- 不同时重度混用 `QProxyStyle + QPalette + Global QSS + Inline QSS`。
- 不从 SCIENTZ 其他项目复制业务代码或资产。
