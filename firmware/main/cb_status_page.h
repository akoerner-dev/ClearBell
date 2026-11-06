// cb_status_page.h - the HTML of the status page (cb_status.cpp serves it at "/").
//
// One self-contained page, no external files (the unit has no internet obligation, and the Content Security
// Policy forbids everything foreign). It loads /status.json every 5 s and builds the tables from it with
// textContent only - nothing from the JSON is ever interpreted as HTML. The page is German (it is for the
// household); the JSON keys stay English like the rest of the firmware.
//
// To change the look: edit the string below, rebuild. To preview without a board, save the string as
// index.html next to a status.json (from `cb.py status <unit> --json`) and open it via a local web server.
#pragma once

static const char kStatusPage[] = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ClearBell Status</title>
<style>
:root{--bg:#f6f6f4;--card:#fff;--fg:#1d1d1b;--mute:#6b6b66;--line:#e2e2dd;--ok:#2f7d32;--bad:#b3261e;--warn:#a15c00}
@media (prefers-color-scheme:dark){:root{--bg:#161615;--card:#21211f;--fg:#ececea;--mute:#9a9a94;--line:#33332f;--ok:#7bc47f;--bad:#f28b82;--warn:#f0b35a}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:15px/1.45 system-ui,sans-serif}
header{padding:16px;display:flex;flex-wrap:wrap;gap:8px 16px;align-items:baseline}
h1{font-size:20px;margin:0}#sub{color:var(--mute)}
main{display:grid;gap:12px;padding:0 16px 24px;grid-template-columns:repeat(auto-fill,minmax(300px,1fr))}
section{background:var(--card);border:1px solid var(--line);border-radius:10px;padding:12px 14px}
h2{font-size:15px;margin:0 0 8px}table{border-collapse:collapse;width:100%}
td,th{padding:3px 0;border-bottom:1px solid var(--line);vertical-align:top;text-align:left}
td:last-child,th:last-child{text-align:right;font-variant-numeric:tabular-nums}
tr:last-child td{border-bottom:0}.ok{color:var(--ok)}.bad{color:var(--bad)}.warn{color:var(--warn)}
.mute{color:var(--mute);font-size:13px}#err{color:var(--bad);padding:0 16px}
input,select,button{font:inherit;margin:4px 4px 0 0}button{padding:4px 12px}section div h2{margin-top:14px}
</style></head><body>
<header><h1 id="title">ClearBell</h1><span id="sub">lädt …</span></header>
<div id="err"></div>
<main id="main"></main>
<script>
"use strict";
const $=(t,c,x)=>{const e=document.createElement(t);if(c)e.className=c;if(x!==undefined)e.textContent=x;return e};
const yesno=(v,good=true)=>[v?'ja':'nein',v===good?'ok':'bad'];
function card(title,rows,note){
  const s=$('section');s.append($('h2','',title));const t=$('table');
  for(const r of rows){if(!r)continue;const tr=$('tr');tr.append($('td','',r[0]));
    const v=Array.isArray(r[1])?r[1]:[r[1]];const td=$('td',v[1]||'',v[0]===undefined||v[0]===null?'–':String(v[0]));tr.append(td);t.append(tr)}
  s.append(t);if(note)s.append($('p','mute',note));return s}
function table(title,head,rows,note){
  const s=$('section');s.append($('h2','',title));const t=$('table');const h=$('tr');
  for(const x of head)h.append($('th','',x));t.append(h);
  for(const r of rows){const tr=$('tr');for(const c of r){const v=Array.isArray(c)?c:[c];tr.append($('td',v[1]||'',String(v[0])))}t.append(tr)}
  if(!rows.length){const tr=$('tr');const td=$('td','mute','keine Einträge');td.colSpan=head.length;tr.append(td);t.append(tr)}
  s.append(t);if(note)s.append($('p','mute',note));return s}
const dur=s=>{const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);return (d?d+' d ':'')+h+' h '+m+' min'};
const ev={'opened':['geöffnet','ok'],'remote':['per App geöffnet','warn'],'lock-out':['abgelehnt: Sperrzeit','warn'],'refused':['abgelehnt','bad'],
 'no answer':['keine Antwort','bad'],'service':['Service (USB)','warn']};
const st={'pressed':['gedrückt','warn'],'rest':['Ruhe','ok'],'learning':['lernt','warn']};
function render(d){
  const m=document.getElementById('main');m.replaceChildren();const sy=d.system;
  document.getElementById('title').textContent='ClearBell '+(sy.name||sy.image);
  const mode={normal:'Normalbetrieb',maintenance:'Wartung',setup:'Einrichtung'}[sy.mode]||sy.mode;
  document.getElementById('sub').textContent=mode+' · '+(sy.time||'Uhrzeit unbekannt (kein NTP)')+' · aktualisiert '+new Date().toLocaleTimeString('de-DE');
  if(d.radio){const r=d.radio;m.append(card('Funk',[
    ['WLAN',[{up:'verbunden',connecting:'verbindet',waiting:'wartet'}[r.wifi]||r.wifi,r.wifi==='up'?'ok':'bad']],
    ['Signal',r.wifi==='up'?[r.rssi+' dBm',r.rssi>-70?'ok':r.rssi>-80?'warn':'bad']:'–'],
    ['Kanal',r.channel||'–'],['Access Point',r.bssid||'–'],['IP',r.ip||'–'],['Name',r.hostname+'.local'],
    ['Verbindungsabrisse',r.disconnects],['Modem-Sleep',r.modem_sleep?'an':'aus']]))}
  if(d.protocol){const p=d.protocol;const rows=p.peers.map(x=>[x.unit,[x.reachable?'erreichbar':'NICHT erreichbar',x.reachable?'ok':'bad']]);
    m.append(card('Protokoll',rows.concat([
    ['gesendet / wiederholt',p.tx+' / '+p.retries],['Klingeln quittiert / verloren',[p.rings_ok+' / '+p.rings_failed,p.rings_failed?'warn':'']],
    ['Klingeln ohne freien Slot',p.rings_no_slot],['empfangen gültig',p.rx_ok],
    ['verworfen: falsche Signatur',[p.rx_bad_tag,p.rx_bad_tag?'warn':'']],['verworfen: sonstige',p.rx_bad_other],
    ['Duplikate / zu alt',p.rx_duplicate+' / '+p.rx_too_old],['ratenbegrenzt',[p.rx_rate_limited,p.rx_rate_limited?'warn':'']],
    ['Sitzungen bestätigt',p.sessions_accepted],
    d.door?['Türbefehle: geöffnet / abgelehnt',p.door_opened+' / '+(p.door_refused+p.door_bad_nonce+p.door_failed)]:null])))}
  const t=d.touch;m.append(table('Touch',['Taste','Rohwert','Baseline','Abweichung'],
    t.channels.map(c=>[c.name+' ('+(st[c.state]||[c.state])[0]+')',c.raw,Math.round(c.baseline),[c.delta_percent.toFixed(1)+' %',c.state==='pressed'?'warn':'']]),
    'Gedrückt ab +'+t.on_percent+' %, losgelassen unter +'+t.off_percent+' %. '+
    (t.armed===undefined?'Beide Tasten zugleich abgewiesen: '+t.rejected_both:('Scharfschaltfenster: '+(t.armed?'offen':'zu')+', abgewiesen: '+t.rejected))+
    (t.simulated?' · SIMULATION läuft':'')));
  if(d.door){const o=d.door;const dc=card('Türöffner',[
    ['bereit',o.available?(o.ready?['ja','ok']:['Sperrzeit läuft','warn']):['ABGESCHALTET','bad']],
    ['Ausgang',o.pin_high?['AN','warn']:['aus','ok']],['Öffnungen seit Start',o.openings_since_boot],
    ['letzter Puls',o.last_pulse_ms?o.last_pulse_ms+' ms':'–'],['Pulsfehler',[o.pulse_errors,o.pulse_errors?'bad':'ok']],
    ['Puls / Sperrzeit',o.pulse_ms+' ms / '+o.lockout_ms+' ms']]);
    if(sy.mode==='maintenance')dc.append(pulseform(o.pulse_ms));m.append(dc)}
  if(d.pairing)m.append(pairing(d.pairing,sy.mode));
  if(d.phones&&d.phones.available)m.append(phones(d.phones,sy.mode));
  if(d.remote&&d.remote.enabled){const f=d.remote;m.append(card('Fern-Öffnen (Push)',[
    ['Handys',f.phones],['Befehlskanal',f.stream_up?['verbunden','ok']:['getrennt','bad']],
    ['letzte Daten vor',f.last_data_s+' s'],['offene Klingel-Token',f.tokens_active],
    ['Push gesendet / fehlgeschlagen',[f.pushes_ok+' / '+f.pushes_failed,f.pushes_failed?'warn':'']],
    ['Befehle empfangen',f.commands],['per App geöffnet',f.opened],
    ['falsche Signatur',[f.bad_signature,f.bad_signature?'bad':'ok']],['sonst abgewiesen',f.rejected_other]],
    'Nur EG. Die Tür öffnet nur mit der Signatur eines eingetragenen Handys und einem frischen Klingel-Token.'))}
  const l=d.doorlog;m.append(table('Türöffnungen',['Zeit','Ergebnis'],l.entries.map(e=>[e.time,ev[e.event]||[e.event]]),
    (l.indoor_key_openings!==undefined?'Öffnungen über die Innentasten (nur als Summe, ohne Zeit und Wohnung): '+l.indoor_key_openings+'. ':'Nur die Türbefehle dieser Wohnung. ')+
    'Insgesamt '+l.total+' Einträge, die neuesten 32 werden gezeigt.'));
  const a=d.audio;const files=a.files.map(f=>[f.name,f.present?(f.status==='ok'?f.seconds+' s, '+(f.rate/1000)+' kHz':[f.status,'bad']):['fehlt','bad']]);
  const ac=card('Audio',[['bereit',yesno(a.ready)],['Lautstärke',a.volume+' %'],['zuletzt',a.last_file],
    ['gespielt / Fehler',a.played+' / '+a.failed],['Speicher',Math.round(a.fs_used/1024)+' von '+Math.round(a.fs_total/1024)+' KB']].concat(files));
  if(sy.mode==='maintenance'){ac.append(upload(a.files))}else{ac.append($('p','mute','Töne tauschen: Wartung einschalten (Boot-Taster 5 s halten).'))}
  m.append(ac);
  const sc=card('System',[['Firmware',sy.version+' ('+sy.image+')'],['Rolle',sy.role],['Laufzeit',dur(sy.uptime_s)],['Start Nr.',sy.boot],
    ['letzter Neustart',[sy.reset_reason,/WATCHDOG|PANIC|BROWNOUT/.test(sy.reset_reason)?'bad':'']],
    ['RAM frei (kleinster Wert)',[Math.round(sy.heap_free/1024)+' KB ('+Math.round(sy.heap_min/1024)+' KB)',sy.heap_min<40960?'bad':'']],
    ['PSRAM frei',Math.round(sy.psram_free/1024)+' KB'],
    ['Firmware-Slot',sy.ota_slot+(sy.update_pending?' (Update unbestätigt)':'')],['Absturzabbild',sy.coredump?['vorhanden','bad']:['keins','ok']],
    ['Selbsttest',[sy.selftest,sy.selftest==='ok'?'ok':'bad']],
    d.update&&d.update.probation?['Update','Probezeit läuft (bis 5 min)']:null]);
  if(sy.mode==='maintenance')sc.append(fwform(sy.image));
  m.append(sc)}
async function post(url,data){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});
  try{return await r.json()}catch(e){return {ok:false,message:'HTTP '+r.status}}}
