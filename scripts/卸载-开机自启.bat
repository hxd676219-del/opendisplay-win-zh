@echo off
chcp 65001 >nul
setlocal EnableExtensions

rem ============================================================
rem  OpenDisplay iPad 副屏 - 卸载开机自启（需管理员权限运行）
rem
rem  删除「开机自启」计划任务，不影响程序本身与已安装的驱动。
rem ============================================================

net session >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] 需要管理员权限。
    echo 请右键本脚本，选择「以管理员身份运行」后再试。
    pause
    exit /b 1
)

set "TASK_NAME=OpenDisplay iPad副屏（开机自启）"

echo 正在删除计划任务：%TASK_NAME%
schtasks /Delete /TN "%TASK_NAME%" /F
if %errorlevel% equ 0 (
    echo [完成] 开机自启已卸载。
) else (
    echo [提示] 未找到该计划任务（可能本就没有启用自启），或删除失败，错误码 %errorlevel%。
)
pause
