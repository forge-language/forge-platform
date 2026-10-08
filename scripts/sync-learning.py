#!/usr/bin/env python3
"""Copy an explicit reviewed course checkout into the website with provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,default=ROOT.parent/'forge-learning')
args = parser.parse_args()
source = args.source.resolve()
revision = subprocess.check_output(['git','-C',str(source),'rev-parse','HEAD'],env=__import__('os').environ|{'GIT_MASTER':'1'},text=True).strip()
tracked = subprocess.check_output(['git','-C',str(source),'show',f'{revision}:course.json'],env=__import__('os').environ|{'GIT_MASTER':'1'})
local = (source/'course.json').read_bytes()
if local!=tracked:raise SystemExit('Commit and verify course changes before syncing.')
course = json.loads(local)
if course.get('schema_version')!=1 or not isinstance(course.get('lessons'),list):raise SystemExit('Unsupported course schema.')
target = ROOT/'frontend/public/learn'
target.mkdir(parents=True,exist_ok=True)
(target/'course.json').write_bytes(local)
(target/'provenance.json').write_text(json.dumps({'repository':'https://github.com/forge-language/forge-learning','revision':revision,'sha256':hashlib.sha256(local).hexdigest()},indent=2)+'\n')
print(f'Synced {len(course["lessons"])} lessons from {revision}')
