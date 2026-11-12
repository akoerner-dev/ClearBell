// cb_status_page.h - the HTML of the status page (cb_status.cpp serves it at "/").
//
// One self-contained page, no external files (the unit has no internet obligation, and the Content Security
// Policy forbids everything foreign): system font, inline SVG icons, own CSS. It loads /status.json every 5 s and
// builds the page from it with textContent only - nothing from the JSON is ever interpreted as HTML (innerHTML is
// used for the constant icon strings below only). The page is German (it is for the household); the JSON keys stay
// English like the rest of the firmware.
//
// Layout (12.11.2026, after the look of current home apps): header with unit and mode, overview tiles, cards for
// what a household uses, diagnosis numbers in collapsible sections, maintenance settings grouped. Messages of an
// action appear as a toast that survives the 5-s refresh (FW-B-12); cards flow as columns without stretched empty
// boxes (FW-B-09); own file picker that shortens long names at the front (FW-B-10).
//
// To change the look: edit the string below, rebuild. To preview without a board, save the string as index.html next
// to a status.json (from `cb.py status <unit> --json`), make the fetch paths relative, open it via a local server.
#pragma once

static const char kStatusPage[] = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light dark"><title>ClearBell</title>
<style>
:root{--bg:#f3f5f4;--card:#fff;--fg:#17201b;--mute:#66706a;--line:#e3e8e5;--soft:#eef2f0;--acc:#1d7a4c;--acc-fg:#fff;
 --acc-soft:#e1f1e8;--ok:#1f8a4c;--ok-soft:#e2f4e9;--warn:#a65f00;--warn-soft:#fdf0dc;--bad:#c0392b;--bad-soft:#fbe6e3;
 --info:#2463b4;--info-soft:#e3edfa;--shadow:0 1px 2px rgba(16,24,20,.06),0 4px 14px rgba(16,24,20,.05)}
@media (prefers-color-scheme:dark){:root{--bg:#0f1311;--card:#181e1b;--fg:#e8eeea;--mute:#98a39c;--line:#27302b;
 --soft:#1f2723;--acc:#3fb37a;--acc-fg:#07170e;--acc-soft:#1b3326;--ok:#5fcf8f;--ok-soft:#173022;--warn:#f0b357;
 --warn-soft:#33270f;--bad:#f2867a;--bad-soft:#3a1d1a;--info:#8ab4f8;--info-soft:#1c2a3f;--shadow:none}}
*{box-sizing:border-box}html{-webkit-text-size-adjust:100%}
body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
svg{width:20px;height:20px;flex:none;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
header{position:sticky;top:0;z-index:5;background:color-mix(in srgb,var(--bg) 88%,transparent);backdrop-filter:blur(10px);
 -webkit-backdrop-filter:blur(10px);border-bottom:1px solid var(--line)}
.bar{max-width:1180px;margin:0 auto;padding:12px 20px;display:flex;align-items:center;gap:12px}
.logo{width:40px;height:40px;border-radius:12px;background:var(--acc);color:var(--acc-fg);display:grid;place-items:center}
.logo svg{width:22px;height:22px}h1{font-size:18px;line-height:1.2;margin:0;font-weight:650}
#sub{font-size:13px;color:var(--mute)}.grow{flex:1;min-width:0}
.chip{display:inline-flex;align-items:center;gap:6px;padding:4px 10px;border-radius:999px;font-size:13px;font-weight:600;white-space:nowrap}
.chip::before{content:"";width:8px;height:8px;border-radius:50%;background:currentColor}
.c-ok{background:var(--ok-soft);color:var(--ok)}.c-warn{background:var(--warn-soft);color:var(--warn)}
.c-bad{background:var(--bad-soft);color:var(--bad)}.c-info{background:var(--info-soft);color:var(--info)}
.c-mute{background:var(--soft);color:var(--mute)}
.wrap{max-width:1180px;margin:0 auto;padding:16px 20px 40px}
.banner{display:flex;gap:12px;align-items:flex-start;padding:12px 16px;border-radius:14px;margin-bottom:16px}
.banner b{display:block}.banner span{font-size:14px}
#err:empty{display:none}
.tiles{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,1fr));gap:12px;margin-bottom:16px}
.tile{background:var(--card);border-radius:16px;padding:14px;box-shadow:var(--shadow);border:1px solid var(--line);display:flex;gap:12px;align-items:center;min-width:0}
.ico{width:40px;height:40px;border-radius:12px;display:grid;place-items:center;flex:none}
.tile small{display:block;color:var(--mute);font-size:12px;text-transform:uppercase;letter-spacing:.04em}
.tile b{display:block;font-size:16px;font-weight:650;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.tile em{display:block;font-style:normal;font-size:13px;color:var(--mute);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.t-ok .ico{background:var(--ok-soft);color:var(--ok)}.t-warn .ico{background:var(--warn-soft);color:var(--warn)}
.t-bad .ico{background:var(--bad-soft);color:var(--bad)}.t-info .ico{background:var(--info-soft);color:var(--info)}
.grid{columns:360px auto;column-gap:16px}
.card{break-inside:avoid;display:inline-block;width:100%;margin:0 0 16px;background:var(--card);border-radius:18px;border:1px solid var(--line);box-shadow:var(--shadow);padding:16px 18px}
.card>h2{display:flex;align-items:center;gap:10px;font-size:16px;font-weight:650;margin:0 0 12px}
.card>h2 .ico{width:32px;height:32px;border-radius:10px;background:var(--acc-soft);color:var(--acc)}
.card>h2 .ico svg{width:18px;height:18px}h3{font-size:14px;margin:18px 0 8px;font-weight:650}
.kv{display:flex;justify-content:space-between;gap:12px;padding:8px 0;border-top:1px solid var(--line);font-size:14px}
.kv:first-of-type{border-top:0}.kv span:first-child{color:var(--mute)}.kv span:last-child{text-align:right;font-variant-numeric:tabular-nums;overflow-wrap:anywhere}
.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}.mute{color:var(--mute);font-size:13px}
.list{display:flex;flex-direction:column}.item{display:flex;align-items:center;gap:12px;padding:10px 0;border-top:1px solid var(--line)}
.item:first-child{border-top:0}.item .grow b{display:block;font-weight:600;font-size:14px}.item .grow span{font-size:13px;color:var(--mute)}
.av{width:34px;height:34px;border-radius:50%;background:var(--soft);display:grid;place-items:center;font-weight:700;font-size:13px;color:var(--mute);flex:none}
.pill{font-size:12px;font-weight:600;padding:3px 9px;border-radius:999px;white-space:nowrap}
.seg{display:grid;grid-template-columns:repeat(5,1fr);gap:4px;background:var(--soft);border-radius:12px;padding:4px}
.seg button{border:0;background:transparent;color:var(--fg);padding:8px 0;border-radius:9px;font-weight:600;margin:0}
.seg button.on{background:var(--card);color:var(--acc);box-shadow:0 1px 3px rgba(0,0,0,.12)}
button,input,select{font:inherit;color:inherit}
button{display:inline-flex;align-items:center;justify-content:center;gap:8px;border:1px solid transparent;background:var(--acc);color:var(--acc-fg);
 padding:9px 16px;border-radius:12px;font-weight:600;cursor:pointer;margin:10px 8px 0 0}
button.sec{background:var(--soft);color:var(--fg)}button.danger{background:var(--bad-soft);color:var(--bad)}
button.sm{padding:5px 12px;font-size:13px;margin:0 0 0 8px;min-height:0}
button:disabled{opacity:.5;cursor:default}button svg{width:18px;height:18px}
input,select{width:100%;padding:10px 12px;border:1px solid var(--line);border-radius:12px;background:var(--bg);margin-top:8px}
input[type=checkbox]{width:auto;margin:0 6px 0 0}.row{display:flex;gap:8px;align-items:flex-end}.row input{flex:1}.row button{margin-top:8px}
label.chk{display:flex;align-items:center;font-size:14px;margin-top:10px}
.file{display:flex;align-items:center;gap:10px;margin-top:8px;padding:8px 8px 8px 12px;border:1px dashed var(--line);border-radius:12px;background:var(--bg)}
.file span{flex:1;min-width:0;font-size:14px;color:var(--mute);white-space:nowrap;overflow:hidden}.file button{margin:0}
details{border-top:1px solid var(--line)}details:first-of-type{border-top:0}
summary{cursor:pointer;list-style:none;display:flex;align-items:center;gap:10px;padding:12px 0;font-weight:600;font-size:14px}
summary::-webkit-details-marker{display:none}summary::after{content:"";margin-left:auto;width:8px;height:8px;border-right:2px solid var(--mute);border-bottom:2px solid var(--mute);transform:rotate(45deg);transition:transform .15s}
details[open] summary::after{transform:rotate(225deg)}details>div{padding:0 0 10px}
table{width:100%;border-collapse:collapse;font-size:13px}th,td{text-align:left;padding:6px 8px 6px 0;border-top:1px solid var(--line)}
th{color:var(--mute);font-weight:600}td:not(:first-child),th:not(:first-child){text-align:right;font-variant-numeric:tabular-nums}
a.step{display:flex;align-items:center;gap:10px;margin-top:10px;padding:12px 14px;border-radius:12px;background:var(--acc-soft);color:var(--acc);font-weight:650;text-decoration:none}
#toast{position:fixed;left:50%;bottom:20px;transform:translate(-50%,40px);opacity:0;transition:.25s;z-index:9;max-width:min(560px,calc(100% - 32px));
 padding:12px 16px;border-radius:14px;background:var(--fg);color:var(--bg);font-size:14px;box-shadow:0 8px 30px rgba(0,0,0,.25);display:flex;gap:10px;align-items:center}
#toast.show{opacity:1;transform:translate(-50%,0)}#toast.bad{background:var(--bad);color:#fff}
:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
@media (max-width:640px){.bar{padding:10px 14px}.wrap{padding:12px 12px 32px}.grid{columns:1}.tiles{grid-template-columns:1fr 1fr;gap:10px}
 .tile{padding:12px;gap:10px}.ico{width:34px;height:34px}.tile b{font-size:15px}input,select,button{font-size:16px}
 .card>button,details>div>button{width:100%;margin-right:0}.row{flex-wrap:wrap}.row button{width:100%;margin-right:0}#upd{display:none}}
@media (pointer:coarse){button{min-height:44px}}
</style></head><body>
<header><div class="bar"><div class="logo" id="logo"></div><div class="grow"><h1 id="title">ClearBell</h1><div id="sub">lädt …</div></div>
<span class="chip c-mute" id="mode">…</span></div></header>
<div class="wrap"><div id="err" class="banner c-bad"></div><div id="banner"></div><div class="tiles" id="tiles"></div><div class="grid" id="main"></div></div>
<div id="toast" role="status"></div>
<script>
"use strict";
const I={bell:'<path d="M6 8a6 6 0 0 1 12 0c0 7 3 9 3 9H3s3-2 3-9"/><path d="M10.3 21a1.9 1.9 0 0 0 3.4 0"/>',
 wifi:'<path d="M5 12.5a10 10 0 0 1 14 0"/><path d="M8.5 16a5 5 0 0 1 7 0"/><path d="M2 9a15 15 0 0 1 20 0"/><circle cx="12" cy="19.5" r=".6"/>',
 link:'<path d="M10 13a5 5 0 0 0 7.5.5l3-3a5 5 0 0 0-7-7l-1.7 1.7"/><path d="M14 11a5 5 0 0 0-7.5-.5l-3 3a5 5 0 0 0 7 7l1.7-1.7"/>',
 door:'<path d="M5 21V4a1 1 0 0 1 1-1h12a1 1 0 0 1 1 1v17"/><path d="M3 21h18"/><circle cx="15" cy="12.5" r=".8"/>',
 vol:'<path d="M11 5 6 9H3v6h3l5 4z"/><path d="M15.5 8.5a5 5 0 0 1 0 7"/><path d="M18.5 5.5a9 9 0 0 1 0 13"/>',
 shield:'<path d="M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z"/><path d="m9 12 2 2 4-4"/>',
 phone:'<rect x="6" y="2" width="12" height="20" rx="2.5"/><path d="M11 18h2"/>',
 list:'<path d="M8 6h13M8 12h13M8 18h13"/><circle cx="3.5" cy="6" r=".6"/><circle cx="3.5" cy="12" r=".6"/><circle cx="3.5" cy="18" r=".6"/>',
 music:'<path d="M9 18V5l12-2v13"/><circle cx="6" cy="18" r="3"/><circle cx="18" cy="16" r="3"/>',
 gear:'<circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.7 1.7 0 0 0 .3 1.8l.1.1a2 2 0 1 1-2.8 2.8l-.1-.1a1.7 1.7 0 0 0-1.8-.3 1.7 1.7 0 0 0-1 1.5V21a2 2 0 1 1-4 0v-.1a1.7 1.7 0 0 0-1.1-1.5 1.7 1.7 0 0 0-1.8.3l-.1.1a2 2 0 1 1-2.8-2.8l.1-.1a1.7 1.7 0 0 0 .3-1.8 1.7 1.7 0 0 0-1.5-1H3a2 2 0 1 1 0-4h.1a1.7 1.7 0 0 0 1.5-1.1 1.7 1.7 0 0 0-.3-1.8l-.1-.1a2 2 0 1 1 2.8-2.8l.1.1a1.7 1.7 0 0 0 1.8.3H9a1.7 1.7 0 0 0 1-1.5V3a2 2 0 1 1 4 0v.1a1.7 1.7 0 0 0 1 1.5 1.7 1.7 0 0 0 1.8-.3l.1-.1a2 2 0 1 1 2.8 2.8l-.1.1a1.7 1.7 0 0 0-.3 1.8V9a1.7 1.7 0 0 0 1.5 1H21a2 2 0 1 1 0 4h-.1a1.7 1.7 0 0 0-1.5 1z"/>',
 chip:'<rect x="5" y="5" width="14" height="14" rx="2"/><path d="M9 9h6v6H9zM9 2v3M15 2v3M9 19v3M15 19v3M2 9h3M2 15h3M19 9h3M19 15h3"/>',
 send:'<path d="m22 2-7 20-4-9-9-4z"/><path d="M22 2 11 13"/>',up:'<path d="M12 16V4M6 10l6-6 6 6M4 20h16"/>',
 play:'<path d="m7 4 13 8-13 8z"/>',search:'<circle cx="11" cy="11" r="7"/><path d="m20 20-4-4"/>',
 info:'<circle cx="12" cy="12" r="10"/><path d="M12 16v-5M12 8h.01"/>',tool:'<path d="M14.7 6.3a4 4 0 0 0-5.4 5.4L3 18l3 3 6.3-6.3a4 4 0 0 0 5.4-5.4l-2.6 2.6-2.4-.6-.6-2.4z"/>'};
const ic=n=>{const s=document.createElementNS('http://www.w3.org/2000/svg','svg');s.setAttribute('viewBox','0 0 24 24');s.innerHTML=I[n];return s};
const $=(t,c,x)=>{const e=document.createElement(t);if(c)e.className=c;if(x!==undefined&&x!==null)e.textContent=x;return e};
const byId=id=>document.getElementById(id);
const UNIT={AUSSEN:'Außeneinheit',EG:'EG',OG:'OG'};
// FW-B-21: inside, the flats are the two bell keys; the buyer sees the names (system.flat_names)
const KEY={EG:'Taste unten',OG:'Taste oben'};let FN={};const flat=u=>FN[u]||UNIT[u]||u;
const TONE={ring:'Klingelton',ok:'Bestätigung',error:'Fehler',nonet:'Kein Netz',tick:'Tipp'};
const EV={'opened':['geöffnet','c-ok'],'remote':['per App geöffnet','c-info'],'lock-out':['Sperrzeit','c-warn'],'refused':['abgelehnt','c-bad'],
 'no answer':['keine Antwort','c-bad'],'service':['Service (USB)','c-mute']};
const ST={'pressed':'gedrückt','rest':'Ruhe','learning':'lernt'};
const dur=s=>{const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return d?d+' T '+h+' h':h?h+' h '+m+' min':m+' min'};
const kb=b=>Math.round(b/1024)+' KB';
const front=(n,max)=>n.length>max?'…'+n.slice(n.length-max+1):n;  // FW-B-10: keep the END of a long file name
let toastT=0;
function toast(r){const t=byId('toast');t.replaceChildren(ic(r.ok?'shield':'info'),$('span','',r.message||'Fehler'));
  t.className='show'+(r.ok?'':' bad');clearTimeout(toastT);toastT=setTimeout(()=>t.className='',r.ok?5000:9000)}
function card(icon,title){const s=$('section','card');const h=$('h2');const i=$('span','ico');i.append(ic(icon));h.append(i,$('span','',title));s.append(h);return s}
function kv(parent,k,v,cls){if(v===null||v===undefined)return;const r=$('div','kv');r.append($('span','',k),$('span',cls||'',String(v)));parent.append(r)}
function tile(icon,label,value,sub,state){const t=$('div','tile t-'+state);const i=$('span','ico');i.append(ic(icon));const g=$('div','grow');
  g.append($('small','',label),$('b','',value));if(sub)g.append($('em','',sub));t.append(i,g);return t}
function btn(text,icon,cls){const b=$('button',cls||'');b.type='button';if(icon)b.append(ic(icon));b.append(text);return b}
const openSec=new Set();  // which collapsible sections are open - survives the refresh
function fold(id,title,fill){const d=$('details');d.open=openSec.has(id);d.ontoggle=()=>d.open?openSec.add(id):openSec.delete(id);
  d.append($('summary','',title));const b=$('div');fill(b);d.append(b);return d}
function quality(rssi){return rssi>-60?['sehr gut','ok']:rssi>-70?['gut','ok']:rssi>-80?['mäßig','warn']:['schwach','bad']}
function render(d){
  const sy=d.system,m=byId('main'),maint=sy.mode==='maintenance';m.replaceChildren();
  FN=sy.flat_names||{};
  const role=sy.role==='AUSSEN'?'Außeneinheit':sy.role==='none'?'nicht eingerichtet':flat(sy.role)+' · '+(KEY[sy.role]||sy.role);
  byId('title').textContent=sy.name||'ClearBell';document.title='ClearBell '+(sy.name||'');
  byId('sub').textContent=role+' · aktualisiert '+new Date().toLocaleTimeString('de-DE',{hour:'2-digit',minute:'2-digit',second:'2-digit'});
  const md={normal:['Normalbetrieb','c-ok'],maintenance:['Wartung','c-warn'],setup:['Einrichtung','c-info']}[sy.mode]||[sy.mode,'c-mute'];
  const mc=byId('mode');mc.textContent=md[0];mc.className='chip '+md[1];
  const bn=byId('banner');bn.replaceChildren();
  if(maint){const b=$('div','banner c-warn');b.append(ic('tool'));const t=$('div');t.append($('b','','Wartung ist aktiv'),
    $('span','','Einstellungen, Töne und Update sind freigeschaltet – bis 10 min oder bis zu einem kurzen Druck auf den Boot-Taster. Die Seite lädt sich dabei nicht selbst neu.'));
    b.append(t);const r=btn('Neu laden','',"sec");r.onclick=()=>load(true);r.id='upd';b.append(r);bn.append(b)}
  // ---- overview tiles
  const T=byId('tiles');T.replaceChildren();
  if(d.radio){const r=d.radio;if(r.wifi==='up'){const q=quality(r.rssi);T.append(tile('wifi','WLAN',q[0],r.rssi+' dBm · Kanal '+r.channel,q[1]))}
    else T.append(tile('wifi','WLAN',{connecting:'verbindet …',waiting:'wartet'}[r.wifi]||'getrennt','',  'bad'))}
  if(d.protocol){const ps=d.protocol.peers;const bad=ps.filter(p=>!p.reachable);
    T.append(tile('link',sy.role==='AUSSEN'?'Wohnungen':'Außeneinheit',!ps.length?'nicht gekoppelt':bad.length?'nicht erreichbar':'verbunden',
      ps.map(p=>flat(p.unit)+(p.reachable?' ✓':' ✗')).join('  '),!ps.length?'warn':bad.length?'bad':'ok'))}
  if(d.door){const o=d.door;T.append(tile('door','Türöffner',!o.available?'abgeschaltet':o.ready?'bereit':'Sperrzeit',
    'Puls '+o.pulse_ms+' ms',!o.available?'bad':o.ready?'ok':'warn'))}
  if(d.audio)T.append(tile('vol','Lautstärke',d.audio.volume+' %',d.audio.ready?'Audio bereit':'Audio gestört',d.audio.ready?'info':'bad'));
  const healthy=sy.selftest==='ok'&&!sy.coredump&&!/WATCHDOG|PANIC|BROWNOUT/.test(sy.reset_reason);
  T.append(tile('shield','Gerät',healthy?'in Ordnung':'prüfen','läuft seit '+dur(sy.uptime_s),healthy?'ok':'warn'));
  // ---- cards
  if(d.settings)m.append(settings(d,maint));
  if(d.pairing)m.append(pairing(d.pairing,maint,sy.role));
  if(d.phones&&d.phones.available)m.append(phones(d.phones,maint));
  if(d.remote&&d.remote.enabled)m.append(remote(d.remote));
  m.append(doorlog(d.doorlog));
  m.append(tones(d.audio,maint));
  if(d.door)m.append(door(d.door,d.protocol,maint));
  if(maint)m.append(maintCard(d));
  m.append(diagnosis(d));
  if(!maint&&sy.mode==='normal'){const c=card('info','Mehr einstellen');c.append($('p','mute',
    'Töne tauschen, LED-Abgleich'+(d.door?', Pulslänge':'')+', Passwort, Handys und Firmware-Update: Wartung einschalten – Boot-Taster der Einheit 5 s halten, dann diese Seite neu laden.'));m.append(c)}}
async function post(url,data){try{const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});
  try{return await r.json()}catch(e){return {ok:false,message:'HTTP '+r.status}}}catch(e){return {ok:false,message:'Keine Verbindung zur Einheit'}}}
function settings(d,maint){
  const st=d.settings,c=card('bell','Klingel');c.append($('div','mute','Lautstärke'));const seg=$('div','seg');seg.style.marginTop='6px';
  for(const v of [15,30,50,75,100]){const b=$('button',v===st.volume?'on':'',v+' %');b.type='button';b.setAttribute('aria-pressed',v===st.volume);
    b.onclick=async()=>{toast(await post('/settings',{vol:v}));load(true)};seg.append(b)}
  const tb=btn('Probeton','play','sec');tb.onclick=async()=>toast(await post('/settings',{tone:'1'}));c.append(seg,tb);
  return c}
function pairing(p,maint,R){
  const c=card('link','Kopplung'),l=$('div','list');
  const item=(av,title,sub,state,txt)=>{const it=$('div','item');it.append($('span','av',av));const g=$('div','grow');g.append($('b','',title),$('span','',sub));
    it.append(g,$('span','pill '+state,txt));l.append(it);return it};
  if(p.paired!==undefined){  // indoor unit
    item(R==='OG'?'↑':'↓',flat(R),KEY[R]+' an der Haustür','c-info',p.own_name?'eigener Name':'Standardname');
    item('AU','Außeneinheit',p.outdoor?'Nr. '+p.outdoor:'',p.paired?'c-ok':'c-bad',p.paired?'gekoppelt':'nicht gekoppelt');c.append(l);
    if(maint){c.append($('h3','','Name der Wohnung'));const row=$('div','row');const n=$('input');n.value=p.own_name;n.maxLength=40;
      n.placeholder='z. B. Familie Müller';n.autocomplete='off';const s=btn('Speichern','');row.append(n,s);
      s.onclick=async()=>{s.disabled=true;const r=await post('/flat/name',{name:n.value});toast(r);s.disabled=false;if(r.ok)load(true)};
      c.append(row,$('p','mute','Höchstens 24 Zeichen, leer = „'+(R==='OG'?'Wohnung oben':'Wohnung unten')+'“. Erscheint in den Klingel-Nachrichten (unverschlüsselt über ntfy.sh) und auf beiden Statusseiten. Bei schon eingetragenen Handys zeigt die ntfy-App den alten Namen, bis sie neu abonniert werden.'));paused=true}
    if(!p.paired)c.append($('p','mute','Koppeln an der Außeneinheit: dort „Inneneinheiten suchen“ und den Gerätecode vom Etikett dieser Einheit eingeben.'));
    if(maint&&p.paired){const b=btn('Wartung der Außeneinheit öffnen','tool','sec');b.onclick=async()=>toast(await post('/outdoor-maint',{go:'1'}));
      c.append(b,$('p','mute','Für 10 min – Töne, Update, Pulslänge der Außeneinheit, ohne ihr Gehäuse zu öffnen.'))}
    return c}
  for(const f of ['og','eg']){const ok=p[f+'_paired'],F=f.toUpperCase();
    const it=item(f==='og'?'↑':'↓',ok?flat(F):KEY[F],ok?KEY[F]+(p[f]?' · Nr. '+p[f]:''):'',ok?'c-ok':'c-mute',ok?'gekoppelt':'frei');
    if(maint&&ok){const b=btn('Entfernen','','danger sm');b.onclick=async()=>{if(confirm('Kopplung „'+flat(F)+'“ ('+KEY[F]+') wirklich entfernen?')){toast(await post('/pair/remove',{flat:f}));load(true)}};it.append(b)}}
  c.append(l);
  const b=btn('Inneneinheiten suchen','search');const list=$('div','list');
  b.onclick=async()=>{paused=true;b.disabled=true;list.replaceChildren($('p','mute','suche … (einige Sekunden)'));
    try{const r=await (await fetch('/pair/list',{cache:'no-store'})).json();list.replaceChildren();
      if(!r.units.length)list.append($('p','mute','Keine Inneneinheit im Netz gefunden – ist sie eingerichtet (WLAN, Klingeltaste)?'));
      for(const u of r.units){const it=$('div');it.style.padding='12px 0';it.style.borderTop='1px solid var(--line)';
        it.append($('b','',u.name+' · '+u.key+' · Nr. '+u.id+(u.paired?' · schon gekoppelt':'')));
        const row=$('div','row');const code=$('input');code.placeholder='Gerätecode vom Etikett';code.autocomplete='off';code.spellcheck=false;
        const k=btn('Koppeln','link');k.onclick=async()=>{k.disabled=true;const r=await post('/pair/start',{ip:u.ip,id:u.id,flat:u.flat,code:code.value});toast(r);k.disabled=false};
        row.append(code,k);it.append(row);list.append(it)}}
    catch(e){list.replaceChildren($('p','bad','Suche fehlgeschlagen.'))}b.disabled=false};
  c.append(b,list,$('p','mute','Erste Kopplung einer Wohnung: Gerätecode der Inneneinheit genügt. Eine bestehende Kopplung ersetzen: nur in der Wartung.'));
  return c}
// R27: a phone needs only the ntfy app and one link; the ring push carries the "Tür öffnen" button.
function linkOut(out,r){out.replaceChildren();if(!r.ok)return;const a=$('a','step','ntfy: Nachrichten abonnieren');a.href=r.ntfy;a.prepend(ic('send'));
  out.append(a,$('p','mute','Auf DEM Handy öffnen, das die Nachrichten bekommen soll. Vorher nur die App „ntfy“ installieren (Play Store, F-Droid oder App Store). Danach hier „Test“ antippen.'))}
function phones(ph,maint){
  const c=card('phone','Handys'),l=$('div','list'),out=$('div');const nokey=ph.nokey||[];
  for(const id of ph.list){const it=$('div','item');it.append($('span','av',id.slice(0,2).toUpperCase()));const g=$('div','grow');
    g.append($('b','',id),$('span',nokey.includes(id)?'bad':'',nokey.includes(id)?'neu hinzufügen (alte Einrichtung)':'bekommt Klingel-Nachrichten'));it.append(g);
    if(!nokey.includes(id)){const t=btn('Test','','sec sm');t.onclick=async()=>toast(await post('/phone/test',{name:id}));it.append(t)}
    if(maint){if(!nokey.includes(id)){const k=btn('Link','','sec sm');k.onclick=async()=>{const r=await post('/phone/link',{name:id});toast(r);linkOut(out,r)};it.append(k)}
      const b=btn('Entfernen','','danger sm');b.onclick=async()=>{if(confirm('Handy '+id+' entfernen? Es bekommt dann keine Nachrichten mehr und kann die Tür nicht öffnen.')){toast(await post('/phone/remove',{name:id}));load(true)}};it.append(b)}
    l.append(it)}
  if(!ph.list.length)l.append($('p','mute','Noch keine Handys eingetragen.'));c.append(l,out);
  if(!maint){c.append($('p','mute','Handy hinzufügen: Wartung einschalten und diese Seite auf dem Handy öffnen.'));return c}
  c.append($('h3','','Handy hinzufügen'));const row=$('div','row');const n=$('input');n.placeholder='Name, z. B. Anna';n.maxLength=8;const b=btn('Hinzufügen','phone');row.append(n,b);
  b.onclick=async()=>{b.disabled=true;const r=await post('/phone/add',{name:n.value});toast(r);b.disabled=false;linkOut(out,r);if(r.ok)n.value=''};
  c.append(row);paused=true;return c}
function remote(f){
  const c=card('send','Fern-Öffnen');
  kv(c,'Nachrichtenkanal',f.stream_up?'verbunden':'getrennt',f.stream_up?'ok':'bad');kv(c,'eingetragene Handys',f.phones+' ('+flat('OG')+' '+f.phones_og+', '+flat('EG')+' '+f.phones_eg+')');
  kv(c,'per App geöffnet',f.opened);kv(c,'Push gesendet / fehlgeschlagen',f.pushes_ok+' / '+f.pushes_failed,f.pushes_failed?'warn':'');
  if(f.rejected)kv(c,'abgewiesene Befehle',f.rejected,'warn');
  c.append($('p','mute','Die Tür öffnet nur mit einem frischen Klingel-Token, das genau einem eingetragenen Handy gehört. Handys trägt jede Wohnung an ihrer Inneneinheit ein.'));return c}
function doorlog(l){
  const c=card('list','Türöffnungen'),list=$('div','list');
  const add=(e,into)=>{const it=$('div','item');const ev=EV[e.event]||[e.event,'c-mute'];const g=$('div','grow');g.append($('b','',e.time));it.append(g,$('span','pill '+ev[1],ev[0]));into.append(it)};
  for(const e of l.entries.slice(0,6))add(e,list);
  if(!l.entries.length)list.append($('p','mute','Noch keine Einträge.'));c.append(list);
  if(l.entries.length>6)c.append(fold('log','Ältere anzeigen ('+(l.entries.length-6)+')',b=>{const x=$('div','list');for(const e of l.entries.slice(6))add(e,x);b.append(x)}));
  c.append($('p','mute',(l.indoor_key_openings!==undefined?'Dazu '+l.indoor_key_openings+' Öffnungen über die Innentasten (nur als Summe). ':'Nur die Türbefehle dieser Wohnung. ')+'Insgesamt '+l.total+' Einträge.'));
  return c}
function tones(a,maint){
  const c=card('music','Töne');const good=f=>f.present&&f.status==='ok';const bad=a.files.filter(f=>!good(f));
  const sum=$('div','item');const g0=$('div','grow');
  g0.append($('b','',bad.length?bad.length+' von '+a.files.length+' Tönen mit Problem':'Alle '+a.files.length+' Töne in Ordnung'),
    $('span','','Speicher '+kb(a.fs_used)+' von '+kb(a.fs_total)));sum.append(g0,$('span','pill '+(bad.length?'c-bad':'c-ok'),bad.length?'prüfen':'ok'));
  c.append(sum,fold('tones','Einzelheiten',b=>{const l=$('div','list');
    for(const f of a.files){const it=$('div','item');const g=$('div','grow');g.append($('b','',TONE[f.name]||f.name),
      $('span','',good(f)?f.seconds+' s · '+(f.rate/1000)+' kHz':f.present?f.status:'fehlt'));
      it.append(g,$('span','pill '+(good(f)?'c-ok':'c-bad'),good(f)?'ok':'Problem'));l.append(it)}
    b.append(l,$('p','mute','zuletzt gespielt: '+(a.last_file||'–')))}));
  if(!maint)return c;
  c.append($('h3','','Ton tauschen'));const sel=$('select');for(const f of a.files){const o=$('option','',TONE[f.name]||f.name);o.value=f.name;sel.append(o)}
  const fp=picker('.wav,audio/wav');const b=btn('Hochladen','up');
  b.onclick=async()=>{const file=fp.file();if(!file){toast({ok:false,message:'Bitte zuerst eine WAV-Datei wählen.'});return}
    b.disabled=true;toast({ok:true,message:'lädt hoch …'});
    try{const r=await fetch('/ton/'+sel.value,{method:'PUT',body:file});toast(await r.json());load(true)}catch(e){toast({ok:false,message:'Fehler: '+e})}b.disabled=false};
  c.append(sel,fp.el,b,$('p','mute','16-Bit-PCM-WAV, höchstens 1 MB.'));paused=true;return c}
// FW-B-10: own file picker - a button and a name line that shortens long names at the front, so the end stays readable
function picker(accept){const inp=$('input');inp.type='file';inp.accept=accept;inp.hidden=true;const w=$('div','file');const name=$('span','','keine Datei gewählt');
  const b=btn('Datei wählen','','sec');b.onclick=()=>inp.click();inp.onchange=()=>{name.textContent=inp.files.length?front(inp.files[0].name,34):'keine Datei gewählt';name.title=inp.files.length?inp.files[0].name:''};
  w.append(name,b,inp);return {el:w,file:()=>inp.files[0]}}
function door(o,p,maint){
  const c=card('door','Türöffner');
  kv(c,'Zustand',!o.available?'abgeschaltet':o.ready?'bereit':'Sperrzeit läuft',!o.available?'bad':o.ready?'ok':'warn');
  kv(c,'Öffnungen seit Start',o.openings_since_boot);kv(c,'letzter Puls',o.last_pulse_ms?o.last_pulse_ms+' ms':'–');
  kv(c,'Puls / Sperrzeit',o.pulse_ms+' ms / '+o.lockout_ms+' ms');if(o.pulse_errors)kv(c,'Pulsfehler',o.pulse_errors,'bad');
  if(o.pin_high)kv(c,'Ausgang','AN','warn');
  if(maint){c.append($('h3','','Pulslänge'));const row=$('div','row');const i=$('input');i.type='number';i.min=100;i.max=5000;i.step=100;i.value=o.pulse_ms;
    const b=btn('Speichern');b.onclick=async()=>toast(await post('/door/pulse',{ms:i.value}));row.append(i,b);
    c.append(row,$('p','mute','100–5000 ms (Standard 2000). So lang, dass Ihr Türöffner sicher freigibt; danach mit Klingeln und Öffnen prüfen.'));paused=true}
  return c}
function maintCard(d){
  const st=d.settings,c=card('gear','Einstellungen');
  c.append(fold('led','LED-Abgleich · '+st.led_green_percent+' %',b=>{const row=$('div','row');const i=$('input');i.type='number';i.min=5;i.max=100;i.step=5;i.value=st.led_green_percent;
    const s=btn('Speichern');s.onclick=async()=>toast(await post('/settings',{led:i.value}));row.append(i,s);
    b.append(row,$('p','mute','5–100 % (Standard 40). Grün und Rot sollen gleich hell wirken – am besten mit der Front davor.'))}));
  c.append(fold('pw','Passwort der Statusseite · '+(st.own_password?'eigenes':'Gerätecode'),b=>pwform(b,st.own_password)));
  if(d.update&&d.update.keys)c.append(fold('keys','Update-Schlüssel · '+(d.update.keys.owner_only?'nur eigener':d.update.keys.owner?'Hersteller und eigener':'Hersteller'),b=>keyform(b,d.update.keys)));
  c.append(fold('fw','Firmware aktualisieren · '+d.system.version,b=>fwform(b,d.system.image)));
  paused=true;return c}
function keyform(s,k){
  const off=k.owner_only?' (abgeschaltet)':'';kv(s,'Hersteller, Hauptschlüssel',k.main+off,k.owner_only?'mute':'');
  kv(s,'Hersteller, Ersatzschlüssel',k.spare+off,k.owner_only?'mute':'');kv(s,'eigener Schlüssel',k.owner||'keiner');
  const i=$('input');i.placeholder='Kennung des eigenen Schlüssels (64 Hex-Zeichen)';i.autocomplete='off';i.maxLength=64;
  const b=btn('Eigenen Schlüssel speichern','','sec');
  b.onclick=async()=>{if(!confirm(i.value?'Eigenen Update-Schlüssel eintragen?':'Eigenen Update-Schlüssel entfernen?'))return;toast(await post('/settings',{ownerkey:i.value||'-'}));load(true)};
  s.append(i,b);
  if(k.owner){const c=btn(k.owner_only?'Herstellerschlüssel wieder annehmen':'Nur noch eigenen Schlüssel annehmen','','sec');
    c.onclick=async()=>{if(!confirm(k.owner_only?'Updates des Herstellers wieder annehmen?':'Updates des Herstellers nicht mehr annehmen? Danach installiert diese Einheit nur noch Dateien, die mit Ihrem eigenen Schlüssel signiert sind.'))return;
      toast(await post('/settings',{owneronly:k.owner_only?'0':'1'}));load(true)};s.append(c)}
  s.append($('p','mute','Nur nötig, wenn Sie Firmware selbst bauen und signieren. Kennung: „cb.py besitzer-schluessel“ oder „espsecure.py digest_sbv2_public_key“. Leer speichern entfernt den eigenen Schlüssel; der Werksreset ebenfalls.'))}
// MD5 (RFC 1321) for the password change: the new password never crosses the WiFi in clear text (cb_http_auth.h).
function md5(str){const K=[],S=[7,12,17,22,5,9,14,20,4,11,16,23,6,10,15,21];for(let i=0;i<64;i++)K[i]=Math.floor(Math.abs(Math.sin(i+1))*4294967296)|0;
  const b=Array.from(str,c=>c.charCodeAt(0)&255),n=b.length;b.push(128);while(b.length%64!==56)b.push(0);
  for(let i=0;i<8;i++)b.push(i<4?((n*8)>>>(8*i))&255:0);
  let a0=0x67452301,b0=0xefcdab89|0,c0=0x98badcfe|0,d0=0x10325476;
  for(let o=0;o<b.length;o+=64){const M=[];for(let i=0;i<16;i++)M[i]=b[o+4*i]|b[o+4*i+1]<<8|b[o+4*i+2]<<16|b[o+4*i+3]<<24;
    let A=a0,B=b0,C=c0,D=d0;
    for(let i=0;i<64;i++){let F,g;const r=i>>4;
      if(r===0){F=(B&C)|(~B&D);g=i}else if(r===1){F=(D&B)|(~D&C);g=(5*i+1)%16}else if(r===2){F=B^C^D;g=(3*i+5)%16}else{F=C^(B|~D);g=(7*i)%16}
      const s=S[r*4+i%4];const t=(A+F+K[i]+M[g])|0;A=D;D=C;C=B;B=(B+((t<<s)|(t>>>(32-s))))|0}
    a0=(a0+A)|0;b0=(b0+B)|0;c0=(c0+C)|0;d0=(d0+D)|0}
  return [a0,b0,c0,d0].map(v=>{let h='';for(let i=0;i<4;i++)h+=((v>>>(8*i))&255).toString(16).padStart(2,'0');return h}).join('')}
function pwform(s,own){
  // FW-B-13: the label calls it "Gerätecode" - the page uses the same word
  s.append($('p','mute',(own?'Ein eigenes Passwort ist gesetzt.':'Es gilt der Gerätecode vom Etikett.')+' Leer lassen und speichern = wieder der Gerätecode. Der Werksreset stellt ihn ebenfalls her.'));
  const f=(ph,ac)=>{const i=$('input');i.type='password';i.placeholder=ph;i.autocomplete=ac;i.maxLength=32;return i};
  const cur=f('aktuelles Passwort','current-password'),nw=f('neues Passwort (4–32 Zeichen)','new-password'),rp=f('neues Passwort wiederholen','new-password');
  const b=btn('Passwort speichern');
  b.onclick=async()=>{
    if(nw.value!==rp.value){toast({ok:false,message:'Die beiden neuen Passwörter sind verschieden.'});return}
    if(nw.value&&(nw.value.length<4||/[^\x20-\x7e]/.test(nw.value))){toast({ok:false,message:'4 bis 32 Zeichen, ohne Umlaute.'});return}
    if(!confirm(nw.value?'Passwort jetzt ändern?':'Wieder den Gerätecode verwenden?'))return;
    try{const n=(await (await fetch('/settings/pw-nonce',{cache:'no-store'})).json()).nonce;
      const ks=md5(cur.value+':'+n+':k1')+md5(cur.value+':'+n+':k2');let enc='';
      for(let i=0;i<32;i++){const c=i<nw.value.length?nw.value.charCodeAt(i):0;enc+=(c^parseInt(ks.substr(2*i,2),16)).toString(16).padStart(2,'0')}
      const r=await post('/settings/password',{enc,mac:md5(cur.value+':'+n+':'+nw.value)});toast(r);
      if(r.ok){cur.value=nw.value=rp.value='';setTimeout(()=>location.reload(),2500)}}catch(e){toast({ok:false,message:'Fehler: '+e})}};
  s.append(cur,nw,rp,b,$('p','mute','Mindestens 10 Zeichen sind sicherer. Keine Umlaute.'))}
function fwform(s,img){
  const fp=picker('.bin');const lb=$('label','chk');const older=$('input');older.type='checkbox';lb.append(older,'ältere Version zulassen (nur zum Zurückgehen)');
  const b=btn('Installieren','up');
  b.onclick=async()=>{const file=fp.file();if(!file){toast({ok:false,message:'Bitte zuerst eine Datei wählen.'});return}
    b.disabled=true;toast({ok:true,message:'wird übertragen und geprüft … (bis zu 1 min, Seite offen lassen)'});
    try{const r=await fetch('/update'+(older.checked?'?older=1':''),{method:'PUT',body:file});const j=await r.json();toast(j);
      if(j.ok)setTimeout(()=>location.reload(),25000)}catch(e){toast({ok:false,message:'Fehler: '+e})}b.disabled=false};
  s.append(fp.el,lb,b,$('p','mute','Nur signierte Dateien „clearbell-…-'+img+'.bin“ von der Release-Seite. Bei Problemen kehrt die Einheit von selbst zur alten Firmware zurück.'))}
function diagnosis(d){
  const c=card('chip','Diagnose'),sy=d.system;
  if(d.radio){const r=d.radio;c.append(fold('radio','Funk',b=>{kv(b,'WLAN',{up:'verbunden',connecting:'verbindet',waiting:'wartet'}[r.wifi]||r.wifi,r.wifi==='up'?'ok':'bad');
    kv(b,'Signal',r.wifi==='up'?r.rssi+' dBm':'–');kv(b,'Kanal',r.channel||'–');kv(b,'Access Point',r.bssid||'–');kv(b,'IP',r.ip||'–');
    kv(b,'Name',r.hostname+'.local');kv(b,'Verbindungsabrisse',r.disconnects);kv(b,'Modem-Sleep',r.modem_sleep?'an':'aus')}))}
  if(d.protocol){const p=d.protocol;c.append(fold('proto','Protokoll',b=>{for(const x of p.peers)kv(b,flat(x.unit),x.reachable?'erreichbar':'NICHT erreichbar',x.reachable?'ok':'bad');
    kv(b,'gesendet / wiederholt',p.tx+' / '+p.retries);kv(b,'Klingeln quittiert / verloren',p.rings_ok+' / '+p.rings_failed,p.rings_failed?'warn':'');
    kv(b,'Klingeln ohne freien Slot',p.rings_no_slot);kv(b,'empfangen gültig',p.rx_ok);kv(b,'verworfen: falsche Signatur',p.rx_bad_tag,p.rx_bad_tag?'warn':'');
    kv(b,'verworfen: sonstige',p.rx_bad_other);kv(b,'Duplikate / zu alt',p.rx_duplicate+' / '+p.rx_too_old);kv(b,'ratenbegrenzt',p.rx_rate_limited,p.rx_rate_limited?'warn':'');
    kv(b,'Sitzungen bestätigt',p.sessions_accepted);if(d.door)kv(b,'Türbefehle geöffnet / abgelehnt',p.door_opened+' / '+(p.door_refused+p.door_bad_nonce+p.door_failed))}))}
  const t=d.touch;c.append(fold('touch','Touch-Tasten',b=>{const tb=$('table');const h=$('tr');for(const x of ['Taste','Rohwert','Baseline','Abw.'])h.append($('th','',x));tb.append(h);
    for(const ch of t.channels){const tr=$('tr');tr.append($('td','',(KEY[ch.name]?KEY[ch.name]+' · '+flat(ch.name):ch.name)+' ('+(ST[ch.state]||ch.state)+')'),$('td','',ch.raw),$('td','',Math.round(ch.baseline)),
      $('td',ch.state==='pressed'?'warn':'',ch.delta_percent.toFixed(1)+' %'));tb.append(tr)}
    b.append(tb,$('p','mute','Gedrückt ab +'+t.on_percent+' %, losgelassen unter +'+t.off_percent+' %. '+
      (t.armed===undefined?'Beide Tasten zugleich abgewiesen: '+t.rejected_both:'Scharfschaltfenster: '+(t.armed?'offen':'zu')+', abgewiesen: '+t.rejected)+(t.simulated?' · SIMULATION läuft':'')))}));
  if(d.remote&&d.remote.outage){const o=d.remote.outage;c.append(fold('outage','Ausfallmeldung',b=>{kv(b,'überwacht',o.watching?'ja':'nein (nicht gekoppelt)');
    kv(b,'Außeneinheit',o.outdoor_reachable?'erreichbar':'NICHT erreichbar',o.outdoor_reachable?'ok':'bad');kv(b,'Meldungen gesendet',o.notices);
    kv(b,'Push gesendet / fehlgeschlagen',o.pushes_ok+' / '+o.pushes_failed,o.pushes_failed?'warn':'');
    b.append($('p','mute','Antwortet die Außeneinheit 2 min nicht, bekommen die Handys dieser Wohnung eine Nachricht – und wieder, wenn sie zurück ist.'))}))}
  const a=d.audio;c.append(fold('audio','Audio',b=>{kv(b,'bereit',a.ready?'ja':'nein',a.ready?'ok':'bad');kv(b,'gespielt / Fehler',a.played+' / '+a.failed,a.failed?'warn':'')}));
  c.append(fold('sys','System',b=>{kv(b,'Firmware',sy.version);kv(b,'gebaut',sy.built);kv(b,'Rolle',sy.role);kv(b,'Laufzeit',dur(sy.uptime_s));kv(b,'Start Nr.',sy.boot);
    kv(b,'letzter Neustart',sy.reset_reason,/WATCHDOG|PANIC|BROWNOUT/.test(sy.reset_reason)?'bad':'');
    kv(b,'RAM frei (kleinster Wert)',kb(sy.heap_free)+' ('+kb(sy.heap_min)+')',sy.heap_min<40960?'bad':'');kv(b,'PSRAM frei',kb(sy.psram_free));
    kv(b,'Firmware-Slot',sy.ota_slot+(sy.update_pending?' (Update unbestätigt)':''));kv(b,'Absturzabbild',sy.coredump?'vorhanden':'keins',sy.coredump?'bad':'ok');
    kv(b,'Selbsttest',sy.selftest,sy.selftest==='ok'?'ok':'bad');if(d.update&&d.update.probation)kv(b,'Update','Probezeit läuft (bis 5 min)','warn')}));
  return c}
let paused=false;
async function load(force){if(paused&&!force)return;try{const r=await fetch('/status.json',{cache:'no-store'});
  // Password changed elsewhere: stop polling at once - every further try would count towards the 30-s lock.
  if(r.status===401||r.status===429){paused=true;byId('err').textContent=
    r.status===401?'Anmeldung nicht mehr gültig (Passwort geändert?) – Seite neu laden und neu anmelden.':'Zu viele falsche Passwörter – 30 s warten, dann Seite neu laden.';return}
  if(!r.ok)throw new Error('HTTP '+r.status);
  paused=false;render(await r.json());byId('err').textContent=''}catch(e){byId('err').textContent='Keine Verbindung zur Einheit: '+e.message}}
byId('logo').append(ic('bell'));load(true);setInterval(()=>load(false),5000);
</script></body></html>
)HTML";
