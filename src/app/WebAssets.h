// 自动生成：由 tools/gen_webassets.ps1 从 src/app/web/* 生成，勿手改。
// 将本地 HTML/CSS/JS 以原始字符串字面量内嵌进可执行文件，保持单文件交付，
// 避免使用 .rc / 资源编译器（本项目明确禁止 .rc）。
#pragma once

#include <string>

namespace od {

inline const char* kSettingsHtml = R"odhtml(<!DOCTYPE html>
<html lang="zh-CN">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1.0" />
  <title>OpenDisplay 设置</title>
  <link rel="stylesheet" href="settings.css" />
</head>
<body>
  <!-- 顶部标题栏（无边框窗口的自定义标题栏，含窗口控制按钮） -->
  <header class="titlebar" id="titlebar">
    <div class="titlebar-left">
      <span class="app-logo"></span>
      <h1>OpenDisplay 设置</h1>
    </div>
    <div class="titlebar-right">
      <span id="status-dot" class="status-dot status-idle"></span>
      <span id="status-text" class="status-text">未连接</span>
      <div class="window-controls">
        <button id="win-min" class="win-btn" title="最小化" aria-label="最小化">&#x2013;</button>
        <button id="win-max" class="win-btn" title="最大化/还原" aria-label="最大化/还原">&#x25A1;</button>
        <button id="win-close" class="win-btn win-btn-close" title="关闭" aria-label="关闭">&#x2715;</button>
      </div>
    </div>
  </header>

  <!-- 主体：左栏 + 右主区 -->
  <div class="layout">
    <!-- 左侧栏 -->
    <aside class="sidebar">
      <!-- 附近的 iPad -->
      <section class="side-group">
        <h2 class="side-title">附近的 iPad</h2>
        <ul id="nearby-list" class="device-list"></ul>
        <div class="side-actions">
          <button id="refresh-nearby" class="btn">刷新</button>
          <button id="add-nearby" class="btn btn-primary">添加 →</button>
        </div>
        <div id="nearby-hint" class="side-hint"></div>
      </section>

      <!-- 我的 iPad -->
      <section class="side-group">
        <h2 class="side-title">我的 iPad</h2>
        <ul id="saved-list" class="device-list"></ul>
        <div class="side-actions">
          <button id="remove-saved" class="btn">移除</button>
          <button id="move-back" class="btn btn-primary">← 移除</button>
        </div>
      </section>
    </aside>

    <!-- 右侧主区：滚动卡片分组 -->
    <main class="content">
      <!-- 外观 -->
      <section class="card">
        <h2 class="card-title">外观</h2>
        <div class="field">
          <label class="field-label" for="theme-select">界面主题</label>
          <select id="theme-select" class="select">
            <option value="system">跟随 Windows 设置</option>
            <option value="light">浅色</option>
            <option value="dark">深色</option>
          </select>
        </div>
      </section>

      <!-- 连接 -->
      <section class="card">
        <h2 class="card-title">连接</h2>
        <label class="switch-row">
          <span class="switch-label">自动连接</span>
          <input type="checkbox" id="auto-reconnect" class="switch" />
          <span class="switch-slider"></span>
        </label>
        <label class="switch-row">
          <span class="switch-label">Wi-Fi 断线自动续连</span>
          <input type="checkbox" id="usb-wifi-failover" class="switch" />
          <span class="switch-slider"></span>
        </label>
        <div class="switch-divider"></div>
        <label class="switch-row">
          <span class="switch-label">开机自启（管理员计划任务）</span>
          <input type="checkbox" id="autostart" class="switch" />
          <span class="switch-slider"></span>
        </label>
        <div class="field-hint" id="autostart-hint">需要管理员权限，切换时可能弹出 UAC 授权</div>
      </section>

      <!-- 画质 -->
      <section class="card">
        <h2 class="card-title">画质</h2>
        <div class="field">
          <label class="field-label" for="picture-preset">画质预设</label>
          <select id="picture-preset" class="select">
            <option value="speed">更快</option>
            <option value="balanced">均衡</option>
            <option value="quality">更清晰</option>
          </select>
        </div>
      </section>

      <!-- 高级 -->
      <section class="card">
        <h2 class="card-title">高级</h2>

