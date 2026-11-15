#!/usr/bin/env python3
"""Development-only benchmark. Python is not part of DeskPI's runtime."""
import argparse
import getpass
import http.cookiejar
import json
import os
from pathlib import Path
import statistics
import ssl
import time
import urllib.request

parser = argparse.ArgumentParser()
parser.add_argument('--seconds', type=int, default=20)
parser.add_argument('--url', default='http://127.0.0.1:8080')
parser.add_argument('--authenticated', action='store_true')
parser.add_argument('--ca-file', help='Trust this local HTTPS certificate for the benchmark.')
parser.add_argument('--output')
args = parser.parse_args()
processes = [p for p in Path('/proc').iterdir() if p.name.isdigit() and (p/'comm').exists() and (p/'comm').read_text().strip() == 'appdeskpi']
if len(processes) != 1:
    raise SystemExit('Run against exactly one DeskPI process.')
pid = processes[0].name
hz = os.sysconf('SC_CLK_TCK')
jar = http.cookiejar.CookieJar()
handlers = [urllib.request.HTTPCookieProcessor(jar)]
if args.ca_file:
    handlers.append(urllib.request.HTTPSHandler(context=ssl.create_default_context(cafile=args.ca_file)))
client = urllib.request.build_opener(*handlers)
csrf = ''

def api(path, method='GET', data=None):
    req = urllib.request.Request(args.url+'/api/v1/'+path, method=method)
    if data is not None:
        req.data = json.dumps(data).encode()
        req.add_header('Content-Type', 'application/json')
        req.add_header('Origin', args.url)
        req.add_header('X-CSRF-Token', csrf)
    with client.open(req, timeout=15) as response:
        return json.load(response)

def sample():
    stat = Path(f'/proc/{pid}/stat').read_text().split()
    mem = {line.split(':')[0]:int(line.split()[1]) for line in Path('/proc/meminfo').read_text().splitlines()}
    status = Path(f'/proc/{pid}/status').read_text()
    rss = int(status.split('VmRSS:')[1].split()[0])
    return int(stat[13])+int(stat[14]), rss, mem['MemTotal']-mem['MemAvailable']

def measure(name, work=None, interval=8):
    before = sample(); start = time.monotonic(); next_request = start; latencies=[]; samples=[]
    while time.monotonic()-start < args.seconds:
        if work and time.monotonic() >= next_request:
            t=time.monotonic(); work(); latencies.append((time.monotonic()-t)*1000); next_request += interval
        samples.append(sample()); time.sleep(.05)
    after = sample(); elapsed=time.monotonic()-start
    result = {'workload':name,'duration_seconds':round(elapsed,2),'rss_kib':after[1],
              'max_rss_kib':max(s[1] for s in samples),'system_used_kib':after[2],
              'cpu_percent_one_core':round((after[0]-before[0])/hz/elapsed*100,3)}
    if latencies:
        result.update(requests=len(latencies),mean_latency_ms=round(statistics.mean(latencies),3),
                      p95_latency_ms=round(sorted(latencies)[min(len(latencies)-1,int(len(latencies)*.95))],3))
    print(json.dumps(result),flush=True)
    return result

results=[measure('idle_no_browser')]
if args.authenticated:
    password=os.environ.get('DESKPI_TEST_PASSWORD') or getpass.getpass('DeskPI administrator password: ')
    csrf=api('login','POST',{'password':password})['csrf']; del password
    original=api('config/export')
    results.append(measure('one_browser_8s_poll',lambda:(api('settings'),api('status')),8))
    results.append(measure('status_refresh_1s',lambda:api('status'),1))
    level=[75]
    def adjust():
        level[0]=80 if level[0]==75 else 75
        api('settings/display','PATCH',{'manual':level[0]})
    results.append(measure('brightness_adjust_4hz',adjust,.25))
    # Restore all prior preferences through the same validated import path.
    api('config/import','POST',original)
    times=[]
    for _ in range(30):
        start=time.monotonic(); api('status'); times.append((time.monotonic()-start)*1000)
    results.append({'workload':'status_latency_30_requests','mean_latency_ms':round(statistics.mean(times),3),
                    'p95_latency_ms':round(sorted(times)[28],3)})
    api('logout','POST',{})
if args.output:
    Path(args.output).write_text(json.dumps(results,indent=2)+'\n')
