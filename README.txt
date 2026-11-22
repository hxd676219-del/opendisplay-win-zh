OpenDisplay-Win-ZH（iPad 当 Windows 副屏 · 中文版）
==================================================

把 iPad / iPhone 变成 Windows 电脑的第二块显示器：真正的扩展屏（不是镜像），
支持 USB 有线 + Wi-Fi 无线双模自动切换，触摸、滚动、Apple Pencil 都能反向控制电脑。
界面已全部汉化。

基于 martinhoess/opendisplay-win（GPL-3.0）做的中文本地化分支，配合 iPad 上原版的
OpenDisplay 客户端使用，iPad 端不需要任何改动。


一、特性
--------------------------------------------------
1. 真扩展屏：Windows 会把它当成一块真实显示器，窗口直接拖过去
2. USB / Wi-Fi 双模：插线自动走 USB（最低延迟），拔线无缝切 Wi-Fi，再插回自动回 USB
3. 触摸 / 滚动 / Apple Pencil 反向控制（含压感、倾角）
4. 全中文界面：托盘菜单、设置对话框、状态气泡、iPad 暂停卡片
5. 硬件 H.264 编码（NVIDIA / AMD / Intel Quick Sync / 微软软编自动选择）
6. 单文件 exe，静态链接，无需安装 VC++ 运行库
7. 局域网直连、无账号、无服务器、无追踪


二、目录结构
--------------------------------------------------
opendisplay-win-zh/
  src/                 源码（已汉化）
  third_party/         上游附带依赖
  tools/               调试工具（mock 接收端等）
  scripts/             开机自启脚本（bat）
  release/             编译好的中文版 exe
  CMakeLists.txt       构建配置
  INSTALL.md           安装/使用说明（中文）
  LICENSE              GPL-3.0
  README.md / README.txt


三、快速开始
--------------------------------------------------
1. iPad 端：App Store 安装并打开免费的 OpenDisplay（iOS/iPadOS 15+）。
2. 电脑端：下载 release/opendisplay-win-zh.exe，双击运行（托盘图标）。
3. 右键托盘 → 设置 → 常规 → 附近的 iPad 里选中设备 → 添加 >。
4. 回到托盘菜单点该设备连接；到 Windows「设置 → 系统 → 显示」把新屏设为扩展。

USB 有线直连需要 Windows 上装有 Apple Devices 或 iTunes（提供 usbmuxd）；
同一台 iPad 首次插线时在 iPad 上点「信任此电脑」。


四、开机自启
--------------------------------------------------
程序需要管理员权限才能管理虚拟显示器，因此推荐用计划任务（最高权限）自启，
而不是普通注册表启动项：

1. 安装自启：右键「以管理员身份运行」scripts\安装-开机自启.bat
2. 关闭自启：右键「以管理员身份运行」scripts\卸载-开机自启.bat

注意：设置界面里的「开机自动启动」开关是普通权限的注册表方式，请勿开启，
否则会和脚本重复启动。


五、从源码编译
--------------------------------------------------
需要 Visual Studio 2022 Build Tools（C++ 桌面工作负载 + Windows 11 SDK）与 CMake 3.20+：

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
    cmake --build build --config Release

产物：build\Release\opendisplay-win.exe


六、常见问题
--------------------------------------------------
问：iPad 一直闪、连不上？
答：多为虚拟显示驱动没就绪。本分支按官方约定读写 C:\VirtualDisplayDriver\vdd_settings.xml；
    若驱动把设置放在别处（例如其它工具自带的 MTT 驱动），需把该目录迁移/指向到
    C:\VirtualDisplayDriver，并给当前用户写权限，再重启适配器。

问：重启后连不上？
答：程序退出时会禁用 MTT 适配器，重启后需要管理员权限重新启用，
    因此请用上面的计划任务方式自启。

问：提示未签名？
答：点「更多信息 → 仍要运行」。代码可自行审计、自行编译。


七、许可
--------------------------------------------------
GPL-3.0-or-later，派生自 OpenDisplay（GPL-3.0）。
third_party/parsec-vdd/parsec-vdd.h 来自 nomi-san/parsec-vdd（MIT，与 GPL-3.0 兼容）。
