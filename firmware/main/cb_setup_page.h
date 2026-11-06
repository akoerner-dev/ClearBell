// cb_setup_page.h - the setup page (cb_setup / cb_status serve it at "/" in setup mode or over the access point).
// One self-contained page like the status page: no external files, values set with textContent only.
#pragma once

static const char kSetupPage[] = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ClearBell einrichten</title>
<style>
:root{--bg:#f6f6f4;--card:#fff;--fg:#1d1d1b;--mute:#6b6b66;--line:#e2e2dd;--ok:#2f7d32;--bad:#b3261e;--acc:#1f5fa8}
@media (prefers-color-scheme:dark){:root{--bg:#161615;--card:#21211f;--fg:#ececea;--mute:#9a9a94;--line:#33332f;--ok:#7bc47f;--bad:#f28b82;--acc:#8ab4f8}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.5 system-ui,sans-serif}
main{max-width:520px;margin:0 auto;padding:16px}h1{font-size:22px;margin:4px 0}h2{font-size:17px;margin:0 0 8px}
section{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin:12px 0}
label{display:block;margin:10px 0 4px;font-weight:600}input,select,button{font:inherit;width:100%;padding:10px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--fg)}
button{background:var(--acc);color:#fff;border:0;margin-top:12px;font-weight:600}button.sec{background:transparent;color:var(--acc);border:1px solid var(--acc)}
button.danger{background:var(--bad)}.mute{color:var(--mute);font-size:14px}.ok{color:var(--ok)}.bad{color:var(--bad)}
.net{display:flex;justify-content:space-between;padding:8px 4px;border-bottom:1px solid var(--line);cursor:pointer}.net:hover{background:var(--bg)}
.radio{display:flex;gap:12px}.radio label{font-weight:400;display:flex;gap:6px;align-items:center}.radio input{width:auto}
</style></head><body><main>
<h1>ClearBell einrichten</h1><p class="mute" id="who">…</p>
<section id="state" hidden></section>
<section id="setup"><h2>1 · WLAN</h2>
<button class="sec" id="scanb">WLAN-Netze suchen</button><div id="nets"></div>
<label for="ssid">WLAN-Name</label><input id="ssid" autocomplete="off" maxlength="32">
<label for="pass">WLAN-Passwort</label><input id="pass" type="password" maxlength="63">
<div id="rolebox" hidden><h2 style="margin-top:16px">2 · Wohnung</h2><div class="radio">
<label><input type="radio" name="role" value="eg">Erdgeschoss (EG)</label><label><input type="radio" name="role" value="og">Obergeschoss (OG)</label></div></div>
<button id="save">Speichern und verbinden</button><p id="out" class="mute"></p></section>
<section><h2>Werkszustand</h2><p class="mute">Löscht WLAN, Kopplung, Handys, eigene Töne und Einstellungen. Der Gerätecode auf dem Etikett bleibt gültig.</p>
<button class="danger" id="reset">Auf Werkszustand zurücksetzen</button><p id="rout" class="mute"></p></section>
</main><script>
"use strict";const $=id=>document.getElementById(id);
async function post(url,data){const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});return r.json()}
async function info(){const i=await (await fetch('/setup/info',{cache:'no-store'})).json();
  $('who').textContent=(i.image==='aussen'?'Außeneinheit':'Inneneinheit')+' · Gerätenummer '+i.serial;
  $('rolebox').hidden=i.image!=='innen';
  if(i.configured){const s=$('state');s.hidden=false;s.replaceChildren();const h=document.createElement('h2');h.textContent='Eingerichtet';
    const p=document.createElement('p');p.textContent='WLAN „'+i.ssid+'“'+(i.role?' · '+i.role:'')+(i.wifi_up?' · verbunden, erreichbar unter http://'+i.hostname:' · zurzeit NICHT verbunden');
    p.className=i.wifi_up?'ok':'bad';s.append(h,p);$('ssid').value=i.ssid}
  if(!i.may_change){$('save').disabled=true;$('reset').disabled=true;$('out').textContent='Ändern nur in der Einrichtung: Boot-Taster zweimal 5 s halten.'}}
$('scanb').onclick=async()=>{$('scanb').textContent='suche … (einige Sekunden)';const n=$('nets');n.replaceChildren();
  try{const r=await (await fetch('/setup/scan',{cache:'no-store'})).json();
    for(const w of r.networks){const d=document.createElement('div');d.className='net';const a=document.createElement('span');a.textContent=w.ssid+(w.open?' (offen)':'');
      const b=document.createElement('span');b.className='mute';b.textContent=w.rssi>-60?'stark':w.rssi>-75?'mittel':'schwach';d.append(a,b);
      d.onclick=()=>{$('ssid').value=w.ssid;$('pass').focus()};n.append(d)}
    if(!r.networks.length)n.textContent='Keine Netze gefunden.'}catch(e){n.textContent='Suche fehlgeschlagen.'}
  $('scanb').textContent='WLAN-Netze suchen'};
$('save').onclick=async()=>{const role=document.querySelector('input[name=role]:checked');
  const r=await post('/setup/save',{ssid:$('ssid').value,pass:$('pass').value,role:role?role.value:''});
  $('out').textContent=r.message;$('out').className=r.ok?'ok':'bad'};
$('reset').onclick=async()=>{if(!confirm('Wirklich alles löschen und auf Werkszustand zurücksetzen?'))return;
  const r=await post('/setup/reset',{confirm:'JA'});$('rout').textContent=r.message;$('rout').className=r.ok?'ok':'bad'};
info();
</script></body></html>
)HTML";
