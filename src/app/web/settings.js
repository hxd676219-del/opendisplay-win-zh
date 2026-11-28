// OpenDisplay 设置 · 前端桥接
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
