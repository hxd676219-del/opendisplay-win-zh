#pragma once

#include <windows.h>

namespace od {

// Runs the tray-icon GUI: a hidden window hosting a SenderApp, a notification
// icon with a Connect/Disconnect/Settings/Exit menu, and a settings dialog for
// IP/port/auto-connect. Blocks on the message loop until the user exits.
// Returns the process exit code.
int RunTray(HINSTANCE hInstance);

// 开机自启=(管理员)计划任务。enable=true 创建（登录时触发+最高权限+3秒延迟），
// false 删除。查询当前是否已启用。errLen>0 时失败信息写入 errBuf（宽字符）。
// 需要管理员权限，否则返回 ACCESS_DENIED。
bool SetAutostartTask(bool enable, wchar_t* errBuf, size_t errLen);
bool IsAutostartTaskEnabled();

} // namespace od
