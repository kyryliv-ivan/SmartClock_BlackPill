#ifndef SETUP_PAGE_H
#define SETUP_PAGE_H

// Local captive-portal page served only while the SmartClock setup AP is up
// (see startSetupMode() in the main .ino) - no internet at that point, so
// everything here is self-contained: system fonts only, no external
// requests besides the device's own /scan, /connect, /settime endpoints.
const char SETUP_PAGE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>SmartClock Setup</title>
<style>
:root{
  --bg:#f2f3f5; --card:#ffffff; --text:#1c1e21; --muted:#6b7280;
  --border:#e5e7eb; --accent:#2563eb; --accent-dark:#1d4ed8;
  --ok:#16a34a; --err:#dc2626; --radius:14px;
}
*{box-sizing:border-box;}
body{
  margin:0; padding:28px 16px 40px; background:var(--bg); color:var(--text);
  font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Arial,sans-serif;
}
.wrap{max-width:420px;margin:0 auto;}
header{text-align:center;margin-bottom:22px;}
header h1{font-size:21px;margin:0 0 4px;letter-spacing:.2px;}
header p{margin:0;color:var(--muted);font-size:13px;}
.card{
  background:var(--card); border:1px solid var(--border);
  border-radius:var(--radius); padding:20px; margin-bottom:16px;
  box-shadow:0 1px 3px rgba(0,0,0,.05);
}
.card h2{font-size:14px;margin:0 0 4px;text-transform:uppercase;letter-spacing:.4px;color:var(--muted);}
.card .hint{font-size:13px;color:var(--muted);margin:0 0 14px;}
label{display:block;font-size:13px;color:var(--muted);margin:14px 0 6px;}
label:first-of-type{margin-top:0;}
select,input[type=password],input[type=date],input[type=time]{
  width:100%; padding:11px 12px; border:1px solid var(--border);
  border-radius:10px; font-size:15px; background:#fafafa; color:var(--text);
}
select:focus,input:focus{outline:none;border-color:var(--accent);background:#fff;}
.row{display:flex;gap:20px;}
.row > div{flex:1;}
button{
  width:100%; padding:12px; margin-top:16px; border:none; border-radius:10px;
  background:var(--accent); color:#fff; font-size:15px; font-weight:600;
}
button:active{background:var(--accent-dark);}
button:disabled{background:#9ca3af;}
.status{margin-top:10px;font-size:13px;color:var(--muted);min-height:16px;}
.status.ok{color:var(--ok);}
.status.err{color:var(--err);}
.sig{color:var(--muted);font-weight:400;}
</style></head><body>
<div class="wrap">
<header>
  <h1>SmartClock</h1>
  <p>Local setup - no internet needed on this page</p>
</header>

<div class="card">
  <h2>Wi-Fi</h2>
  <p class="hint">Pick your home network and enter its password.</p>
  <label for="ssid">Network</label>
  <select id="ssid"><option>Scanning...</option></select>
  <label for="password">Password</label>
  <input type="password" id="password" placeholder="Wi-Fi password">
  <button id="connectBtn" onclick="doConnect()">Connect</button>
  <p class="status" id="wifiStatus"></p>
</div>

<div class="card">
  <h2>Date &amp; Time</h2>
  <p class="hint">Only needed if you're not connecting to Wi-Fi - once online, the clock syncs itself automatically.</p>
  <div class="row">
    <div>
      <label for="date">Date</label>
      <input type="date" id="date">
    </div>
    <div>
      <label for="time">Time</label>
      <input type="time" id="time">
    </div>
  </div>
  <button id="timeBtn" onclick="doSetTime()">Set clock</button>
  <p class="status" id="timeStatus"></p>
</div>

</div>
<script>
function signalBars(rssi) {
  if (rssi >= -55) return '****';
  if (rssi >= -65) return '*** ';
  if (rssi >= -75) return '**  ';
  return '*   ';
}

fetch('/scan').then(r => r.json()).then(list => {
  const sel = document.getElementById('ssid');
  sel.innerHTML = '';
  if (!list.length) {
    sel.innerHTML = '<option>No networks found</option>';
    return;
  }
  list.sort((a, b) => b.rssi - a.rssi).forEach(n => {
    const opt = document.createElement('option');
    opt.value = n.ssid;
    opt.textContent = n.ssid + '   [' + signalBars(n.rssi) + ']';
    sel.appendChild(opt);
  });
}).catch(() => {
  document.getElementById('ssid').innerHTML = '<option>Scan failed - reload page</option>';
});

function doConnect() {
  const ssid = document.getElementById('ssid').value;
  const password = document.getElementById('password').value;
  const btn = document.getElementById('connectBtn');
  const status = document.getElementById('wifiStatus');

  btn.disabled = true;
  status.className = 'status';
  status.textContent = 'Connecting to ' + ssid + '...';

  const form = new URLSearchParams();
  form.append('ssid', ssid);
  form.append('password', password);
  fetch('/connect', { method: 'POST', body: form })
    .then(r => r.text())
    .then(t => { status.className = 'status'; status.innerHTML = t; })
    .catch(() => { status.className = 'status err'; status.textContent = 'Request failed - try again.'; })
    .finally(() => { btn.disabled = false; });
}

(function prefillNow() {
  const now = new Date();
  const pad = n => String(n).padStart(2, '0');
  document.getElementById('date').value =
    now.getFullYear() + '-' + pad(now.getMonth() + 1) + '-' + pad(now.getDate());
  document.getElementById('time').value = pad(now.getHours()) + ':' + pad(now.getMinutes());
})();

function doSetTime() {
  const date = document.getElementById('date').value;
  const time = document.getElementById('time').value;
  const btn = document.getElementById('timeBtn');
  const status = document.getElementById('timeStatus');

  if (!date || !time) {
    status.className = 'status err';
    status.textContent = 'Pick both a date and a time.';
    return;
  }

  btn.disabled = true;
  status.className = 'status';
  status.textContent = 'Setting clock...';

  const form = new URLSearchParams();
  form.append('date', date);
  form.append('time', time);
  fetch('/settime', { method: 'POST', body: form })
    .then(r => r.ok ? r.text() : Promise.reject())
    .then(t => { status.className = 'status ok'; status.textContent = t; })
    .catch(() => { status.className = 'status err'; status.textContent = 'Could not set clock - try again.'; })
    .finally(() => { btn.disabled = false; });
}
</script>
</body></html>
)HTML";

#endif /* SETUP_PAGE_H */
