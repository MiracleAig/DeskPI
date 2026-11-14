'use strict';
const $ = id => document.getElementById(id);
let csrf = '', authenticated = false, polling = null, busy = 0, settings = null, sliderTimer = null;
let chain = Promise.resolve();
function message(text, error = false) {
  const target = authenticated ? $('notice') : $('global-error');
  target.textContent = text; target.hidden = false; target.classList.toggle('error', error);
}
function api(path, method = 'GET', data) {
  return fetch('/api/v1/' + path, {method, credentials: 'same-origin', cache: 'no-store', headers: data === undefined ? {} : {'Content-Type':'application/json','X-CSRF-Token':csrf}, body:data === undefined ? undefined : JSON.stringify(data)})
    .then(response => response.json().then(value => {
      if (!response.ok) {
        if (response.status === 401 && authenticated) showLogin();
        throw new Error((value.error && value.error.message) || `Request failed (${response.status})`);
      }
      return value;
    }));
}
function showLogin() {
  authenticated = false; csrf = ''; clearInterval(polling); polling = null;
  $('shell').hidden = true; $('login').hidden = false;
}
function time(minutes) { return String(Math.floor(minutes/60)).padStart(2,'0') + ':' + String(minutes%60).padStart(2,'0'); }
function minute(value) { const p = value.split(':').map(Number); return p[0]*60+p[1]; }
function setInput(id, value, check = false) {
  const input = $(id);
  if (document.activeElement === input || input.dataset.dirty) return;
  input[check ? 'checked' : 'value'] = value;
}
function render(value, force = false) {
  settings = value;
  if (force) document.querySelectorAll('[data-dirty]').forEach(e => delete e.dataset.dirty);
  const d = value.display, c = value.clock, a = value.appearance;
  const safe = Math.max(5,d.minimum);
  $('brightness').min = safe; $('day').min = safe; $('night').min = safe;
  setInput('brightness',d.current); $('brightness').disabled = !d.available;
  if (document.activeElement !== $('brightness')) $('brightness-output').textContent = d.current + '%';
  $('display-error').textContent = d.error || '';
  $('display-status').textContent = d.status;
  $('brightness-overview').textContent = d.available ? d.current+'%' : 'Unavailable';
  $('brightness-status').textContent = d.status;
  for (const id of ['automatic','day','night','day-start','night-start']) $(id).disabled = !d.available;
  $('display-form').querySelector('button').disabled = !d.available;
  setInput('automatic',d.automatic,true); setInput('day',d.day); setInput('night',d.night);
  setInput('day-start',time(d.dayStart)); setInput('night-start',time(d.nightStart));
  setInput('format24',String(c.format24)); setInput('seconds',c.seconds,true); setInput('date',c.date,true); setInput('date-format',c.dateFormat);
  setInput('theme',a.theme); setInput('accent',a.accent);
  document.documentElement.dataset.theme = a.theme;
  document.documentElement.style.setProperty('--accent',a.accent);
}
function bytes(value) { return value == null ? 'Unavailable' : (value/1024/1024/1024 >= 1 ? (value/1024/1024/1024).toFixed(1)+' GB' : (value/1024/1024).toFixed(0)+' MB'); }
function status(value) {
  $('hostname').textContent = value.hostname; $('system-hostname').textContent = value.hostname;
  $('cpu').textContent = value.cpuPercent == null ? 'Measuring…' : value.cpuPercent.toFixed(1)+'%'; $('cpu-bar').value = value.cpuPercent || 0;
  $('memory').textContent = value.memoryUsed == null ? 'Unavailable' : Math.round(value.memoryUsed/value.memoryTotal*100)+'%';
  $('memory-bar').value = value.memoryTotal ? value.memoryUsed/value.memoryTotal*100 : 0;
  $('memory-detail').textContent = bytes(value.memoryUsed)+' of '+bytes(value.memoryTotal);
  $('temperature').textContent = value.temperatureC == null ? 'Unavailable' : value.temperatureC.toFixed(1)+' °C';
  const hours = Math.floor((value.uptimeSeconds || 0)/3600); $('uptime').textContent = Math.floor(hours/24)+'d '+hours%24+'h';
  $('disk').textContent = bytes(value.diskUsed)+' / '+bytes(value.diskTotal); $('rss').textContent = bytes(value.applicationRss);
  $('version').textContent = value.version+' / '+value.qtVersion; $('footer-version').textContent = value.version;
  $('os').textContent = value.os+' · '+value.architecture; $('network').textContent = value.networkConnected ? 'Connected' : 'No active interface';
  $('addresses').textContent = (value.addresses || []).join(', ') || 'Unavailable';
}
function refresh() {
  if (!authenticated || document.hidden || busy || document.querySelector('[data-dirty]')) return Promise.resolve();
  return Promise.all([api('settings'),api('status')]).then(values => { render(values[0]); status(values[1]); })
    .catch(error => message('Connection interrupted. Retrying automatically. '+error.message,true));
}
function write(section, patch) {
  message('Saving settings…');
  ++busy;
  chain = chain.catch(()=>{}).then(()=>api('settings/'+section,'PATCH',patch)).then(saved=>{
    const ids = {manual:'brightness',dayStart:'day-start',nightStart:'night-start',dateFormat:'date-format'};
    for (const key of Object.keys(patch)) {const element=$(ids[key]||key);if(element)delete element.dataset.dirty;}
    render(saved); message('Settings saved and applied.');
  }).catch(error => message(error.message,true)).then(()=>{--busy;});
  return chain;
}
function navigate(page) {
  document.querySelectorAll('[data-view]').forEach(e=>e.hidden = e.dataset.view!==page);
  document.querySelectorAll('nav [data-page]').forEach(e=>e.classList.toggle('active',e.dataset.page===page));
  $('page-title').textContent = page[0].toUpperCase()+page.slice(1);
}
function signedIn(session) {
  csrf = session.csrf; authenticated = true; $('password').value = '';
  $('login').hidden = true; $('shell').hidden = false; $('global-error').hidden = true;
  return refresh().then(()=>{clearInterval(polling);polling=setInterval(refresh,8000);});
}
document.querySelectorAll('[data-page]').forEach(b=>b.addEventListener('click',()=>navigate(b.dataset.page)));
document.querySelectorAll('input,select').forEach(e=>e.addEventListener('input',()=>{if(e.id!=='password' && e.id!=='import')e.dataset.dirty='true';}));
$('login-form').addEventListener('submit',event=>{
  event.preventDefault(); const b=event.submitter; b.disabled=true;b.textContent='Signing in…';
  api('login','POST',{password:$('password').value}).then(signedIn).catch(error=>message(error.message,true)).then(()=>{b.disabled=false;b.textContent='Sign in →';});
});
$('logout').addEventListener('click',()=>api('logout','POST',{}).then(showLogin).catch(e=>message(e.message,true)));
$('brightness').addEventListener('input',()=>{
  $('brightness-output').textContent=$('brightness').value+'%'; clearTimeout(sliderTimer);
  sliderTimer=setTimeout(()=>write('display',{manual:Number($('brightness').value)}),220);
});
$('display-form').addEventListener('submit',e=>{
  e.preventDefault(); if($('day-start').value===$('night-start').value){message('Day and night start times must differ.',true);return;}
  write('display',{automatic:$('automatic').checked,day:Number($('day').value),night:Number($('night').value),dayStart:minute($('day-start').value),nightStart:minute($('night-start').value)});
});
$('clock-form').addEventListener('submit',e=>{e.preventDefault();write('clock',{format24:$('format24').value==='true',seconds:$('seconds').checked,date:$('date').checked,dateFormat:$('date-format').value});});
$('appearance-form').addEventListener('submit',e=>{e.preventDefault();write('appearance',{theme:$('theme').value,accent:$('accent').value});});
$('accent').addEventListener('input',()=>document.documentElement.style.setProperty('--accent',$('accent').value));
$('export').addEventListener('click',()=>{
  api('config/export').then(config=>{
    const url=URL.createObjectURL(new Blob([JSON.stringify(config,null,2)],{type:'application/json'}));
    const a=document.createElement('a');a.href=url;a.download='deskbuddy-config.json';a.click();URL.revokeObjectURL(url);
  }).catch(e=>message(e.message,true));
});
$('import').addEventListener('change',()=>{
  const file=$('import').files[0];if(!file)return;
  if(file.size>16384){message('Configuration must be 16 KiB or smaller.',true);$('import').value='';return;}
  file.text().then(text=>{
    const data=JSON.parse(text);
    if(!confirm('Replace DeskBuddy settings? A recovery backup will be saved first.'))return;
    ++busy;
    return api('config/import','POST',data).then(config=>{render(config,true);message('Configuration imported and applied.');})
      .then(()=>{--busy;},error=>{--busy;throw error;});
  }).catch(error=>message(error.message,true)).then(()=>{$('import').value='';});
});
$('restart').addEventListener('click',()=>{
  if(!confirm('Restart DeskBuddy now? The clock will reconnect briefly.'))return;
  api('application/restart','POST',{confirm:true}).then(()=>{
    message('Restarting. Sign in again when DeskBuddy returns.');setTimeout(()=>location.reload(),2500);
  }).catch(error=>message(error.message,true));
});
document.addEventListener('visibilitychange',()=>{if(!document.hidden)refresh();});
api('session').then(session=>{$('setup').hidden=!session.setupRequired;if(session.authenticated)return signedIn(session);})
  .catch(error=>message('Unable to connect. '+error.message,true));
