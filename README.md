<p align="center">
  <img src="assets/logo.svg" width="200" alt="一台电脑显示器和一台 iPad 共享同一个 Windows 桌面">
</p>

<h1 align="center">OpenDisplay 副屏 · 中文版（opendisplay-win-zh）</h1>

<p align="center">
  <b>把 iPad 当作 Windows 第二块显示器（真扩展屏），支持 USB / Wi-Fi 双模，全中文界面。</b>
</p>

---

## 这是什么

本项目是基于 [martinhoess/opendisplay-win](https://github.com/martinhoess/opendisplay-win)
（C++，GPL-3.0）的**中文本地化分支**。它驱动 iPad 上**未经改动**的 App Store
应用 [OpenDisplay](https://github.com/peetzweg/opendisplay)（开源，GPL-3.0），
用后者的原生线协议（wire protocol）通信，因此 iPad 端无需任何改动。

OpenDisplay 官方只提供 macOS 发送端；opendisplay-win 在 Windows 上重写了发送端，
本项目在此基础上做了如下定制：

| 变更项 | 说明 |
|---|---|
| **全中文界面** | 托盘菜单、设置窗口、状态提示、iPad 端暂停/锁定卡片全部中文化 |
| **设置窗口用 WebView2 + 本地 HTML** | 设置界面改为内嵌 WebView2 加载本地 HTML/CSS/JS（深/浅色跟随系统），不再用 Win32 控件与 .rc |
| **开机自启改用管理员计划任务** | 用「登录时触发 + 最高权限 + 延迟 3 秒」的计划任务替代 HKCU Run 键，解决重启后 MTT 适配器需管理员权限重新启用的问题 |
| **托盘图标自恢复** | 监听 `TaskbarCreated` 广播，任务栏重启/慢登录后自动重新注册图标 |
| **源码编码规范化** | 强制 UTF-8 编译（`/utf-8`），界面全部在代码里构建（不用 .rc） |

> 本项目同样是 **GPL-3.0-or-later** 授权，保留上游署名，**不得闭源发布**。
> 上游 MTT、USB 传输、暂停模式与编码器重构由 Michael Sciscenti（[@mshish](https://github.com/mshish)）
> 在 [PR #1](https://github.com/martinhoess/opendisplay-win/pull/1) 中贡献。

## 功能一览

- **真扩展屏**：Windows 将 iPad 识别为独立显示器，可拖拽窗口过去，设置里可选「扩展」。
- **USB + Wi-Fi 双模**：插线优先走 USB，拔线自动回落 Wi-Fi，再插回自动回 USB。
- **触摸 / 滚动 / Apple Pencil 反控**：手指映射为 Windows 触摸，双指滚动映射为滚轮，
  Apple Pencil 映射为真正的 Windows 手写笔（含压感、倾角、悬停）。
- **H.264 硬件编码优先**：Intel Quick Sync 优先，其次 NVIDIA / AMD 等硬件编码器，
  失败自动回退微软软件编码器，目标 60fps。
- **虚拟显示驱动**：默认使用本机已装的 MTT Virtual Display Driver（单台 iPad），
  多台 iPad 可切换到 Parsec VDD。
- **暂停模式**：Windows 锁屏、屏幕关闭或 UAC 安全桌面时，连接不断，iPad 显示中文暂停卡片。

## 工作原理

iPad 端监听 TCP 9000 端口；Windows 发送端连接它，随后：

1. **虚拟显示器**：按 iPad 面板尺寸创建无头显示器，Windows 视其为真实第二屏。
2. **传输**：Wi-Fi 直连，或经 Apple usbmuxd 走 USB 线；支持断线自动切换与重连。
   通过 Bonjour（`_opensidecar._tcp`）自动发现运行 OpenDisplay 的 iPad。
3. **采集**：DXGI Desktop Duplication 抓取该显示器画面；光标形状与位置单独同步（位置走 UDP 旁路）。
4. **编码**：Media Foundation H.264，输出 Annex-B 字节流（4 字节起始码，关键帧前附 SPS/PPS）。
5. **推流**：单条 TCP 连接，按长度前缀逐帧发送；桌面无变化时降至约 1 帧/秒保活。
6. **输入**：iPad 的触摸/滚动/笔事件沿同一连接回传，注入到虚拟显示器对应的屏幕区域。

## 系统要求

- Windows 10 / 11（x64）
- iPad 端安装 App Store 应用 **OpenDisplay**
- 已安装虚拟显示驱动：单台 iPad 用 MTT VDD，多台用 parsec-vdd
- （可选，用 USB 时）Apple Devices 或 iTunes（提供 usbmuxd）
- **Microsoft Edge WebView2 Runtime**（设置窗口渲染所需，见下文「WebView2 运行时」）

## WebView2 运行时

设置窗口用 WebView2 渲染本地 HTML/CSS/JS（不再用 Win32 控件、不用 .rc）。

- **如何随程序加载**：WebView2 分「运行时」和「加载器」两部分。
  - **加载器（WebView2Loader）」已静态链接**进 exe（`third_party/webview2/x64/WebView2LoaderStatic.lib`），
    所以 exe 仍是**单文件**，无需随程序附带任何 DLL。
  - **运行时（WebView2 Runtime）」由系统提供**：Windows 11 通常已预装；若未装，
    首次打开「设置」时程序会提示「无法连接任务计划程序」之外的 WebView2 初始化失败。
    可到 <https://developer.microsoft.com/microsoft-edge/webview2/> 下载「Evergreen」运行时安装
    （自动后台更新）。本机已检测到 Runtime `158.0.4370.45`。
- **代码写法**：程序用 `AddScriptToExecuteOnDocumentCreated` 把当前配置注入
  `window.__odBoot`、用 `PostWebMessageAsString` 下发状态/附近列表；页面用
  `window.chrome.webview.postMessage` 回传「确定/取消/刷新」等操作，
  点「确定」时把整份配置 JSON 读回保存。详见 `src/app/SettingsWindow.cpp` 与 `src/app/web/`。

## 安装与使用

详细步骤见 **[INSTALL.md](INSTALL.md)**（中文）。

快速开始：

1. 安装虚拟显示驱动（可选，USB）Apple Devices / iTunes。
2. 下载并运行 `opendisplay-win-zh.exe`，其在系统托盘运行。
3. 打开「设置」，从「附近的 iPad」里添加你的 iPad。
4. （可选）运行 `scripts\安装-开机自启.bat`（管理员权限）启用开机自启。

## 设置项说明

设置窗口为 WebView2 渲染的本地页面，自上而下：

- **左侧栏**：「附近的 iPad」列表 + 刷新/添加；「我的 iPad」列表 + 移除。
- **右主区（滚动卡片）**：
  - **连接**：自动连接开关、Wi-Fi 断线自动续连开关
  - **画质**：画质预设下拉（更快 / 均衡 / 更清晰）
  - **高级**：手动添加地址的多行框、端口、显示驱动下拉、USB/Wi-Fi 编码设置
    （分别模式下「动态/固定」+「快速/均衡/高画质」）
- **底部**：右下角「取消」「确定」。
- 顶部标题栏右侧有当前连接状态圆点（灰=未连接、绿=已连接、红=连接中/等待）。

## 暂停模式（中文卡片）

锁屏（Win+L）、屏幕关闭、或 UAC 提示切到安全桌面时，连接不断，采集与编码停止，
iPad 显示中文卡片：

- **已锁定**：解锁后继续。
- **等待管理员授权**：UAC 提示正在安全桌面。
- **屏幕已休眠**：唤醒屏幕后继续。
- **已暂停**：其他无法采集桌面的时刻；自动恢复。

## 从源码构建

需要 Visual Studio Build Tools（桌面 C++ 工作负载）或 Visual Studio + Windows 10/11 SDK，
以及 CMake ≥ 3.20。仅**编译**不需要显示驱动；**运行**才需要。

**WebView2 SDK 已随源码 vendored**（`third_party/webview2/`，含 `WebView2.h` 与
`WebView2LoaderStatic.lib`），编译时无需另装 SDK；运行时仍需系统装 WebView2 Runtime。

**HTML/CSS/JS 内嵌**：设置页面源码在 `src/app/web/`，用
`tools/gen_webassets.ps1` 生成 `src/app/WebAssets.h`（把三份文件以 C++ 原始字符串字面量
内联进 exe）。改动页面后用本机 PowerShell 重新生成一次即可：

```
powershell -ExecutionPolicy Bypass -File tools\gen_webassets.ps1
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --parallel
```

本项目**不使用 .rc**（设置窗口改用 WebView2，托盘/其余也全是代码构建），
源码直接放在任意路径即可编译。

产物为 `build\Release\opendisplay-win.exe`，静态链接 CRT 与 WebView2Loader（单文件，
免装 VC++ 运行库）；仅需系统预装 WebView2 Runtime。

## 运行

**无参数**：以托盘程序运行（每个 Windows 会话一个实例）。托盘图标状态点：
绿 = 至少一台 iPad 推流中，红 = 已配置但无推流，灰 = 未配置。
右键菜单列出每台 iPad 及其状态，可单独连接/断开；下方为「全部连接/断开」「设置」「退出」。
配置保存在 `%APPDATA%\opendisplay-win\config.json`，日志在同目录 `log.txt`（上一次为 `log.prev.txt`）。

```
opendisplay-win-zh.exe
```

**带 IP 参数**：以无界面（headless）模式连接指定 iPad，便于测试/脚本化，日志写到 `log-<pid>.txt`：

```
opendisplay-win-zh.exe <ipad-ip>
```

## 无 iPad 的本地自测

`tools/mock_receiver.py`（Python 3，仅标准库）可模拟 iPad：监听 9000、发送 `hello`、
记录控制消息，并按 Annex-B 规则（4 字节起始码、关键帧前 SPS/PPS）校验每条视频负载。

```
python3 tools/mock_receiver.py --width 2388 --height 1668
# 另开终端：
opendisplay-win-zh.exe 127.0.0.1
```

## 许可证与署名

本项目为 **GPL-3.0-or-later**，是 [OpenDisplay](https://github.com/peetzweg/opendisplay)
（GPL-3.0）的派生作品。版权归上游各作者所有，详见 [LICENSE](LICENSE)。

- 上游 Windows 发送端：[martinhoess/opendisplay-win](https://github.com/martinhoess/opendisplay-win)
- iPhone/iPad 端：[peetzweg/opendisplay](https://github.com/peetzweg/opendisplay)
- `third_party/parsec-vdd/parsec-vdd.h` 原样 vendored 自 [nomi-san/parsec-vdd](https://github.com/nomi-san/parsec-vdd)（MIT，与 GPL-3.0 兼容）。

> 请遵守 GPL-3.0：二次分发必须保留本 LICENSE 与上游署名，并同样以 GPL 兼容许可开源。
