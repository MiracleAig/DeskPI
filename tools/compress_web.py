#!/usr/bin/env python3
"""Refresh committed gzip assets after editing web/; not a build/runtime dependency."""
from pathlib import Path
import gzip
root=Path(__file__).resolve().parents[1]/'web'
for p in root.iterdir():
    if p.suffix in ('.html','.css','.js'):
        Path(str(p)+'.gz').write_bytes(gzip.compress(p.read_bytes(),compresslevel=9,mtime=0))
print('Compressed assets:',sum(p.stat().st_size for p in root.glob('*.gz')),'bytes')
