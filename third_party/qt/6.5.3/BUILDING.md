# Qt 6.5.3 工具包与重建

本目录记录本项目分发的 Qt 依赖，不为本应用指定许可证。

- 平台：Windows x86_64，共享库。
- 原工具链：MinGW GCC 11.2.0。
- `qconfig.pri`：安装工具包提供的 Qt 配置。
- `module-revisions.txt`：安装工具包记录的相关模块修订号。
- `runtime-sha256.json`：v1.0 发行包中 Qt DLL 的 SHA-256 及与工具包一致性的核对结果。后续打包会重新生成此文件。
- `source.json`：实际提供的完整源码文件、来源、项目托管地址、大小和 SHA-256。

Qt 库由官方工具包提供，项目未添加 Qt 源码补丁。修改后重新构建无需得到项目维护者的额外许可；按相关源文件的许可证执行即可。

## 构建自己的共享库

从 `source.json` 的项目 Release 地址获取完整源码，核对 SHA-256 后解压至无特殊字符的目录。
安装兼容的 MinGW 11.2、CMake、Ninja，以及 Qt 构建文档要求的其他工具。源码中包含 `configure.bat`、CMake 文件及模块构建脚本。

可先构建 Qt Base，然后按依赖顺序构建需要修改的附加模块。在已配置 MinGW、CMake、Ninja 的终端中，例如：

```powershell
# 将下面路径换成自己的目录。在源码目录之外构建。
cmake -S C:/qt-src/qt-everywhere-src-6.5.3/qtbase -B C:/qt-build/base -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DCMAKE_INSTALL_PREFIX=C:/qt-modified
cmake --build C:/qt-build/base --parallel
cmake --install C:/qt-build/base

# 附加模块分别使用已安装的 Qt 配置，示例为 Shader Tools。
New-Item -ItemType Directory -Force C:/qt-build/shadertools
Set-Location C:/qt-build/shadertools
& C:/qt-modified/bin/qt-configure-module.bat C:/qt-src/qt-everywhere-src-6.5.3/qtshadertools
cmake --build . --parallel
cmake --install .
```

对附加模块，应先进入各自独立的空构建目录再运行 `qt-configure-module.bat`，不要在源码目录或 Qt Base 构建目录中运行。
本应用涉及的源模块包括 `qtbase`、`qtshadertools`、`qtdeclarative`、`qt5compat`、`qtsvg`、`qtimageformats` 和 `qttranslations`。Qt Declarative 需要对应的 Shader Tools；按源码中的依赖检查补齐所需模块。
某些图像、TLS 等功能还取决于原配置中的第三方依赖，应参照 `qconfig.pri` 和 Qt 官方 Windows 构建说明调整。

以上是修改 Qt 时的构建起点，不承诺生成与官方工具包逐字节相同的二进制。兼容性取决于编译器、ABI、Qt 特性及配套插件的一致性。
安装修改版库后，按照根目录 `QT_LGPL_COMPLIANCE.md` 的步骤替换程序副本中的 DLL 和配套插件。

官方说明：https://doc.qt.io/qt-6.5/windows-building.html
