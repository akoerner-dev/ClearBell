// cb_setup_page.h - the setup page (cb_setup / cb_status serve it at "/" in setup mode or over the access point).
// One self-contained page like the status page (same look since 12.11.2026): no external files, values set with
// textContent only, the constant icons via innerHTML.
#pragma once

static const char kSetupPage[] = R"HTML(<!doctype html>
<html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="light dark"><title>ClearBell einrichten</title>
<style>
:root{--bg:#f3f5f4;--card:#fff;--fg:#17201b;--mute:#66706a;--line:#e3e8e5;--soft:#eef2f0;--acc:#1d7a4c;--acc-fg:#fff;--acc-soft:#e1f1e8;
 --ok:#1f8a4c;--ok-soft:#e2f4e9;--bad:#c0392b;--bad-soft:#fbe6e3;--shadow:0 1px 2px rgba(16,24,20,.06),0 4px 14px rgba(16,24,20,.05)}
@media (prefers-color-scheme:dark){:root{--bg:#0f1311;--card:#181e1b;--fg:#e8eeea;--mute:#98a39c;--line:#27302b;--soft:#1f2723;--acc:#3fb37a;
 --acc-fg:#07170e;--acc-soft:#1b3326;--ok:#5fcf8f;--ok-soft:#173022;--bad:#f2867a;--bad-soft:#3a1d1a;--shadow:none}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
svg{width:20px;height:20px;flex:none;fill:none;stroke:currentColor;stroke-width:2;stroke-linecap:round;stroke-linejoin:round}
main{max-width:520px;margin:0 auto;padding:20px 16px 40px}
.top{display:flex;align-items:center;gap:12px;margin-bottom:16px}.logo{width:44px;height:44px;border-radius:14px;background:var(--acc);color:var(--acc-fg);display:grid;place-items:center}
h1{font-size:20px;margin:0;line-height:1.2}#who{font-size:14px;color:var(--mute)}
section{background:var(--card);border:1px solid var(--line);border-radius:18px;box-shadow:var(--shadow);padding:18px;margin:0 0 16px}
h2{display:flex;align-items:center;gap:10px;font-size:17px;margin:0 0 12px}.n{width:28px;height:28px;border-radius:50%;background:var(--acc-soft);color:var(--acc);display:grid;place-items:center;font-size:14px;font-weight:700}
label{display:block;margin:14px 0 6px;font-weight:600;font-size:14px}
input,button{font:inherit;color:inherit;width:100%;padding:12px 14px;border-radius:12px;border:1px solid var(--line);background:var(--bg)}
button{display:flex;align-items:center;justify-content:center;gap:8px;background:var(--acc);color:var(--acc-fg);border:0;margin-top:14px;font-weight:650;cursor:pointer}
button.sec{background:var(--soft);color:var(--fg)}button.danger{background:var(--bad-soft);color:var(--bad)}button:disabled{opacity:.5}
.mute{color:var(--mute);font-size:14px}.ok{color:var(--ok)}.bad{color:var(--bad)}
.nets{margin-top:6px}.net{display:flex;align-items:center;gap:12px;padding:12px 4px;border-top:1px solid var(--line);cursor:pointer}
.net:first-child{border-top:0}.net:hover{background:var(--soft)}.net b{flex:1;font-weight:600}.bars{display:flex;gap:2px;align-items:flex-end;height:16px}
.bars i{width:4px;border-radius:1px;background:var(--line)}.bars i.on{background:var(--acc)}
.radio{display:grid;grid-template-columns:1fr 1fr;gap:10px}.radio label{margin:0;display:flex;gap:10px;align-items:center;padding:14px;border:1px solid var(--line);border-radius:12px;font-weight:600;cursor:pointer}
.radio input{width:auto;margin:0;accent-color:var(--acc)}.radio label:has(input:checked){border-color:var(--acc);background:var(--acc-soft)}
.state{display:flex;gap:12px;align-items:flex-start}.state .i{width:36px;height:36px;border-radius:12px;display:grid;place-items:center;flex:none}
#out:empty,#rout:empty{display:none}#out,#rout{margin:12px 0 0;padding:10px 12px;border-radius:12px;font-size:14px}
#out.ok,#rout.ok{background:var(--ok-soft)}#out.bad,#rout.bad{background:var(--bad-soft)}
:focus-visible{outline:2px solid var(--acc);outline-offset:2px}
</style></head><body><main>
<div class="top"><div class="logo" id="logo"></div><div><h1>ClearBell einrichten</h1><div id="who">…</div></div></div>
<section id="state" hidden></section>
<section id="setup"><h2><span class="n">1</span>WLAN</h2>
<button class="sec" id="scanb">WLAN-Netze suchen</button><div class="nets" id="nets"></div>
<label for="ssid">WLAN-Name</label><input id="ssid" autocomplete="off" maxlength="32">
<label for="pass">WLAN-Passwort</label><input id="pass" type="password" maxlength="63">
<div id="rolebox" hidden><h2 style="margin-top:22px"><span class="n">2</span>Wohnung</h2><div class="radio">
<label><input type="radio" name="role" value="eg">Erdgeschoss (EG)</label><label><input type="radio" name="role" value="og">Obergeschoss (OG)</label></div></div>
<button id="save">Speichern und verbinden</button><p id="out" class="mute"></p></section>
<section><h2>Werkszustand</h2><p class="mute">Löscht WLAN, Kopplung, Handys, eigene Töne und Einstellungen. Der Gerätecode auf dem Etikett bleibt gültig.</p>
<button class="danger" id="reset">Auf Werkszustand zurücksetzen</button><p id="rout" class="mute"></p></section>
</main><script>
"use strict";const $=id=>document.getElementById(id);
const I={bell:'<path d="M6 8a6 6 0 0 1 12 0c0 7 3 9 3 9H3s3-2 3-9"/><path d="M10.3 21a1.9 1.9 0 0 0 3.4 0"/>',
 ok:'<path d="M20 6 9 17l-5-5"/>',warn:'<path d="M12 9v4M12 17h.01"/><path d="M10.3 3.9 1.8 18a2 2 0 0 0 1.7 3h17a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/>',
 search:'<circle cx="11" cy="11" r="7"/><path d="m20 20-4-4"/>'};
const ic=n=>{const s=document.createElementNS('http://www.w3.org/2000/svg','svg');s.setAttribute('viewBox','0 0 24 24');s.innerHTML=I[n];return s};
const el=(t,c,x)=>{const e=document.createElement(t);if(c)e.className=c;if(x!==undefined)e.textContent=x;return e};
async function post(url,data){try{const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:new URLSearchParams(data)});return await r.json()}
  catch(e){return {ok:false,message:'Keine Verbindung zur Einheit.'}}}
async function info(){const i=await (await fetch('/setup/info',{cache:'no-store'})).json();
  $('who').textContent=(i.image==='aussen'?'Außeneinheit':'Inneneinheit')+' · Gerätenummer '+i.serial;
  $('rolebox').hidden=i.image!=='innen';
  if(i.configured){const s=$('state');s.hidden=false;s.replaceChildren();const w=el('div','state');const ico=el('span','i');
    ico.style.background=i.wifi_up?'var(--ok-soft)':'var(--bad-soft)';ico.style.color=i.wifi_up?'var(--ok)':'var(--bad)';ico.append(ic(i.wifi_up?'ok':'warn'));
    const t=el('div');t.append(el('b','','Eingerichtet'),el('div','mute','WLAN „'+i.ssid+'“'+(i.role?' · '+i.role:'')+(i.wifi_up?' · verbunden, erreichbar unter http://'+i.hostname:' · zurzeit NICHT verbunden')));
    w.append(ico,t);s.append(w);$('ssid').value=i.ssid}
  if(!i.may_change){$('save').disabled=true;$('reset').disabled=true;$('out').textContent='Ändern nur in der Einrichtung: Boot-Taster zweimal 5 s halten.'}}
function bars(rssi){const b=el('span','bars');const lv=rssi>-60?4:rssi>-70?3:rssi>-80?2:1;for(let k=1;k<=4;k++){const i=el('i',k<=lv?'on':'');i.style.height=(k*4)+'px';b.append(i)}return b}
$('scanb').prepend(ic('search'));
$('scanb').onclick=async()=>{const sb=$('scanb');sb.disabled=true;sb.lastChild.textContent='suche … (einige Sekunden)';const n=$('nets');n.replaceChildren();
  try{const r=await (await fetch('/setup/scan',{cache:'no-store'})).json();
    for(const w of r.networks){const d=el('div','net');d.append(el('b','',w.ssid),el('span','mute',w.open?'offen':''),bars(w.rssi));
      d.onclick=()=>{$('ssid').value=w.ssid;$('pass').focus()};n.append(d)}
    if(!r.networks.length)n.append(el('p','mute','Keine Netze gefunden.'))}catch(e){n.append(el('p','bad','Suche fehlgeschlagen.'))}
  sb.disabled=false;sb.lastChild.textContent='WLAN-Netze suchen'};
$('save').onclick=async()=>{const role=document.querySelector('input[name=role]:checked');
  const r=await post('/setup/save',{ssid:$('ssid').value,pass:$('pass').value,role:role?role.value:''});
  $('out').textContent=r.message;$('out').className=r.ok?'ok':'bad'};
$('reset').onclick=async()=>{if(!confirm('Wirklich alles löschen und auf Werkszustand zurücksetzen?'))return;
  const r=await post('/setup/reset',{confirm:'JA'});$('rout').textContent=r.message;$('rout').className=r.ok?'ok':'bad'};
$('logo').append(ic('bell'));info();
</script></body></html>
)HTML";