        <div class="field">
          <label class="field-label" for="manual-devices">手动添加地址（每行一个，可带昵称）</label>
          <textarea id="manual-devices" class="textarea" rows="3"
            placeholder="例：192.168.1.42 客厅 iPad"></textarea>
        </div>

        <div class="field-row">
          <div class="field">
            <label class="field-label" for="port">端口</label>
            <input type="number" id="port" class="input" min="1" max="65535" value="9000" />
          </div>
          <div class="field">
            <label class="field-label" for="display-backend">显示驱动</label>
            <select id="display-backend" class="select">
              <option value="auto">自动</option>
              <option value="mtt">MTT（1 台 iPad）</option>
              <option value="parsec">Parsec（多台 iPad）</option>
            </select>
          </div>
        </div>

        <label class="switch-row">
          <span class="switch-label">USB 与 Wi-Fi 分别设置编码</span>
          <input type="checkbox" id="separate-usb-wifi" class="switch" />
          <span class="switch-slider"></span>
        </label>

        <div id="encode-panel" class="encode-panel">
          <div class="encode-block">
            <h3 class="encode-title">USB</h3>
            <div class="field-row">
              <div class="field">
                <label class="field-label" for="usb-mode">模式</label>
                <select id="usb-mode" class="select">
                  <option value="dynamic">动态</option>
                  <option value="fixed">固定</option>
                </select>
              </div>
              <div class="field">
                <label class="field-label" for="usb-preset">预设</label>
                <select id="usb-preset" class="select">
                  <option value="speed">快速</option>
                  <option value="balanced">均衡</option>
                  <option value="quality">高画质</option>
                </select>
              </div>
            </div>
          </div>
          <div class="encode-block">
            <h3 class="encode-title">Wi-Fi</h3>
            <div class="field-row">
              <div class="field">
                <label class="field-label" for="wifi-mode">模式</label>
                <select id="wifi-mode" class="select">
                  <option value="dynamic">动态</option>
                  <option value="fixed">固定</option>
                </select>
              </div>
              <div class="field">
                <label class="field-label" for="wifi-preset">预设</label>
                <select id="wifi-preset" class="select">
                  <option value="speed">快速</option>
                  <option value="balanced">均衡</option>
                  <option value="quality">高画质</option>
                </select>
              </div>
            </div>
          </div>
        </div>
      </section>
    </main>
  </div>

  <!-- 底部按钮 -->
  <footer class="footer">
    <button id="cancel" class="btn">取消</button>
    <button id="ok" class="btn btn-primary">确定</button>
  </footer>

  <script src="settings.js"></script>
</body>
</html>
)odhtml";

inline const char* kSettingsCss = R"odcss(/* OpenDisplay 设置 · 本地样式
   深色/浅色/跟随系统（由 JS 在 <html data-theme="..."> 上控制），微软雅黑优先。 */

:root {
  --font: "Microsoft YaHei UI", "Microsoft YaHei", "微软雅黑", "Segoe UI", sans-serif;
  /* 浅色（默认） */
  --bg: #ffffff;
  --panel: #f7f7f7;
  --card: #ffffff;
  --card-border: #e0e0e0;
  --text: #1f1f1f;
  --text-dim: #6a6a6a;
  --accent: #0067b8;
  --accent-text: #ffffff;
  --dot-idle: #9a9a9a;
  --dot-on: #28c850;
  --dot-warn: #e14137;
  --input-bg: #ffffff;
  --input-border: #d0d0d0;
  --hover: #f0f0f0;
  --list-selected: #e6f2fb;
}

/* 深色主题：由 JS 设置 data-theme="dark"（显式深色或跟随系统且系统为深色时） */
:root[data-theme="dark"] {
  --bg: #1f1f1f;
  --panel: #242424;
  --card: #2a2a2a;
  --card-border: #3a3a3a;
  --text: #e8e8e8;
  --text-dim: #a8a8a8;
  --accent: #4cc2ff;
  --accent-text: #102a3a;
  --dot-idle: #7a7a7a;
  --dot-on: #2fd080;
  --dot-warn: #ff6b60;
  --input-bg: #1c1c1c;
  --input-border: #4a4a4a;
  --hover: #313131;
  --list-selected: #234050;
}

* { margin: 0; padding: 0; box-sizing: border-box; }

