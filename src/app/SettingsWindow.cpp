#include "app/SettingsWindow.h"

#include "app/Config.h"
#include "app/Log.h"
#include "app/TrayApp.h"
#include "app/WebAssets.h"

#include <wrl/event.h>  // Microsoft::WRL::Callback

#include <shellapi.h>

#include <sstream>
#include <atomic>
#include <cctype>
#include <thread>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

namespace od {

namespace {

// ---------- 极简 JSON 工具（与 config.json 相同的扁平风格）----------

std::string JsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

std::string JsonString(const std::string& s) { return "\"" + JsonEscape(s) + "\""; }

std::string JsonBool(bool b) { return b ? "true" : "false"; }

std::string JsonStringArray(const std::vector<std::string>& v)
{
    std::string out = "[";
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += ",";
        out += JsonString(v[i]);
    }
    out += "]";
    return out;
}

// 把设备条目（"addr[ 昵称]"）拆成 {address,label} 两列，供页面列表显示。
struct DeviceRow {
    std::string address;
    std::string label;
};
DeviceRow SplitRow(const std::string& entry)
{
    size_t sp = entry.find(' ');
    if (sp == std::string::npos)
        return {entry, entry};
    return {entry.substr(0, sp), entry.substr(entry.find_first_not_of(' ', sp))};
}

// 生成「设备列表」JSON（每项 {address,label}）。
std::string DevicesToJson(const std::vector<std::string>& devices)
{
    std::string out = "[";
    for (size_t i = 0; i < devices.size(); ++i) {
        DeviceRow r = SplitRow(devices[i]);
        if (i) out += ",";
        out += "{\"address\":" + JsonString(r.address) + ",\"label\":" + JsonString(r.label) + "}";
    }
    out += "]";
    return out;
}

// 生成初始配置 JSON（发给页面 fillForm）。
std::string ConfigToInitJson(const Config& cfg)
{
    std::ostringstream o;
    o << "{\"config\":{"
      << "\"devices\":" << JsonStringArray(cfg.devices) << ","
      << "\"port\":" << cfg.port << ","
      << "\"autoReconnect\":" << JsonBool(cfg.autoReconnect) << ","
      << "\"usbWifiFailover\":" << JsonBool(cfg.usbWifiFailover) << ","
      << "\"displayBackend\":" << JsonString(cfg.displayBackend) << ","
      << "\"theme\":" << JsonString(cfg.theme) << ","
      << "\"picturePreset\":" << JsonString(cfg.picturePreset) << ","
      << "\"separateUsbWifiPicture\":" << JsonBool(cfg.separateUsbWifiPicture) << ","
      << "\"usbEncodeMode\":" << JsonString(cfg.usbEncodeMode) << ","
      << "\"usbEncodePreset\":" << JsonString(cfg.usbEncodePreset) << ","
      << "\"wifiEncodeMode\":" << JsonString(cfg.wifiEncodeMode) << ","
      << "\"wifiEncodePreset\":" << JsonString(cfg.wifiEncodePreset) << "}"
      << ",\"devices\":" << DevicesToJson(cfg.devices)
      << "}";
    return o.str();
}

// ---------- 极简 JSON 读取（字段级定位，用于解析页面 save 消息）----------

std::string ValueAfter(const std::string& json, const std::string& key)
{
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos)
        return {};
    pos = json.find(':', pos + needle.size());
    if (pos == std::string::npos)
        return {};
    ++pos;
    while (pos < json.size() && std::isspace(static_cast<unsigned char>(json[pos])))
        ++pos;
    return json.substr(pos);
}

std::string ReadString(const std::string& json, const std::string& key)
{
    std::string v = ValueAfter(json, key);
    if (v.empty() || v.front() != '"')
        return {};
    size_t end = v.find('"', 1);
    if (end == std::string::npos)
        return {};
    return v.substr(1, end - 1);
}

bool ReadBool(const std::string& json, const std::string& key, bool& out)
{
    std::string v = ValueAfter(json, key);
    if (v.compare(0, 4, "true") == 0) { out = true; return true; }
    if (v.compare(0, 5, "false") == 0) { out = false; return true; }
    return false;
}

long ReadInt(const std::string& json, const std::string& key, long& out)
{
    std::string v = ValueAfter(json, key);
    if (v.empty())
        return false;
    char* endp = nullptr;
    long x = strtol(v.substr(0, 16).c_str(), &endp, 10);
    if (!endp || endp == v.substr(0, 16).c_str())
        return false;
    out = x;
    return true;
}

