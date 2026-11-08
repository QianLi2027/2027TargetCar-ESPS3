//
// HTTP + WebSocket 服务器。网页直接内嵌在固件里(不依赖文件系统)。
//
// 协议:浏览器每 200ms 发一条文本,逗号分隔共 4 个字段:
//   "导轨转速,导轨方向,6020方向,6020挡位"   例:"3500,STOP,FORWARD,2"
//     rail_dir : LEFT / STOP / RIGHT
//     gm_dir   : FORWARD / STOP / REVERSE
//     gm_gear  : 1=25rpm / 2=45 / 3=90 / 4=120
//   既是控制指令,也是心跳。
//

#include "web_server.h"

#include <cstdlib>
#include <cstring>
#include <strings.h>   // strcasecmp
#include <unistd.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "control_state.h"

namespace
{
    constexpr const char* TAG = "Web";

    // ─────────────────────── 内嵌网页 ───────────────────────
    const char* kIndexHtml = R"HTMLPAGE(<!DOCTYPE html>
<html lang="zh">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>重庆大学千里战队靶车控制台</title>
<style>
  :root{--line:#262b33;--fg:#e8eaed;--dim:#8b94a3;
        --blue:#2563eb;--green:#22c55e;--red:#dc2626}
  *{box-sizing:border-box;-webkit-tap-highlight-color:transparent}
  /* 整页背景：底层是队徽图，上面盖一层深色遮罩保证文字可读 */
  html{background:linear-gradient(rgba(15,17,21,.30),rgba(15,17,21,.55)),
       #0f1115 url(data:image/jpeg;base64,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QDFFN1BUAxRTdQVAMUU3UFQDFFN1BUA1XQroogSuhXRQEroV0UBK6FdFASuhXRQEroV0UBK6FdFASuhXRQEroV0UBK6FdFASuhXRQEroV0UBK6FdFASuhXRQEroV0UBK6FdFASuhXRQEroV0UBK6FdFASuhXRQEroV0UBK6FdFASuhXRQEroV0UBK6FdFASuhXRQEroV0UBK6FdFASuhXRQEroV0UBK6FdFASuhXRQEroV0UBK6FdFASuhXRQEroV0UBK6FdFASuhXRQEroV0UBK6FdFASuhXRQEroV0UBK6FdFASuhXRQEroV0UBoAQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAo0AyNAMjQDI0AyNAMjQDI0AyNAMjQDI0AyNAMjQDI0AyNAMjQDI0AyNAMjQDI0AyNAMjQDI0AyNAMjQDI0AyNAMjQDI0AyNAMjQDI0AyNAMjQDI0AyNAMjQDI0AyNAMjQDI0AyNAMjQDI0AyNAMjQDVQVCgrUqCoUCpUFQoFSoKhQKlQVCgVKgqFAqVBUKBUqCoUCpUFQoFSoKhQKlQVCgVKgqFAqVBUKBUqCoUCpUFQoFSoKhQKlQVCgVKgqFAqVBUKBUqCoUCpUFQoFSoKhQKlQVCgVKgqFAqVBUKBUqCoUCpUFQoFSoKhQKlQVCgVKgqFAqVBUKBUqCoUCpUFQoFSoKhQKlQVCgVKgqFAqVBUKBUqCoUCpUFQoFSoKhQKlQVCgVKgqFAqVBUKBUqCoUCpUFQoFWilqSpQhKKWpKkEopakqQSilqSpBKKWpKkEopakqQSilqSpBKKWpKkEopakqQSilqSpBKKWpKkEopakqQSilqSpBKKWpKkEopakqQSilqSpBKKWpKkEopakqQSilqSpBKKWpKkEopakqQSilqSpBKKWpKkEopakqQSilqSpBKKWpKkEopakqQSilqSpBKKWpKkEopakqQSilqSpBKKWpKkEopakqQSilqSpBKKWpKkEopakqQSilqSpBKKWpKkEopakqQSilqSpBKKWpKkEopakqQSilqSpBKKWpKkEopakqQaFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKgtlhUFssKtdCujQKs10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAM10K6NAKKCEFAQUBBQEFAQUBBQEFAQUBBQEFAQUBBQEFAQUBBQEFAQUBBQEFAQUBBQEFAQUBBQEFAQUBBQEFAQUBBQEFAQUBBQEFAQUBBQEFAQUBBQEFAQUBBQEFAQUBaKWihFSilopBUopaKCpRS0UFSilooKlFLRQVKKWigqUUtFBUopaKCpRS0UFSilooKlFLRQVKKWigqUUtFBUopaKCpRS0UFSilooKlFLRQVKKWigqUUtFBUopaKCpRS0UFSilooKlFLRQVKKWigqUUtFBUopaKCpRS0UFSilooKlFLRQVKKWigqUUtFBUopaKCpRS0UFSilooKlFLRQVKKWigqUUtFBUopaKCpRS0UFSilooKlFLRQVKKWigqUUtFBUopaKCtUU1RSFWaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooGaKaooFopaKBKKWigSilooEopaKBKKWigSilooEopaKBKKWigSilooEopaKBKKWigSilooEopaKBKKWigSilooEopaKBKKWigSilooEopaKBKKWigSilooEopaKBKKWigSilooEopaKBKKWigSilooEopaKBKKWigSilooEopaKBKKWigSilooEopaKBKKWigSilooEopaKBKKWigSilooEopaKBKKWigSilooEopaKBKKWigSilooGqKaEe0M0U0HsZopoPYzRTQexmimg9jNFNB7GaKaD2M0U0HsZopoPYzRTQexmimg9jNFNB7GaKaD2M0U0HsZopoPYzRTQexmimg9jNFNB7GaKaD2M0U0HsZopoPYzRTQexmimg9jNFNB7GaKaD2M0U0HsZopoPYzRTQexmimg9jNFNB7GaKaD2M0U0HsZopoPYzRTQexmimg9jNFNB7GaKaD2M0U0HsZopoPYzRTQexmimg9jNFNB7GaKaD2M0U0HsZopoPYC0UK1BaKEVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCoLRQVBaKCqN0UDA3RQMDdFAwN0UDA3RQMDdFAwN0UDA3RQMDdFAwN0UDA3RQMDdFAwN0UDA3RQMDdFAwN0UDA3RQMDdFAwN0UDA3RQMDdFAwN0UDA3RQMDdFAwN0UDA3RQMDdFAwN0UDA3RQMDdFAwN0UDA3RQMDdFAwN0UDA3RQMDdFAwN0UDA3RQMDdFAwN0UDA3RQMDdFAwN0UDA3RQMDdFAtFNCSs0U0BWaKaArNFNAVmimgKzRTQFZopoCs0U0BWaKaArNFNAVmimgKzRTQFZopoCs0U0BWaKaArNFNAVmimgKzRTQFZopoCs0U0BWaKaArNFNAVmimgKzRTQFZopoCs0U0BWaKaArNFNAVmimgKzRTQFZopoCs0U0BWaKaArNFNAVmimgKzRTQFZopoCs0U0BWaKaArNFNAVmimgKzRTQFZopoCs0U0BWaKaAoFFIQBRQAUUAFFABRQAUUAFFABRQAUUAFFABRQAUUAFFABRQAUUAFFABRQAUUAFFABRQAUUAFFABRQAUUAFFABRQAUUAFFABRQAUUAFFABRQAUUAFFABRQAUUAFFABRQAUUAFFABRQAUUAFFABRQAUUDdFLRStRUopaKKVKKWiilSiloopUopaKKVKKWiilSiloopUopaKKVKKWiilSiloopUopaKKVKKWiilSiloopUopaKKVKKWiilSiloopUopaKKVKKWiilSiloopUopaKKVKKWiilSiloopUopaKKVKKWiilSiloopUopaKKVKKWiilSiloopUopaKKVKKWiilSiloopUopaKKVKKWiilSiloopUopaKKVKKWiilSiloopUopaKKVKKWiilSiloopUopaKKVKKWiilSiloopUopaKKVKKWiilf/2Q==) center center/cover no-repeat fixed}
  body{font-family:system-ui,-apple-system,sans-serif;margin:0;padding:16px 16px 40px;
       background:transparent;color:var(--fg);max-width:560px;margin:0 auto}
  header{margin:8px 0 14px}
  h1{font-size:19px;font-weight:650;margin:0}

  .status{display:flex;align-items:center;gap:14px;background:rgba(25,28,34,.55);
          backdrop-filter:blur(6px);-webkit-backdrop-filter:blur(6px);
          border-radius:16px;padding:16px 18px;margin-bottom:14px;
          border:1px solid transparent;transition:border-color .2s}
  .status.ok{border-color:rgba(34,197,94,.35)}
  .status.bad{border-color:rgba(220,38,38,.35)}
  .dot{width:13px;height:13px;border-radius:50%;flex:0 0 auto;background:#4b5563;transition:background .2s}
  .dot.ok{background:var(--green);animation:pulse 1.6s infinite}
  .dot.bad{background:var(--red)}
  @keyframes pulse{0%{box-shadow:0 0 0 0 rgba(34,197,94,.55)}
                   70%{box-shadow:0 0 0 11px rgba(34,197,94,0)}
                   100%{box-shadow:0 0 0 0 rgba(34,197,94,0)}}
  .stxt{flex:1 1 auto;min-width:0}
  .stitle{font-size:15px;font-weight:650;margin-bottom:3px}
  .ssub{font-size:12px;color:var(--dim)}

  .panel{background:rgba(25,28,34,.55);backdrop-filter:blur(6px);
         -webkit-backdrop-filter:blur(6px);border-radius:16px;padding:20px;margin-bottom:14px}
  .panel h2{font-size:15px;font-weight:600;margin:0 0 4px}
  .panel .sub{font-size:12px;color:var(--dim);margin-bottom:18px}

  .btn{padding:17px 10px;font-size:16px;font-weight:650;border:0;border-radius:13px;
       background:#232830;color:var(--fg);cursor:pointer;transition:background .12s,transform .06s}
  .btn:active{transform:scale(.97)}
  .btn.active{background:var(--blue);color:#fff;box-shadow:0 0 0 2px rgba(37,99,235,.35)}

  .seg{display:grid;gap:9px;margin-top:10px}
  .seg.c3{grid-template-columns:1fr 1fr 1fr}
  .seg.c4{grid-template-columns:repeat(4,1fr)}
  .seg.c2{grid-template-columns:1fr 1fr}
  .seg .btn.fwd.active{background:var(--green);box-shadow:0 0 0 2px rgba(34,197,94,.3)}
  .seg .btn.rev.active{background:var(--red);box-shadow:0 0 0 2px rgba(220,38,38,.3)}
  .seg .btn.stop.active{background:#4b5563;box-shadow:0 0 0 2px rgba(75,85,99,.35)}

  .grp{margin-bottom:22px}
  .grp:last-child{margin-bottom:0}
  .labelrow{display:flex;align-items:baseline;justify-content:space-between;margin-bottom:10px}
  .labelrow .k{font-size:13px;color:var(--dim)}

  .fld{display:flex;flex-direction:column;gap:6px;font-size:13px;color:var(--dim)}
  .fld input{background:#101319;border:1px solid var(--line);border-radius:10px;
             padding:14px 13px;color:var(--fg);font-size:20px;width:100%;
             font-variant-numeric:tabular-nums;font-family:inherit}
  .fld input:focus{outline:none;border-color:var(--blue)}

  .off{opacity:.32;pointer-events:none}

  /* 控制权条 */
  .ctrlcard{display:flex;align-items:center;gap:12px;margin-top:10px;
            padding:14px 16px;border-radius:14px;background:rgba(25,28,34,.55);
            border:1px solid #232830;backdrop-filter:blur(6px)}
  .ctrlcard.on{border-color:rgba(34,197,94,.55);box-shadow:0 0 0 2px rgba(34,197,94,.12)}
  .ctrlcard .ctxt{flex:1;min-width:0}
  .ctrlcard .ctitle{font-size:15px;font-weight:600;color:var(--fg)}
  .ctrlbtn{padding:10px 18px;border-radius:10px;border:0;font-size:14px;font-weight:600;
           background:#2563eb;color:#fff;cursor:pointer;white-space:nowrap}
  .ctrlbtn.on{background:#16a34a}
  .ctrlbtn:active{transform:scale(.97)}
  .modecard{display:flex;align-items:center;gap:12px;margin-top:10px;
            padding:14px 16px;border-radius:14px;background:rgba(25,28,34,.55);
            border:1px solid #232830;backdrop-filter:blur(6px)}
  .modecard .ctxt{flex:1;min-width:0}
  .modecard .ctitle{font-size:15px;font-weight:600;color:var(--fg)}
  .modecard .seg{min-width:210px;margin:0}
</style>
</head>
<body>

<header><h1>重庆大学千里战队靶车控制台</h1></header>

<div class="status" id="statusCard">
  <div class="dot" id="dot"></div>
  <div class="stxt">
    <div class="stitle" id="stitle">正在连接…</div>
    <div class="ssub"  id="ssub">连接开发板中</div>
  </div>
</div>

<!-- ─────────── 控制权 ─────────── -->
<div class="ctrlcard" id="ctrlCard">
  <div class="ctxt">
    <div class="ctitle" id="ctrlTitle">未接管控制</div>
  </div>
  <button class="ctrlbtn" id="ctrlBtn">接管控制</button>
</div>

<!-- ─────────── 工作模式 ─────────── -->
<div class="modecard" id="modeCard">
  <div class="ctxt">
    <div class="ctitle" id="modeTitle">默认模式</div>
  </div>
  <div class="seg c2">
    <button class="btn active" id="modeDef" data-v="0">默认</button>
    <button class="btn"        id="modeNet" data-v="1">无线控制</button>
  </div>
</div>

<!-- ─────────── 导轨 ─────────── -->
<div class="panel" id="railPanel">
  <h2>导轨 · 2×M3508</h2>

  <div class="grp">
    <div class="fld">转速 (0 ~ 9000 rpm)
      <input id="railSpeedIn" type="number" min="0" max="9000" step="50" value="3500">
    </div>
  </div>
  <button class="btn" id="presetRM" style="margin-top:9px">RMUL / RMUC 完整系统 · 8000</button>
  <div class="seg c3">
    <button class="btn" id="railLeft"  data-v="LEFT">◀ 左</button>
    <button class="btn" id="railStop"  data-v="STOP">停</button>
    <button class="btn" id="railRight" data-v="RIGHT">右 ▶</button>
  </div>
</div>

<!-- ─────────── 主体 ─────────── -->
<div class="panel" id="gmPanel">
  <h2>主体 · GM6020</h2>

  <div class="grp">
    <div class="labelrow"><span class="k">方向</span></div>
    <div class="seg c3" id="gmDir">
      <button class="btn fwd"         data-v="FORWARD">正转</button>
      <button class="btn stop active" data-v="STOP">停</button>
      <button class="btn rev"         data-v="REVERSE">反转</button>
    </div>
  </div>
  <div class="grp">
    <div class="labelrow"><span class="k">转速挡位</span></div>
    <div class="seg c4" id="gmGear">
      <button class="btn active" data-v="1">25 ⭐</button>
      <button class="btn"        data-v="2">45</button>
      <button class="btn"        data-v="3">90</button>
      <button class="btn"        data-v="4">120</button>
    </div>
    <div class="sub" style="margin-top:8px">25 = RMUL / RMUC 考核值 · 45 低 · 90 中 · 120 高</div>
  </div>
</div>

<script>
var $ = function(id){ return document.getElementById(id); };

// 方向用可读词,方便对着串口日志看
var st  = { railRpm:3500, railDir:'STOP', gmDir:'STOP', gmGear:1 };

// 控制权：同一时刻只有一个网页的指令会生效
var hasCtrl = false;
// 工作模式：0 = 默认（导轨自动来回），1 = 无线控制
var curMode = 0;

// 统一刷新界面：控制权 + 模式 共同决定"面板能不能点"
function refreshUi(){
  // 控制权卡片
  $('ctrlCard').classList.toggle('on', hasCtrl);
  $('ctrlBtn').classList.toggle('on', hasCtrl);
  $('ctrlBtn').textContent = hasCtrl ? '控制中' : '接管控制';
  $('ctrlTitle').textContent = hasCtrl ? '● 你正在控制' : '未接管控制';

  // 只有在"有控制权 + 无线控制"时，两个面板才可以点
  var canOperate = hasCtrl && (curMode === 1);
  $('railPanel').classList.toggle('off', !canOperate);
  $('gmPanel').classList.toggle('off', !canOperate);
}

function setCtrl(on){
  hasCtrl = on;
  refreshUi();
}

// 模式按钮：直接用内联样式，保证颜色一定跟着切换
function paintModeBtn(id, on){
  var b = $(id);
  if (!b) return;
  b.style.background = on ? '#2563eb' : '#232a35';
  b.style.color      = on ? '#ffffff' : '#c7cedb';
  b.style.boxShadow  = on ? '0 0 0 2px rgba(37,99,235,.45)' : 'none';
}

function setMode(m){
  curMode = (m === 0) ? 0 : (m === 1 ? 1 : 2);   // 2 = 未选择
  paintModeBtn('modeDef', curMode === 0);
  paintModeBtn('modeNet', curMode === 1);
  if (curMode === 0) {
    $('modeTitle').textContent = '默认模式';
  } else if (curMode === 1) {
    $('modeTitle').textContent = '无线控制';
  } else {
    $('modeTitle').textContent = '未选择模式';
  }
  refreshUi();
}
var ws = null, connecting = false, failCount = 0;

function clamp(v, lo, hi){ return v < lo ? lo : (v > hi ? hi : v); }

// 只发 4 个控制字段（PID / 前馈参数写死在固件里）
function payload(){
  return st.railRpm + ',' + st.railDir + ',' + st.gmDir + ',' + st.gmGear;
}

function render(){
  $('railSpeedIn').value = st.railRpm;

  var i, kids;
  kids = document.querySelectorAll('#railLeft,#railStop,#railRight');
  for (i = 0; i < kids.length; i++) kids[i].classList.toggle('active', kids[i].getAttribute('data-v') === st.railDir);

  kids = $('gmDir').children;
  for (i = 0; i < kids.length; i++) kids[i].classList.toggle('active', kids[i].getAttribute('data-v') === st.gmDir);
  kids = $('gmGear').children;
  for (i = 0; i < kids.length; i++) kids[i].classList.toggle('active', kids[i].getAttribute('data-v') === String(st.gmGear));
}

function setStatus(kind, title, sub){
  $('statusCard').className = 'status' + (kind==='ok' ? ' ok' : kind==='bad' ? ' bad' : '');
  $('dot').className = 'dot' + (kind==='ok' ? ' ok' : kind==='bad' ? ' bad' : '');
  $('stitle').textContent = title;
  $('ssub').textContent   = sub;
}

// 只有持有控制权时才发控制指令，否则只发心跳（不改变状态）
function send(){
  if (!ws || ws.readyState !== 1) return;

  // 无线控制才发控制数据；默认模式由固件自己跑，这里只发心跳保活
  if (hasCtrl && curMode === 1) ws.send(payload());
  else                          ws.send('#PING');
}

function connect(){
  if (connecting) return;
  connecting = true;
  setStatus('', '正在连接开发板…', 'ws://' + location.host + '/ws');
  try { ws = new WebSocket('ws://' + location.host + '/ws'); }
  catch(e){ connecting = false; setStatus('bad', '无法建立连接', e.message); return; }

  ws.onopen  = function(){
    connecting = false; failCount = 0;
    setStatus('ok', '已连接到开发板', '指令收发中');
    ws.send('#STATE');     // 连上后主动查一次控制权/模式
    send();
  };
  ws.onmessage = function(e){
    var m = e.data;
    if (typeof m !== 'string') return;
    if (m === '#CTRL,1')        setCtrl(true);
    else if (m === '#CTRL,0')   setCtrl(false);
    else if (m === '#MODE,0')   setMode(0);
    else if (m === '#MODE,1')   setMode(1);
    else if (m === '#MODE,2')   setMode(2);   // 未选择
  };

  ws.onclose = function(){ connecting = false; failCount++; setCtrl(false);
    setStatus('bad', '未连接到开发板', '请连上热点 TARGET_CAR 后刷新 · 已重试 ' + failCount + ' 次');
    setTimeout(connect, 2000); };
  ws.onerror = function(){ try{ ws.close(); }catch(e){} };
}

/* ── 工作模式切换（只有控制者能改）── */
$('modeDef').onclick = function(){
  if (!hasCtrl) return;
  setMode(0);
  // 默认模式由 ESP32 自己跑状态机（回零 → 0.8s → 10%~90% 位置往复）
  if (ws && ws.readyState === 1) ws.send('#MODE,0');
};

$('modeNet').onclick = function(){
  if (!hasCtrl) return;
  // 切到无线控制时：先把导轨和靶车都置停，再切模式
  st.railDir = 'STOP';
  st.gmDir   = 'STOP';
  render();
  if (ws && ws.readyState === 1) ws.send(payload());   // 先发一次"全停"
  setMode(1);
  if (ws && ws.readyState === 1) ws.send('#MODE,1');
};

/* ── 导轨转速预设：RMUL / RMUC 完整系统 ── */
$('presetRM').onclick = function(){
  st.railRpm = 8000;
  $('railSpeedIn').value = 8000;
  render();
  send();                               // 满足条件（有控制权 + 无线控制）才会真的发
};

/* ── 接管控制 ── */
$('ctrlBtn').onclick = function(){
  if (!ws || ws.readyState !== 1) return;
  ws.send('#TAKE');
};

/* ── 导轨:输入式 ── */
function applyRail(){
  st.railRpm = clamp(parseInt($('railSpeedIn').value) || 3500, 0, 9000);
  $('railSpeedIn').value = st.railRpm;
  render(); send();
}
$('railSpeedIn').onchange = applyRail;
$('railSpeedIn').onblur   = applyRail;

/* 导轨方向:点哪个就是哪个,要停请点"停" */
function pickRailDir(v){
  st.railDir = v;
  render(); send();
}
$('railLeft').onclick  = function(){ pickRailDir('LEFT');  };
$('railRight').onclick = function(){ pickRailDir('RIGHT'); };
$('railStop').onclick  = function(){ st.railDir = 'STOP'; render(); send(); };

/* ── 6020 方向/挡位 ── */
$('gmDir').onclick  = function(e){ var v=e.target.getAttribute('data-v'); if(v){ st.gmDir = v;  render(); send(); } };
$('gmGear').onclick = function(e){ var v=e.target.getAttribute('data-v'); if(v){ st.gmGear= +v; render(); send(); } };



setInterval(send, 200);   // 5Hz:指令 + 心跳

render();
connect();
</script>
</body>
</html>)HTMLPAGE";

    httpd_handle_t s_server = nullptr;

    // ── 多网页 + 控制权令牌 ────────────────────────────────
    //   允许最多 4 个网页同时连着，但同一时刻只有一个"持有控制权"。
    //   谁点"接管控制"（发 "#TAKE"），控制权就归谁，其他人收到 "#CTRL,0"。
    //   控制权切换时蜂鸣器响两声。
    constexpr int kMaxClients = 4;

    int s_ws_fds[kMaxClients] = {-1, -1, -1, -1};   // 已连接的网页
    int s_ctrl_fd = -1;                             // 当前持有控制权的 fd（-1 = 无人）

    void clientAdd(int fd)
    {
        for (int i = 0; i < kMaxClients; ++i) {
            if (s_ws_fds[i] == -1) { s_ws_fds[i] = fd; return; }
        }
        // 满了：踢掉最早的那个
        s_ws_fds[0] = fd;
    }

    void clientRemove(int fd)
    {
        for (int i = 0; i < kMaxClients; ++i) {
            if (s_ws_fds[i] == fd) s_ws_fds[i] = -1;
        }
        if (s_ctrl_fd == fd) s_ctrl_fd = -1;
    }

    int clientCount()
    {
        int n = 0;
        for (int i = 0; i < kMaxClients; ++i) {
            if (s_ws_fds[i] != -1) ++n;
        }
        return n;
    }

    // 当前模式对应的通知字符串：0=默认 1=无线 2=未选择
    const char* modeString()
    {
        const uint8_t m = ControlState_GetMode();
        if (m == 0) return "#MODE,0";
        if (m == 1) return "#MODE,1";
        return "#MODE,2";
    }

    // 给指定网页发一条文本帧（异步，不阻塞）
    void sendText(int fd, const char* text)
    {
        if (s_server == nullptr || fd < 0) return;

        httpd_ws_frame_t f = {};
        f.type    = HTTPD_WS_TYPE_TEXT;
        f.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(text));
        f.len     = std::strlen(text);
        (void)httpd_ws_send_frame_async(s_server, fd, &f);
    }

    // 给"当前正在处理的这个 socket"发一条文本帧。
    //   ⚠ 必须用 httpd_ws_send_frame(req, ...)，不能用 async 版本，
    //   否则会把 WebSocket 字节流写乱（浏览器直接 RST）。
    void sendTextNow(httpd_req_t* req, const char* text)
    {
        httpd_ws_frame_t f = {};
        f.type    = HTTPD_WS_TYPE_TEXT;
        f.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(text));
        f.len     = std::strlen(text);
        (void)httpd_ws_send_frame(req, &f);
    }

    void broadcastExcept(int except_fd, const char* text)
    {
        for (int i = 0; i < kMaxClients; ++i) {
            if (s_ws_fds[i] != -1 && s_ws_fds[i] != except_fd) {
                sendText(s_ws_fds[i], text);
            }
        }
    }

    esp_err_t index_get_handler(httpd_req_t* req)
    {
        // 这条日志证明「浏览器确实打开了页面」—— 和 WebSocket 能否握手是两回事

        // ★ 禁止缓存：页面里带 WebSocket 逻辑，浏览器一旦缓存了旧版本，
        //   就会出现"改了固件但行为没变"的假象（比如心跳逻辑没生效，
        //   5 秒后 WebSocket 被 httpd 超时关闭，表现为莫名其妙掉线）。
        httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        httpd_resp_set_hdr(req, "Pragma", "no-cache");
        httpd_resp_set_hdr(req, "Expires", "0");

        httpd_resp_set_type(req, "text/html; charset=utf-8");
        return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
    }

    esp_err_t ws_handler(httpd_req_t* req)
    {
        if (req->method == HTTP_GET)
        {
            const int new_fd = httpd_req_to_sockfd(req);

            clientAdd(new_fd);
            ControlState_SetWsConnected(true);

            // ★ 握手阶段只登记，不发任何 WebSocket 帧！
            //   此时 HTTP Upgrade 还没完成，发帧会破坏协议，
            //   浏览器会判定连接非法并关闭 -> 表现为"连上就断、反复重连"。
            //   页面连上后会主动发 "#STATE" 来查询控制权和模式。
            return ESP_OK;
        }

        const int recv_fd = httpd_req_to_sockfd(req);

        // ★ 无论如何都要把帧读掉，否则后续帧会解析错位
        httpd_ws_frame_t pkt = {};
        pkt.type = HTTPD_WS_TYPE_TEXT;

        esp_err_t ret = httpd_ws_recv_frame(req, &pkt, 0);
        if (ret != ESP_OK || pkt.len == 0)
        {
            return ret;
        }

        char buf[256] = {};
        if (pkt.len >= sizeof(buf))
        {
            pkt.len = sizeof(buf) - 1;
        }
        pkt.payload = reinterpret_cast<uint8_t*>(buf);

        ret = httpd_ws_recv_frame(req, &pkt, pkt.len);
        if (ret != ESP_OK)
        {
            return ret;
        }
        buf[pkt.len] = '\0';

        // 只处理文本帧（服务端发的 PING 会收到 PONG，别把 PONG 当指令解析）
        if (pkt.type != HTTPD_WS_TYPE_TEXT) {
            return ESP_OK;
        }

        // ── 控制权命令（以 '#' 开头）──────────────────────────
        if (buf[0] == '#')
        {
            if (std::strncmp(buf, "#MODE,", 6) == 0)
            {
                // 只有控制者能改模式
                if (recv_fd == s_ctrl_fd)
                {
                    const uint8_t m = (buf[6] == '0') ? 0 : 1;
                    ControlState_SetMode(m);

                    // 只通知"其他网页"；当前网页在点击时已经本地更新过 UI，
                    // 不需要再收一帧（少一次写当前 socket 的机会）。
                    broadcastExcept(recv_fd, modeString());
                    ESP_LOGW(TAG, "工作模式 -> %s", m == 0 ? "默认(自动来回)" : "无线控制");
                }
                return ESP_OK;
            }

            if (std::strcmp(buf, "#STATE") == 0)
            {
                // 页面刚连上时查一次：我是控制者吗？当前什么模式？
                sendTextNow(req, (recv_fd == s_ctrl_fd) ? "#CTRL,1" : "#CTRL,0");
                sendTextNow(req, modeString());
                return ESP_OK;
            }

            if (std::strcmp(buf, "#PING") == 0)
            {
                return ESP_OK;
            }

            if (std::strcmp(buf, "#TAKE") == 0)
            {
                s_ctrl_fd = recv_fd;
                ControlState_SetControllerActive(true);

                // 发给"自己"必须走 sendTextNow（当前 socket）
                sendTextNow(req, "#CTRL,1");
                sendTextNow(req, modeString());

                // 发给"别人"才用 async
                broadcastExcept(recv_fd, "#CTRL,0");
                ControlState_BumpTakeover();    // 计数 +1 -> buzzer 任务会响"嘀-嘀"

                ESP_LOGW(TAG, "控制权切换 -> fd=%d", recv_fd);
            }
            return ESP_OK;
        }

        // ── 普通控制指令：只有控制者生效 ────────────────────
        if (recv_fd != s_ctrl_fd)
        {
            // 不是控制者，静默丢弃（避免两个页面互相覆盖状态）
            return ESP_OK;
        }

        // 协议(用可读词,方便看日志):
        //   "导轨转速,导轨方向,6020方向,6020挡位"
        //   例:"3500,STOP,FORWARD,2"
        //     导轨方向 : LEFT / STOP / RIGHT
        //     6020方向 : FORWARD / STOP / REVERSE

        // 备份原始报文（下面会原地把 ',' 改成 '\0'，之后就看不到了）
        char raw[256];
        strncpy(raw, buf, sizeof(raw) - 1);
        raw[sizeof(raw) - 1] = '\0';

        // 原地按逗号切成 4 段(把 ',' 替换成 '\0')
        char* tok[4] = {};
        int   ntok   = 0;
        tok[ntok++]  = buf;
        for (char* p = buf; *p != '\0' && ntok < 4; ++p)
        {
            if (*p == ',')
            {
                *p = '\0';
                tok[ntok++] = p + 1;
            }
        }

        const auto field = [&](int i) -> const char* {
            return (i < ntok) ? tok[i] : "";
        };

        if (ntok >= 4)
        {
            char rail_dir = 'N';
            if      (strcasecmp(field(1), "LEFT")  == 0) rail_dir = 'L';
            else if (strcasecmp(field(1), "RIGHT") == 0) rail_dir = 'R';

            char gm_dir = 'S';
            if      (strcasecmp(field(2), "FORWARD") == 0) gm_dir = 'F';
            else if (strcasecmp(field(2), "REVERSE") == 0) gm_dir = 'R';

            ControlState_SetFromWeb(
                static_cast<int16_t>(atoi(field(0))),
                rail_dir,
                gm_dir,
                static_cast<uint8_t>(atoi(field(3))));
        }
        else
        {
            // 解析失败才打日志(节流,别刷屏)—— 会把收到的原文打出来
            static uint32_t fail_n = 0;
            fail_n = fail_n + 1;
            if (fail_n % 50 == 1)
            {
                ESP_LOGW(TAG, "指令解析失败(第%u次) 只切出%d段,收到: '%s'",
                         (unsigned)fail_n, ntok, raw);
            }
        }

        return ESP_OK;
    }

    // 会话关闭。网页断开时浏览器会走到这里 —— 这就是"离线检测"的快速通道。
    void on_session_close(httpd_handle_t hd, int sockfd)
    {
        (void)hd;

        const bool was_ctrl = (sockfd == s_ctrl_fd);

        clientRemove(sockfd);

        if (clientCount() == 0) {
            ControlState_SetWsConnected(false);
            ControlState_SetControllerActive(false);
            s_ctrl_fd = -1;

            // 注意：这里**不**复位模式。
            //   模式是"用户在网页上的选择"，不是"连接状态"。
            //   复位会导致：手机锁屏 -> 连接断 -> 模式变回未选择 ->
            //   默认模式的自动来回停掉，看起来像"切默认就坏"。
        }

        // 控制者断开 -> 控制权释放，通知其他网页可以接管
        if (was_ctrl) {
            ControlState_SetControllerActive(false);
            broadcastExcept(-1, "#CTRL,0");
        }


        close(sockfd);
    }

    // ── 强制门户:任何未知路径都 303 回首页 ──
    //   手机连上 WiFi 后会自动访问 captive.apple.com / connectivitycheck.gstatic.com
    //   之类的地址来探测有没有外网。这些请求在板子上都是 404,这里把它们全部
    //   重定向回控制页面,手机就会判定"这是需要登录的 WiFi"并自动弹窗。
    esp_err_t http_404_handler(httpd_req_t* req, httpd_err_code_t err)
    {
        (void)err;

        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/");
        // iOS 需要响应体里有内容才认得出这是强制门户,只给重定向头不够
        httpd_resp_send(req, "Redirect to the captive portal", HTTPD_RESP_USE_STRLEN);

        return ESP_OK;
    }
}

esp_err_t WebServer_Start()
{
    // ★ 回退到初始配置：只保留"recv 超时放宽"这一项（它只影响长连接存活）
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();

    // ★ 关键：WebSocket 是长连接，手机锁屏/切后台时浏览器会停掉 JS 定时器，
    //   页面就不再发任何帧。默认的 recv 超时只有 5 秒，会直接把连接关掉，
    //   表现为"停在默认模式几分钟就掉线、灯灭、蜂鸣器响"。
    //   这里拉到 1 小时：只要手机还连着，连接就不该因为我们"收不到帧"而断。
    config.recv_wait_timeout = 3600;
    config.send_wait_timeout = 30;

    // 多给几个槽位，避免手机的门户探测把连接挤掉
    config.lru_purge_enable = true;
    config.max_open_sockets = 7;

    // ★ 必须放大 handler 槽位：默认只有 8 个，
    //   我们注册了 7 个强制门户探测路径 + / + /ws = 9 个，
    //   槽位不够会导致 /ws 注册失败（日志 ESP_ERR_HTTPD_HANDLERS_FULL），
    //   表现为网页能打开但连不上/打不开。
    config.max_uri_handlers = 16;

    config.close_fn         = on_session_close;

    esp_err_t err = httpd_start(&s_server, &config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "httpd_start failed: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t uri_index = {};
    uri_index.uri     = "/";
    uri_index.method  = HTTP_GET;
    uri_index.handler = index_get_handler;
    err = httpd_register_uri_handler(s_server, &uri_index);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "注册 '/' 失败: %s", esp_err_to_name(err));
    }

    httpd_uri_t uri_ws = {};
    uri_ws.uri          = "/ws";
    uri_ws.method       = HTTP_GET;
    uri_ws.handler      = ws_handler;
    uri_ws.is_websocket = true;
    err = httpd_register_uri_handler(s_server, &uri_ws);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "注册 '/ws' 失败: %s  ← 如果看到这条,说明 WebSocket 路由没建起来",
                 esp_err_to_name(err));
    }

    // 强制门户:404 一律重定向回首页
    httpd_register_err_handler(s_server, HTTPD_404_NOT_FOUND, http_404_handler);

    return ESP_OK;
}
