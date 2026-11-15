#!/usr/bin/env python3
"""Isolated development benchmark using actual app/companion processes; never a service.
Uses a temporary config, TLS identity, admin password and real dashboard pairing.
"""
import argparse,hashlib,http.cookiejar,json,os,socket,ssl,subprocess,tempfile,time,urllib.request
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--build',default='build');p.add_argument('--seconds',type=float,default=10);p.add_argument('--output',default='docs/media/benchmark.json');a=p.parse_args()
build=Path(a.build).resolve();processes=[]
def port():
    with socket.socket() as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
def sample(pid):
    stat=Path(f'/proc/{pid}/stat').read_text().split();lines=Path(f'/proc/{pid}/smaps_rollup').read_text().splitlines()
    return {'ticks':int(stat[13])+int(stat[14]),**{key.lower()+'_kib':int(next(l for l in lines if l.startswith(key+':')).split()[1]) for key in ('Rss','Pss')}}
def measure(label,pids):
    before={name:sample(pid) for name,pid in pids.items()};t=time.monotonic();time.sleep(a.seconds);elapsed=time.monotonic()-t
    result={'workload':label,'seconds':round(elapsed,3),'processes':{}}
    for name,pid in pids.items():
        after=sample(pid);after['cpu_percent_one_core']=round((after.pop('ticks')-before[name]['ticks'])/os.sysconf('SC_CLK_TCK')/elapsed*100,3);result['processes'][name]=after
    mem={l.split(':')[0]:int(l.split()[1]) for l in Path('/proc/meminfo').read_text().splitlines()};result['system_used_kib']=mem['MemTotal']-mem['MemAvailable'];return result
