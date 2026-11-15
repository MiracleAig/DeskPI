#!/usr/bin/env python3
"""Package a previously built and runnable native companion. Never invoked by HTTP."""
import argparse, hashlib, json, os, platform, shutil, subprocess, tarfile, tempfile
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--binary',default='build/companion/deskbuddy-agent')
p.add_argument('--output',default='releases')
p.add_argument('--platform',required=True,help='Exact tested distro, Qt and glibc constraints')
a=p.parse_args()
root=Path(__file__).resolve().parents[1]
binary=Path(a.binary).resolve()
version=subprocess.check_output([str(binary),'--version'],text=True).strip().split()[-1]
arch=platform.machine()
# Refuse a foreign executable that happened to be runnable through emulation.
info=subprocess.check_output(['file','-b',str(binary)],text=True)
if (arch=='x86_64' and 'x86-64' not in info) or (arch=='aarch64' and 'ARM aarch64' not in info): raise SystemExit('Binary architecture does not match build host')
out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
name=f'deskbuddy-agent-{version}-linux-{arch}.tar.gz'
with tempfile.TemporaryDirectory() as d:
    dest=Path(d)/name.removesuffix('.tar.gz');dest.mkdir()
    shutil.copy2(binary,dest/'deskbuddy-agent')
    for source in ['deskbuddy-agent.service','deskbuddy-agent.desktop','install.sh','uninstall.sh']:
        shutil.copy2(root/'companion'/source,dest/source)
    shutil.copy2(root/'docs/media/INSTALL.md',dest/'INSTALL.md')
    (dest/'ARCHITECTURE').write_text(arch+'\n')
    (dest/'BUILD.txt').write_text(a.platform+'\n'+info)
    with tarfile.open(out/name,'w:gz') as archive: archive.add(dest,arcname=dest.name)
manifest=out/'manifest.json'
items=json.loads(manifest.read_text()) if manifest.exists() else []
items=[v for v in items if v['file']!=name]
items.append({'file':name,'version':version,'architecture':arch,'platform':a.platform,'sha256':hashlib.sha256((out/name).read_bytes()).hexdigest(),'size':(out/name).stat().st_size})
manifest.write_text(json.dumps(items,indent=2)+'\n')
print(out/name)
