#include "app/TrayApp.h"

#include "app/Config.h"
#include "app/Log.h"
#include "app/PauseSignals.h"
#include "app/SenderApp.h"
#include "app/SettingsWindow.h"
#include "app/resources.h"
#include "display/MttVddSettings.h"
#include "net/Mdns.h"

#include <commctrl.h>
#include <uxtheme.h>
#include <shellapi.h>
#include <wtsapi32.h>
#include <taskschd.h>
#include <wrl/client.h>
#include <comdef.h>

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "taskschd.lib")
#pragma comment(lib, "ole32.lib")

namespace od {

namespace {

constexpr UINT WM_APP_TRAY = WM_APP + 1;
// Sender thread -> tray: show a balloon. lParam = heap std::wstring (deleted here).
constexpr UINT WM_APP_NOTIFY = WM_APP + 2;
// A sender's state / transport / wait reason / pause changed (or discovery
// renamed an iPad): refresh icon, tooltip and balloons. No periodic timer.
constexpr UINT WM_APP_STATUS = WM_APP + 3;
// 设置窗口（worker 线程）关闭 → 托盘主线程：lParam = 堆上的 SettingsDone*（在此删除）。
constexpr UINT WM_APP_SETTINGS_DONE = WM_APP + 4;
// WM_DISPLAYCHANGE debounce: a Display Settings Apply fires several changes.
constexpr UINT kDisplayChangeTimerId = 2;
constexpr UINT kDisplayChangeDebounceMs = 1500;
// MTT head already on the desktop at tray start (previous run / reboot): keep
// it this long for the iPad to reconnect, then remove it.
constexpr int kStaleMttHeadGraceMs = 30000;

enum : UINT {
    IDM_CONNECT = 40001,
    IDM_DISCONNECT,
    IDM_SETTINGS,
    IDM_EXIT,

    // One command per configured iPad, IDM_DEVICE_FIRST + index. Toggles that
    // device's sender on its own, so the others keep streaming.
    IDM_DEVICE_FIRST = 41000,
};

const wchar_t* const kWndClass = L"opendisplay-win-tray";

// Explorer broadcasts this message when the taskbar (re)appears and icon
// registrations are lost — e.g. when the app starts before the shell finishes
// logging in, or the shell restarts. Re-add the tray icon on receipt, otherwise
// the icon silently stays missing for the whole session.
UINT g_taskbarCreatedMsg = 0; // RegisterWindowMessageW(L"TaskbarCreated") result

// GUID_CONSOLE_DISPLAY_STATE, spelled out so no INITGUID / extra lib is needed.
constexpr GUID kConsoleDisplayState = {0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};

struct TrayContext {
    HINSTANCE hInstance = nullptr;
    HPOWERNOTIFY displayStateNotify = nullptr; // GUID_CONSOLE_DISPLAY_STATE
    HWINEVENTHOOK desktopSwitchHook = nullptr; // EVENT_SYSTEM_DESKTOPSWITCH
    // One sender per configured iPad, index-aligned with cfg.devices: each
    // drives its own virtual monitor, capture, encoder and input injection.
    // unique_ptr because SenderApp owns a thread and can't be moved.
    std::vector<std::unique_ptr<SenderApp>> apps;
    Config cfg;
    NOTIFYICONDATAW nid{};
    HICON iconGreen = nullptr; // at least one iPad streaming
    HICON iconRed = nullptr;   // configured but nothing streaming
    HICON iconGrey = nullptr;  // no iPad configured
    // Last display-driver problem shown as a balloon, so it is said once per
    // distinct problem rather than on every status refresh.
    std::string shownDriverProblem;
    // Per sender: already told the user its MTT screen is in use (once per wait).
    std::vector<bool> shownInUse;