try:
 with tempfile.TemporaryDirectory(prefix='deskbuddy-benchmark-') as tmp:
    tmp=Path(tmp);env=dict(os.environ,XDG_CONFIG_HOME=str(tmp/'config'),QT_QPA_PLATFORM='offscreen',QT_QUICK_BACKEND='software',DESKBUDDY_MEDIA_PORT=str(port()))
    if Path('releases/manifest.json').exists():env['DESKBUDDY_RELEASE_DIR']=str(Path('releases').resolve())
    config=tmp/'config/DeskPI';config.mkdir(parents=True)
    subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-keyout',str(tmp/'key.pem'),'-out',str(tmp/'cert.pem'),'-days','1','-subj','/CN=localhost','-addext','subjectAltName=IP:127.0.0.1'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
    os.chmod(tmp/'key.pem',0o600)
    password=os.urandom(24).hex();salt=os.urandom(32);digest=hashlib.pbkdf2_hmac('sha256',password.encode(),salt,600000)
    (config/'admin-auth.ini').write_text('[General]\niterations=600000\nsalt='+salt.hex()+'\nhash='+digest.hex()+'\n');os.chmod(config/'admin-auth.ini',0o600)
    adminport=port();base=f'https://127.0.0.1:{adminport}'
    subprocess.run([str(build/'appdeskpi'),'--admin-configure','--bind','127.0.0.1','--port',str(adminport),'--tls-cert',str(tmp/'cert.pem'),'--tls-key',str(tmp/'key.pem')],env=env,check=True,stdout=subprocess.DEVNULL)
    jar=http.cookiejar.CookieJar();client=urllib.request.build_opener(urllib.request.HTTPCookieProcessor(jar),urllib.request.HTTPSHandler(context=ssl.create_default_context(cafile=str(tmp/'cert.pem'))));csrf=''
    def api(path,method='GET',data=None):
        req=urllib.request.Request(base+'/api/v1/'+path,method=method)
        if data is not None:req.data=json.dumps(data).encode();req.add_header('Content-Type','application/json');req.add_header('Origin',base);req.add_header('X-CSRF-Token',csrf)
        with client.open(req,timeout=4) as r:return json.load(r)
    clocklog=open(tmp/'clock.log','w+')
    clock=subprocess.Popen([str(build/'appdeskpi'),'--media-disabled','--admin-disabled'],env=env,stdout=clocklog,stderr=clocklog);processes.append(clock);time.sleep(1)
    clockresult=measure('clock_only_current_binary_no_listeners',{'deskpi':clock.pid});clock.terminate();clock.wait()
    adminlog=open(tmp/'admin.log','w+')
    adminonly=subprocess.Popen([str(build/'appdeskpi'),'--media-disabled'],env=env,stdout=adminlog,stderr=adminlog);processes.append(adminonly);time.sleep(1)
    api('health');api('login','POST',{'password':password})
    adminresult=measure('existing_dashboard_tls_media_disabled',{'deskpi':adminonly.pid});adminonly.terminate();adminonly.wait()
    log=open(tmp/'app.log','w+');t=time.monotonic();app=subprocess.Popen([str(build/'appdeskpi')],env=env,stdout=log,stderr=log);processes.append(app)
    for _ in range(100):
        try:api('health');break
        except Exception:time.sleep(.05)
    else:raise RuntimeError('App did not start: '+(tmp/'app.log').read_text())
    startup=(time.monotonic()-t)*1000;csrf=api('login','POST',{'password':password})['csrf'];time.sleep(1)
    results=[clockresult,adminresult,measure('receiver_idle',{'deskpi':app.pid})]
    pair=api('media/pair','POST',{})
    agentenv=dict(env,DESKBUDDY_PAIR_CODE=pair['code'])
    cmd=[str(build/'companion/deskbuddy-agent'),'--simulate','--url',f"wss://127.0.0.1:{env['DESKBUDDY_MEDIA_PORT']}",'--pin',pair['certificate_sha256'],'--config',str(tmp/'agent/config.json')]
    agentlog=open(tmp/'agent.log','w+');agent=subprocess.Popen(cmd,env=agentenv,stdout=agentlog,stderr=agentlog);processes.append(agent)
    for _ in range(100):
        devices=api('media/devices')
        if devices['pending']:break
        time.sleep(.05)
    else:raise RuntimeError('No pairing request: '+(tmp/'agent.log').read_text())
    api('media/approve','POST',{'id':devices['pending'][0]['id']});t=time.monotonic()
    for _ in range(200):
        devices=api('media/devices')
        if devices['devices'] and devices['devices'][0].get('session')=='simulator':break
        time.sleep(.05)
    else:raise RuntimeError('Agent did not synchronize: '+(tmp/'agent.log').read_text())
    sync=(time.monotonic()-t)*1000
    results.append(measure('simulated_playback_visible',{'deskpi':app.pid,'agent':agent.pid}))
    mappings={};current=''
    for line in Path(f'/proc/{agent.pid}/smaps').read_text().splitlines():
        if '-' in line.split()[0] and len(line.split())>=5:current=line.split()[-1] if len(line.split())>=6 else '[anonymous]'
        elif line.startswith('Rss:'):mappings[current]=mappings.get(current,0)+int(line.split()[1])
    agentmaps=sorted(mappings.items(),key=lambda v:v[1],reverse=True)[:15]
    agent.terminate();agent.wait();time.sleep(1)
    results.append(measure('host_disconnected',{'deskpi':app.pid}))
    releasecheck={}
    releases=api('media/releases')['releases']
    if releases:
        artifact=releases[0]
        with client.open(base+'/api/v1/media/download/'+artifact['file'],timeout=5) as response:payload=response.read()
        releasecheck={'file':artifact['file'],'bytes':len(payload),'sha256_verified':hashlib.sha256(payload).hexdigest()==artifact['sha256']}
        if not releasecheck['sha256_verified']:raise RuntimeError('Downloaded release checksum differs from manifest')
    report={'platform':os.uname().machine,'qt':subprocess.check_output(['pkg-config','--modversion','Qt6Core'],text=True).strip(),'rendering':'offscreen Qt Quick software','pi_hardware':False,'startup_to_https_health_ms':round(startup,1),'approval_to_initial_sync_ms':round(sync,1),'release_download':releasecheck,'agent_largest_rss_mappings_kib':agentmaps,'results':results,'application_log':(tmp/'app.log').read_text(),'agent_log':(tmp/'agent.log').read_text()}
    Path(a.output).write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
finally:
 for proc in reversed(processes):
    if proc.poll() is None:proc.terminate();proc.wait(timeout=5)
