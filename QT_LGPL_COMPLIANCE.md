# Qt 许可、对应源码与运行库替换

本软件的 Windows x64 发行包动态链接 Qt 6.5.3，采用 LGPLv3 许可选项。
Qt 的版权属于 The Qt Company Ltd. 及其他贡献者。
完整 GPLv3 / LGPLv3 正文见发行包 `licenses/Qt-LICENSE.txt`，版权及第三方归属文件见 `licenses/`。
这些许可证适用于相应的第三方组件，不会自动为本应用自己的源码指定许可证。

## 普通用户

继续下载 `LightNovelTranslator-1.0-Windows-x64.zip`，完整解压后运行。
下面的 Qt 源码是供查看、修改和重新构建 Qt 的可选下载，正常运行本软件不需要下载、解压或安装它。

## 对应 Qt 源码

项目维护者在与 Windows 程序相同的 Release 中另行提供完整 Qt 源码，无需额外费用：

- [Qt 6.5.3 对应源码：qt-everywhere-src-6.5.3.tar.xz](https://github.com/a-a-yaya/Local-Light-Novel-Translator/releases/download/v1.0/qt-everywhere-src-6.5.3.tar.xz)
- 大小：798402796 字节（约 761 MiB）。
- SHA-256：`7cda4d119aad27a3887329cfc285f2aba5da85601212bcb0aea27bd6b7b544cb`。
- 上游：[Qt 6.5.3 官方归档](https://download.qt.io/archive/qt/6.5/6.5.3/single/)。项目 Release 提供实际源码副本，上游链接用于注明来源。

源码保留上游各模块及第三方内容的许可证、构建脚本和文档；完整归档包含本应用未使用的其他模块，不代表本应用使用了它们。
当前发行包的 Qt DLL 与安装的 Qt 6.5.3 Windows x86_64 MinGW 11.2 官方工具包逐一核对一致；项目未对这些 Qt DLL 应用自行修改的补丁。
核对记录和工具包配置位于 `third_party/qt/6.5.3/`。

若 Release 要求登录，需要使用能下载本程序的同一 GitHub 账号访问对应源码。发布或迁移程序下载渠道时，维护者必须同时向同一批接收者提供对应源码，不能只迁移程序文件。

## 替换 Qt 运行库

用户可以修改 Qt，并用接口兼容的版本替换发行包中的 Qt 库；本项目不禁止为调试这些修改而进行必要的逆向工程。
普通用户无需执行以下操作。

1. 退出软件，备份完整程序目录及另行保存的书库、设置。建议在程序目录的副本上操作。
2. 准备兼容的 Windows x86_64 Qt 共享库。以 Qt 6.5.3 源码、MinGW 11.2 和兼容配置为起点，可减少 ABI、编译器和插件不匹配的问题。
3. 替换程序目录根部相应的 `Qt6*.dll`，并按修改涉及的模块同步替换配套插件及 QML 文件：包括 `platforms/`、`imageformats/`、`iconengines/`、`networkinformation/`、`tls/`、`qml/` 等实际存在的目录。保持现有目录布局和 `qt.conf`。
4. 从该副本运行 `LightNovelTranslator.exe`，检查启动及涉及的功能。系统会加载程序目录及其插件目录内的共享库，无需修改或重新签名本程序。
5. 出现插件加载或 ABI 错误时，检查 Qt 版本、CPU 架构、编译器、构建类型及依赖是否兼容；也可恢复备份。

这里的版本建议是兼容性提示，不限制用户尝试其他接口兼容的修改版本。不能承诺任意版本、任意修改的 Qt 都能正常运行本应用。

## 构建与进一步说明

源码内含 Qt 的配置与构建脚本；原工具包的配置、模块修订号及重建说明见 `third_party/qt/6.5.3/BUILDING.md`。
本项目未设置阻止替换 Qt DLL 的签名校验或设备锁定机制。

参考：[LGPLv3 正文](https://doc.qt.io/qt-6.5/lgpl.html)、[Qt 开源许可说明](https://www.qt.io/faq/qt-open-source-licensing)。