    // Names/addresses the iPads advertise over Bonjour. Filled by a background
    // browse so the menu and Settings never wait on the network.
    mutable std::mutex discoveredMutex;
    std::map<std::string, std::string> discovered;       // address -> label
    std::map<std::string, std::string> discoveredById;   // TXT id -> address
    std::map<std::string, std::string> discoveredIdLabel; // TXT id -> label
    std::thread discovery;
    std::atomic<bool> discoveryRunning{false};
    HANDLE discoveryStop = CreateEventW(nullptr, TRUE, FALSE, nullptr);  // manual: quit
    HANDLE discoveryWake = CreateEventW(nullptr, FALSE, FALSE, nullptr); // browse now
    HWND hwnd = nullptr;
};

// One Nearby row = one Wi-Fi (mDNS) iPad candidate.
struct NearbyEntry {
    std::string address;   // Wi-Fi IPv4 from mDNS Nearby
    std::string label;
    std::string id;        // TXT / hello id when known
    std::string usbSerial; // Apple serial when USB is part of this row
    bool viaUsb = false;
    bool viaWifi = false;
};

struct SettingsDlgState {
    TrayContext* tray = nullptr;
    Config* cfg = nullptr;
    std::map<std::string, std::string> pendingIds; // address -> id from Nearby Add
    std::vector<NearbyEntry> nearby;
    std::vector<std::string> devices; // working copy; listbox + advanced edit stay in sync
    int settingsTab = 0; // 0 = General, 1 = Advanced
    bool restartAdminRequested = false;
};

void RebuildSenders(TrayContext* ctx)
{
    for (auto& app : ctx->apps)
        app->Stop();
    ctx->apps.clear();
    for (size_t i = 0; i < ctx->cfg.devices.size(); ++i)
        ctx->apps.push_back(std::make_unique<SenderApp>());
}

// The label an iPad publishes for itself. Its Bonjour instance name is what the
// app's settings call the name - but that field falls back to the system name,
// and iOS hands out a plain "iPad" there, so a generic instance name is passed
// over for the host name (the iOS device name, e.g. "iPad-Pro.local").
std::string ReceiverLabel(const MdnsReceiver& receiver)
{
    bool generic = receiver.instance.empty() || receiver.instance == "iPad" || receiver.instance == "iPhone" ||
                   receiver.instance == "OpenDisplay";
    if (!generic)
        return receiver.instance;

    std::string host = receiver.host;
    const std::string suffix = ".local";
    if (host.size() > suffix.size() && host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0)
        host.resize(host.size() - suffix.size());
    return host.empty() ? receiver.instance : host;
}

void MergeDiscovered(TrayContext* ctx, const std::vector<MdnsReceiver>& receivers)
{
    if (receivers.empty())
        return;
    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(ctx->discoveredMutex);
        // Merged, not replaced: an iPad that is asleep doesn't answer, and
        // dropping its name would make the menu flip back to a raw address.
        for (const MdnsReceiver& receiver : receivers) {
            const std::string label = ReceiverLabel(receiver);
            std::string& name = ctx->discovered[receiver.address];
            changed |= name != label;
            name = label;
            if (!receiver.id.empty()) {
                std::string& addr = ctx->discoveredById[receiver.id];
                changed |= addr != receiver.address;
                addr = receiver.address;
                ctx->discoveredIdLabel[receiver.id] = label;
            }
        }
    }
    if (changed && ctx->hwnd)
        PostMessageW(ctx->hwnd, WM_APP_STATUS, 0, 0); // names show in the tooltip
}

// No query timer: one browse at start, then whatever the receivers announce
// (they do on launch and on joining a network), a fresh browse when the
// network changes, and one when the user opens the menu or Settings.
void RunDiscovery(TrayContext* ctx)
{
    constexpr int kBrowseMs = 800;
    MergeDiscovered(ctx, BrowseReceivers(kBrowseMs));

    MdnsListener listener;
    std::vector<MdnsReceiver> heard;
    while (ctx->discoveryRunning) {
        switch (listener.Wait(ctx->discoveryStop, ctx->discoveryWake, heard)) {
            case MdnsListener::Event::Stop:
                return;
            case MdnsListener::Event::Heard:
                MergeDiscovered(ctx, heard);
                break;
            case MdnsListener::Event::Wake:
            case MdnsListener::Event::NetworkChanged:
                MergeDiscovered(ctx, BrowseReceivers(kBrowseMs));
                break;
        }
    }
}

bool AnyStreaming(const TrayContext* ctx)
{
    for (const auto& app : ctx->apps)
        if (app->GetState() == SenderApp::State::Streaming)
            return true;
    return false;
}

bool AnyRunning(const TrayContext* ctx)
{
    for (const auto& app : ctx->apps)
        if (app->IsRunning())
            return true;
    return false;
}

HICON MakeStatusIcon(COLORREF dotColor)
{
    constexpr int S = 32;
    HDC screen = GetDC(nullptr);
    HDC colorDC = CreateCompatibleDC(screen);
    HDC maskDC = CreateCompatibleDC(screen);
    HBITMAP colorBmp = CreateCompatibleBitmap(screen, S, S);
    HBITMAP maskBmp = CreateBitmap(S, S, 1, 1, nullptr);
    ReleaseDC(nullptr, screen);

    HBITMAP oldColor = static_cast<HBITMAP>(SelectObject(colorDC, colorBmp));
    HBITMAP oldMask = static_cast<HBITMAP>(SelectObject(maskDC, maskBmp));

    RECT rc{0, 0, S, S};
    FillRect(colorDC, &rc, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    FillRect(maskDC, &rc, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));

    auto opaqueMask = [&] {
        SelectObject(maskDC, GetStockObject(BLACK_BRUSH));
        SelectObject(maskDC, GetStockObject(BLACK_PEN));
    };

    HBRUSH bodyBrush = CreateSolidBrush(RGB(55, 65, 90));
    HPEN edgePen = CreatePen(PS_SOLID, 1, RGB(20, 24, 34));
    SelectObject(colorDC, bodyBrush);
    SelectObject(colorDC, edgePen);
    RoundRect(colorDC, 2, 5, 30, 23, 5, 5);
    Rectangle(colorDC, 13, 22, 19, 27);
    Rectangle(colorDC, 8, 27, 24, 30);
    opaqueMask();
    RoundRect(maskDC, 2, 5, 30, 23, 5, 5);
    Rectangle(maskDC, 13, 22, 19, 27);
    Rectangle(maskDC, 8, 27, 24, 30);

    HBRUSH innerBrush = CreateSolidBrush(RGB(120, 150, 195));
    SelectObject(colorDC, innerBrush);
    SelectObject(colorDC, static_cast<HPEN>(GetStockObject(NULL_PEN)));
    RoundRect(colorDC, 5, 8, 27, 20, 3, 3);

    HBRUSH dotBrush = CreateSolidBrush(dotColor);
    HPEN dotPen = CreatePen(PS_SOLID, 1, RGB(245, 245, 245));
    SelectObject(colorDC, dotBrush);
    SelectObject(colorDC, dotPen);
    Ellipse(colorDC, 0, 0, 14, 14);
    opaqueMask();
    Ellipse(maskDC, 0, 0, 14, 14);

    SelectObject(colorDC, oldColor);
    SelectObject(maskDC, oldMask);
    DeleteDC(colorDC);
    DeleteDC(maskDC);
    DeleteObject(bodyBrush);
    DeleteObject(edgePen);
    DeleteObject(innerBrush);
    DeleteObject(dotBrush);
    DeleteObject(dotPen);

    ICONINFO ii{};
    ii.fIcon = TRUE;
    ii.hbmColor = colorBmp;
    ii.hbmMask = maskBmp;
    HICON icon = CreateIconIndirect(&ii);

    DeleteObject(colorBmp);
    DeleteObject(maskBmp);
    return icon;
}

HICON PickIcon(TrayContext* ctx)
{
    if (ctx->cfg.devices.empty())
        return ctx->iconGrey;
    if (AnyStreaming(ctx))
        return ctx->iconGreen;
    return ctx->iconRed;
}

std::wstring Widen(const std::string& s)
{
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& w)
{
    if (w.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// JSON 字符串转义（用于向 WebView2 页面下发设备名/地址）。
std::string JsonEsc(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
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

// A configured device is "<address>[ <name>]": everything up to the first space
// is the address, the rest is the label; without one the address is the label.
struct DeviceEntry {
    std::string address;
    std::string label;
};

DeviceEntry SplitDevice(const std::string& entry)
{
    size_t space = entry.find_first_of(" \t");
    if (space == std::string::npos)
        return {entry, entry};

    std::string address = entry.substr(0, space);
    size_t nameStart = entry.find_first_not_of(" \t", space);
    if (nameStart == std::string::npos)
        return {address, address};

    size_t nameEnd = entry.find_last_not_of(" \t");
    return {address, entry.substr(nameStart, nameEnd - nameStart + 1)};
}

bool IsLoopbackHost(const std::string& host)
{
    return host == "127.0.0.1" || host == "::1" || host == "localhost";
}

// USB-discovery leftovers used 127.0.0.1 + serial as the Your iPads row.
// Nearby (Wi-Fi) adds always store a LAN IP; USB is only a later transport.
bool IsUsbOnlyPlaceholder(const std::string& device)
{
    return IsLoopbackHost(SplitDevice(device).address);
}

void PruneUsbOnlyPlaceholders(std::vector<std::string>& devices,
                              std::map<std::string, std::string>* pendingIds)
{
    std::vector<std::string> kept;
    kept.reserve(devices.size());
    for (const std::string& device : devices) {
        if (IsUsbOnlyPlaceholder(device)) {
            if (pendingIds)
                pendingIds->erase(SplitDevice(device).address);
            continue;
        }
        kept.push_back(device);
    }
    devices = std::move(kept);
}

bool IsElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION el{};
    DWORD size = sizeof(el);
    bool ok = GetTokenInformation(token, TokenElevation, &el, sizeof(el), &size) != 0;
    CloseHandle(token);
    return ok && el.TokenIsElevated;
}

// 开机自启：用「管理员权限」计划任务（登录时触发、最高权限、延迟 3 秒）而非
// 注册表 Run 键。原因：程序退出会禁用 MTT 适配器，重启后需管理员权限重新启用，
// 普通权限的 Run 键无法做到；延迟 3 秒则避免任务栏未就绪时托盘图标注册失败。
const wchar_t* const kAutostartTaskName = L"OpenDisplay iPad副屏（开机自启）";

// 通过任务计划程序 COM 接口查询/创建/删除自启计划任务。
// enable=true 创建（登录时触发 + 最高权限 + 3 秒延迟）；false 删除。
// 返回 true 表示成功，false 为失败（errBuf 给出原因）。
bool SetAutostartTaskImpl(bool enable, wchar_t* errBuf, size_t errLen)
{
    if (errBuf && errLen)
        errBuf[0] = L'\0';

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // 仅当本轮调用真正初始化了 COM（S_OK）时才负责反初始化；S_FALSE（同线程已
    // 初始化）或 RPC_E_CHANGED_MODE（他人以不同模式初始化）由调用方/进程持有。
    const bool comOwned = (hr == S_OK);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        if (errBuf && errLen)
            wcscpy_s(errBuf, errLen, L"COM 初始化失败");
        return false;
    }

    auto setErr = [&](const wchar_t* text) {
        if (errBuf && errLen)
            wcscpy_s(errBuf, errLen, text);
    };

    do {
        Microsoft::WRL::ComPtr<ITaskService> svc;
        hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&svc));
        if (FAILED(hr)) { setErr(L"无法连接任务计划程序"); break; }
        hr = svc->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
        if (FAILED(hr)) { setErr(L"无法连接任务计划程序"); break; }

        Microsoft::WRL::ComPtr<ITaskFolder> root;
        hr = svc->GetFolder(_bstr_t(L"\\"), &root);
        if (FAILED(hr)) break;

        if (enable) {
            // 创建任务定义：登录时触发、最高权限（管理员）、执行本 exe。
            Microsoft::WRL::ComPtr<ITaskDefinition> task;
            hr = svc->NewTask(0, &task);
            if (FAILED(hr)) break;

            {
                Microsoft::WRL::ComPtr<IPrincipal> principal;
                task->get_Principal(&principal);
                principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN);
                principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);
            }

            {
                Microsoft::WRL::ComPtr<ITriggerCollection> triggers;
                task->get_Triggers(&triggers);
                Microsoft::WRL::ComPtr<ITrigger> trigger;
                triggers->Create(TASK_TRIGGER_LOGON, &trigger);
                Microsoft::WRL::ComPtr<ILogonTrigger> logon;
                trigger->QueryInterface(IID_PPV_ARGS(&logon));
                // 3 秒登录延迟：等任务栏就绪，避免托盘图标注册失败。
                logon->put_Delay(_bstr_t(L"PT3S"));
            }

            {
                Microsoft::WRL::ComPtr<IActionCollection> actions;
                task->get_Actions(&actions);
                Microsoft::WRL::ComPtr<IAction> action;
                actions->Create(TASK_ACTION_EXEC, &action);
                Microsoft::WRL::ComPtr<IExecAction> exec;
                action->QueryInterface(IID_PPV_ARGS(&exec));
                wchar_t exe[MAX_PATH + 1] = {};
                GetModuleFileNameW(nullptr, exe, MAX_PATH);
                exec->put_Path(_bstr_t(exe));
            }