function show(el,r){el.textContent=r.message;el.className=r.ok?'ok':'bad'}
function pulseform(ms){const s=$('div');s.append($('h2','','Pulslänge einstellen'));const i=$('input');i.type='number';i.min=100;i.max=5000;i.step=100;i.value=ms;
  const b=$('button','','Speichern');const o=$('p','mute','100–5000 ms. So lang, dass Ihr Türöffner sicher freigibt; danach mit „door-test“ bzw. Klingeln prüfen.');
  b.onclick=async()=>{show(o,await post('/door/pulse',{ms:i.value}))};s.append(i,b,o);paused=true;return s}
function pairing(p,mode){
  const s=$('section');s.append($('h2','','Kopplung'));const t=$('table');
  const row=(a,b)=>{const tr=$('tr');tr.append($('td','',a));const v=Array.isArray(b)?b:[b];tr.append($('td',v[1]||'',v[0]));t.append(tr)};
  if(p.paired!==undefined){  // indoor unit
    row('Außeneinheit',p.paired?['gekoppelt'+(p.outdoor?' ('+p.outdoor+')':''),'ok']:['nicht gekoppelt','bad']);s.append(t);
    if(!p.paired)s.append($('p','mute','Koppeln an der Außeneinheit: dort „Inneneinheiten suchen“ und den Gerätecode vom Etikett DIESER Einheit eingeben.'));
    if(mode==='maintenance'&&p.paired){const b=$('button','','Wartung der Außeneinheit öffnen');const o=$('p','mute','Öffnet die Wartung der Außeneinheit für 10 min – Töne, Update, Pulslänge – ohne das Außengehäuse zu öffnen.');
      b.onclick=async()=>{show(o,await post('/outdoor-maint',{go:'1'}))};s.append(b,o)}
    return s}
  for(const f of ['eg','og']){row(f.toUpperCase(),p[f+'_paired']?['gekoppelt'+(p[f]?' ('+p[f]+')':''),'ok']:['nicht gekoppelt','warn']);}
  s.append(t);
  if(mode==='maintenance')for(const f of ['eg','og'])if(p[f+'_paired']){const b=$('button','','Kopplung '+f.toUpperCase()+' entfernen');const o=$('p','mute','');
    b.onclick=async()=>{if(confirm('Kopplung '+f.toUpperCase()+' wirklich entfernen?'))show(o,await post('/pair/remove',{flat:f}))};s.append(b,o)}
  const b=$('button','','Inneneinheiten suchen');const list=$('div');
  b.onclick=async()=>{paused=true;list.textContent='suche … (einige Sekunden)';try{const r=await (await fetch('/pair/list',{cache:'no-store'})).json();list.replaceChildren();
    if(!r.units.length)list.textContent='Keine Inneneinheit im Netz gefunden – ist sie eingerichtet (WLAN, Wohnung)?';
    for(const u of r.units){const d=$('div');d.append($('p','',u.flat.toUpperCase()+' · Gerätenummer '+u.id+(u.paired?' · schon gekoppelt':'')));
      const c=$('input');c.placeholder='Gerätecode vom Etikett der Inneneinheit';c.autocomplete='off';const k=$('button','','Koppeln');const o=$('p','mute','');
      k.onclick=async()=>{o.textContent='kopple …';show(o,await post('/pair/start',{ip:u.ip,id:u.id,flat:u.flat,code:c.value}))};d.append(c,k,o);list.append(d)}}
    catch(e){list.textContent='Suche fehlgeschlagen.'}};
  s.append($('p','mute','Erst-Kopplung einer Wohnung: Gerätecode der Inneneinheit genügt. Ersetzen einer bestehenden Kopplung: nur in der Wartung.'),b,list);
  return s}