std::vector<std::string> ReadStringArray(const std::string& json, const std::string& key)
{
    std::vector<std::string> out;
    std::string v = ValueAfter(json, key);
    if (v.empty() || v.front() != '[')
        return out;
    size_t end = v.find(']');
    if (end == std::string::npos)
        return out;
    v = v.substr(1, end - 1);
    for (size_t pos = v.find('"'); pos != std::string::npos; pos = v.find('"', pos + 1)) {
        size_t close = v.find('"', pos + 1);
        if (close == std::string::npos)
            break;
        out.emplace_back(v.substr(pos + 1, close - pos - 1));
        pos = close;
    }
    return out;
}

// ---------- WebView2 设置窗口实现 ----------

const wchar_t* const kSettingsWndClass = L"opendisplay-win-settings-webview";

// 定时刷新顶部连接状态圆点（毫秒）。
constexpr UINT kStatusTimerId = 1;
constexpr UINT kStatusTimerMs = 1000;

// 每个窗口一个上下文，挂在 GWLP_USERDATA。
struct SettingsCtx {
    HWND hwnd = nullptr;
    Microsoft::WRL::ComPtr<ICoreWebView2Controller> controller;
    Microsoft::WRL::ComPtr<ICoreWebView2> webview;
    EventRegistrationToken msgToken{};
    std::function<std::string()> nearbyProvider;
    SettingsWindow::StatusProvider statusProvider;
    Config* outCfg = nullptr;
    bool ok = false;          // 点了确定
    bool envFailed = false;   // WebView2 初始化失败
    RECT dragStartRect{};     // 手动拖动起点：窗口矩形
    POINT dragStartCursor{};  // 手动拖动起点：光标屏幕坐标
};

// UTF-8 → UTF-16 辅助
std::wstring U8ToW(const std::string& s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    if (n > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// UTF-16 → UTF-8 辅助
std::string WToU8(const wchar_t* ws)
{
    if (!ws || !ws[0])
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    std::string s(n > 0 ? n - 1 : 0, '\0');
    if (n > 0)
        WideCharToMultiByte(CP_UTF8, 0, ws, -1, s.data(), n, nullptr, nullptr);
    return s;
}

// 当前进程是否以管理员权限运行。
bool IsUserAnAdmin()
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                 0, 0, 0, 0, 0, 0, &adminGroup)) {
        if (!CheckTokenMembership(nullptr, adminGroup, &isAdmin))
            isAdmin = FALSE;
        FreeSid(adminGroup);
    }
    return isAdmin != FALSE;
}

void PostToPage(SettingsCtx* ctx, const std::string& json)
{
    if (!ctx->webview)
        return;
    ctx->webview->PostWebMessageAsString(U8ToW(json).c_str());
}

// 把当前连接状态推给页面顶部圆点。statusProvider 返回 {"state":"...","link":"..."}，
// 这里包装成 {"type":"status",...} 走页面 message 通道。
void PushStatus(SettingsCtx* ctx)
{
    if (!ctx->webview)
        return;
    std::string s = ctx->statusProvider ? ctx->statusProvider() : "{\"state\":\"idle\",\"link\":\"\"}";
    if (!s.empty() && s.front() == '{')
        s = "{\"type\":\"status\"," + s.substr(1);
    PostToPage(ctx, s);
}

// 把 save 消息里的 config JSON 写回 outCfg
void ApplyConfigFromJson(const std::string& json, Config& cfg)
{
    // devices 数组
    std::vector<std::string> devices = ReadStringArray(json, "devices");
    if (!devices.empty() || json.find("\"devices\"") != std::string::npos)
        cfg.devices = std::move(devices);

    long port = 0;
    if (ReadInt(json, "port", port) && port > 0 && port <= 65535)
        cfg.port = static_cast<uint16_t>(port);

    bool b = false;
    if (ReadBool(json, "autoReconnect", b)) cfg.autoReconnect = b;
    if (ReadBool(json, "usbWifiFailover", b)) cfg.usbWifiFailover = b;
    if (ReadBool(json, "separateUsbWifiPicture", b)) cfg.separateUsbWifiPicture = b;

    std::string s = ReadString(json, "displayBackend");
    if (!s.empty()) cfg.displayBackend = s;
    s = ReadString(json, "theme");
    if (s == "light" || s == "dark" || s == "system") cfg.theme = s;
    s = ReadString(json, "picturePreset");
    if (!s.empty()) cfg.picturePreset = s;
    s = ReadString(json, "usbEncodeMode");
    if (!s.empty()) cfg.usbEncodeMode = s;
    s = ReadString(json, "usbEncodePreset");
    if (!s.empty()) cfg.usbEncodePreset = s;
    s = ReadString(json, "wifiEncodeMode");
    if (!s.empty()) cfg.wifiEncodeMode = s;
    s = ReadString(json, "wifiEncodePreset");
    if (!s.empty()) cfg.wifiEncodePreset = s;
}

