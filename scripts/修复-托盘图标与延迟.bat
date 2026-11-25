@echo off
chcp 65001 >nul
title 修复 OpenDisplay 托盘图标
net session >nul 2>&1
if errorlevel 1 (
    echo 请右键“以管理员身份运行”本脚本。
    pause
    exit /b 1
)

rem 1) 登录后延迟 3 秒再启动，等任务栏托盘就绪，避免开机图标注册失败
powershell -NoProfile -Command "$t=Get-ScheduledTask -TaskName 'OpenDisplay iPad副屏（开机自启）'; $t.Triggers[0].Delay='PT3S'; Set-ScheduledTask -TaskName $t.TaskName -Trigger $t.Triggers"

rem 2) 结束当前实例（托盘图标未注册的那个）
taskkill /IM opendisplay-win-zh.exe /F
timeout /t 3 /nobreak >nul

rem 3) 重新通过计划任务以管理员权限启动
schtasks /Run /TN "OpenDisplay iPad副屏（开机自启）"

echo.
echo 已重启程序，请查看任务栏 ^ 折叠区里的 OpenDisplay 图标。
pause
