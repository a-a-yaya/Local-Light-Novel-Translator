# 维护 Qt 对应源码与发行材料

用户入口为根目录 `QT_LGPL_COMPLIANCE.md`。普通用户只下载 Windows 应用 ZIP；Qt 源码是同一 Release 的可选附件。

## 使用现有 Qt 6.5.3 工具包打包

先从 `6.5.3/source.json` 指定的渠道取得对应源码，保存为清单指定的文件名，然后执行：

```powershell
python tools/package_windows.py --binary kakuCH01/build/release/kakuCH01.exe --qt D:/download/QT/6.5.3/mingw_64 --mingw D:/download/QT/Tools/mingw1120_64 --qt-source C:/qt-sources/qt-everywhere-src-6.5.3.tar.xz --output dist/1.0
```

脚本先验证源码大小和 SHA-256，然后在输出目录生成应用 ZIP、单独的 Qt 源码副本及包含两者的 `SHA256SUMS.txt`。不会将约 761 MiB 的源码塞入应用 ZIP。
应用 ZIP 自动包含许可证、归属说明、Qt 合规与替换指南及当前运行库校验清单。打包脚本会核对 DLL 与所选工具包一致。

发布时上传源码附件并确保 `source.json` 中的 `distribution_url` 实际可用；只生成本地文件不等于已完成源码提供。
同版本 Qt 的后续应用 Release 可以继续链接现有 v1.0 源码附件，但必须保留该附件，并确保程序接收者有相同的源码下载权限。
将 Windows ZIP 放到其他渠道时，也要携带其内的许可证与说明，并向同一批接收者提供对应源码；不能依赖他们无法访问的私有仓库。

## 更换 Qt 或使用修改版 Qt

为新版本建立对应的元数据目录，更新实际源码副本、版本、哈希、模块修订号、配置及补丁记录，并更新根目录指南与实际下载链接。
若修改了 Qt，需要提供含修改的完整对应源码和必要构建材料，不能继续使用本目录中未修改版 Qt 的源码清单。
工具包与包内 DLL 一致只是分发核对，不能代替对新工具包来源和其对应源码的确认。

本次补充材料不更改应用自身的许可证，也不处理模型、小说和其他界面参考素材的授权。
