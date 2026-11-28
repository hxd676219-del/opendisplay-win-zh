#pragma once

#include <windows.h>
#include <wrl/client.h>
#include <WebView2.h>

#include <functional>
#include <string>

namespace od {

struct Config;

// 用 WebView2 渲染「设置」窗口：内嵌本地 HTML/CSS/JS（已在 WebAssets.h 内联进
// 可执行文件），通过 postMessage / addScriptToExecuteOnDocumentCreated 与 C++
// 双向打通配置读写。异步运行（立即返回，不阻塞托盘主线程）。
//
// 用于替换原先基于 Win32 对话框模板（.rc / 内存模板）的设置界面，彻底不再用
// Win32 控件与 .rc。托盘逻辑不变，只有设置窗口改为 WebView2。
class SettingsWindow {
public:
    using StatusProvider = std::function<std::string()>; // 返回状态 JSON
    using OnDone = std::function<void(bool ok, Config&& cfg)>; // 窗口关闭时回调（worker 线程）

    // 异步打开设置窗口，立即返回。窗口在独立 STA 线程运行（WebView2 需要 STA，
    // 而托盘主线程已被 main() CoInitializeEx(MTA)），关闭时在 worker 线程回调
    // onDone；托盘应在其内 PostMessage 回主线程再 ApplyAndRestart。
    // nearbyProvider / statusProvider 在 worker 线程被调用，须线程安全。
    static void ShowAsync(HINSTANCE hInstance, Config cfg,
                          std::function<std::string()> nearbyProvider,
                          StatusProvider statusProvider,
                          OnDone onDone);
};

} // namespace od