            {
                Microsoft::WRL::ComPtr<ITaskSettings> settings;
                task->get_Settings(&settings);
                settings->put_StartWhenAvailable(VARIANT_TRUE);
                settings->put_ExecutionTimeLimit(_bstr_t(L"PT0S")); // 无时间限制
                settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE);
                settings->put_StopIfGoingOnBatteries(VARIANT_FALSE);
            }

            Microsoft::WRL::ComPtr<IRegisteredTask> registered;
            hr = root->RegisterTaskDefinition(_bstr_t(kAutostartTaskName), task.Get(),
                                              TASK_CREATE_OR_UPDATE, _variant_t(), _variant_t(),
                                              TASK_LOGON_INTERACTIVE_TOKEN, _variant_t(), &registered);
            if (FAILED(hr)) {
                if (hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED))
                    setErr(L"需要管理员权限才能创建开机自启任务");
                else
                    setErr(L"创建计划任务失败");
            }
        } else {
            hr = root->DeleteTask(_bstr_t(kAutostartTaskName), 0);
            if (FAILED(hr) && hr != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
                setErr(L"删除计划任务失败");
            else
                hr = S_OK; // 不存在也视为已卸载
        }
    } while (false);

    if (comOwned)
        CoUninitialize();
    return SUCCEEDED(hr);
}

bool IsAutostartTaskEnabledImpl()
{
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comOwned = (hr == S_OK);
    bool enabled = false;

    Microsoft::WRL::ComPtr<ITaskService> svc;
    if (SUCCEEDED(CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&svc))) &&
        SUCCEEDED(svc->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t()))) {
        Microsoft::WRL::ComPtr<ITaskFolder> root;
        if (SUCCEEDED(svc->GetFolder(_bstr_t(L"\\"), &root))) {
            Microsoft::WRL::ComPtr<IRegisteredTask> task;
            if (SUCCEEDED(root->GetTask(_bstr_t(kAutostartTaskName), &task)))
                enabled = true;
        }
    }

    if (comOwned)
        CoUninitialize();
    return enabled;
}

// Prefer the live address from mDNS when we have a stored TXT id.
std::string ResolveDeviceAddress(const TrayContext* ctx, size_t index)
{
    DeviceEntry entry = SplitDevice(ctx->cfg.devices[index]);
    if (index < ctx->cfg.deviceIds.size() && !ctx->cfg.deviceIds[index].empty()) {
        std::lock_guard<std::mutex> lock(ctx->discoveredMutex);
        auto it = ctx->discoveredById.find(ctx->cfg.deviceIds[index]);
        if (it != ctx->discoveredById.end() && !it->second.empty())
            return it->second;
    }
    return entry.address;
}

// The iPad's Bonjour TXT id saved with this entry (empty if none).
std::string EntryId(const TrayContext* ctx, size_t index)
{
    return index < ctx->cfg.deviceIds.size() ? ctx->cfg.deviceIds[index] : std::string{};
}

// Ids the senders learned from a Wi-Fi hello at a typed-in address: saved
// once per change, so USB and the next start match on them.
void SaveLearnedIds(TrayContext* ctx)
{
    Config& cfg = ctx->cfg;
    cfg.deviceIds.resize(cfg.devices.size());
    cfg.deviceIdFromHello.resize(cfg.devices.size());
    bool changed = false;
    for (size_t i = 0; i < ctx->apps.size() && i < cfg.devices.size(); ++i) {
        if (!cfg.IdFollowsHello(i))
            continue;
        const std::string id = ctx->apps[i]->EntryId();
        if (id.empty() || id == cfg.deviceIds[i])
            continue;
        cfg.deviceIds[i] = id;
        cfg.deviceIdFromHello[i] = "1";
        changed = true;
    }
    if (!changed)
        return;
    // Senders write USB bindings to the file: keep those, not the tray's copy.
    const Config onDisk = Config::Load();
    cfg.usbLinkSerials = onDisk.usbLinkSerials;
    cfg.usbLinkIds = onDisk.usbLinkIds;
    cfg.Save();
}

std::wstring DeviceLabel(const TrayContext* ctx, size_t index)
{
    DeviceEntry entry = SplitDevice(ctx->cfg.devices[index]);
    if (entry.label != entry.address)
        return Widen(entry.label);

    if (index < ctx->cfg.deviceIds.size() && !ctx->cfg.deviceIds[index].empty()) {
        std::lock_guard<std::mutex> lock(ctx->discoveredMutex);
        auto byId = ctx->discoveredIdLabel.find(ctx->cfg.deviceIds[index]);
        if (byId != ctx->discoveredIdLabel.end())
            return Widen(byId->second);
    }

    std::lock_guard<std::mutex> lock(ctx->discoveredMutex);
    auto discovered = ctx->discovered.find(entry.address);
    return Widen(discovered != ctx->discovered.end() ? discovered->second : entry.address);
}

// Label of the iPad whose sender holds the display this one waits for.
std::wstring HolderLabel(const TrayContext* ctx, const SenderApp& app)
{
    const void* holder = app.BlockedByApp();
    for (size_t i = 0; holder && i < ctx->apps.size() && i < ctx->cfg.devices.size(); ++i)
        if (ctx->apps[i].get() == holder)
            return DeviceLabel(ctx, i);
    return {};
}

std::wstring DeviceStatusText(const TrayContext* ctx, size_t index)
{
    std::wstring text = DeviceLabel(ctx, index);
    const SenderApp& app = *ctx->apps[index];
    switch (app.GetState()) {
        case SenderApp::State::Streaming: {
            const wchar_t* link = L"";
            switch (app.GetTransport()) {
                case SenderApp::Transport::Usb:
                    link = L" (USB)";
                    break;
                case SenderApp::Transport::Wifi:
                    link = L" (Wi-Fi)";
                    break;
                default:
                    break;
            }
            return text + L"  已连接" + link;
        }
        case SenderApp::State::Connecting:
            return text + L"  正在连接…";
        case SenderApp::State::Blocked: {
            const std::wstring holder = HolderLabel(ctx, app);
            if (app.BlockedExclusive() && !holder.empty())
                return text + L"  等待中 - 屏幕正被 " + holder + L" 占用";
            const uint32_t bw = app.BlockedByWidth(), bh = app.BlockedByHeight();
            if (bw && bh)
                return text + L"  等待 " + std::to_wstring(bw) + L"x" + std::to_wstring(bh);
            if (!holder.empty())
                return text + L"  等待 " + holder;
            return text + L"  等待显示器";
        }
        case SenderApp::State::DriverMissing:
            return text + L"  缺少显示驱动";
        default:
            return text + L"  未连接";
    }
}

void AddTrayIcon(TrayContext* ctx)
{
    ctx->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    ctx->nid.hIcon = PickIcon(ctx);
    Shell_NotifyIconW(NIM_ADD, &ctx->nid);
}

void UpdateStatus(TrayContext* ctx)
{
    std::wstring tip = L"OpenDisplay";
    if (ctx->cfg.devices.empty()) {
        tip += L" - 尚未添加 iPad";
    } else {
        for (size_t i = 0; i < ctx->cfg.devices.size(); ++i)
            tip += (i == 0 ? L" - " : L" | ") + DeviceStatusText(ctx, i);
    }

    ctx->nid.hIcon = PickIcon(ctx);
    wcsncpy_s(ctx->nid.szTip, tip.c_str(), _TRUNCATE);
    ctx->nid.uFlags = NIF_ICON | NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &ctx->nid);

    // The chosen display driver is missing: say so instead of silently using
    // the other one (Settings > Advanced > Display driver).
    std::string problem;
    for (const auto& app : ctx->apps) {
        if (app->GetState() == SenderApp::State::DriverMissing) {
            problem = app->StatusDetail();
            break;
        }
    }
    if (!problem.empty() && problem != ctx->shownDriverProblem) {
        NOTIFYICONDATAW info = ctx->nid;
        info.uFlags = NIF_INFO;
        info.dwInfoFlags = NIIF_WARNING;
        wcsncpy_s(info.szInfoTitle, L"OpenDisplay", _TRUNCATE);
        const std::wstring msg = Widen(problem) + L" 请在 设置 > 高级 > 显示驱动 中更改。";
        wcsncpy_s(info.szInfo, msg.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &info);
    }
    ctx->shownDriverProblem = problem;

    // MTT drives one iPad: a second one that connects waits, and says so once.
    ctx->shownInUse.resize(ctx->apps.size(), false);
    for (size_t i = 0; i < ctx->apps.size() && i < ctx->cfg.devices.size(); ++i) {
        const SenderApp& app = *ctx->apps[i];
        const bool waiting = app.GetState() == SenderApp::State::Blocked && app.BlockedExclusive();
        if (!waiting) {
            ctx->shownInUse[i] = false;
            continue;
        }
        if (ctx->shownInUse[i])
            continue;
        const std::wstring holder = HolderLabel(ctx, app);
        if (holder.empty())
            continue;
        ctx->shownInUse[i] = true;
        NOTIFYICONDATAW info = ctx->nid;
        info.uFlags = NIF_INFO;
        info.dwInfoFlags = NIIF_INFO;
        wcsncpy_s(info.szInfoTitle, L"OpenDisplay", _TRUNCATE);
        const std::wstring msg = DeviceLabel(ctx, i) + L" 正在等待：屏幕正被 " + holder +
                                 L" 占用，该 iPad 断开后会连上。（在 设置 > 高级 中选 Parsec 可同时驱动多台 iPad。）";
        wcsncpy_s(info.szInfo, msg.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &info);
    }
}

