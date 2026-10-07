#!/usr/bin/env python3
"""Sync fallback evidence and its index for an offline frontend development build."""
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[1]
index = {'schema_version': 1, 'documents': [], 'reports': []}
for kind, group in [('project', 'documents'), ('reports', 'reports')]:
    source = ROOT / 'content' / kind
    target = ROOT / 'frontend/public' / kind
    target.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source, target, dirs_exist_ok=True)
    for path in sorted(source.glob('*')):
        if path.is_file() and path.suffix in ('.md', '.json'):
            index[group].append({'title': path.stem.replace('-', ' '), 'url': '/' + kind + '/' + path.name,
                                 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                                 'provenance': 'checked-in historical snapshot'})
(ROOT / 'frontend/public/content-manifest.json').write_text(json.dumps(index, indent=2) + '\n')
legacy = {'updated_at': None, 'checked_at': None, 'reports': [], 'errors': []}
for item in index['reports']:
    legacy['reports'].append({'id': item['url'], 'title': item['title'], 'path': item['url'].lstrip('/'),
        'file': item['url'], 'format': Path(item['url']).suffix.lstrip('.'), 'source': None,
        'repository': None, 'kind': 'local_snapshot', 'sha256': item['sha256']})
(ROOT / 'frontend/public/benchmarks.json').write_text(json.dumps(legacy, indent=2) + '\n')
print('Synced fallback documents, original performance evidence and viewer indices')
