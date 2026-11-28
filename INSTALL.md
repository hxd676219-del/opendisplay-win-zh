# 安装说明（中文版）

需要 Windows 10/11（x64），iPad 端安装 App Store 应用
[OpenDisplay](https://github.com/peetzweg/opendisplay)。

## 1. 安装虚拟显示驱动

两种都是**独立的第三方驱动**，均不随本程序打包。没有驱动就没有可采集的虚拟显示器，
程序不会连接。请以管理员身份安装其一（或两者），只需一次。

### MTT Virtual Display Driver（推荐，单台 iPad）

1. 从 [Virtual-Display-Driver releases](https://github.com/VirtualDrivers/Virtual-Display-Driver/releases)
   下载 Virtual Driver Control（`VDD.Control.<version>.zip`），解压运行，点 **Install Driver**。
2. 驱动设置目录在 `C:\VirtualDisplayDriver`。本程序会在其中写两个文件：
   `vdd_settings.xml`（模式列表、`CustomEdid`、`PreventSpoof`，以及若为 0 则把显示器数量设为 1）
   与 `user_edid.bin`（为你的 iPad 面板生成的 EDID）。首次写前各备份一次为 `.bak`。
3. 仅当 iPad 尺寸与已安装的不同时（首次连接、或之后换了不同型号）才需要管理员权限：
   托盘会提示，Windows 弹出一次授权，随后程序重启 `Root\MttVdd` 适配器使其加载新模式。
   同一台 iPad 重连不会再问。若拒绝授权，iPad 仍按已装的 EDID 连接。

> 关于本机：MTT 驱动已就位（设备实例 `ROOT\DISPLAY\0002`，硬件 ID `Root\MttVDD`，
> 设置目录 `C:\VirtualDisplayDriver`），通常无需再安装。

### parsec-vdd（多台 iPad 同屏）

1. 从 `https://builds.parsec.app/vdd/` 下载 `parsec-vdd-0.45.0.0.exe` 并运行（静默安装加 `/S`）。
2. 以**管理员权限**运行 `"C:\Program Files\Parsec Virtual Display Driver\vddinstall.bat"`
   （右键 → 以管理员身份运行，或从提权终端执行）。此步**不可省略**：第 1 步只是解压，
   真正创建设备节点靠这个脚本。只 `/S` 会导致驱动在但无设备节点。
3. 验证：设备管理器（`devmgmt.msc`）→ 显示适配器 → 应有 **"Parsec Virtual Display Adapter"** 条目。

**二者选择**：设置 → 高级 → 显示驱动，提供 自动 / MTT（1 台 iPad）/ Parsec（多台 iPad）。
自动 在装了 MTT 时用 MTT，否则用 Parsec。若所选驱动缺失，iPad 不连接，
菜单显示「缺少显示驱动」并弹气泡提示；程序绝不擅自切换另一驱动。

## 2. USB（可选）

USB 走 Apple 的 usbmuxd，来自 **Apple Devices** 或 **iTunes**，二选一安装即可。
iPad 需信任本机，且已打开 OpenDisplay。

无需逐台配置。条目一旦知道 iPad 的 id 就会用 USB：从「附近」添加的条目自带 id；
手输地址的条目在首次 Wi-Fi 连接时学到。在那之前走 Wi-Fi。
「USB 断线后通过 Wi-Fi 保持连接」（默认开）可让拔线时不中断，
重新插回再切回 USB；菜单会显示当前链路「已连接 (USB)」或「已连接 (Wi-Fi)」。

## 3. 下载 opendisplay-win-zh.exe

取 `opendisplay-win-zh.exe`（单文件、静态链接、免装 VC++ 运行库）。可放到任意目录
（配置写在 `%APPDATA%\opendisplay-win\`，不写在自身旁边）。

**SmartScreen 会提示。** 程序尚未签名，首次运行 Windows 会显示「Windows 已保护你的电脑」，
点「更多信息」→「仍要运行」即可。在签约完成前，所有发行版均未签名。
如不愿信任来路不明的未签名程序，可[从源码构建](README.md#从源码构建)，只需两条命令。

## 4. 首次启动

双击 exe，其在系统托盘运行（见 [运行](README.md#运行)）；右键 →「设置」。
「常规」页在「附近的 iPad」里列出局域网中发现的 iPad（iPad 端需已打开 OpenDisplay 且同一 Wi-Fi）；
选中点「添加 >」移到「我的 iPad」。「刷新」重新搜索。手输地址用「高级」页的
「手动添加 / 重命名」：每行一台，地址后跟可选昵称（如 `192.168.1.42 客厅 iPad`）。

菜单按 iPad Bonjour 广播的名称显示；若仍是通用「iPad」，则用主机名（`iPad-Pro.local` → *iPad-Pro*）。
你设的昵称优先。从「附近」添加的条目按 iPad id 记忆，DHCP 换地址后仍能找到。

**同一时刻只一种面板尺寸。** 不同面板尺寸的 iPad 不会同屏。Parsec 下驱动给所有虚拟显示器
放同一自定义分辨率，同尺寸的可同屏，不同尺寸的等待（菜单显示「等待 2732x2048」）；
MTT 下第二台 iPad 始终等待（「等待中 - 屏幕正被某某占用」），第一台断开后自动连上。

首次连接**新**尺寸的 iPad 时，Windows 会弹一次管理员授权（见下文「管理员权限」），
接受即可，同一台 iPad 不会再问。

## 管理员权限

程序**以非提权方式运行**。需要管理员的操作取决于驱动，两种情况都会自动提权一个一次性助手
（一次 UAC 弹窗）后继续以非提权运行：

- **MTT**：写 `C:\VirtualDisplayDriver` 下的 `vdd_settings.xml` / `user_edid.bin` 并重启驱动，
  仅当 iPad 尺寸变化时（以及显示器数量为 0 时一次）。
- **Parsec**：把 iPad 原生分辨率（横竖两种）注册为 parsec-vdd 自定义模式，每种面板尺寸写一次 `HKLM`。

以管理员身份运行整个程序是**可选**的（设置 → 高级 →「以管理员身份重启」），
仅当你需要触控控制 iPad 屏幕上的**提权**窗口时才需要（Windows 会阻止低完整性进程
向高完整性窗口注入输入，即 UIPI）。

## 开机自启（管理员计划任务）

本分支**不使用**注册表 Run 键，改用**管理员计划任务**——因为程序退出会禁用 MTT 适配器，
重启后需要管理员权限才能重新启用，普通权限的 Run 键做不到。

两种启用方式：

1. **一键脚本（推荐）**：右键「以管理员身份运行」
   `scripts\安装-开机自启.bat`；移除自启运行 `scripts\卸载-开机自启.bat`。
2. **程序内勾选**：设置 → 常规 →「开机自动启动」。若程序未提权，创建任务会提示需要管理员权限。

计划任务效果：登录时触发、最高权限、延迟 3 秒启动（等任务栏就绪，避免托盘图标注册失败）。

> ⚠️ 二者**不要同时**启用（勿既跑安装脚本又在设置里勾选），否则会重复启动、互相抢虚拟屏。

## 已知注意事项

- **托盘图标偶发不显示**：程序启动早于任务栏就绪时可能失败。本分支已加 `TaskbarCreated`
  自恢复 + 计划任务 3 秒延迟，通常无需手动处理。
- **重启后连不上**：多为 MTT 适配器被禁用，管理员执行
  `Enable-PnpDevice -InstanceId ROOT\DISPLAY\0002`（或 `pnputil /restart-device "ROOT\DISPLAY\0002"`）。
- **Parsec 会残留显示器设备**：parsec-vdd 每次 add 生成新设备节点并残留，定期运行
  `opendisplay-win-zh.exe --cleanup-monitors` 清理。MTT 无此问题。
