/*
    Configuration web page. Static HTML; values are filled from /api/config and /status.
    Video Walrus 2026
*/
#pragma once
#include <Arduino.h>

static const char PAGE_HTML[] PROGMEM = R"HTML(<!DOCTYPE html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><meta name="color-scheme" content="dark light">
<script>try{var _t=localStorage.getItem('theme');if(_t==='light'||_t==='dark')document.documentElement.setAttribute('data-theme',_t)}catch(e){}</script>
<link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><circle cx='50' cy='50' r='45' fill='%23ff0000'/></svg>">
<title>TSL Tally Light</title>
<style>
:root{color-scheme:dark;--bg:#1a1a2e;--card:#16213e;--field:#0f3460;--text:#eee;--muted:#888;--accent:#00d4ff;--accent-text:#1a1a2e;--border:#0f3460;--hover:#1a4a7a;--footer:#666;--shadow:none;--bg-green:#0a3d0a;--bg-red:#4d0000;--bg-yellow:#4d4d00;--t-off:#888;--t-green:#0f0;--t-red:#f00;--t-yellow:#ff0}
@media(prefers-color-scheme:light){:root:not([data-theme=dark]){color-scheme:light;--bg:#f2f4f8;--card:#fff;--field:#e6ebf2;--text:#1a1a2e;--muted:#667;--accent:#0077a8;--accent-text:#fff;--border:#d0d7e2;--hover:#d5dde8;--footer:#778;--shadow:0 1px 3px rgba(0,0,0,.08);--bg-green:#d8f0d8;--bg-red:#f6d6d6;--bg-yellow:#f3eebc;--t-off:#889;--t-green:#1b8a1b;--t-red:#c62828;--t-yellow:#9a7d00}}
:root[data-theme=light]{color-scheme:light;--bg:#f2f4f8;--card:#fff;--field:#e6ebf2;--text:#1a1a2e;--muted:#667;--accent:#0077a8;--accent-text:#fff;--border:#d0d7e2;--hover:#d5dde8;--footer:#778;--shadow:0 1px 3px rgba(0,0,0,.08);--bg-green:#d8f0d8;--bg-red:#f6d6d6;--bg-yellow:#f3eebc;--t-off:#889;--t-green:#1b8a1b;--t-red:#c62828;--t-yellow:#9a7d00}
body{font-family:Arial,sans-serif;margin:20px;background:var(--bg);color:var(--text);transition:background-color .3s}
body.tally-green{background:var(--bg-green)}body.tally-red{background:var(--bg-red)}body.tally-yellow{background:var(--bg-yellow)}
.container{max-width:560px;margin:0 auto}
h1{color:var(--accent);text-align:center;margin-bottom:4px}
.sub{text-align:center;color:var(--muted);margin:0 0 12px}
.card{background:var(--card);padding:20px;border-radius:10px;margin-bottom:20px;box-shadow:var(--shadow)}
.card h2{margin-top:0;color:var(--accent);border-bottom:1px solid var(--border);padding-bottom:10px}
label{display:block;width:fit-content;margin:10px 0 5px;font-weight:bold}
label.chk{font-weight:normal;margin-top:12px}
input[type=text],input[type=number],input[type=password],select{width:100%;padding:10px;border:1px solid var(--border);border-radius:5px;background:var(--field);color:var(--text);box-sizing:border-box;font-size:16px}
input:focus,select:focus{outline:none;border-color:var(--accent)}
input[type=checkbox]{width:18px;height:18px;vertical-align:middle}
.masked{-webkit-text-security:disc}
.hide{display:none!important}
button{padding:15px;background:var(--accent);color:var(--accent-text);border:none;border-radius:5px;font-size:16px;font-weight:bold;cursor:pointer}
button:hover{opacity:.85}
button:disabled{opacity:.5;cursor:default}
.status{background:var(--field);padding:15px;border-radius:5px}
.status-item{display:flex;justify-content:space-between;align-items:center;padding:5px 0;gap:10px}
.status-item span:last-child{text-align:right;word-break:break-word}
#tallyState{font-weight:bold}
#tallyState.tally-off{color:var(--t-off)}#tallyState.tally-green{color:var(--t-green)}#tallyState.tally-red{color:var(--t-red)}#tallyState.tally-yellow{color:var(--t-yellow)}
.small{padding:4px 12px;font-size:11px;margin-left:8px}
.note{font-size:12px;color:var(--muted);margin-top:5px}
.conn-eth{color:#4CAF50}.conn-wifi{color:#2196F3}.conn-ap{color:#FF9800}
.test-btns{display:flex;gap:10px;margin-top:10px}
.test-btns button{flex:1;padding:15px 10px;font-size:14px;user-select:none;-webkit-user-select:none;-webkit-touch-callout:none}
.btn-green{background:#0f0;color:#000}.btn-red{background:#f00;color:#fff}.btn-yellow{background:#ff0;color:#000}
.device-list{max-height:300px;overflow-y:auto}
.device-item{display:flex;align-items:center;padding:10px;background:var(--field);border-radius:5px;margin-bottom:8px}
.device-status{width:12px;height:12px;border-radius:50%;margin-right:10px;flex-shrink:0}
.device-status.off{background:#666}.device-status.green{background:#0f0}.device-status.red{background:#f00}.device-status.yellow{background:#ff0}
.device-info{flex:1;min-width:0}
.device-name{font-weight:bold;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.device-details{font-size:12px;color:var(--muted)}
.device-link{padding:8px 12px;background:var(--accent);color:var(--accent-text);text-decoration:none;border-radius:4px;font-size:12px;white-space:nowrap}
.refresh-btn{background:var(--field);color:var(--text);padding:8px 15px;margin-bottom:15px;font-size:14px}
.bulk-btns{display:flex;gap:8px;margin-top:15px}.bulk-btns button{flex:1;padding:10px;font-size:12px}
.no-devices{text-align:center;color:var(--muted);padding:20px}
.wifi-list{display:none;max-height:220px;overflow-y:auto;margin-top:8px}.wifi-list.show{display:block}
.wifi-item{display:flex;justify-content:space-between;padding:8px 10px;background:var(--field);border-radius:5px;margin-bottom:5px;cursor:pointer}
.wifi-item:hover{background:var(--hover)}
.theme{display:flex;justify-content:flex-end;margin:0 0 10px}
.th{background:none;color:var(--muted);border:1px solid var(--border);border-radius:0;padding:4px 10px;font-size:12px;font-weight:normal;margin-left:-1px}
.th:first-child{border-radius:5px 0 0 5px;margin-left:0}.th:last-child{border-radius:0 5px 5px 0}
.th.on{background:var(--accent);color:var(--accent-text);border-color:var(--accent)}
.th:hover{opacity:1;color:var(--text)}.th.on:hover{color:var(--accent-text)}
.tabs{display:flex;gap:4px;margin:0 0 20px;border-bottom:1px solid var(--border)}
.tab{flex:1;background:none;color:var(--muted);border:none;border-bottom:3px solid transparent;border-radius:0;padding:12px 6px;font-size:15px;font-weight:bold;cursor:pointer}
.tab.on{color:var(--accent);border-bottom-color:var(--accent)}
.tab:hover{opacity:1;color:var(--text)}
.tab.on:hover{color:var(--accent)}
.panel{display:none}.panel.on{display:block}
.btns{display:flex;gap:10px;margin-top:15px}
footer{text-align:center;margin-top:30px;padding:20px;color:var(--footer);font-size:12px}
footer a{color:var(--accent)}
.ovl{position:fixed;inset:0;background:rgba(0,0,0,.55);display:none;align-items:center;justify-content:center;z-index:8;padding:16px}.ovl.show{display:flex}
.modal{background:var(--card);color:var(--text);border-radius:10px;padding:22px;max-width:440px;width:100%;box-shadow:0 8px 30px rgba(0,0,0,.4)}
.modal h3{margin:0 0 4px;color:var(--accent);font-size:20px}.modal .d{color:var(--muted);font-size:12px;margin-bottom:12px}
.modal ul{margin:0 0 18px;padding-left:20px}.modal li{margin:5px 0}.modal p{margin:0 0 16px}.modal .btns{margin-top:0}
.toast{position:fixed;top:16px;left:50%;transform:translateX(-50%);background:#4CAF50;color:#fff;padding:10px 18px;border-radius:6px;font-weight:bold;z-index:9;box-shadow:0 2px 8px rgba(0,0,0,.4);transition:opacity .4s}
.disco-overlay{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,.9);z-index:99;justify-content:center;align-items:center;flex-direction:column}
.disco-overlay.active{display:flex}
.disco-text{font-size:48px;font-weight:bold;text-align:center;animation:disco-rainbow .5s linear infinite}
@keyframes disco-rainbow{0%{color:#f00}16%{color:#ff0}33%{color:#0f0}50%{color:#0ff}66%{color:#00f}83%{color:#f0f}100%{color:#f00}}
.disco-cancel{margin-top:40px;padding:20px 40px;font-size:20px;background:#c00;color:#fff;border-radius:10px}
</style></head><body><div class="container">
<h1>TSL Tally Light</h1>
<p class="sub"><span id="hostTitle">&nbsp;</span> <span id="hdrIp"></span> <span id="hdrConn"></span></p>

<div class="theme"><button type="button" class="th" data-th="auto" onclick="setTheme('auto')">Auto</button><button type="button" class="th" data-th="light" onclick="setTheme('light')">Light</button><button type="button" class="th" data-th="dark" onclick="setTheme('dark')">Dark</button></div>
<div class="tabs"><button type="button" class="tab" data-tab="op" onclick="showTab('op')">Operation</button><button type="button" class="tab" data-tab="cfg" onclick="showTab('cfg')">Configuration</button><button type="button" class="tab" data-tab="sys" onclick="showTab('sys')">System</button></div>

<div id="tab-op" class="panel">
<div class="card"><h2>Status</h2><div class="status">
<div class="status-item"><span>Tally State:</span><span id="tallyState" class="tally-off">Off</span></div>
<div class="status-item"><span>TSL Text:</span><span id="tallyText">-</span></div>
<div class="status-item"><span id="tslLabel">TSL data:</span><span id="tsl"></span></div>
</div></div>

<div class="card"><h2>Test Tally</h2>
<p class="note">Hold a button to test. Release returns to the last state from the switcher.</p>
<div class="test-btns">
<button type="button" class="btn-green" onmousedown="testOn(1)" onmouseup="testOff()" onmouseleave="testOff()" ontouchstart="event.preventDefault();testOn(1)" ontouchend="event.preventDefault();testOff()" ontouchcancel="testOff()">GREEN</button>
<button type="button" class="btn-red" onmousedown="testOn(2)" onmouseup="testOff()" onmouseleave="testOff()" ontouchstart="event.preventDefault();testOn(2)" ontouchend="event.preventDefault();testOff()" ontouchcancel="testOff()">RED</button>
<button type="button" class="btn-yellow" onmousedown="testOn(3)" onmouseup="testOff()" onmouseleave="testOff()" ontouchstart="event.preventDefault();testOn(3)" ontouchend="event.preventDefault();testOff()" ontouchcancel="testOff()">YELLOW</button>
</div></div>

<div class="card"><h2>Network Devices</h2>
<p class="note">Other TSL tally lights found on the network via mDNS</p>
<button type="button" class="refresh-btn" onclick="discoverDevices()">Scan Network</button>
<div id="deviceList" class="device-list"><p class="no-devices">Scanning...</p></div>
<div class="bulk-btns"><button type="button" class="btn-green" onclick="bulkTest(1)">All GREEN</button><button type="button" class="btn-red" onclick="bulkTest(2)">All RED</button><button type="button" style="background:#333;color:#fff" onclick="bulkTest(0)">All OFF</button></div>
<p class="note">Bulk buttons latch every discovered device plus this one. Hold a Test Tally button and release to return this device to the switcher.</p>
</div>
</div>

<form action="/save" method="POST" id="cfgForm" autocomplete="off" onsubmit="return validateTabs()">
<div id="tab-cfg" class="panel">
<div class="card"><h2>Tally Source</h2>
<label for="src">Source</label>
<select id="src" name="src" onchange="toggleSource()"><option value="0">TSL 3.1 (UDP multicast)</option><option value="1">Tally Arbiter</option></select>
<p class="note">Changing the source reboots the device.</p>
</div>

<div class="card" id="tslCard"><h2>TSL Settings</h2>
<label for="tslAddr">TSL Address (0-126)</label>
<input type="number" id="tslAddr" name="tslAddr" min="0" max="126">
<label for="tslMcast">Multicast Address</label>
<input type="text" id="tslMcast" name="tslMcast">
<label for="tslPort">TSL Port</label>
<input type="number" id="tslPort" name="tslPort" min="1" max="65535">
<p class="note">Changing the multicast address or port reboots the device. The address applies as soon as you save.</p>
</div>

<div class="card hide" id="taCard"><h2>Tally Arbiter</h2>
<label for="taHost">Server</label>
<input type="text" id="taHost" name="taHost" placeholder="hostname or IP">
<button type="button" id="taScanBtn" style="width:100%;margin-top:8px;padding:10px;font-size:14px" onclick="taScan()">Find server</button>
<div id="taList" class="wifi-list"></div>
<label for="taPort">Port</label>
<input type="number" id="taPort" name="taPort" min="1" max="65535">
<label for="taDevice">Device</label>
<select id="taDevice" name="taDevice"><option value="unassigned">Assign from Tally Arbiter</option></select>
<button type="button" id="taRefreshBtn" style="width:100%;margin-top:8px;padding:10px;font-size:14px" onclick="taRefresh()">Refresh device list</button>
<p class="note">The tally follows this device: program lights red, preview green, both yellow. Reassigning it from Tally Arbiter's producer page also works and is remembered. Changing the server or port reboots the device; changing the device applies as soon as you save.</p>
</div>

<div class="card"><h2>LED Settings</h2>
<label for="maxBright">Max Brightness (1-255)</label>
<input type="number" id="maxBright" name="maxBright" min="1" max="255" required>
<p class="note">TSL brightness (0-3) maps to 0 - max brightness</p>
<label for="ledAnim">LED Animation</label>
<select id="ledAnim" name="ledAnim"><option value="0">Solid</option><option value="1">Spin</option></select>
<p class="note">Spin keeps the middle LED on and sweeps a bright point around the outer six while a tally is active</p>
</div>

<div class="btns"><button type="submit" style="flex:1">Save</button></div>
<p class="note" style="text-align:center">Saving applies Configuration and System together. Address, brightness and animation apply at once.</p>
</div>

<div id="tab-sys" class="panel">
<div class="card"><h2>Device</h2><div class="status">
<div class="status-item"><span>Connection:</span><span id="conn"></span></div>
<div class="status-item"><span>IP Address:</span><span id="ip"></span></div>
<div class="status-item"><span>Hostname:</span><span id="host"></span></div>
<div class="status-item"><span>MAC:</span><span id="mac"></span></div>
<div class="status-item"><span>Clock (UTC):</span><span id="clk"></span></div>
<div class="status-item"><span>Settings lock:</span><span id="lockst"></span></div>
<div class="status-item hide" id="apRow"><span>AP SSID:</span><span id="apSsid"></span></div>
<div class="status-item"><span>Firmware:</span><span><span id="fw"></span><button type="button" class="small" onclick="checkUpdate()">Check</button></span></div>
<div class="status-item hide" id="updateNotice"><span style="color:#ff6b6b">Update available:</span><span><span id="latest" style="color:#ff6b6b"></span><button type="button" class="small" style="background:#4CAF50;color:#fff" onclick="installUpdate()">Install</button></span></div>
</div></div>

<div class="card"><h2>Network Settings</h2>
<label for="hostname">Hostname</label>
<input type="text" id="hostname" name="hostname" maxlength="32" required>
<label for="dhcp">IP Configuration</label>
<select id="dhcp" name="dhcp" onchange="toggleIP()"><option value="1">DHCP (Automatic)</option><option value="0">Static IP</option></select>
<div id="ipFields" class="hide">
<label for="sip">IP Address</label><input type="text" id="sip" name="ip">
<label for="gw">Gateway</label><input type="text" id="gw" name="gw">
<label for="sn">Subnet Mask</label><input type="text" id="sn" name="sn">
<label for="dns">DNS Server</label><input type="text" id="dns" name="dns">
</div>
<p class="note">Applies to Ethernet and WiFi. Changes here reboot the device.</p>
</div>

<div class="card"><h2>WiFi Settings</h2>
<label for="wifiEn">WiFi</label>
<select id="wifiEn" name="wifiEn" onchange="toggleWifi()"><option value="0">Disabled</option><option value="1">Enabled (used when Ethernet is down)</option></select>
<div id="wifiFields" class="hide">
<label for="wifiSSID">WiFi SSID</label><input type="text" id="wifiSSID" name="wifiSSID" maxlength="32">
<button type="button" id="scanBtn" style="width:100%;margin-top:8px;padding:10px;font-size:14px" onclick="wifiScan()">Scan for Networks</button>
<div id="wifiList" class="wifi-list"></div>
<label for="wifiPass">WiFi Password</label><input type="text" class="masked" id="wifiPass" name="wifiPass" maxlength="64" autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false" oninput="typing('wifiPass','showPassRow','showPass')">
<label class="chk hide" id="showPassRow"><input type="checkbox" id="showPass" onchange="$('wifiPass').classList.toggle('masked',!this.checked)"> Show while typing</label>
<label class="chk"><input type="checkbox" id="passclear" name="passclear" value="1"> Network has no password</label>
<p class="note">The stored password is never shown. Leave the field blank to keep it.</p>
</div>
<p class="note" id="apNote"></p>
</div>

<div class="card"><h2>Settings Lock</h2>
<label for="newpin">PIN</label>
<input type="text" id="newpin" name="newpin" class="masked" maxlength="16" autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false" placeholder="4 to 16 characters, blank = no lock" oninput="typing('newpin','showPinRow','showPin')">
<label class="chk hide" id="showPinRow"><input type="checkbox" id="showPin" onchange="$('newpin').classList.toggle('masked',!this.checked)"> Show while typing</label>
<label class="chk hide" id="pinclearRow"><input type="checkbox" id="pinclear" name="pinclear" value="1"> Remove lock</label>
<p class="note">With a PIN set, Save, Reset Defaults, firmware Install, the test buttons and disco ask for it once per browser session. Status stays visible to everyone. Bulk buttons send this PIN to the other tallies, so use the same PIN across them. Forgotten PIN: hold BOOT on the unit for 3 seconds to unlock it for 10 minutes, or 10 seconds to factory reset.</p>
</div>
<input type="hidden" id="pinField" name="pin">

<div class="btns"><button type="submit" style="flex:2">Save</button><button type="button" style="flex:1;background:#c00;color:#fff" onclick="resetDefaults()">Reset Defaults</button></div>
<p class="note" style="text-align:center">Saving applies Configuration and System together. Network changes reboot the device.</p>
</div>
</form>
<div class="ovl" id="ovl" onclick="if(event.target===this)closeModal()"><div class="modal" role="dialog" aria-modal="true"><h3 id="mTitle"></h3><div class="d" id="mDate"></div><ul id="mNotes"></ul><p id="mText" class="hide"></p><input type="text" id="mPin" class="masked hide" maxlength="16" autocomplete="off" autocorrect="off" autocapitalize="off" spellcheck="false" style="margin-bottom:14px"><div class="btns" id="mBtns"></div></div></div>
<footer>&copy; 2026 <a href="https://videowalrus.com">Video Walrus</a> &middot; <span id="build"></span></footer>
</div>
<div id="discoOverlay" class="disco-overlay"><div class="disco-text">DISCO MODE<br>ACTIVATED</div><button type="button" class="disco-cancel" onclick="stopDisco()">STOP THE PARTY</button></div>
<script>
var apMode=false,roMode=false,curFw='',devices=[],upd=null,testHeld=0;
function $(i){return document.getElementById(i)}
function esc(s){return String(s).replace(/[&<>"']/g,function(c){return '&#'+c.charCodeAt(0)+';'})}
function j(u){return fetch(u).then(function(r){return r.json()})}
var lock={pinSet:false,phys:false};
function getPin(){try{return sessionStorage.getItem('pin')||''}catch(e){return ''}}
function setPin(v){try{if(v)sessionStorage.setItem('pin',v);else sessionStorage.removeItem('pin')}catch(e){}}
function needPin(){return lock.pinSet&&!lock.phys&&!getPin()}
function addPin(u,p){return p?u+(u.indexOf('?')>=0?'&':'?')+'pin='+encodeURIComponent(p):u}
function withPin(u){if(!lock.pinSet||lock.phys)return u;return addPin(u,getPin())}
function withPinAny(u){return addPin(u,getPin())}
function askPin(then,msg){showModal(msg?'Confirm PIN':'Settings locked','',null,msg||'Enter the PIN to continue.',[['Unlock',function(){go()},''],['Cancel',closeModal,'background:var(--field);color:var(--text)']]);
var f=$('mPin');f.classList.remove('hide');f.value='';f.placeholder='PIN';setTimeout(function(){f.focus()},50);
function go(){var v=f.value;if(!v)return;j('/api/unlock?pin='+encodeURIComponent(v)).then(function(r){if(r.ok){setPin(v);closeModal();then();}else{f.value='';f.placeholder='Wrong PIN, try again';}}).catch(function(){});}
f.onkeydown=function(e){if(e.key==='Enter'){e.preventDefault();go();}};}
function guarded(fn){if(needPin())askPin(fn);else fn();}
function jp(u){return j(withPin(u)).then(function(r){if(r&&r.error==='locked'){setPin('');askPin(function(){});throw 'locked';}return r;})}
function typing(f,row,cb){var has=$(f).value.length>0;$(row).classList.toggle('hide',!has);if(!has){$(cb).checked=false;$(f).classList.add('masked');}}
function jt(u,ms){var ac=window.AbortController?new AbortController():null;var t=ac?setTimeout(function(){ac.abort()},ms):null;return fetch(u,ac?{signal:ac.signal,cache:'no-store'}:{cache:'no-store'}).then(function(r){if(t)clearTimeout(t);return r.json()})}
function setText(id,t){var e=$(id);if(e&&e.textContent!==t)e.textContent=t}
function setClass(e,c){if(e&&e.className!==c)e.className=c}
function age(ms){var a=ms/1000;return a<1?'now':a<60?Math.round(a)+'s ago':Math.round(a/60)+'m ago'}
function applyTally(d){if(!d||!d.tally)return;var s=d.tally,c='tally-'+s.toLowerCase();setText('tallyState',s);setClass($('tallyState'),c);if(document.body.className!==c)document.body.className=c;if('text' in d)setText('tallyText',d.text||'-');}
function applyStatus(s){if(!s)return;applyTally(s);var c=s.connection||'',cls=c.indexOf('Ethernet')>=0?'conn-eth':c.indexOf('WiFi')>=0?'conn-wifi':c.indexOf('AP')>=0?'conn-ap':'';
setText('conn',c);setClass($('conn'),cls);setText('ip',s.ip||'');setText('hdrIp',s.ip?'\u00b7 '+s.ip:'');setText('hdrConn',c?'\u00b7 '+c:'');setClass($('hdrConn'),cls);
var tl;if(s.src==1){setText('tslLabel','Tally Arbiter:');tl=(s.taConn?'Connected to ':'Not connected to ')+s.taHost+':'+s.taPort+(s.taDevice?' \u00b7 '+s.taDevice:' \u00b7 no device')+(s.pkts?' \u00b7 '+s.pkts+' updates \u00b7 '+age(s.age):'');$('tsl').style.color=s.taConn?'':'#ff6b6b';}
else{setText('tslLabel','TSL data:');tl=s.pkts?s.pkts+' pkts from '+s.from+' \u00b7 '+age(s.age):'No data for this address yet';$('tsl').style.color=s.pkts?'':'var(--muted)';}setText('tsl',tl);
lock.pinSet=!!s.pinSet;lock.phys=!!s.phys;$('pinclearRow').classList.toggle('hide',!s.pinSet);if(!$('newpin').value)$('newpin').placeholder=s.pinSet?'blank = keep current PIN':'4 to 16 characters, blank = no lock';
setText('lockst',!s.pinSet?'Off':(s.phys?'Unlocked at the device, '+Math.max(1,Math.ceil(s.physLeft/60))+' min left':'PIN required'));$('lockst').style.color=s.pinSet&&s.phys?'#e0a800':'';
setText('clk',s.time>1750000000?new Date(s.time*1000).toISOString().replace('T',' ').slice(0,19):'not synced');$('clk').style.color=s.time>1750000000?'':'var(--muted)';}
function poll(){j('/status').then(applyStatus).catch(function(){})}
function testOn(s){if(needPin()){askPin(function(){});return;}testHeld=1;jp('/test?state='+s).then(applyTally).catch(function(){})}
function testOff(){if(!testHeld)return;testHeld=0;jp('/test?restore=1').then(applyTally).catch(function(){})}
function discoverDevices(){$('deviceList').innerHTML='<p class="no-devices">Scanning...</p>';
j('/discover').then(function(d){devices=d.devices||[];var h='';
if(!devices.length){h='<p class="no-devices">No other devices found</p>';}
else{devices.forEach(function(dev){var id=dev.ip.replace(/\./g,'-');
h+='<div class="device-item"><div class="device-status off" id="status-'+id+'"></div><div class="device-info"><div class="device-name">'+esc(dev.hostname)+'</div><div class="device-details">TSL:'+dev.tslAddress+' | '+esc(dev.ip)+'</div></div><a href="http://'+esc(dev.ip)+'/" target="_blank" class="device-link">Open</a></div>';});}
$('deviceList').innerHTML=h;updateDeviceStatuses();}).catch(function(){$('deviceList').innerHTML='<p class="no-devices">Scan failed</p>';});}
function updateDeviceStatuses(){devices.forEach(function(dev){j('http://'+dev.ip+'/status').then(function(d){setClass($('status-'+dev.ip.replace(/\./g,'-')),'device-status '+String(d.tally).toLowerCase());}).catch(function(){});});}
function bulkTest(state){guarded(function(){devices.forEach(function(dev){fetch(withPinAny('http://'+dev.ip+'/test?state='+state)).catch(function(){});});jp('/test?state='+state).then(applyTally).catch(function(){});});}
function loadConfig(){j('/api/config').then(function(c){apMode=!!c.apMode;roMode=apMode;if(apMode)showTab('sys');
setText('hostTitle',c.hostname);setText('host',c.hostname);setText('mac',c.mac);setText('fw',c.fw);setText('build','build '+c.build);
$('apRow').classList.toggle('hide',!apMode);setText('apSsid',c.apSsid);
$('apNote').textContent='If Ethernet and WiFi both fail, the device starts an access point: '+c.apSsid+' (password: '+c.apPass+')';
$('tslAddr').value=c.tslAddr;$('tslMcast').value=c.mcast;$('tslPort').value=c.port;$('maxBright').value=c.maxBright;$('ledAnim').value=c.ledAnim;
$('src').value=c.src;$('taHost').value=c.taHost;$('taPort').value=c.taPort;fillDevices(c.taDevices||[],c.taDevice);
$('hostname').value=c.hostname;$('dhcp').value=c.dhcp?'1':'0';$('sip').value=c.ip;$('gw').value=c.gw;$('sn').value=c.sn;$('dns').value=c.dns;
$('wifiEn').value=c.wifiEn?'1':'0';$('wifiSSID').value=c.ssid;$('wifiPass').value='';$('wifiPass').placeholder=c.passSet?'(unchanged)':'';lock.pinSet=!!c.pinSet;
if(apMode){document.querySelectorAll('#cfgForm input').forEach(function(e){if(e.type!=='checkbox')e.readOnly=true;});}
toggleSource();toggleIP();toggleWifi();whatsNew(c);}).catch(function(){});}
function fillDevices(list,cur){var sel=$('taDevice'),h='<option value="unassigned">Assign from Tally Arbiter</option>',seen=false;
list.forEach(function(d){if(!d.id)return;if(d.id===cur)seen=true;h+='<option value="'+esc(d.id)+'">'+esc(d.name||d.id)+'</option>';});
if(cur&&cur!=='unassigned'&&!seen)h+='<option value="'+esc(cur)+'">'+esc(cur)+' (not on server)</option>';sel.innerHTML=h;sel.value=cur||'unassigned';}
function taRefresh(){var b=$('taRefreshBtn');b.disabled=true;j('/api/config').then(function(c){fillDevices(c.taDevices||[],$('taDevice').value);b.disabled=false;}).catch(function(){b.disabled=false;});}
function toggleSource(){var ta=$('src').value==='1';$('tslCard').classList.toggle('hide',ta);$('taCard').classList.toggle('hide',!ta);
['tslAddr','tslMcast','tslPort'].forEach(function(i){$(i).required=!ta});['taHost','taPort'].forEach(function(i){$(i).required=ta});}
function taScan(){var l=$('taList'),b=$('taScanBtn');l.classList.add('show');l.innerHTML='<p class="note">Searching...</p>';b.disabled=true;
j('/api/ta-scan').then(function(d){b.disabled=false;var h='';(d.servers||[]).forEach(function(sv){h+='<div class="wifi-item" onclick="pickTa(this)" data-host="'+esc(sv.ip)+'" data-port="'+sv.port+'"><span>'+esc(sv.host)+'</span><span>'+esc(sv.ip)+':'+sv.port+'</span></div>';});
l.innerHTML=h||'<p class="note">No Tally Arbiter server found (it announces itself over mDNS while running)</p>';}).catch(function(){b.disabled=false;l.innerHTML='<p class="note">Search failed</p>';});}
function pickTa(el){$('taHost').value=el.getAttribute('data-host');$('taPort').value=el.getAttribute('data-port');$('taList').classList.remove('show');}
function applyTheme(t){if(t==='light'||t==='dark')document.documentElement.setAttribute('data-theme',t);else document.documentElement.removeAttribute('data-theme');document.querySelectorAll('.th').forEach(function(b){b.classList.toggle('on',b.getAttribute('data-th')===t)});}
function setTheme(t){try{localStorage.setItem('theme',t)}catch(e){}applyTheme(t);}
function showTab(t){document.querySelectorAll('.tab').forEach(function(b){b.classList.toggle('on',b.getAttribute('data-tab')===t)});document.querySelectorAll('.panel').forEach(function(p){p.classList.toggle('on',p.id==='tab-'+t)});try{localStorage.setItem('tab',t)}catch(e){}}
function validateTabs(){var f=$('cfgForm');if(!f.checkValidity()){var bad=f.querySelector(':invalid');if(bad){var p=bad.closest('.panel');if(p)showTab(p.id.slice(4));bad.reportValidity();}return false;}
var np=$('newpin').value,clr=$('pinclear').checked;function remember(){if(clr)setPin('');else if(np)setPin(np);}
var lockChange=(np||clr)&&lock.pinSet&&!lock.phys;
if(needPin()||lockChange){askPin(function(){$('pinField').value=getPin();remember();f.submit();},lockChange?'Enter the current PIN to '+(clr?'remove the lock.':'change the PIN.'):null);return false;}
$('pinField').value=getPin();remember();return true;}
function toggleIP(){$('ipFields').classList.toggle('hide',$('dhcp').value!=='0')}
function toggleWifi(){$('wifiFields').classList.toggle('hide',$('wifiEn').value!=='1')}
var wifiTimer=null;
function wifiScan(start){var l=$('wifiList'),b=$('scanBtn');
if(start!==false){start=true;clearTimeout(wifiTimer);l.classList.add('show');l.innerHTML='<p class="note">Scanning...</p>';b.disabled=true;b.textContent='Scanning...';}
j('/api/wifi-scan'+(start?'?start=1':'')).then(function(d){
if(d.scanning){wifiTimer=setTimeout(function(){wifiScan(false)},750);return;}
b.disabled=false;b.textContent='Scan for Networks';
if(!d.networks){l.innerHTML='<p class="note">Scan failed</p>';return;}
var seen={},h='';d.networks.sort(function(a,c){return c.rssi-a.rssi}).forEach(function(w){
if(!w.ssid||seen[w.ssid])return;seen[w.ssid]=1;var q=w.rssi>=-55?4:w.rssi>=-65?3:w.rssi>=-75?2:1;
h+='<div class="wifi-item" onclick="pickWifi(this)" data-ssid="'+esc(w.ssid)+'"><span>'+esc(w.ssid)+'</span><span>'+(w.enc?'&#128274; ':'')+'&#9679;'.repeat(q)+'&#9675;'.repeat(4-q)+'</span></div>';});
l.innerHTML=h||'<p class="note">No networks found</p>';
}).catch(function(){b.disabled=false;b.textContent='Scan for Networks';l.innerHTML='<p class="note">Scan failed</p>';});}
function pickWifi(el){$('wifiSSID').value=el.getAttribute('data-ssid');$('wifiList').classList.remove('show');}
function resetDefaults(){if(confirm('Reset all settings to factory defaults?\n\nThis will erase all configuration and reboot the device.')){guarded(function(){window.location.href=withPin('/reset');});}}
function showModal(title,date,notes,text,btns){$('mPin').classList.add('hide');setText('mTitle',title);setText('mDate',date||'');var u=$('mNotes');u.innerHTML='';(notes||[]).forEach(function(n){var li=document.createElement('li');li.textContent=n;u.appendChild(li);});
var t=$('mText');t.classList.toggle('hide',!text);t.textContent=text||'';var b=$('mBtns');b.innerHTML='';(btns||[]).forEach(function(x){var e=document.createElement('button');e.type='button';e.textContent=x[0];if(x[2])e.setAttribute('style',x[2]);e.onclick=x[1];b.appendChild(e);});
b.classList.toggle('hide',!(btns&&btns.length));$('ovl').classList.add('show');}
function closeModal(){$('ovl').classList.remove('show');}
function offerUpdate(d){upd=d;$('updateNotice').classList.remove('hide');setText('latest',d.latest);
showModal('Update available: v'+String(d.latest).replace(/^v/,''),(d.date?'Released '+d.date+' \u00b7 ':'')+'you have v'+d.current,d.notes,null,[['Install',function(){closeModal();guarded(function(){installUpdate(true);});},'background:#4CAF50;color:#fff'],['Later',closeModal,'background:var(--field);color:var(--text)']]);}
function checkUpdate(){$('updateNotice').classList.add('hide');j('/api/check-update').then(function(d){setText('fw',d.current);
if(d.updateAvailable)offerUpdate(d);else if(d.error)showModal('Update check failed','Version '+d.current,null,d.error,[['Close',closeModal]]);else showModal('Firmware is up to date','Version '+d.current,null,'This is the latest release.',[['Close',closeModal]]);
}).catch(function(){showModal('Update check failed','',null,'The device could not fetch the release manifest.',[['Close',closeModal]]);});}
function installUpdate(fromModal){if(!fromModal&&!confirm('Install firmware update?\n\nThe device will download the new firmware and reboot.'))return;
var v=upd?upd.latest:'';showModal('Updating firmware'+(v?' to v'+String(v).replace(/^v/,''):''),'',null,'Downloading and flashing. The device reboots when done and this page reloads.',[]);
$('updateNotice').innerHTML='<span style="color:#ff6b6b">Updating... device will reboot</span>';fetch(withPin('/api/update')).catch(function(){});
var t0=Date.now();setTimeout(function w(){if(Date.now()-t0>120000){showModal('Update taking longer than expected','',null,'The device has not come back with a new version yet. Reload to check.',[['Reload',function(){location.reload()}]]);return;}
jt('/api/update-status',2500).then(function(c){if(c.fw&&c.fw!==curFw){location.reload();}else if(!c.inProgress&&c.error){showModal('Update failed','',null,c.error,[['Close',closeModal]]);$('updateNotice').innerHTML='<span style="color:#ff6b6b">Update failed</span>';}else setTimeout(w,2000);}).catch(function(){setTimeout(w,2000);});},5000);}
function whatsNew(c){curFw=c.fw;var seen=null;try{seen=localStorage.getItem('seenFw')}catch(e){}if(seen===c.fw||apMode)return;
j('/api/check-update').then(function(d){try{localStorage.setItem('seenFw',c.fw)}catch(e){}
if(d.updateAvailable)offerUpdate(d);else if(d.notes&&d.notes.length&&String(d.latest).replace(/^v/,'')===d.current)showModal("What's new in v"+d.current,d.date?'Released '+d.date:'',d.notes,null,[['Close',closeModal]]);}).catch(function(){});}
var discoBuffer='',discoTimer=null;
document.addEventListener('keydown',function(e){if(isField(e.target))return;discoBuffer=(discoBuffer+String(e.key).toLowerCase()).slice(-5);if(discoBuffer==='disco')startDisco();});
function startDisco(){guarded(function(){$('discoOverlay').classList.add('active');fetch(withPin('/disco?duration=30')).catch(function(){});
var go=function(){devices.forEach(function(dev){fetch(withPinAny('http://'+dev.ip+'/disco?duration=30')).catch(function(){});});};
if(devices.length)go();else j('/discover').then(function(d){devices=d.devices||[];go();}).catch(function(){});
clearTimeout(discoTimer);discoTimer=setTimeout(function(){$('discoOverlay').classList.remove('active');},30000);});}
function stopDisco(){clearTimeout(discoTimer);$('discoOverlay').classList.remove('active');fetch(withPin('/disco-stop')).catch(function(){});
devices.forEach(function(dev){fetch(withPinAny('http://'+dev.ip+'/disco-stop')).catch(function(){});});}
var lastUser=0,lastTarget=null;
function isField(t){return !!t&&/^(INPUT|TEXTAREA)$/.test(t.tagName)&&t.type!=='checkbox';}
function isCtl(t){return !!t&&/^(INPUT|SELECT|TEXTAREA|BUTTON)$/.test(t.tagName);}
function tapOn(t){if(!lastTarget||Date.now()-lastUser>1000)return false;if(lastTarget===t)return true;var l=lastTarget.closest?lastTarget.closest('label'):null;return !!l&&(l.htmlFor===t.id||l.contains(t));}
document.addEventListener('mousedown',function(e){lastUser=Date.now();lastTarget=e.target;if(isField(e.target)&&e.target.readOnly)e.target.readOnly=false;},true);
document.addEventListener('keydown',function(){lastUser=Date.now();lastTarget=null;},true);
document.addEventListener('focusin',function(e){var t=e.target;if(!isField(t))return;var ok=isCtl(e.relatedTarget)||tapOn(t)||(!lastTarget&&Date.now()-lastUser<1000);if(ok){if(t.readOnly)t.readOnly=false;}else{t.blur();}});
document.addEventListener('focusout',function(e){var t=e.target;if(roMode&&isField(t))t.readOnly=true;});
var th0='auto';try{th0=localStorage.getItem('theme')||'auto'}catch(e){}applyTheme(th0);
var t0='op';try{t0=localStorage.getItem('tab')||'op'}catch(e){}if(!$('tab-'+t0))t0='op';showTab(t0);
loadConfig();poll();setInterval(poll,1000);discoverDevices();setInterval(updateDeviceStatuses,5000);
if(/[?&]saved=1/.test(location.search)){var tst=document.createElement('div');tst.className='toast';tst.textContent='Settings saved and applied';document.body.appendChild(tst);
setTimeout(function(){tst.style.opacity='0';setTimeout(function(){tst.remove()},500)},2500);history.replaceState(null,'',location.pathname);}
</script></body></html>
)HTML";
