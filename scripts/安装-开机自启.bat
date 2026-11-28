@echo off
chcp 65001 >nul
setlocal EnableExtensions

rem ============================================================
rem  OpenDisplay iPad 副屏 - 安装开机自启（需管理员权限运行）
rem
rem  本脚本创建「管理员权限」计划任务：登录时自动启动副屏程序，
rem  并延迟 3 秒（等任务栏就绪，避免托盘图标注册失败）。
rem  若尚未运行，请右键本脚本 →「以管理员身份运行」。
rem ============================================================

rem 定位本脚本所在目录（即程序安装目录）
set "SCRIPT_DIR=%~dp0"
rem 程序文件路径（exe 在 scripts 的上一级 release 目录；若位置不同请改这里）
set "APP_EXE=%SCRIPT_DIR%..\release\opendisplay-win-zh.exe"
rem 将相对路径解析为绝对路径（去掉中间的 ".."），供计划任务可靠引用
for %%I in ("%APP_EXE%") do set "APP_EXE=%%~fI"

if not exist "%APP_EXE%" (
    echo [错误] 未找到程序文件：
    echo        %APP_EXE%
    echo 请确认 exe 与本脚本的相对位置正确，或自行修改脚本中的 APP_EXE。
    pause
    exit /b 1
)

rem 校验是否以管理员权限运行
net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 需要管理员权限。
    echo 请右键本脚本，选择「以管理员身份运行」后再试。
    pause
    exit /b 1
)

set "TASK_NAME=OpenDisplay iPad副屏（开机自启）"

echo [1/2] 正在创建计划任务：%TASK_NAME%
schtasks /Create /TN "%TASK_NAME%" /TR "\"%APP_EXE%\"" /SC ONLOGON /RL HIGHEST /DELAY 0000:03 /F
if %errorlevel% neq 0 (
    echo [错误] 创建计划任务失败，错误码 %errorlevel%。
    pause
    exit /b 1
)

echo [2/2] 完成。开机自启已启用 ① 登录时自动启动 ② 最高权限 ③ 延迟 3 秒。
echo.
echo 提示：请不要在程序「设置」里同时勾选“开机自动启动”，二者会重复启动。
pause
