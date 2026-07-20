ConnectTool alpha（Windows x86_64）
==================================

直接双击 connecttool-qt.exe 启动，无需另行安装 Visual C++ 运行库。
程序会先尝试系统默认图形渲染；如果首帧前崩溃或超时，
同一个 connecttool-qt.exe 会自动切换到 Qt 软件渲染。

目录说明：
- connecttool-qt.exe、Qt6*.dll、steam*.dll：程序及其直接运行依赖
- runtime/qml：运行时 QML 模块
- runtime/plugins：Qt 平台、SVG、样式和 Windows TLS 插件
- licenses：第三方许可证与声明
- logs：首次启动后自动生成；出现问题时请提供 connecttool.log。若发生
  native 闪退，同目录还会生成 connecttool-crash.dmp

当前版本使用 Valve 的公开测试 AppID 480，因此 Steam 会将运行状态显示为
Spacewar。这是 Steamworks 测试 AppID 的正常行为，不代表 ZIP 中包含了
Spacewar，也不会额外启动一个游戏程序。
