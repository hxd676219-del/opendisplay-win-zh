@echo off
chcp 65001 >nul
setlocal
title 关闭开机自启 - OpenDisplay
net session >nul 2>&1
if errorlevel 1 (
    echo 请右键“以管理员身份运行”本脚本。
    pause
    exit /b 1
)
schtasks /Delete /F /TN "OpenDisplay iPad副屏（开机自启）"
if %errorlevel%==0 (
    echo 开机自启已关闭。
) else (
    echo 未找到该自启任务，可能本来就没开启。
)
pause