// Settings > Advanced "Display driver" combo; index order matches kBackendValues.
const char* const kBackendValues[] = {"auto", "mtt", "parsec"};

void FillBackendCombo(HWND dlg, const std::string& value)
{
    HWND cb = GetDlgItem(dlg, IDC_DISPLAY_BACKEND);
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动"));
    SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"MTT（1 台 iPad）"));
    SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Parsec（多台 iPad）"));
    int sel = 0;
    for (int i = 0; i < 3; ++i)
        if (value == kBackendValues[i])
            sel = i;
    SendMessageW(cb, CB_SETCURSEL, sel, 0);
}

std::string ReadBackendCombo(HWND dlg)
{
    const LRESULT sel = SendDlgItemMessageW(dlg, IDC_DISPLAY_BACKEND, CB_GETCURSEL, 0, 0);
    return (sel >= 0 && sel < 3) ? kBackendValues[sel] : "auto";
}

void ApplyAndRestart(TrayContext* ctx)
{
    RebuildSenders(ctx);
    for (size_t i = 0; i < ctx->apps.size(); ++i)
        ctx->apps[i]->Start(ResolveDeviceAddress(ctx, i), ctx->cfg.port, EntryId(ctx, i), ctx->cfg.IdFollowsHello(i));
    UpdateStatus(ctx);
}

void FillPictureCombo(HWND dlg)
{
    HWND cb = GetDlgItem(dlg, IDC_PICTURE_PRESET);
    SendMessageW(cb, CB_RESETCONTENT, 0, 0);
    SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"响应更快"));
    SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"均衡"));
    SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"更清晰"));
}

void SelectPictureCombo(HWND dlg, const std::string& preset)
{
    int idx = 0;
    if (preset == "balanced")
        idx = 1;
    else if (preset == "quality")
        idx = 2;
    SendDlgItemMessageW(dlg, IDC_PICTURE_PRESET, CB_SETCURSEL, idx, 0);
}

std::string ReadPictureCombo(HWND dlg)
{
    LRESULT i = SendDlgItemMessageW(dlg, IDC_PICTURE_PRESET, CB_GETCURSEL, 0, 0);
    if (i == 1)
        return "balanced";
    if (i == 2)
        return "quality";
    return "speed";
}

void FillEncodeCombos(HWND dlg)
{
    auto fillMode = [&](int id) {
        HWND cb = GetDlgItem(dlg, id);
        SendMessageW(cb, CB_RESETCONTENT, 0, 0);
        SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"动态"));
        SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"固定"));
    };
    auto fillPreset = [&](int id) {
        HWND cb = GetDlgItem(dlg, id);
        SendMessageW(cb, CB_RESETCONTENT, 0, 0);
        SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"快速"));
        SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"均衡"));
        SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"高画质"));
    };
    fillMode(IDC_USB_ENCODE_MODE);
    fillMode(IDC_WIFI_ENCODE_MODE);
    fillPreset(IDC_USB_ENCODE_PRESET);
    fillPreset(IDC_WIFI_ENCODE_PRESET);
}

void SelectEncodeCombos(HWND dlg, const Config& cfg)
{
    auto selMode = [&](int id, const std::string& mode) {
        SendDlgItemMessageW(dlg, id, CB_SETCURSEL, mode == "fixed" ? 1 : 0, 0);
    };
    auto selPreset = [&](int id, const std::string& preset) {
        int idx = 0;
        if (preset == "balanced")
            idx = 1;
        else if (preset == "quality")
            idx = 2;
        SendDlgItemMessageW(dlg, id, CB_SETCURSEL, idx, 0);
    };
    selMode(IDC_USB_ENCODE_MODE, cfg.usbEncodeMode);
    selMode(IDC_WIFI_ENCODE_MODE, cfg.wifiEncodeMode);
    selPreset(IDC_USB_ENCODE_PRESET, cfg.usbEncodePreset);
    selPreset(IDC_WIFI_ENCODE_PRESET, cfg.wifiEncodePreset);
}

void ReadEncodeCombos(HWND dlg, Config& cfg)
{
    auto modeOf = [&](int id) -> std::string {
        return SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0) == 1 ? "fixed" : "dynamic";
    };
    auto presetOf = [&](int id) -> std::string {
        LRESULT i = SendDlgItemMessageW(dlg, id, CB_GETCURSEL, 0, 0);
        if (i == 1)
            return "balanced";
        if (i == 2)
            return "quality";
        return "speed";
    };
    cfg.usbEncodeMode = modeOf(IDC_USB_ENCODE_MODE);
    cfg.wifiEncodeMode = modeOf(IDC_WIFI_ENCODE_MODE);
    cfg.usbEncodePreset = presetOf(IDC_USB_ENCODE_PRESET);
    cfg.wifiEncodePreset = presetOf(IDC_WIFI_ENCODE_PRESET);
}

void SyncDevicesEdit(HWND dlg, const SettingsDlgState* state);
void RefreshSavedList(HWND dlg, SettingsDlgState* state);
void ReadDevicesEdit(HWND dlg, SettingsDlgState* state);

const int kGeneralControlIds[] = {
    IDC_LEAD_HINT, IDC_NEARBY_GROUP, IDC_NEARBY, IDC_REFRESH_NEARBY, IDC_NEARBY_HINT, IDC_ADD_NEARBY,
    IDC_REMOVE_SAVED, IDC_SAVED_GROUP, IDC_SAVED, IDC_CONNECTION_GROUP, IDC_AUTORECONNECT,
    IDC_USB_WIFI_FAILOVER, IDC_AUTOSTART, IDC_QUALITY_GROUP, IDC_QUALITY_LABEL, IDC_PICTURE_PRESET,
};

const int kAdvancedControlIds[] = {
    IDC_ADV_MANUAL_GROUP, IDC_ADV_MANUAL_HINT, IDC_DEVICES, IDC_ADV_MANUAL_EXAMPLE,
    IDC_ADV_PORT_LABEL, IDC_PORT, IDC_ADV_BACKEND_LABEL, IDC_DISPLAY_BACKEND, IDC_SEPARATE_USB_WIFI,
    IDC_ADV_USB_LABEL, IDC_ADV_USB_MODE_LABEL, IDC_USB_ENCODE_MODE,
    IDC_ADV_USB_PRESET_LABEL, IDC_USB_ENCODE_PRESET,
    IDC_ADV_WIFI_LABEL, IDC_ADV_WIFI_MODE_LABEL, IDC_WIFI_ENCODE_MODE,
    IDC_ADV_WIFI_PRESET_LABEL, IDC_WIFI_ENCODE_PRESET,
    IDC_RESTART_ADMIN, IDC_ADMIN_NOTE,
};

void UpdateSeparateEncodeEnable(HWND dlg, bool advancedVisible)
{
    // Separate USB/Wi-Fi combos only useful when Advanced is showing and that checkbox is on.
    const bool separate = IsDlgButtonChecked(dlg, IDC_SEPARATE_USB_WIFI) == BST_CHECKED;
    const BOOL enableSeparate = (advancedVisible && separate) ? TRUE : FALSE;
    for (int id : {IDC_USB_ENCODE_MODE, IDC_USB_ENCODE_PRESET, IDC_WIFI_ENCODE_MODE, IDC_WIFI_ENCODE_PRESET,
                   IDC_ADV_USB_LABEL, IDC_ADV_USB_MODE_LABEL, IDC_ADV_USB_PRESET_LABEL, IDC_ADV_WIFI_LABEL,
                   IDC_ADV_WIFI_MODE_LABEL, IDC_ADV_WIFI_PRESET_LABEL})
        EnableWindow(GetDlgItem(dlg, id), enableSeparate);
}

void ShowSettingsTab(HWND dlg, SettingsDlgState* state, int tab)
{
    // Sync devices list <-> advanced multiline when leaving/entering Advanced.
    if (state->settingsTab == 1 && tab != 1) {
        ReadDevicesEdit(dlg, state);
        RefreshSavedList(dlg, state);
    } else if (tab == 1 && state->settingsTab != 1) {
        SyncDevicesEdit(dlg, state);
    }

    state->settingsTab = tab;
    const bool general = (tab == 0);
    for (int id : kGeneralControlIds)
        ShowWindow(GetDlgItem(dlg, id), general ? SW_SHOW : SW_HIDE);
    for (int id : kAdvancedControlIds)
        ShowWindow(GetDlgItem(dlg, id), general ? SW_HIDE : SW_SHOW);
    UpdateSeparateEncodeEnable(dlg, !general);
}