function phones(ph,mode){
  const s=$('section');s.append($('h2','','Handys (Fern-Öffnen)'));const t=$('table');
  for(const id of ph.list){const tr=$('tr');tr.append($('td','',id));const td=$('td');
    if(mode==='maintenance'){const b=$('button','','Entfernen');b.onclick=async()=>{if(confirm('Handy '+id+' entfernen? Es kann die Tür dann nicht mehr öffnen.')){const r=await post('/phone/remove',{name:id});alert(r.message);load()}};td.append(b)}
    tr.append(td);t.append(tr)}
  if(!ph.list.length){const tr=$('tr');tr.append($('td','mute','noch keine Handys'));t.append(tr)}
  s.append(t);
  if(mode!=='maintenance'){s.append($('p','mute','Handy hinzufügen oder entfernen: Wartung einschalten (Boot-Taster 5 s) und diese Seite auf dem Handy öffnen.'));return s}
  const n=$('input');n.placeholder='Name des Handys, z. B. Anna';n.maxLength=8;const b=$('button','','Handy hinzufügen');const out=$('div');
  b.onclick=async()=>{out.textContent='trage ein …';const r=await post('/phone/add',{name:n.value});out.replaceChildren();const m=$('p',r.ok?'ok':'bad',r.message);out.append(m);
    if(r.ok){const a1=document.createElement('a');a1.href=r.ntfy;a1.textContent='1 · ntfy abonnieren';const a2=document.createElement('a');a2.href=r['import'];a2.textContent='2 · Kurzbefehl importieren';
      for(const a of [a1,a2]){a.style.display='block';a.style.margin='8px 0';a.style.fontWeight='600';out.append(a)}
      out.append($('p','mute','Vorher die Apps „ntfy“ und „HTTP Shortcuts“ installieren. Beide Links funktionieren nur auf dem Handy im Heim-WLAN; der zweite 10 min lang.'))}};
  s.append(n,b,out);paused=true;return s}