// 解析页面发来的消息（类型 + 负载），返回是否触发了关闭。
bool HandleWebMessage(SettingsCtx* ctx, const std::string& json)
{
    std::string type = ReadString(json, "type");

    if (type == "save") {
        // 页面发回完整 config；就地解析进 outCfg
        if (ctx->outCfg)
            ApplyConfigFromJson(json, *ctx->outCfg);
        ctx->ok = true;
        PostMessageW(ctx->hwnd, WM_CLOSE, 0, 0);
        return true;
    }
    if (type == "cancel") {
        ctx->ok = false;
        PostMessageW(ctx->hwnd, WM_CLOSE, 0, 0);
        return true;
    }
    if (type == "refreshNearby") {
        // 调 nearbyProvider 拿附近列表，回推给页面
        std::string list = ctx->nearbyProvider ? ctx->nearbyProvider() : "[]";
        PostToPage(ctx, "{\"type\":\"nearby\",\"list\":" + list + "}");
        return false;
    }
    if (type == "window") {
        // 自定义标题栏的窗口控制（最小化/最大化/关闭/手动拖动）
        std::string action = ReadString(json, "action");
        if (action == "minimize") {
            ShowWindow(ctx->hwnd, SW_MINIMIZE);
        } else if (action == "maximize") {
            ShowWindow(ctx->hwnd, IsZoomed(ctx->hwnd) ? SW_RESTORE : SW_MAXIMIZE);
        } else if (action == "close") {
            PostMessageW(ctx->hwnd, WM_CLOSE, 0, 0);
        } else if (action == "dragStart") {
            GetWindowRect(ctx->hwnd, &ctx->dragStartRect);
            long x = 0, y = 0;
            ReadInt(json, "x", x);
            ReadInt(json, "y", y);
            ctx->dragStartCursor.x = static_cast<int>(x);
            ctx->dragStartCursor.y = static_cast<int>(y);
        } else if (action == "dragMove") {
            long x = 0, y = 0;
            ReadInt(json, "x", x);
            ReadInt(json, "y", y);
            int dx = static_cast<int>(x) - ctx->dragStartCursor.x;
            int dy = static_cast<int>(y) - ctx->dragStartCursor.y;
            SetWindowPos(ctx->hwnd, nullptr,
                         ctx->dragStartRect.left + dx, ctx->dragStartRect.top + dy,
                         0, 0, SWP_NOSIZE | SWP_NOZORDER);
        } else if (action == "dragEnd") {
            // 结束拖动，无需处理
        }
        return false;
    }
    if (type == "setAutostart") {
        // 开关切换：创建/删除管理员计划任务。非管理员时自提权执行一次性命令。
        bool enable = false;
        ReadBool(json, "enable", enable);

        bool okReq = false;
        wchar_t err[256] = {};

        if (IsUserAnAdmin()) {
            okReq = SetAutostartTask(enable, err, _countof(err));
        } else {
            // 自提权：ShellExecuteEx runas 运行自身 --set-autostart on|off
            wchar_t exe[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exe, MAX_PATH);
            std::wstring params = std::wstring(L"--set-autostart ") + (enable ? L"on" : L"off");
            SHELLEXECUTEINFOW sei{};
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_NOCLOSEPROCESS;
            sei.lpVerb = L"runas";
            sei.lpFile = exe;
            sei.lpParameters = params.c_str();
            sei.nShow = SW_HIDE;
            okReq = ShellExecuteExW(&sei) == TRUE;
            if (okReq && sei.hProcess) {
                WaitForSingleObject(sei.hProcess, 30000); // 等待提权子进程完成（最多 30 秒）
                DWORD code = 0;
                if (GetExitCodeProcess(sei.hProcess, &code))
                    okReq = (code == 0);
                CloseHandle(sei.hProcess);
            }
            if (!okReq)
                wcscpy_s(err, _countof(err), L"需要管理员权限，请在 UAC 弹窗中点击“是”");
        }

        bool nowEnabled = IsAutostartTaskEnabled();
        std::string errStr = err[0] ? WToU8(err) : "";
        std::string resultMsg = std::string("{\"type\":\"autostartResult\",\"enabled\":") +
                                (nowEnabled ? "true" : "false") + ",\"ok\":" +
                                (okReq ? "true" : "false") + ",\"error\":\"" + JsonEscape(errStr) + "\"}";
        PostToPage(ctx, resultMsg);
        return false;
    }
    if (type == "addNearby" || type == "removeSaved") {
        // 这些需要操作设备列表；由 Tray 层提供回调？（见 Show 的接线）
        // 这里仅记录，具体由调用方在 nearbyProvider/statusProvider 之外处理。
        // 为简化：add/remove 在下个版本再细化，当前只刷新列表。
        return false;
    }
    return false;
}

LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* ctx = reinterpret_cast<SettingsCtx*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_GETMINMAXINFO: {
            // 无边框窗口最大化时限制到工作区（不遮挡任务栏）。
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
            MONITORINFO mi{sizeof(mi)};
            HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            if (GetMonitorInfoW(mon, &mi)) {
                mmi->ptMaxPosition.x = mi.rcWork.left - mi.rcMonitor.left;
                mmi->ptMaxPosition.y = mi.rcWork.top - mi.rcMonitor.top;
                mmi->ptMaxSize.x = mi.rcWork.right - mi.rcWork.left;
                mmi->ptMaxSize.y = mi.rcWork.bottom - mi.rcWork.top;
            }
            return 0;
        }
        case WM_SIZE:
            if (ctx && ctx->controller) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                ctx->controller->put_Bounds(rc);
            }
            return 0;
        case WM_TIMER:
            if (wParam == kStatusTimerId && ctx) {
                PushStatus(ctx);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            KillTimer(hwnd, kStatusTimerId);
            if (ctx && ctx->webview && ctx->msgToken.value) {
                ctx->webview->remove_WebMessageReceived(ctx->msgToken);
                ctx->msgToken = {};
            }
            PostQuitMessage(0); // 退出本（worker）线程的消息循环
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

void SettingsWindow::ShowAsync(HINSTANCE hInstance, Config cfg,
                               std::function<std::string()> nearbyProvider,
                               StatusProvider statusProvider, OnDone onDone)
{
    // 整个 WebView2 窗口在独立 STA 线程运行（WebView2 需要 STA，而托盘主线程
    // 已被 main() CoInitializeEx(MTA)）。线程立即 detach，托盘主线程不阻塞，
    // 右键托盘等操作全程保持响应。关闭时在 worker 线程回调 onDone。
    std::thread worker([hInstance, cfg = std::move(cfg),
                        nearbyProvider = std::move(nearbyProvider),
                        statusProvider = std::move(statusProvider),
                        onDone = std::move(onDone)]() mutable {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

        // 注册窗口类
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = SettingsWndProc;
        wc.hInstance = hInstance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kSettingsWndClass;
        wc.hbrBackground = [] {
            // 深色画刷（匹配深色主题 #1f1f1f），避免无边框窗口边缘露白条。
            static HBRUSH brush = CreateSolidBrush(RGB(31, 31, 31));
            return brush;
        }();
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            Logf("webview", "RegisterClassEx 失败 (%lu)\n", GetLastError());
            if (onDone) onDone(false, std::move(cfg));
            CoUninitialize();
            return;
        }

        // 无边框顶层窗口：自定义标题栏由 HTML 绘制，窗口控制按钮也在页面里。
        // 纯 WS_POPUP（无标题栏、无缩放边框），避免 WS_THICKFRAME 的边框被 DWM 画成白条。
        const int width = 900, height = 640;
        HWND hwnd = CreateWindowExW(0, kSettingsWndClass, L"OpenDisplay 设置",
                                    WS_POPUP,
                                    CW_USEDEFAULT, CW_USEDEFAULT, width, height, nullptr, nullptr,
                                    hInstance, nullptr);
        if (!hwnd) {
            Logf("webview", "设置窗口创建失败 (%lu)\n", GetLastError());
            if (onDone) onDone(false, std::move(cfg));
            CoUninitialize();
            return;
        }
        Logf("webview", "设置窗口创建成功 hwnd=0x%p\n", static_cast<void*>(hwnd));
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);

        auto* ctx = new SettingsCtx();
        ctx->hwnd = hwnd;
        ctx->nearbyProvider = std::move(nearbyProvider);
        ctx->statusProvider = std::move(statusProvider);
        ctx->outCfg = &cfg; // cfg 是本线程的局部副本，随窗口会话存活
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx));

        // 异步创建环境 → 控制器
        CreateCoreWebView2EnvironmentWithOptions(
            nullptr, nullptr, nullptr,
            Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [hwnd, ctx, cfg](HRESULT hr, ICoreWebView2Environment* env) -> HRESULT {
                    if (FAILED(hr) || !env) {
                        Logf("webview", "CreateCoreWebView2Environment 失败 hr=0x%08lX\n", hr);
                        ctx->envFailed = true;
                        PostMessageW(hwnd, WM_CLOSE, 0, 0);
                        return hr;
                    }
                    env->CreateCoreWebView2Controller(
                        hwnd,
                        Microsoft::WRL::Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [hwnd, ctx, cfg](HRESULT hr, ICoreWebView2Controller* controller) -> HRESULT {
                                if (FAILED(hr) || !controller) {
                                    Logf("webview", "CreateCoreWebView2Controller 失败 hr=0x%08lX\n", hr);
                                    ctx->envFailed = true;
                                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                                    return hr;
                                }
                                ctx->controller = controller;
                                ctx->controller->get_CoreWebView2(&ctx->webview);
                                Logf("webview", "WebView2 控制器创建成功\n");

                                // 默认背景设为深色，避免页面加载前/边缘露出白色。
                                Microsoft::WRL::ComPtr<ICoreWebView2Controller2> controller2;
                                if (SUCCEEDED(controller->QueryInterface(IID_PPV_ARGS(&controller2)))) {
                                    COREWEBVIEW2_COLOR bg{255, 31, 31, 31}; // A,R,G,B
                                    controller2->put_DefaultBackgroundColor(bg);
                                }

                                std::string initJson = ConfigToInitJson(cfg);
                                std::string statusJson = ctx->statusProvider
                                    ? ctx->statusProvider()
                                    : "{\"state\":\"idle\",\"link\":\"\"}";
                                std::string initScript =
                                    "window.__odBoot = " + initJson +
                                    "; window.__odBoot.status = " + statusJson +
                                    "; window.__odBoot.autostartEnabled = " +
                                    (IsAutostartTaskEnabled() ? "true" : "false") + ";";
                                ctx->webview->AddScriptToExecuteOnDocumentCreated(U8ToW(initScript).c_str(), nullptr);

                                ctx->webview->add_WebMessageReceived(
                                    Microsoft::WRL::Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                                        [ctx](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                            // 页面用 postMessage(对象) 发消息，需用 get_WebMessageAsJson
                                            // 取 JSON（TryGetWebMessageAsString 只对字符串消息有效）。
                                            LPWSTR s = nullptr;
                                            if (SUCCEEDED(args->get_WebMessageAsJson(&s)) && s) {
                                                int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
                                                std::string utf8(n > 0 ? n - 1 : 0, '\0');
                                                if (n > 0)
                                                    WideCharToMultiByte(CP_UTF8, 0, s, -1, utf8.data(), n, nullptr, nullptr);
                                                HandleWebMessage(ctx, utf8);
                                                CoTaskMemFree(s);
                                            }
                                            return S_OK;
                                        }).Get(),
                                    &ctx->msgToken);

                                Microsoft::WRL::ComPtr<ICoreWebView2Settings> settings;
                                ctx->webview->get_Settings(&settings);
                                if (settings) {
                                    settings->put_IsStatusBarEnabled(FALSE);
                                    settings->put_AreDevToolsEnabled(FALSE);
                                }

                                std::string html = BuildSettingsHtml();
                                ctx->webview->NavigateToString(U8ToW(html).c_str());

                                // 启动状态刷新定时器（每 1 秒推一次圆点状态）
                                SetTimer(hwnd, kStatusTimerId, kStatusTimerMs, nullptr);

                                RECT rc;
                                GetClientRect(hwnd, &rc);
                                ctx->controller->put_Bounds(rc);
                                return S_OK;
                            }).Get());
                    return S_OK;
                }).Get());

        // 消息循环（worker 线程；WM_DESTROY → PostQuitMessage 结束）
        MSG m;
        while (GetMessageW(&m, nullptr, 0, 0) > 0) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }

        bool ok = ctx->ok;
        Config resultCfg = std::move(cfg); // cfg 已被页面改动写回

        // 清理
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete ctx;
        CoUninitialize();

        if (onDone)
            onDone(ok, std::move(resultCfg));
    });
    worker.detach();
}

} // namespace od