void InitSettingsTabs(HWND dlg)
{
    HWND tabs = GetDlgItem(dlg, IDC_SETTINGS_TABS);
    TCITEMW item{};
    item.mask = TCIF_TEXT;
    item.pszText = const_cast<wchar_t*>(L"常规");
    TabCtrl_InsertItem(tabs, 0, &item);
    item.pszText = const_cast<wchar_t*>(L"高级");
    TabCtrl_InsertItem(tabs, 1, &item);
    TabCtrl_SetCurSel(tabs, 0);
}

// Enlarge the tab control to the band above OK/Cancel, then place General /
// Advanced siblings inside TCM_ADJUSTRECT's display rect so content sits in
// the tab page frame (not below a short strip).
void LayoutSettingsTabPages(HWND dlg)
{
    HWND tabs = GetDlgItem(dlg, IDC_SETTINGS_TABS);
    HWND ok = GetDlgItem(dlg, IDOK);
    if (!tabs || !ok)
        return;

    RECT dlgClient{};
    GetClientRect(dlg, &dlgClient);

    RECT okRect{};
    GetWindowRect(ok, &okRect);
    MapWindowPoints(HWND_DESKTOP, dlg, reinterpret_cast<POINT*>(&okRect), 2);

    // Match the dialog-template margins: 8 DU inset; small gap above OK/Cancel.
    RECT margin{8, 6, 8, 8};
    MapDialogRect(dlg, &margin);
    const int gap = margin.bottom; // 8 DU -> px
    const int left = margin.left;
    const int top = margin.top;
    const int right = dlgClient.right - margin.right;
    const int bottom = okRect.top - gap;
    if (right <= left || bottom <= top)
        return;

    SetWindowPos(tabs, HWND_BOTTOM, left, top, right - left, bottom - top,
                 SWP_NOACTIVATE);

    RECT display{};
    GetClientRect(tabs, &display);
    TabCtrl_AdjustRect(tabs, FALSE, &display);
    MapWindowPoints(tabs, dlg, reinterpret_cast<POINT*>(&display), 2);

    // Center the page content (extent from the dialog template, in dialog units) in the tab.
    RECT design{16, 34, 324, 264};
    MapDialogRect(dlg, &design);

    const int dx = (display.left + display.right - design.left - design.right) / 2;
    const int dy = (display.top + display.bottom - design.top - design.bottom) / 2;

    auto moveId = [&](int id) {
        HWND ctl = GetDlgItem(dlg, id);
        if (!ctl)
            return;
        RECT rc{};
        GetWindowRect(ctl, &rc);
        MapWindowPoints(HWND_DESKTOP, dlg, reinterpret_cast<POINT*>(&rc), 2);
        OffsetRect(&rc, dx, dy);
        SetWindowPos(ctl, nullptr, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    };

    for (int id : kGeneralControlIds)
        moveId(id);
    for (int id : kAdvancedControlIds)
        moveId(id);
}


void SetNearbyHint(HWND dlg, bool empty)
{
    // Short one-liner next to Refresh; Add > is enough when the list has rows.
    const wchar_t* text = empty ? L"当前 Wi-Fi 未发现设备。" : L"";
    SetDlgItemTextW(dlg, IDC_NEARBY_HINT, text);
}

void SyncDevicesEdit(HWND dlg, const SettingsDlgState* state)
{
    std::wstring list;
    for (const std::string& device : state->devices)
        list += Widen(device) + L"\r\n";
    SetDlgItemTextW(dlg, IDC_DEVICES, list.c_str());
}

void RefreshSavedList(HWND dlg, SettingsDlgState* state)
{
    PruneUsbOnlyPlaceholders(state->devices, &state->pendingIds);

    HWND list = GetDlgItem(dlg, IDC_SAVED);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const std::string& device : state->devices) {
        DeviceEntry entry = SplitDevice(device);
        std::string label = entry.label;

        // Prefer live Wi-Fi / mDNS host name when we know the device id.
        auto idIt = state->pendingIds.find(entry.address);
        if (idIt != state->pendingIds.end() && !idIt->second.empty() && state->tray) {
            std::lock_guard<std::mutex> lock(state->tray->discoveredMutex);
            auto byId = state->tray->discoveredIdLabel.find(idIt->second);
            if (byId != state->tray->discoveredIdLabel.end() && !byId->second.empty())
                label = byId->second;
        } else if (state->tray) {
            std::lock_guard<std::mutex> lock(state->tray->discoveredMutex);
            auto byAddr = state->tray->discovered.find(entry.address);
            if (byAddr != state->tray->discovered.end() && !byAddr->second.empty())
                label = byAddr->second;
        }

        std::wstring line = Widen(label);
        if (label != entry.address) {
            line += L"  (";
            line += Widen(entry.address);
            line += L")";
        }
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
}

// Pull the advanced multiline edit back into state->devices (manual add / rename).
void ReadDevicesEdit(HWND dlg, SettingsDlgState* state)
{
    wchar_t buf[4096] = {};
    GetDlgItemTextW(dlg, IDC_DEVICES, buf, static_cast<int>(std::size(buf)));
    state->devices.clear();
    std::wstring text(buf);
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find_first_of(L"\r\n", pos);
        if (end == std::wstring::npos)
            end = text.size();
        std::wstring line = text.substr(pos, end - pos);
        size_t first = line.find_first_not_of(L" \t");
        size_t last = line.find_last_not_of(L" \t");
        if (first != std::wstring::npos) {
            std::string device = Narrow(line.substr(first, last - first + 1));
            if (IsUsbOnlyPlaceholder(device))
                continue; // never re-admit USB-only leftover rows
            std::string address = SplitDevice(device).address;
            bool known = false;
            for (const std::string& existing : state->devices)
                known = known || SplitDevice(existing).address == address;
            if (!known)
                state->devices.push_back(device);
        }
        pos = end + 1;
    }
}

NearbyEntry WifiOnlyEntry(const MdnsReceiver& receiver)
{
    NearbyEntry e;
    e.address = receiver.address;
    e.label = ReceiverLabel(receiver);
    e.id = receiver.id;
    e.viaWifi = true;
    return e;
}

// Nearby = Wi-Fi (mDNS) only. USB devices still connect via Your iPads / usbmux;
// they are not listed here for discovery.
std::vector<NearbyEntry> BuildNearbyEntries(const std::vector<MdnsReceiver>& wifi)
{
    std::vector<NearbyEntry> out;
    out.reserve(wifi.size());
    for (const MdnsReceiver& r : wifi)
        out.push_back(WifiOnlyEntry(r));
    return out;
}

void RefreshNearbyList(HWND dlg, SettingsDlgState* state)
{
    const std::vector<MdnsReceiver> wifi = BrowseReceivers(800);
    state->nearby = BuildNearbyEntries(wifi);

    HWND list = GetDlgItem(dlg, IDC_NEARBY);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    for (const NearbyEntry& entry : state->nearby) {
        std::wstring line = Widen(entry.label);
        line += L"  (";
        line += Widen(entry.address);
        line += L")";
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
    }
    SetNearbyHint(dlg, state->nearby.empty());
}

void OnRefreshNearby(HWND dlg, SettingsDlgState* state)
{
    SetDlgItemTextW(dlg, IDC_NEARBY_HINT, L"正在查找…");
    UpdateWindow(GetDlgItem(dlg, IDC_NEARBY_HINT));
    RefreshNearbyList(dlg, state);
}

void AddSelectedNearby(HWND dlg, SettingsDlgState* state)
{
    HWND list = GetDlgItem(dlg, IDC_NEARBY);
    LRESULT sel = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || sel < 0 || static_cast<size_t>(sel) >= state->nearby.size())
        return;

    const NearbyEntry& receiver = state->nearby[static_cast<size_t>(sel)];
    if (IsLoopbackHost(receiver.address) || !receiver.viaWifi)
        return; // Your iPads only accepts Wi-Fi Nearby discoveries
    for (size_t i = 0; i < state->devices.size(); ++i) {
        if (SplitDevice(state->devices[i]).address == receiver.address) {
            // Already listed, typed in without an id: take the one Nearby
            // reports, so USB can match this iPad. Saved with the dialog.
            std::string& id = state->pendingIds[receiver.address];
            if (id.empty() && !receiver.id.empty()) {
                id = receiver.id;
                RefreshSavedList(dlg, state);
                SyncDevicesEdit(dlg, state);
            }
            return; // no duplicate
        }
        if (!receiver.id.empty()) {
            auto it = state->pendingIds.find(SplitDevice(state->devices[i]).address);
            if (it != state->pendingIds.end() && it->second == receiver.id)
                return;
        }
    }

    std::string entry = receiver.address;
    if (!receiver.label.empty() && receiver.label != receiver.address)
        entry += " " + receiver.label;

    state->devices.push_back(entry);
    if (!receiver.id.empty())
        state->pendingIds[receiver.address] = receiver.id;
    // USB-only Add: remember serial so a later hello can bind without re-guessing.
    if (receiver.viaUsb && !receiver.usbSerial.empty() && !receiver.id.empty() && state->cfg)
        state->cfg->RememberUsbSerialId(receiver.usbSerial, receiver.id);
    RefreshSavedList(dlg, state);
    SyncDevicesEdit(dlg, state);
}

