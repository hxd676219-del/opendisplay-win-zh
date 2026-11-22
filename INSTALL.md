# 安装与使用说明（中文版）

需要：Windows 10/11（x64）；iPad 上安装 App Store 的免费 App「OpenDisplay」。

## 1. 安装虚拟显示驱动（二选一，只需一次）

本程序需要一个虚拟显示器驱动，才能在 Windows 里造出可拖窗口的副屏。两个驱动都是独立第三方驱动，都不随 exe 打包，需以管理员安装其一。

### MTT Virtual Display Driver（推荐，一台 iPad）

1. 从 [Virtual-Display-Driver releases](https://github.com/VirtualDrivers/Virtual-Display-Driver/releases) 下载 `VDD.Control.<版本>.zip`，解压后运行，点 **Install Driver**。
2. 驱动设置默认在 `C:\VirtualDisplayDriver`。程序会在首次连接新尺寸的 iPad 时写入 `vdd_settings.xml` 和 `user_edid.bin`（首次会弹一次 UAC）。

### parsec-vdd（多台 iPad）

1. 下载 `parsec-vdd-0.45.0.0.exe`（`https://builds.parsec.app/vdd/`）并运行（静默安装加 `/S`）。
2. **以管理员**运行 `"C:\Program Files\Parsec Virtual Display Driver\vddinstall.bat"`（不可省略）。
3. 设备管理器 → 显示适配器，应出现「Parsec Virtual Display Adapter」。
4. 在程序「设置 → 高级 → 显示驱动」里选 Parsec。

## 2. USB 直连（可选）

USB 走 Apple 的 usbmuxd，需安装 **Apple Devices** 或 **iTunes** 之一。iPad 需在弹窗中「信任此电脑」，并保持 OpenDisplay 打开。插线自动切 USB，拔线自动落回 Wi-Fi。

## 3. 下载并运行 exe

取 `release/opendisplay-win-zh.exe`，放到任意位置双击即可（托盘运行，配置写入 `%APPDATA%\opendisplay-win`，不写 exe 旁边）。

**首次运行**：exe 未签名，Windows 可能提示「Windows 已保护你的电脑」，点 **更多信息 → 仍要运行**。不信任第三方二进制就自己编译。

## 4. 首次连接

1. iPad 打开 OpenDisplay，与电脑连同一 Wi-Fi。
2. 右键托盘 → **设置** → **常规**，在 **附近的 iPad** 选中设备 → **添加 >**（手动添加在 **高级 → 手动添加/重命名**，一行一个 `地址 昵称`）。
3. 托盘菜单点设备连接；勾选表示正在投屏。首次连接新尺寸会弹一次 UAC 写 EDID，同意即可。

## 5. 开机自启（推荐方式）

用管理员权限的计划任务：

- 右键「以管理员身份运行」`scripts\安装-开机自启.bat` → 开启
- 右键「以管理员身份运行」`scripts\卸载-开机自启.bat` → 关闭

**不要**用设置界面里的「开机自动启动」开关（普通权限，无法在重启后重新启用驱动）。

## 常见问题

- 不同尺寸的 iPad 不能同时投（要同时投用 Parsec 驱动）。
- 日志：`%APPDATA%\opendisplay-win\log.txt`（上一轮 `log.prev.txt`）；配置：同目录 `config.json`。
- 想触摸操作管理员权限窗口：设置 → 高级 → 以管理员身份重启。
