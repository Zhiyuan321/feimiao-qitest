# 权限与安全

AuthorizationPolicy 负责角色与操作权限。当前本地角色配置不是完整身份认证系统。
新增硬件动作必须同时核对权限、用户确认和设备自身互锁；不要只禁用一个按钮就当作安全控制。
模型、插件和导入文件都不能绕过这里。对应 tests/SecurityTests.cpp 与 InstrumentControlTests.cpp。