void RemoveSelectedSaved(HWND dlg, SettingsDlgState* state)
{
    HWND list = GetDlgItem(dlg, IDC_SAVED);
    LRESULT sel = SendMessageW(list, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR || sel < 0 || static_cast<size_t>(sel) >= state->devices.size())
        return;
    state->devices.erase(state->devices.begin() + static_cast<size_t>(sel));
    RefreshSavedList(dlg, state);
    SyncDevicesEdit(dlg, state);
}

bool RelaunchElevated(HWND hwnd, TrayContext* ctx)
{
    for (auto& app : ctx->apps)
        app->Stop();
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.lpVerb = L"runas";
    sei.lpFile = exe;
    sei.nShow = SW_NORMAL;
    if (ShellExecuteExW(&sei)) {
        DestroyWindow(hwnd);
        return true;
    }
    ApplyAndRestart(ctx);
    return false;
}

INT_PTR CALLBACK SettingsDlgProc(HWND dlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg) {
        case WM_INITDIALOG: {
            auto* state = reinterpret_cast<SettingsDlgState*>(lParam);
            SetWindowLongPtrW(dlg, GWLP_USERDATA, static_cast<LONG_PTR>(lParam));
            Config* cfg = state->cfg;

            state->devices = cfg->devices;
            PruneUsbOnlyPlaceholders(state->devices, &state->pendingIds);

            // Seed pending ids from the saved config so hand-edited nicknames keep ids.
            cfg->deviceIds.resize(cfg->devices.size());
            for (size_t i = 0; i < cfg->devices.size(); ++i) {
                if (IsUsbOnlyPlaceholder(cfg->devices[i]))
                    continue;
                if (!cfg->deviceIds[i].empty())
                    state->pendingIds[SplitDevice(cfg->devices[i]).address] = cfg->deviceIds[i];
            }

            SyncDevicesEdit(dlg, state);
            RefreshSavedList(dlg, state);
            SetDlgItemInt(dlg, IDC_PORT, cfg->port, FALSE);
            CheckDlgButton(dlg, IDC_AUTORECONNECT, cfg->autoReconnect ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_USB_WIFI_FAILOVER, cfg->usbWifiFailover ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_AUTOSTART, IsAutostartTaskEnabledImpl() ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dlg, IDC_SEPARATE_USB_WIFI, cfg->separateUsbWifiPicture ? BST_CHECKED : BST_UNCHECKED);

            FillPictureCombo(dlg);
            SelectPictureCombo(dlg, cfg->picturePreset);
            FillEncodeCombos(dlg);
            FillBackendCombo(dlg, cfg->displayBackend);
            SelectEncodeCombos(dlg, *cfg);

            // The controls are the tab's siblings: give the dialog the tab body's
            // background, or they paint a different shade over it.
            EnableThemeDialogTexture(dlg, ETDT_ENABLETAB);
            InitSettingsTabs(dlg);
            LayoutSettingsTabPages(dlg);
            state->settingsTab = -1; // force first ShowSettingsTab to treat as switch-in
            ShowSettingsTab(dlg, state, 0);
            RefreshNearbyList(dlg, state);

            if (IsElevated())
                EnableWindow(GetDlgItem(dlg, IDC_RESTART_ADMIN), FALSE);
            return TRUE;
        }
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<LPNMHDR>(lParam);
            if (hdr && hdr->idFrom == IDC_SETTINGS_TABS && hdr->code == TCN_SELCHANGE) {
                auto* state = reinterpret_cast<SettingsDlgState*>(GetWindowLongPtrW(dlg, GWLP_USERDATA));
                const int tab = TabCtrl_GetCurSel(hdr->hwndFrom);
                ShowSettingsTab(dlg, state, tab);
                return TRUE;
            }
            break;
        }
        case WM_COMMAND: {
            auto* state = reinterpret_cast<SettingsDlgState*>(GetWindowLongPtrW(dlg, GWLP_USERDATA));
            const int id = LOWORD(wParam);
            if (id == IDC_SEPARATE_USB_WIFI) {
                UpdateSeparateEncodeEnable(dlg, state->settingsTab == 1);
                return TRUE;
            }
            if (id == IDC_REFRESH_NEARBY) {
                OnRefreshNearby(dlg, state);
                return TRUE;
            }
            if (id == IDC_ADD_NEARBY) {
                AddSelectedNearby(dlg, state);
                return TRUE;
            }
            if (id == IDC_NEARBY && HIWORD(wParam) == LBN_DBLCLK) {
                AddSelectedNearby(dlg, state);
                return TRUE;
            }
            if (id == IDC_REMOVE_SAVED) {
                RemoveSelectedSaved(dlg, state);
                return TRUE;
            }
            if (id == IDC_RESTART_ADMIN)
                state->restartAdminRequested = true;
            if (id == IDOK || id == IDC_RESTART_ADMIN) {
                Config* cfg = state->cfg;

                // Multiline may be hidden on General; GetDlgItemText still works.
                ReadDevicesEdit(dlg, state);
                PruneUsbOnlyPlaceholders(state->devices, &state->pendingIds);

                // Hello-learned ids stay hello-learned while the address and
                // id are unchanged (also ones learned while this was open).
                std::map<std::string, std::string> learned; // address -> id
                cfg->deviceIds.resize(cfg->devices.size());
                cfg->deviceIdFromHello.resize(cfg->devices.size());
                for (size_t i = 0; i < cfg->devices.size(); ++i)
                    if (cfg->deviceIdFromHello[i] == "1" && !cfg->deviceIds[i].empty())
                        learned[SplitDevice(cfg->devices[i]).address] = cfg->deviceIds[i];

                cfg->devices = state->devices;
                std::vector<std::string> ids;
                std::vector<std::string> fromHello;
                ids.reserve(cfg->devices.size());
                fromHello.reserve(cfg->devices.size());
                for (const std::string& device : cfg->devices) {
                    const std::string address = SplitDevice(device).address;
                    auto it = state->pendingIds.find(address);
                    std::string id = it != state->pendingIds.end() ? it->second : std::string{};
                    auto was = learned.find(address);
                    if (id.empty() && was != learned.end())
                        id = was->second;
                    fromHello.push_back(was != learned.end() && was->second == id ? "1" : "");
                    ids.push_back(std::move(id));
                }
                cfg->deviceIds = std::move(ids);
                cfg->deviceIdFromHello = std::move(fromHello);

                BOOL ok = FALSE;
                UINT port = GetDlgItemInt(dlg, IDC_PORT, &ok, FALSE);
                if (ok && port > 0 && port <= 65535)
                    cfg->port = static_cast<uint16_t>(port);
                cfg->autoReconnect = IsDlgButtonChecked(dlg, IDC_AUTORECONNECT) == BST_CHECKED;
                cfg->usbWifiFailover = IsDlgButtonChecked(dlg, IDC_USB_WIFI_FAILOVER) == BST_CHECKED;
                cfg->displayBackend = ReadBackendCombo(dlg);
                // 开机自启 = 管理员计划任务。切换失败（常因未提权）时弹提示。
                {
                    const bool wantAutostart = IsDlgButtonChecked(dlg, IDC_AUTOSTART) == BST_CHECKED;
                    if (wantAutostart != IsAutostartTaskEnabledImpl()) {
                        wchar_t err[256] = {};
                        if (!SetAutostartTaskImpl(wantAutostart, err, _countof(err))) {
                            const wchar_t* msg = err[0] ? err : L"开机自启设置失败";
                            MessageBoxW(dlg, msg, L"OpenDisplay", MB_OK | MB_ICONWARNING);
                        }
                    }
                }
                cfg->picturePreset = ReadPictureCombo(dlg);
                cfg->separateUsbWifiPicture = IsDlgButtonChecked(dlg, IDC_SEPARATE_USB_WIFI) == BST_CHECKED;
                ReadEncodeCombos(dlg, *cfg);
                if (!cfg->separateUsbWifiPicture)
                    cfg->SyncTransportsFromPicturePreset();
                EndDialog(dlg, IDOK);
                return TRUE;
            }
            if (id == IDCANCEL) {
                EndDialog(dlg, IDCANCEL);
                return TRUE;
            }
            break;
        }
    }
    return FALSE;
}

