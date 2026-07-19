ConnectTool alpha（Windows x86_64）
==================================

直接双击 connecttool-qt.exe 启动，无需另行安装 Visual C++ 运行库。

目录说明：
- connecttool-qt.exe、Qt6*.dll、steam*.dll：程序及其直接运行依赖
- runtime/qml：运行时 QML 模块
- runtime/plugins：Qt 平台、SVG、样式和 Windows TLS 插件
- licenses：第三方许可证与声明
- logs：首次启动后自动生成，出现启动问题时请提供 connecttool.log

当前版本使用 Valve 的公开测试 AppID 480，因此 Steam 会将运行状态显示为
Spacewar。这是 Steamworks 测试 AppID 的正常行为，不代表 ZIP 中包含了
Spacewar，也不会额外启动一个游戏程序。