function fwform(img){
  const s=$('div');s.append($('h2','','Firmware aktualisieren'));const inp=$('input');inp.type='file';inp.accept='.bin';
  const b=$('button','','Installieren');const out=$('p','mute','Datei „clearbell-…-'+img+'.bin“ von der Release-Seite. Bei Problemen kehrt die Einheit von selbst zur alten Firmware zurück.');
  b.onclick=async()=>{if(!inp.files.length){out.textContent='Bitte eine Datei wählen.';return}
    out.textContent='wird übertragen … (bis zu 1 min, Seite offen lassen)';out.className='mute';
    try{const r=await fetch('/update',{method:'PUT',body:inp.files[0]});const j=await r.json();out.textContent=j.message;out.className=j.ok?'ok':'bad';
      if(j.ok)setTimeout(()=>location.reload(),25000)}catch(e){out.textContent='Fehler: '+e;out.className='bad'}};
  s.append(inp,b,out);paused=true;return s}
function upload(files){
  const s=$('div');s.append($('h2','','Ton tauschen'));const sel=$('select');
  for(const f of files){const o=$('option','',f.name);o.value=f.name;sel.append(o)}
  const inp=$('input');inp.type='file';inp.accept='.wav,audio/wav';const b=$('button','','Hochladen');const out=$('p','mute','16-Bit-PCM-WAV, höchstens 1 MB.');
  b.onclick=async()=>{if(!inp.files.length){out.textContent='Bitte eine Datei wählen.';return}
    out.textContent='lädt hoch …';try{const r=await fetch('/ton/'+sel.value,{method:'PUT',body:inp.files[0]});
    const j=await r.json();out.textContent=j.message;out.className=j.ok?'ok':'bad';if(j.ok)load()}catch(e){out.textContent='Fehler: '+e;out.className='bad'}};
  s.append(sel,inp,b,out);paused=true;return s}
let paused=false;
async function load(){try{const r=await fetch('/status.json',{cache:'no-store'});if(!r.ok)throw new Error('HTTP '+r.status);
  render(await r.json());document.getElementById('err').textContent=''}catch(e){document.getElementById('err').textContent='Keine Daten: '+e.message}}
load();setInterval(()=>{if(!paused)load()},5000);
</script></body></html>
)HTML";
