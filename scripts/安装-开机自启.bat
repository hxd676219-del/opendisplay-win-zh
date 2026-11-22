@echo off
chcp 65001 >nul
setlocal
title 安装开机自启 - OpenDisplay
net session >nul 2>&1
if errorlevel 1 (
    echo 请右键“以管理员身份运行”本脚本。
    pause
    exit /b 1
)
set "EXE=%~dp0..\release\opendisplay-win-zh.exe"
if not exist "%EXE%" (
    echo 找不到程序：%EXE%
    pause
    exit /b 1
)
schtasks /Create /F /TN "OpenDisplay iPad副屏（开机自启）" /TR "\"%EXE%\"" /SC ONLOGON /RL HIGHEST
if %errorlevel%==0 (
    echo 开机自启已开启：登录时将以管理员权限自动启动 OpenDisplay。
) else (
    echo 创建失败，请检查是否有同名任务或权限问题。
)
pause
