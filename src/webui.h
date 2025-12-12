#pragma once

namespace ConnectToolWebUI {

constexpr const char kAdminHtml[] = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ConnectTool Server</title>
  <style>
    :root {
      color-scheme: dark;
      --bg: #0b0f14;
      --panel: #111823;
      --panel-2: #0f1620;
      --text: #e6edf3;
      --muted: #9aa4b2;
      --accent: #23c9a9;
      --accent-2: #2ad2ff;
      --danger: #ff6b6b;
      --border: #1f2937;
      font-family: system-ui, -apple-system, Segoe UI, Roboto, Helvetica, Arial, sans-serif;
    }
    body { margin: 0; background: var(--bg); color: var(--text); }
    header {
      display: flex; align-items: center; justify-content: space-between;
      padding: 14px 18px; background: #0e141c; border-bottom: 1px solid var(--border);
      position: sticky; top: 0;
    }
    header h1 { font-size: 18px; margin: 0; font-weight: 600; letter-spacing: 0.2px; }
    header .status { font-size: 13px; color: var(--muted); }
    main { max-width: 1100px; margin: 18px auto; padding: 0 14px; display: grid; grid-template-columns: 1fr 1fr; gap: 14px; }
    .card { background: var(--panel); border: 1px solid var(--border); border-radius: 10px; padding: 14px; }
    .card h2 { font-size: 15px; margin: 0 0 10px; }
    .row { display: grid; grid-template-columns: 120px 1fr; align-items: center; gap: 8px; margin: 8px 0; }
    .row label { color: var(--muted); font-size: 13px; }
    .row > div, .row > input, .row > select { min-width: 0; }
    input, select {
      width: 100%; padding: 8px 10px; background: var(--panel-2);
      border: 1px solid var(--border); border-radius: 8px; color: var(--text);
      font-size: 14px;
    }
    .inline { display: flex; gap: 8px; align-items: center; }
    .inline input { flex: 1 1 auto; min-width: 0; width: auto; }
    .inline button { flex: 0 0 auto; white-space: nowrap; }
    button {
      appearance: none; border: 1px solid var(--border); background: var(--panel-2);
      color: var(--text); padding: 8px 12px; border-radius: 8px; cursor: pointer;
      font-size: 14px;
    }
    button.primary { background: linear-gradient(135deg, var(--accent), var(--accent-2)); border: none; color: #031b1c; font-weight: 700; }
    button.danger { background: transparent; border-color: #3b1b1b; color: var(--danger); }
    button:disabled { opacity: .6; cursor: not-allowed; }
    .pill { display: inline-flex; padding: 2px 8px; border-radius: 999px; font-size: 12px; border: 1px solid var(--border); color: var(--muted); }
    .pill.good { color: var(--accent); border-color: #164c3f; }
    .pill.warn { color: #fbbf24; border-color: #3d2f10; }
    .pill.bad { color: var(--danger); border-color: #3b1b1b; }
    .muted { color: var(--muted); font-size: 13px; }
    .mono { font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; }
    .actions { display: flex; gap: 8px; flex-wrap: wrap; margin-top: 8px; }
    .full { grid-column: 1 / -1; }
    .toast {
      position: fixed; right: 14px; bottom: 14px; background: #0c1117; border: 1px solid var(--border);
      padding: 10px 12px; border-radius: 8px; font-size: 13px; opacity: 0; transform: translateY(6px);
      transition: all .18s ease;
    }
    .toast.show { opacity: 1; transform: translateY(0); }
    .overlay {
      position: fixed; inset: 0; background: rgba(0,0,0,.6); display: none; align-items: center; justify-content: center;
    }
    .overlay .panel { width: 360px; background: var(--panel); border: 1px solid var(--border); border-radius: 10px; padding: 16px; }
  </style>
</head>
<body>
  <header>
    <h1>ConnectTool Server</h1>
    <div class="status" id="headerStatus">loading…</div>
  </header>

  <main>
    <section class="card">
      <h2>Monitor</h2>
      <div class="row"><label>Steam</label><div id="steamReady" class="pill">-</div></div>
      <div class="row"><label>Mode</label><div id="modePill" class="pill">-</div></div>
      <div class="row"><label>Room</label><div><span id="lobbyName">-</span> <span class="muted mono" id="lobbyId">-</span></div></div>
      <div class="row"><label>Status</label><div id="statusPill" class="pill">-</div></div>
      <div class="row"><label>Publish</label><div class="mono" id="publishView">-</div></div>
      <div class="row" id="localPortRow"><label>local_port</label><div class="mono" id="localPortView">-</div></div>
      <div class="row" id="bindPortRow"><label>bind_port</label><div class="mono" id="bindPortView">-</div></div>
      <div class="row" id="tunInfoRow"><label>TUN Info</label><div><span class="mono" id="tunIp">-</span> <span class="muted mono" id="tunDev">-</span></div></div>
      <div class="row" id="tcpClientsRow"><label>TCP clients</label><div class="mono" id="tcpClients">0</div></div>
      <div class="actions">
        <button class="primary" id="startHost">Start Hosting</button>
        <button class="danger" id="disconnect">Stop Hosting</button>
      </div>
    </section>

    <section class="card">
      <h2>服务端配置</h2>
      <div class="row">
        <label>mode</label>
        <select id="modeSel">
          <option value="tcp">TCP</option>
          <option value="tun">TUN</option>
        </select>
      </div>
      <div class="row" id="localPortInputRow"><label>local_port</label><input id="localPort" type="number" min="1" max="65535"></div>
      <div class="row" id="bindPortInputRow"><label>bind_port</label><input id="bindPort" type="number" min="1" max="65535"></div>
      <div class="row">
        <label>publish</label>
        <select id="publishSel">
          <option value="true">true</option>
          <option value="false">false</option>
        </select>
      </div>
      <div class="row"><label>room_name</label><input id="roomName" type="text" maxlength="64" placeholder="ConnectTool 房间"></div>
      <div class="actions">
        <button id="saveConfig">保存设置</button>
      </div>
      <p class="muted">保存后会立即生效；切换模式可能需要重新建房。</p>
    </section>

    <section class="card full">
      <h2>Sharing</h2>
      <div class="row">
        <label>Link</label>
        <div class="inline">
          <input id="joinUrl" class="mono" readonly>
          <button id="copyJoin">复制</button>
        </div>
      </div>
      <div class="row">
        <label>Code</label>
        <div class="inline">
          <input id="shareCode" class="mono" readonly>
          <button id="copyCode">复制</button>
        </div>
      </div>
      <p class="muted">复制分享码后打开 ConnectTool，会自动识别并加入。</p>
    </section>
  </main>

  <div class="toast" id="toast"></div>

  <div class="overlay" id="loginOverlay">
    <div class="panel">
      <h2 style="margin-top:0;">管理员登录</h2>
      <p class="muted">请输入 admin_token。</p>
      <input id="tokenInput" class="mono" placeholder="token">
      <div class="actions" style="justify-content:flex-end;margin-top:12px;">
        <button class="primary" id="tokenSave">保存</button>
      </div>
    </div>
  </div>

  <script>
    const qs = (s) => document.querySelector(s);
    const toastEl = qs('#toast');
    const overlay = qs('#loginOverlay');
	    const tokenInput = qs('#tokenInput');

	    let token = localStorage.getItem('ct_admin_token') || new URLSearchParams(location.search).get('token') || '';
	    let configDirty = false;

	    function markConfigDirty(){
	      configDirty = true;
	    }

	    ['#modeSel', '#localPort', '#bindPort', '#publishSel', '#roomName'].forEach(sel => {
	      const el = qs(sel);
	      if (!el) return;
	      el.addEventListener('change', markConfigDirty);
	      el.addEventListener('input', markConfigDirty);
	    });

    function showToast(msg, ms=1600){
      toastEl.textContent = msg;
      toastEl.classList.add('show');
      setTimeout(()=>toastEl.classList.remove('show'), ms);
    }

    function showLogin(){
      overlay.style.display = 'flex';
      tokenInput.value = token || '';
    }

    qs('#tokenSave').onclick = () => {
      token = tokenInput.value.trim();
      localStorage.setItem('ct_admin_token', token);
      overlay.style.display = 'none';
      refresh();
    };

    function authHeaders(){
      return token ? {'Authorization': 'Bearer ' + token} : {};
    }

    async function api(path, opts={}){
      opts.headers = Object.assign({'Content-Type':'application/json'}, authHeaders(), opts.headers||{});
      const res = await fetch(path, opts);
      if (res.status === 401) {
        showLogin();
        throw new Error('unauthorized');
      }
      return res;
    }

    function setPill(el, text, cls){
      el.textContent = text;
      el.className = 'pill ' + (cls||'');
    }

    function updateState(s){
      setPill(qs('#steamReady'), s.steamReady ? 'Steam Ready' : 'Steam Not Ready', s.steamReady ? 'good':'bad');
      setPill(qs('#modePill'), s.connectionMode === 1 ? 'TUN' : 'TCP', 'good');
      setPill(qs('#statusPill'),
        s.isHost ? 'Host' : (s.isConnected ? 'Connected' : 'Idle'),
        s.isHost || s.isConnected ? 'good':'');

      qs('#lobbyName').textContent = s.lobbyName || '-';
      qs('#lobbyId').textContent = s.lobbyId || '';
      qs('#publishView').textContent = (s.publishLobby === false) ? 'false' : 'true';
      qs('#localPortView').textContent = s.localPort;
      qs('#bindPortView').textContent = s.localBindPort;
      qs('#tunIp').textContent = s.tunLocalIp || '-';
      qs('#tunDev').textContent = s.tunDeviceName || '';
      qs('#tcpClients').textContent = s.tcpClients ?? 0;

      const isTun = s.connectionMode === 1;
      qs('#localPortRow').style.display = isTun ? 'none' : 'grid';
      qs('#bindPortRow').style.display = isTun ? 'none' : 'grid';
      qs('#tunInfoRow').style.display = isTun ? 'grid' : 'none';
      qs('#tcpClientsRow').style.display = isTun ? 'none' : 'grid';

	      if (!configDirty) {
	        qs('#modeSel').value = isTun ? 'tun' : 'tcp';
	        qs('#publishSel').value = (s.publishLobby === false) ? 'false' : 'true';
	        qs('#localPort').value = s.localPort;
	        qs('#bindPort').value = s.localBindPort;
	        qs('#roomName').value = s.lobbyName || '';
	      }
	      updateConfigVisibility();

      qs('#headerStatus').textContent = s.lobbyId
        ? ('房间 ' + (s.lobbyName||'') + ' ' + s.lobbyId)
        : '未建房';
    }

    function updateConfigVisibility(){
      const isTun = qs('#modeSel').value === 'tun';
      qs('#localPortInputRow').style.display = isTun ? 'none' : 'grid';
      qs('#bindPortInputRow').style.display = isTun ? 'none' : 'grid';
    }

    qs('#modeSel').onchange = updateConfigVisibility;

	    async function refreshJoin(){
	      try{
	        const res = await api('/admin/join');
	        const d = await res.json();
	        qs('#joinUrl').value = d.joinUrl || d.inviteUrl || '';
	        qs('#shareCode').value = d.shareCode || '';
	      }catch(e){
	        qs('#joinUrl').value = '';
	        qs('#shareCode').value = '';
	      }
	    }

    async function refresh(){
      try{
        const res = await api('/admin/state');
        const s = await res.json();
        updateState(s);
        if (s.lobbyId) await refreshJoin();
      }catch(e){}
    }

    qs('#startHost').onclick = async () => {
      try{
        await api('/admin/host/start', {method:'POST', body:'{}'});
        showToast('已请求开房');
        refresh();
      }catch(e){}
    };

    qs('#disconnect').onclick = async () => {
      try{
        await api('/admin/disconnect', {method:'POST', body:'{}'});
        showToast('已断开');
        refresh();
      }catch(e){}
    };

    qs('#saveConfig').onclick = async () => {
      const mode = qs('#modeSel').value;
      const payload = {
        mode,
        publish: qs('#publishSel').value === 'true'
      };
      if (mode !== 'tun') {
        payload.localPort = parseInt(qs('#localPort').value||'0',10);
        payload.localBindPort = parseInt(qs('#bindPort').value||'0',10);
      }
      const roomName = qs('#roomName').value.trim();
      if (roomName) payload.roomName = roomName;
	      try{
	        await api('/admin/config', {method:'POST', body: JSON.stringify(payload)});
	        showToast('已保存');
	        configDirty = false;
	        refresh();
	      }catch(e){}
	    };

	    qs('#copyJoin').onclick = async () => {
	      const url = qs('#joinUrl').value;
	      if (!url) return;
	      try{
	        await navigator.clipboard.writeText(url);
	        showToast('已复制');
	      }catch(e){
	        qs('#joinUrl').select();
	        document.execCommand('copy');
	        showToast('已复制');
	      }
	    };

	    qs('#copyCode').onclick = async () => {
	      const code = qs('#shareCode').value;
	      if (!code) return;
	      try{
	        await navigator.clipboard.writeText(code);
	        showToast('已复制');
	      }catch(e){
	        qs('#shareCode').select();
	        document.execCommand('copy');
	        showToast('已复制');
	      }
	    };

    if (!token) showLogin();
    refresh();
    setInterval(refresh, 2000);
  </script>
</body>
</html>
)HTML";

constexpr const char kIndexHtml[] = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ConnectTool Server</title>
  <style>
    body { margin:0; font-family: system-ui, -apple-system, Segoe UI, Roboto, Helvetica, Arial, sans-serif; background:#0b0f14; color:#e6edf3; display:flex; align-items:center; justify-content:center; min-height:100vh; }
    .box { background:#111823; border:1px solid #1f2937; padding:18px 20px; border-radius:10px; width:420px; }
    a { color:#2ad2ff; }
    .muted { color:#9aa4b2; font-size:14px; }
  </style>
</head>
<body>
  <div class="box">
    <h2 style="margin-top:0;">ConnectTool Server 已启动</h2>
    <p class="muted">Admin:<a href="/admin/ui">/admin/ui</a>（需要 token）</p>
    <p class="muted">Join:<code>/join/&lt;lobbyId&gt;</code></p>
  </div>
</body>
</html>
)HTML";

} // namespace ConnectToolWebUI