void ShowContextMenu(HWND hwnd, TrayContext* ctx)
{
    SetEvent(ctx->discoveryWake); // refresh names in the background for next time
    HMENU menu = CreatePopupMenu();

    bool several = ctx->cfg.devices.size() > 1;
    for (size_t i = 0; i < ctx->cfg.devices.size(); ++i) {
        // One toggle per iPad (connect / disconnect just that one); the check
        // mark means it is streaming right now.
        UINT flags = MF_STRING | (ctx->apps[i]->GetState() == SenderApp::State::Streaming ? MF_CHECKED : 0);
        AppendMenuW(menu, flags, IDM_DEVICE_FIRST + static_cast<UINT>(i), DeviceStatusText(ctx, i).c_str());
    }
    if (!ctx->cfg.devices.empty())
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    if (AnyRunning(ctx)) {
        AppendMenuW(menu, MF_STRING, IDM_DISCONNECT, several ? L"全部断开" : L"断开连接");
    } else {
        AppendMenuW(menu, MF_STRING | (ctx->cfg.devices.empty() ? MF_GRAYED : 0), IDM_CONNECT,
                    several ? L"全部连接" : L"连接");
    }
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_SETTINGS, L"设置");
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"退出");

    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

// 设置窗口结果（worker 线程 → 托盘主线程，经 WM_APP_SETTINGS_DONE 传递）。
struct SettingsDone {
    bool ok = false;
    Config oldCfg;
    Config newCfg;
};

void OpenSettingsAndApply(HWND hwnd, TrayContext* ctx)
{
    // Senders write to the config at runtime (USB serial->id bindings); start
    // from the file, not the tray's copy from launch.
    ctx->cfg = Config::Load();
    SetEvent(ctx->discoveryWake);
    const Config oldCfg = ctx->cfg;

    // 附近 iPad 列表 provider：调 mDNS 浏览，返回页面所需的 {address,label} JSON。
    // 在 worker 线程被调用，仅依赖无共享状态的 BrowseReceivers，线程安全。
    auto nearbyProvider = []() -> std::string {
        const std::vector<MdnsReceiver> wifi = BrowseReceivers(800);
        std::string out = "[";
        bool first = true;
        for (const MdnsReceiver& r : wifi) {
            std::string label = ReceiverLabel(r);
            if (!first) out += ",";
            first = false;
            out += "{\"address\":\"" + JsonEsc(r.address) + "\",\"label\":\"" + JsonEsc(label) + "\"}";
        }
        out += "]";
        return out;
    };

    // 状态 provider（worker 线程调用，只读原子状态）。
    auto statusProvider = [ctx]() -> std::string {
        std::string state = "idle", link = "";
        for (const auto& app : ctx->apps) {
            if (app->GetState() == SenderApp::State::Streaming) {
                state = "streaming";
                link = (app->GetTransport() == SenderApp::Transport::Usb) ? "usb" : "wifi";
                break;
            }
            if (app->GetState() == SenderApp::State::Connecting)
                state = "connecting";
            else if (app->GetState() == SenderApp::State::Blocked)
                state = "blocked";
            else if (app->GetState() == SenderApp::State::DriverMissing)
                state = "missing";
        }
        return "{\"state\":\"" + state + "\",\"link\":\"" + link + "\"}";
    };

    // 窗口关闭回调（worker 线程）：把结果 PostMessage 回托盘主线程处理。
    auto onDone = [hwnd, oldCfg](bool ok, Config&& newCfg) {
        auto* done = new SettingsDone();
        done->ok = ok;
        done->oldCfg = std::move(oldCfg);
        done->newCfg = std::move(newCfg);
        PostMessageW(hwnd, WM_APP_SETTINGS_DONE, 0, reinterpret_cast<LPARAM>(done));
    };

    SettingsWindow::ShowAsync(ctx->hInstance, ctx->cfg, std::move(nearbyProvider),
                              std::move(statusProvider), std::move(onDone));
}

// 设置窗口关闭后，在托盘主线程应用结果。
static void ApplySettingsDone(TrayContext* ctx, SettingsDone* done)
{
    std::unique_ptr<SettingsDone> guard(done);
    if (!done->ok)
        return;

    Config& newCfg = done->newCfg;

    // Bindings learned while the dialog was open: keep the file's, the dialog
    // never edits them.
    {
        const Config onDisk = Config::Load();
        newCfg.usbLinkSerials = onDisk.usbLinkSerials;
        newCfg.usbLinkIds = onDisk.usbLinkIds;
    }

    ctx->cfg = std::move(newCfg);
    ctx->cfg.Save();

    const Config& old = done->oldCfg;
    const bool targetsChanged =
        ctx->cfg.devices != old.devices || ctx->cfg.deviceIds != old.deviceIds || ctx->cfg.port != old.port;
    const bool encodeChanged =
        ctx->cfg.usbEncodeMode != old.usbEncodeMode || ctx->cfg.usbEncodePreset != old.usbEncodePreset ||
        ctx->cfg.wifiEncodeMode != old.wifiEncodeMode || ctx->cfg.wifiEncodePreset != old.wifiEncodePreset ||
        ctx->cfg.picturePreset != old.picturePreset || ctx->cfg.separateUsbWifiPicture != old.separateUsbWifiPicture;
    const bool backendChanged = ctx->cfg.displayBackend != old.displayBackend;
    if (targetsChanged || encodeChanged || backendChanged)
        ApplyAndRestart(ctx);
}

