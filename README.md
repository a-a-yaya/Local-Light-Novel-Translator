# Light Novel Translator

## 介绍视频

https://github.com/user-attachments/assets/90502b9e-3627-43b7-8a58-f798d0c921f6

本地轻小说下载、日译中与阅读工具，使用 **C++17 / Qt Quick / QML** 开发。支持通过本机 `llama-server` 运行 Sakura、Murasaki 和 Qwen GGUF 模型，将下载、术语维护、章节翻译与阅读放在同一个桌面应用中。


## 功能

- **小说下载**：下载页提供カクヨム、Pixiv、エブリスタ、NOVEL DAYS、ノベルバ、ソリスピア入口；网站页可浏览カクヨム排行、搜索作品和作者，支持概要与评论翻译。
- **本地翻译**：独立的 Sakura、Murasaki、Qwen 翻译页；可以进行轻小说的本地翻译
- **术语表**：按小说维护人工术语表和 Qwen 术语表，支持提取、审核、编辑、禁用。
- **阅读**：日文、中文与双语阅读，支持阅读位置、书签、正文编辑、查找替换和应用内阅读全屏。
- **界面与说明**：支持深浅主题、背景图片与组件外观设置，设置页内置可离线阅读的使用说明。

网站支持不等于所有作品都能下载。登录限制、付费内容、已删除章节、网络和网站改版都可能影响获取结果。模型输出也需要结合原文核对。

## 使用教程

点击跳转： **[使用说明](kakuCH01/docs/USER_GUIDE.md)**，也可在应用的「设置 → 使用说明」打开。

## 译文展示

[查看 Sakura 14B、Murasaki 8B 与官方译文对照](译文展示.md)

## Qt 许可与对应源码

Windows 发行包动态链接 LGPLv3 许可的 Qt 6.5.3，附带许可和第三方声明。
[Qt 对应源码、许可与运行库替换说明](QT_LGPL_COMPLIANCE.md)。Qt 源码在 Release 中单独提供，普通用户只需下载 Windows 程序 ZIP。



## 数据保存

保存根目录可以在设置中更改，建议选择空文件夹。主要目录如下：

| 目录 | 内容 |
| --- | --- |
| `DOWNLOAD` | 下载的原文、书籍信息和小说术语表 |
| `CHDL` | 书库章节的中文译文 |
| `CJDL` | 双语阅读资源 |
| `user` | 自行添加文件的分批翻译结果 |
| `OVERVIEW` / `COMMENTS` | 概要与评论译文 |
| `CACHE` | 网站会话临时缓存，下次启动时清理上一会话缓存 |



## 源码结构

```text
.
├─ kakuCH01/
│  ├─ controllers/          # QML 与业务逻辑之间的控制器
│  ├─ models/               # 下载目录、翻译队列、书库和阅读数据模型
│  ├─ qml/                  # 页面、通用组件与主题
│  │  ├─ components/        # 通用界面组件
│  │  ├─ pages/             # 功能页面
│  │  └─ theme/             # 主题与配色
│  ├─ shaders/              # 界面着色器源码
│  ├─ icons/                # SVG / PNG / ICO application icons
│  ├─ docs/USER_GUIDE.md     # 内置使用说明
│  ├─ *translator.*         # 各引擎翻译与术语处理
│  ├─ *manager.*            # 模型进程与术语管理等
│  ├─ modelgpuconfig.*      # 按模型文件配置 GPU 层数
│  ├─ main.cpp              # 应用入口
│  ├─ app.rc.in             # Windows 文件版本信息模板
│  └─ CMakeLists.txt        # 主程序构建配置
├─ tools/
│  └─ package_windows.py    # Windows 发行包制作脚本
├─ .gitignore               # 本地数据与构建产物排除规则
├─ README.md                # 项目介绍
└─ 免责声明.md             
```



# 小说内容、第三方模型与推理后端的相关权利归各自权利人所有；请确认有权获取和使用相关内容，并遵守各自的使用规则。模型译文可能包含误译或遗漏，详细说明见[免责声明](免责声明.md)。