html, body {
  height: 100%;
  font-family: var(--font);
  background: var(--bg);
  color: var(--text);
  font-size: 14px;
  overflow: hidden;
}

body {
  display: flex;
  flex-direction: column;
}

/* ---- 顶部标题栏 ---- */
.titlebar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  padding: 12px 20px;
  background: var(--panel);
  border-bottom: 1px solid var(--card-border);
  flex: 0 0 auto;
}
.titlebar-left { display: flex; align-items: center; gap: 10px; }
.app-logo {
  width: 18px; height: 18px; border-radius: 4px;
  background: linear-gradient(135deg, #4cc2ff 0%, #0067b8 100%);
}
.titlebar h1 { font-size: 16px; font-weight: 600; }
.titlebar-right { display: flex; align-items: center; gap: 8px; }
.status-dot { width: 10px; height: 10px; border-radius: 50%; display: inline-block; }
.status-dot.status-idle  { background: var(--dot-idle); }
.status-dot.status-on    { background: var(--dot-on); }
.status-dot.status-warn  { background: var(--dot-warn); }
.status-text { font-size: 13px; color: var(--text-dim); }

/* ---- 自定义标题栏：可拖动 + 窗口控制按钮 ---- */
.titlebar {
  -webkit-user-select: none;
  user-select: none;
  cursor: default;
}
.window-controls { display: flex; margin-left: 12px; height: 100%; }
.win-btn {
  width: 40px; height: 32px;
  border: none; background: transparent;
  color: var(--text); font-size: 13px; line-height: 1;
  cursor: pointer; display: flex; align-items: center; justify-content: center;
  border-radius: 4px;
}
.win-btn:hover { background: var(--hover); }
.win-btn-close:hover { background: #e81123; color: #ffffff; }

/* ---- 主体布局 ---- */
.layout {
  flex: 1 1 auto;
  display: flex;
  min-height: 0;
}

/* ---- 左侧栏 ---- */
.sidebar {
  width: 260px;
  flex: 0 0 auto;
  background: var(--panel);
  border-right: 1px solid var(--card-border);
  padding: 14px;
  display: flex;
  flex-direction: column;
  gap: 18px;
  overflow-y: auto;
}
.side-group { display: flex; flex-direction: column; gap: 8px; }
.side-title { font-size: 13px; font-weight: 600; color: var(--text-dim); }
.device-list {
  list-style: none;
  border: 1px solid var(--input-border);
  border-radius: 6px;
  background: var(--input-bg);
  min-height: 64px;
  max-height: 140px;
  overflow-y: auto;
}
.device-list li {
  padding: 7px 10px;
  cursor: pointer;
  font-size: 13px;
  border-bottom: 1px solid var(--card-border);
  user-select: none;
}
.device-list li:last-child { border-bottom: none; }
.device-list li:hover { background: var(--hover); }
.device-list li.selected { background: var(--list-selected); }
.device-list li .addr { color: var(--text-dim); font-size: 12px; margin-left: 6px; }
.device-list li.empty {
  padding: 10px; color: var(--text-dim); font-style: italic; cursor: default;
}
.side-actions { display: flex; gap: 8px; justify-content: flex-end; }
.side-hint { font-size: 12px; color: var(--text-dim); min-height: 16px; }

/* ---- 右侧主区 ---- */
.content {
  flex: 1 1 auto;
  overflow-y: auto;
  padding: 16px 20px;
  display: flex;
  flex-direction: column;
  gap: 14px;
}
.card {
  background: var(--card);
  border: 1px solid var(--card-border);
  border-radius: 8px;
  padding: 16px;
}
.card-title { font-size: 14px; font-weight: 600; margin-bottom: 12px; }

/* ---- 开关 ---- */
.switch-row {
  display: flex; align-items: center; gap: 12px;
  padding: 8px 0; cursor: pointer;
}
.switch { position: absolute; opacity: 0; pointer-events: none; }
.switch-slider {
  margin-left: auto;
  width: 40px; height: 22px; border-radius: 11px;
  background: var(--input-border);
  position: relative; transition: background 0.15s;
  flex: 0 0 auto;
}
.switch-slider::after {
  content: ''; position: absolute; top: 3px; left: 3px;
  width: 16px; height: 16px; border-radius: 50%;
  background: #fff; transition: left 0.15s;
}
.switch:checked + .switch-slider { background: var(--accent); }
.switch:checked + .switch-slider::after { left: 21px; }
.switch-divider { height: 1px; background: var(--card-border); margin: 6px 0; }
.field-hint { font-size: 12px; color: var(--text-dim); padding: 2px 0 0 0; }

/* ---- 表单字段 ---- */
.field { display: flex; flex-direction: column; gap: 6px; }
.field-row { display: flex; gap: 16px; margin-top: 10px; }
.field-row > .field { flex: 1; }
.field-label { font-size: 13px; color: var(--text-dim); }
.input, .select, .textarea {
  font-family: var(--font);
  font-size: 14px;
  color: var(--text);
  background: var(--input-bg);
  border: 1px solid var(--input-border);
  border-radius: 6px;
  padding: 7px 10px;
  outline: none;
}
.input:focus, .select:focus, .textarea:focus { border-color: var(--accent); }
.textarea { resize: vertical; }
.select { cursor: pointer; }

/* ---- 编码面板 ---- */
.encode-panel { margin-top: 10px; display: flex; flex-direction: column; gap: 12px; }
.encode-panel.hidden { display: none; }
.encode-block { border-left: 3px solid var(--accent); padding-left: 12px; }
.encode-title { font-size: 13px; font-weight: 600; margin-bottom: 4px; }
.encode-block .field-row { margin-top: 4px; }

/* ---- 底部按钮 ---- */
.footer {
  flex: 0 0 auto;
  display: flex; justify-content: flex-end; gap: 10px;
  padding: 12px 20px;
  background: var(--panel);
  border-top: 1px solid var(--card-border);
}

/* ---- 按钮 ---- */
.btn {
  font-family: var(--font);
  font-size: 14px;
  padding: 7px 18px;
  border-radius: 6px;
  border: 1px solid var(--input-border);
  background: var(--input-bg);
  color: var(--text);
  cursor: pointer;
}
.btn:hover { background: var(--hover); }
.btn-primary { background: var(--accent); color: var(--accent-text); border-color: var(--accent); }
.btn-primary:hover { filter: brightness(1.1); }
.btn:disabled { opacity: 0.5; cursor: default; }
)odcss";

inline const char* kSettingsJs = R"odjs(// OpenDisplay 设置 · 前端桥接
// 通过 window.chrome.webview 与 C++ 双向通信：
//   接收：C++ addScriptToExecuteOnDocumentCreated 注入 window.__odConfig 初始化配置，
//        并通过 postMessage 推送「状态更新」与「附近列表刷新」。
//   发送：window.chrome.webview.postMessage({type:...}) 把用户操作回传 C++。

(function () {
  'use strict';

  const webview = window.chrome && window.chrome.webview;

  // ---------- 由 C++ 注入的初始配置与状态 ----------
  // 形如 { config: {...}, devices: [...], nearby: [...], status: {...} }
  const boot = window.__odBoot || { config: {}, devices: [], nearby: [], status: {} };

  // ---------- 元素引用 ----------
  const $ = (id) => document.getElementById(id);
  const el = {
    statusDot: $('status-dot'),
    statusText: $('status-text'),
    nearbyList: $('nearby-list'),
    savedList: $('saved-list'),
    nearbyHint: $('nearby-hint'),
    refreshNearby: $('refresh-nearby'),
    addNearby: $('add-nearby'),
    removeSaved: $('remove-saved'),
    moveBack: $('move-back'),
    autoReconnect: $('auto-reconnect'),
    usbWifiFailover: $('usb-wifi-failover'),
    autostart: $('autostart'),
    autostartHint: $('autostart-hint'),
    themeSelect: $('theme-select'),
    picturePreset: $('picture-preset'),
    manualDevices: $('manual-devices'),
    port: $('port'),
    displayBackend: $('display-backend'),
    separateUsbWifi: $('separate-usb-wifi'),
    encodePanel: $('encode-panel'),
    usbMode: $('usb-mode'),
    usbPreset: $('usb-preset'),
    wifiMode: $('wifi-mode'),
    wifiPreset: $('wifi-preset'),
    ok: $('ok'),
    cancel: $('cancel'),
    titlebar: $('titlebar'),
    winMin: $('win-min'),
    winMax: $('win-max'),
    winClose: $('win-close'),
  };

  // ---------- 状态存储（设备列表的选中项）----------
  let selectedNearby = -1;
  let selectedSaved = -1;

  // ---------- 辅助：向 C++ 发送消息 ----------
  function send(msg) {
    if (webview) webview.postMessage(msg);
  }

  // ---------- 主题切换 ----------
  // theme ∈ "light" | "dark" | "system"。跟随系统时用 matchMedia 判断，
  // 并监听系统深浅色变化实时更新。
  const darkMedia = window.matchMedia('(prefers-color-scheme: dark)');
  function applyTheme(theme) {
    let dark = (theme === 'dark');
    if (theme === 'system')
      dark = darkMedia.matches;
    document.documentElement.setAttribute('data-theme', dark ? 'dark' : 'light');
  }

  // ---------- 文本转义 ----------
  function esc(s) {
    return String(s).replace(/[&<>"']/g, (c) => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
    }[c]));
  }

  // ---------- 渲染附近列表 ----------
  function renderNearby(list) {
    const ul = el.nearbyList;
    ul.innerHTML = '';
    if (!list || list.length === 0) {
      ul.innerHTML = '<li class="empty">当前 Wi-Fi 未发现设备</li>';
      return;
    }
    list.forEach((d, i) => {
      const li = document.createElement('li');
      if (i === selectedNearby) li.classList.add('selected');
      li.innerHTML = '<span>' + esc(d.label || d.address) + '</span>' +
        (d.address ? '<span class="addr">' + esc(d.address) + '</span>' : '');
      li.addEventListener('click', () => {
        selectedNearby = i;
        renderNearby(list);
      });
      ul.appendChild(li);
    });
  }

  // ---------- 渲染我的 iPad 列表 ----------
  function renderSaved(list) {
    const ul = el.savedList;
    ul.innerHTML = '';
    if (!list || list.length === 0) {
      ul.innerHTML = '<li class="empty">尚未添加 iPad</li>';
      return;
    }
    list.forEach((d, i) => {
      const li = document.createElement('li');
      if (i === selectedSaved) li.classList.add('selected');
      li.innerHTML = '<span>' + esc(d.label || d.address) + '</span>' +
        (d.address ? '<span class="addr">' + esc(d.address) + '</span>' : '');
      li.addEventListener('click', () => {
        selectedSaved = i;
        renderSaved(list);
      });
      ul.appendChild(li);
    });
  }

  // ---------- 渲染状态圆点 ----------
  function renderStatus(s) {
    const state = s && s.state ? s.state : 'idle'; // idle | connecting | streaming | blocked | missing
    let cls = 'status-idle', text = '未连接';
    switch (state) {
      case 'streaming': cls = 'status-on'; text = '已连接'; break;
      case 'connecting': cls = 'status-warn'; text = '正在连接…'; break;
      case 'blocked': cls = 'status-warn'; text = '等待显示器'; break;
      case 'missing': cls = 'status-warn'; text = '缺少显示驱动'; break;
    }
    if (s && s.link) {
      if (text === '已连接') text += (s.link === 'usb' ? '（USB）' : '（Wi-Fi）');
    }
    el.statusDot.className = 'status-dot ' + cls;
    el.statusText.textContent = text;
  }

  // ---------- 用配置填充表单 ----------
  function fillForm(cfg) {
    el.autoReconnect.checked = !!cfg.autoReconnect;
    el.usbWifiFailover.checked = !!cfg.usbWifiFailover;
    const theme = cfg.theme || 'system';
    el.themeSelect.value = theme;
    applyTheme(theme);
    el.picturePreset.value = cfg.picturePreset || 'speed';
    el.manualDevices.value = (cfg.devices || []).join('\n');
    el.port.value = cfg.port || 9000;
    el.displayBackend.value = cfg.displayBackend || 'auto';
    el.separateUsbWifi.checked = !!cfg.separateUsbWifiPicture;
    el.usbMode.value = cfg.usbEncodeMode || 'dynamic';
    el.usbPreset.value = cfg.usbEncodePreset || 'speed';
    el.wifiMode.value = cfg.wifiEncodeMode || 'dynamic';
    el.wifiPreset.value = cfg.wifiEncodePreset || 'speed';
    // 开机自启状态来自 window.__odBoot.autostartEnabled（非 config 字段）
    el.autostart.checked = !!boot.autostartEnabled;
    updateEncodePanel();
  }

  // ---------- 编码面板显隐 ----------
  function updateEncodePanel() {
    el.encodePanel.classList.toggle('hidden', !el.separateUsbWifi.checked);
  }

  // ---------- 收集表单到配置对象 ----------
  function collectConfig() {
    const devices = el.manualDevices.value
      .split(/\r?\n/)
      .map((s) => s.trim())
      .filter((s) => s.length > 0);
    return {
      devices: devices,
      port: parseInt(el.port.value, 10) || 9000,
      autoReconnect: el.autoReconnect.checked,
      usbWifiFailover: el.usbWifiFailover.checked,
      displayBackend: el.displayBackend.value,
      theme: el.themeSelect.value,
      picturePreset: el.picturePreset.value,
      separateUsbWifiPicture: el.separateUsbWifi.checked,
      usbEncodeMode: el.usbMode.value,
      usbEncodePreset: el.usbPreset.value,
      wifiEncodeMode: el.wifiMode.value,
      wifiEncodePreset: el.wifiPreset.value,
    };
  }

  // ---------- 事件绑定 ----------
  el.separateUsbWifi.addEventListener('change', updateEncodePanel);

  // 主题下拉切换（立即生效）
  el.themeSelect.addEventListener('change', () => {
    applyTheme(el.themeSelect.value);
  });
  // 跟随系统时，系统深浅色变化实时更新
  darkMedia.addEventListener('change', () => {
    if (el.themeSelect.value === 'system')
      applyTheme('system');
  });

  // 开机自启开关：切换即操作（需要管理员权限，立即生效，不随「确定」）。
  el.autostart.addEventListener('change', () => {
    el.autostartHint.textContent = '正在应用…';
    send({ type: 'setAutostart', enable: el.autostart.checked });
  });

  el.refreshNearby.addEventListener('click', () => {
    el.nearbyHint.textContent = '正在查找…';
    send({ type: 'refreshNearby' });
  });

  el.addNearby.addEventListener('click', () => {
    const list = boot.nearby || [];
    if (selectedNearby >= 0 && selectedNearby < list.length) {
      send({ type: 'addNearby', index: selectedNearby });
    }
  });

  el.removeSaved.addEventListener('click', () => {
    const list = currentSaved();
    if (selectedSaved >= 0 && selectedSaved < list.length) {
      send({ type: 'removeSaved', index: selectedSaved });
    }
  });

  // 「← 移除」按钮：从「高级」多行框里读取当前列表再移除（与右侧输入框同步）。
  el.moveBack.addEventListener('click', () => {
    const list = currentSaved();
    if (selectedSaved >= 0 && selectedSaved < list.length) {
      send({ type: 'removeSaved', index: selectedSaved });
    }
  });

  el.ok.addEventListener('click', () => {
    send({ type: 'save', config: collectConfig() });
  });

  el.cancel.addEventListener('click', () => {
    send({ type: 'cancel' });
  });

  // ---------- 自定义标题栏：窗口控制 + 手动拖动 ----------
  el.winMin.addEventListener('click', () => send({ type: 'window', action: 'minimize' }));
  el.winMax.addEventListener('click', () => send({ type: 'window', action: 'maximize' }));
  el.winClose.addEventListener('click', () => send({ type: 'window', action: 'close' }));

  // 手动拖动：标题栏按下记录起点，随光标移动持续上报屏幕坐标，由 C++ SetWindowPos 移动。
  // 用绝对屏幕坐标做增量，即使中间漏掉一两个 mousemove 也不漂移。
  let dragging = false;
  let startX = 0, startY = 0;

  el.titlebar.addEventListener('mousedown', (e) => {
    if (e.button !== 0) return;
    if (e.target.closest('.win-btn')) return; // 按钮不触发拖动
    dragging = true;
    startX = e.screenX;
    startY = e.screenY;
    send({ type: 'window', action: 'dragStart', x: startX, y: startY });
    e.preventDefault();
  });

  window.addEventListener('mousemove', (e) => {
    if (!dragging) return;
    send({ type: 'window', action: 'dragMove', x: e.screenX, y: e.screenY });
  });

  window.addEventListener('mouseup', () => {
    if (!dragging) return;
    dragging = false;
    send({ type: 'window', action: 'dragEnd' });
  });

  // 双击标题栏切换最大化/还原
  el.titlebar.addEventListener('dblclick', (e) => {
    if (e.target.closest('.win-btn')) return;
    send({ type: 'window', action: 'maximize' });
  });

  // 当前「我的 iPad」列表：以 boot.devices 为基准，叠加本地移除操作。
  function currentSaved() {
    return boot.devices || [];
  }

  // ---------- 接收 C++ 消息 ----------
  // WebView2 中 C++ 通过 PostWebMessageAsString 发送的消息，经
  // window.chrome.webview 的 message 事件送达（data 为 JS 对象）。
  if (webview) {
    webview.addEventListener('message', (ev) => {
      const data = ev.data;
      if (!data || typeof data !== 'object') return;
      handleMessage(data);
    });
  }
  function handleMessage(data) {
    switch (data.type) {
      case 'init':
        // 初始化配置与设备列表
        if (data.config) fillForm(data.config);
        if (Array.isArray(data.config && data.config.devices)) {
          boot.devices = data.config.devices.map(devToRow);
        } else if (data.devices) {
          boot.devices = data.devices;
        }
        if (data.nearby) boot.nearby = data.nearby;
        renderNearby(boot.nearby);
        renderSaved(boot.devices);
        if (data.status) renderStatus(data.status);
        break;
      case 'status':
        renderStatus(data.status || data);
        break;
      case 'nearby':
        boot.nearby = data.list || [];
        selectedNearby = -1;
        renderNearby(boot.nearby);
        el.nearbyHint.textContent = boot.nearby.length ? '' : '当前 Wi-Fi 未发现设备';
        break;
      case 'saved':
        boot.devices = data.list || [];
        renderSaved(boot.devices);
        // 保持右侧多行框同步
        el.manualDevices.value = (data.list || []).map((d) => d.address).join('\n');
        break;
      case 'autostartResult':
        // 开机自启操作结果回传
        el.autostart.checked = !!data.enabled;
        if (data.ok) {
          el.autostartHint.textContent = data.enabled ? '已启用（登录时自动启动）' : '已关闭';
        } else {
          el.autostartHint.textContent = data.error || '设置失败，请以管理员身份运行后再试';
        }
        break;
    }
  }

  // 把 C++ 注入的初始设备行转成 {label,address} 结构
  function devToRow(d) {
    if (typeof d === 'string') {
      const sp = d.indexOf(' ');
      if (sp < 0) return { label: d, address: d };
      return { label: d.slice(sp + 1).trim(), address: d.slice(0, sp) };
    }
    return d; // 已是 {label,address}
  }

  // ---------- 初始化 ----------
  const initCfg = boot.config || {};
  fillForm(initCfg);
  renderNearby(boot.nearby);
  const savedRows = (boot.devices || []).map(devToRow);
  renderSaved(savedRows);
  renderStatus(boot.status);

  // 通知 C++：页面已就绪，等待真正的「init」配置下发（若尚未）——
  // 这里 C++ 会通过 message 'init' 或 addScriptToExecuteOnDocumentCreated
  // 注入的 window.__odBoot 提供初始数据，二者取其一即可。
})();
)odjs";

// 把 CSS 与 JS 内联进 HTML，返回可直接传给 NavigateToString 的完整页面。
inline std::string BuildSettingsHtml()
{
    std::string html = kSettingsHtml;
    const std::string css = kSettingsCss;
    const std::string js = kSettingsJs;

    const std::string cssLink = "<link rel=\"stylesheet\" href=\"settings.css\" />";
    std::string style = "<style>\n" + css + "\n</style>";
    size_t pos = html.find(cssLink);
    if (pos != std::string::npos)
        html.replace(pos, cssLink.size(), style);

    const std::string jsTag = "<script src=\"settings.js\"></script>";
    std::string script = "<script>\n" + js + "\n</script>";
    pos = html.find(jsTag);
    if (pos != std::string::npos)
        html.replace(pos, jsTag.size(), script);

    return html;
}

} // namespace od