// Input desktop switched (lock screen, UAC secure desktop, back to Default):
// wake paused senders so they re-check at once.
void CALLBACK OnDesktopSwitch(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD)
{
    PauseSignals::Notify();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    auto* ctx = reinterpret_cast<TrayContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    // Explorer recreated the taskbar (slow login, or shell restart): the tray
    // icon registration is gone. Re-add it, then refresh so icon and tooltip
    // track current state.
    if (g_taskbarCreatedMsg && msg == g_taskbarCreatedMsg && ctx) {
        AddTrayIcon(ctx);
        UpdateStatus(ctx);
        return 0;
    }

    switch (msg) {
        case WM_APP_NOTIFY: {
            std::unique_ptr<std::wstring> text(reinterpret_cast<std::wstring*>(lParam));
            if (text && ctx) {
                NOTIFYICONDATAW info = ctx->nid;
                info.uFlags = NIF_INFO;
                info.dwInfoFlags = NIIF_INFO;
                wcsncpy_s(info.szInfoTitle, L"OpenDisplay", _TRUNCATE);
                wcsncpy_s(info.szInfo, text->c_str(), _TRUNCATE);
                Shell_NotifyIconW(NIM_MODIFY, &info);
            }
            return 0;
        }

        case WM_APP_TRAY:
            if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU)
                ShowContextMenu(hwnd, ctx);
            else if (LOWORD(lParam) == WM_LBUTTONDBLCLK)
                OpenSettingsAndApply(hwnd, ctx);
            return 0;

        case WM_COMMAND:
            if (LOWORD(wParam) >= IDM_DEVICE_FIRST && LOWORD(wParam) < IDM_DEVICE_FIRST + ctx->apps.size()) {
                size_t index = LOWORD(wParam) - IDM_DEVICE_FIRST;
                SenderApp& app = *ctx->apps[index];
                if (app.IsRunning())
                    app.Stop();
                else
                    app.Start(ResolveDeviceAddress(ctx, index), ctx->cfg.port, EntryId(ctx, index), ctx->cfg.IdFollowsHello(index));
                UpdateStatus(ctx);
                return 0;
            }

            switch (LOWORD(wParam)) {
                case IDM_CONNECT:
                    for (size_t i = 0; i < ctx->apps.size(); ++i)
                        ctx->apps[i]->Start(ResolveDeviceAddress(ctx, i), ctx->cfg.port, EntryId(ctx, i), ctx->cfg.IdFollowsHello(i));
                    UpdateStatus(ctx);
                    return 0;
                case IDM_DISCONNECT:
                    for (auto& app : ctx->apps)
                        app->Stop();
                    UpdateStatus(ctx);
                    return 0;
                case IDM_SETTINGS:
                    OpenSettingsAndApply(hwnd, ctx);
                    return 0;
                case IDM_EXIT:
                    DestroyWindow(hwnd);
                    return 0;
            }
            return 0;

        case WM_APP_STATUS:
            SenderApp::StatusMessageHandled(); // before reading: a change during UpdateStatus posts again
            if (ctx) {
                SaveLearnedIds(ctx);
                UpdateStatus(ctx);
            }
            return 0;

        case WM_APP_SETTINGS_DONE:
            if (ctx && lParam) {
                ApplySettingsDone(ctx, reinterpret_cast<SettingsDone*>(lParam));
                UpdateStatus(ctx);
            }
            return 0;

        case WM_TIMER:
            if (wParam == kDisplayChangeTimerId) {
                KillTimer(hwnd, kDisplayChangeTimerId);
                // User layout change while the MTT head is attached: persist it
                // instead of reverting. No-op if MTT is inactive/absent or the
                // change was one of our own applies. Inside the settle window
                // after one of our applies, try once more when it has passed.
                if (const uint32_t deferMs = MttTopologySaveDeferMs())
                    SetTimer(hwnd, kDisplayChangeTimerId, deferMs, nullptr);
                else
                    (void)SaveMttDisplayTopologyIfUserChange("display");
            }
            return 0;

        case WM_WTSSESSION_CHANGE:
            if (wParam == WTS_SESSION_LOCK || wParam == WTS_SESSION_UNLOCK) {
                PauseSignals::locked = wParam == WTS_SESSION_LOCK;
                Logf("pause", "session %s\n", wParam == WTS_SESSION_LOCK ? "locked" : "unlocked");
                PauseSignals::Notify();
            }
            return 0;

        case WM_POWERBROADCAST:
            if (wParam == PBT_POWERSETTINGCHANGE && lParam) {
                const auto* ps = reinterpret_cast<const POWERBROADCAST_SETTING*>(lParam);
                if (IsEqualGUID(ps->PowerSetting, kConsoleDisplayState) && ps->DataLength >= sizeof(DWORD)) {
                    // 0 off, 1 on, 2 dimmed (still visible: not a pause).
                    const DWORD state = *reinterpret_cast<const DWORD*>(ps->Data);
                    const bool off = state == 0;
                    if (PauseSignals::displayOff.exchange(off) != off) {
                        Logf("pause", "console display %s\n", off ? "off" : "on");
                        PauseSignals::Notify();
                    }
                }
            }
            return TRUE;

        case WM_DISPLAYCHANGE:
            ++PauseSignals::displayChangeGen; // senders re-read the refresh rate
            SetTimer(hwnd, kDisplayChangeTimerId, kDisplayChangeDebounceMs, nullptr);
            return 0;

        case WM_QUERYENDSESSION:
            return TRUE;

        case WM_ENDSESSION:
            if (wParam) {
                // Logoff / shutdown / restart: same teardown as Quit, so the
                // MTT head does not survive into the next session.
                Logf("mtt", "session ending - tearing down\n");
                BeginMttShutdown();
                KillTimer(hwnd, kDisplayChangeTimerId);
                for (auto& app : ctx->apps)
                    app->Stop();
                (void)ShutdownMttVddForExit("exit");
            }
            return 0;

        case WM_DESTROY:
            SenderApp::SetStatusWindow(nullptr, 0);
            WTSUnRegisterSessionNotification(hwnd);
            if (ctx->desktopSwitchHook) {
                UnhookWinEvent(ctx->desktopSwitchHook);
                ctx->desktopSwitchHook = nullptr;
            }
            if (ctx->displayStateNotify) {
                UnregisterPowerSettingNotification(ctx->displayStateNotify);
                ctx->displayStateNotify = nullptr;
            }
            SetMttUserNotifier(nullptr);
            KillTimer(hwnd, kDisplayChangeTimerId);
            Shell_NotifyIconW(NIM_DELETE, &ctx->nid);
            // Quit: block re-attach from any racing pipeline rebuild, stop the
            // senders (a streaming one tears down as user-initiated), then tear
            // down unconditionally - covers idle/connecting senders and a
            // pending link-loss grace, which previously left the head behind.
            BeginMttShutdown();
            for (auto& app : ctx->apps)
                app->Stop();
            (void)ShutdownMttVddForExit("exit");
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

} // namespace

bool SetAutostartTask(bool enable, wchar_t* errBuf, size_t errLen)
{
    return SetAutostartTaskImpl(enable, errBuf, errLen);
}

bool IsAutostartTaskEnabled()
{
    return IsAutostartTaskEnabledImpl();
}

int RunTray(HINSTANCE hInstance)
{
    // One tray per session. A second tray would adopt / tear down the MTT head
    // the first one is streaming to. Wait briefly so a self-relaunch
    // (RelaunchElevated) can take over once the old instance has exited.
    HANDLE trayMutex = CreateMutexW(nullptr, FALSE, L"Local\\opendisplay-win-tray");
    if (trayMutex == nullptr) {
        Logf("tray", "another opendisplay-win tray is running (mutex unavailable) - exiting\n");
        return 0;
    }
    const DWORD w = WaitForSingleObject(trayMutex, 10000);
    if (w != WAIT_OBJECT_0 && w != WAIT_ABANDONED) {
        Logf("tray", "another opendisplay-win tray is running - exiting\n");
        CloseHandle(trayMutex);
        return 0;
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_TAB_CLASSES};
    InitCommonControlsEx(&icc);

    TrayContext ctx;
    ctx.hInstance = hInstance;
    ctx.cfg = Config::Load();
    RebuildSenders(&ctx);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kWndClass;
    RegisterClassExW(&wc);

    // Hidden top-level window (not HWND_MESSAGE): message-only windows never
    // receive broadcasts, and we need WM_ENDSESSION (teardown on logoff /
    // shutdown) and WM_DISPLAYCHANGE (save user layout changes).
    HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kWndClass, L"opendisplay-win", WS_POPUP, 0, 0, 0, 0, nullptr,
                                nullptr, hInstance, nullptr);
    if (hwnd == nullptr)
        return 1;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));
    // Pause mode inputs: lock/unlock and console display on/off.
    if (!WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION))
        Logf("pause", "WTSRegisterSessionNotification failed (%lu)\n", GetLastError());
    ctx.displayStateNotify =
        RegisterPowerSettingNotification(hwnd, &kConsoleDisplayState, DEVICE_NOTIFY_WINDOW_HANDLE);
    if (!ctx.displayStateNotify)
        Logf("pause", "RegisterPowerSettingNotification failed (%lu)\n", GetLastError());
    // Out-of-context: delivered through this thread's message loop.
    ctx.desktopSwitchHook = SetWinEventHook(EVENT_SYSTEM_DESKTOPSWITCH, EVENT_SYSTEM_DESKTOPSWITCH, nullptr,
                                            OnDesktopSwitch, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!ctx.desktopSwitchHook)
        Logf("pause", "SetWinEventHook(DESKTOPSWITCH) failed (%lu)\n", GetLastError());

    ctx.iconGreen = MakeStatusIcon(RGB(40, 200, 80));
    ctx.iconRed = MakeStatusIcon(RGB(225, 65, 55));
    ctx.iconGrey = MakeStatusIcon(RGB(150, 150, 150));

    ctx.nid.cbSize = sizeof(ctx.nid);
    ctx.nid.hWnd = hwnd;
    ctx.nid.uID = 1;
    ctx.nid.uCallbackMessage = WM_APP_TRAY;
    ctx.nid.hIcon = PickIcon(&ctx);
    wcsncpy_s(ctx.nid.szTip, L"OpenDisplay", _TRUNCATE);
    AddTrayIcon(&ctx);

    // Re-add the icon if Explorer recreates the taskbar after login (the app
    // may start before the shell is ready; NIM_ADD then fails silently).
    g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");

    SenderApp::SetStatusWindow(hwnd, WM_APP_STATUS);
    PostMessageW(hwnd, WM_APP_STATUS, 0, 0); // first paint

    ctx.hwnd = hwnd;
    ctx.discoveryRunning = true;
    ctx.discovery = std::thread([&ctx] { RunDiscovery(&ctx); });

    // One-time notice right before an EDID-change UAC prompt (sender thread ->
    // tray thread via PostMessage).
    SetMttUserNotifier([hwnd](const std::wstring& text) {
        auto* copy = new std::wstring(text);
        if (!PostMessageW(hwnd, WM_APP_NOTIFY, 0, reinterpret_cast<LPARAM>(copy)))
            delete copy;
    });

    AdoptStaleMttHeadAtStartup("mtt", kStaleMttHeadGraceMs);

    if (ctx.cfg.autoReconnect)
        for (size_t i = 0; i < ctx.apps.size(); ++i)
            ctx.apps[i]->Start(ResolveDeviceAddress(&ctx, i), ctx.cfg.port, EntryId(&ctx, i), ctx.cfg.IdFollowsHello(i));
    UpdateStatus(&ctx);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ctx.discoveryRunning = false;
    SetEvent(ctx.discoveryStop);
    if (ctx.discovery.joinable())
        ctx.discovery.join();

    // Normal process exit safety net (idempotent after WM_DESTROY).
    for (auto& app : ctx.apps)
        app->Stop();
    (void)ShutdownMttVddForExit("exit");

    DestroyIcon(ctx.iconGreen);
    DestroyIcon(ctx.iconRed);
    DestroyIcon(ctx.iconGrey);
    return 0;
}

} // namespace od
