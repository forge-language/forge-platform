#!/usr/bin/env python3
"""Disposable integration-only GitHub transport; production never loads this file."""
import base64
import copy
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
from urllib.parse import unquote

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('checker',ROOT/'repository-checker/service.py')
checker = importlib.util.module_from_spec(spec);spec.loader.exec_module(checker)
seed = json.loads((ROOT/'backend/seed.json').read_text())
COMMIT = 'a'*40
IDS = {'Helloworld0822':1,'TestUser':2,'OtherUser':3}

class FixtureGitHub:
    selected={}
    def __init__(self, token='', **kw): self.token=token
    def get(self,path):
        if path=='/user':
            if self.token!='fixture-user-token': raise checker.Rejected('invalid token',401)
            return {'id':2,'login':'TestUser'}
        match=re.match(r'^/repos/([^/]+)/([^/?]+)(.*)$',path)
        if not match: raise checker.Rejected('missing fixture')
        owner,repo,tail=match.groups();url=f'https://github.com/{owner}/{repo}'
        candidates=[value for value in seed if value['repository_url']==url]
        requested=unquote(tail[len('/commits/'):]) if tail.startswith('/commits/') else ''
        official=next((value for value in candidates if value['git_commit']==requested),candidates[-1] if candidates else None)
        # Tree/blob requests follow the chosen immutable commit.
        if candidates and requested and requested!='main' and official['git_commit']!=requested:raise checker.Rejected('commit missing')
        if candidates and not requested:official=FixtureGitHub.selected.get(url,candidates[-1])
        if candidates and requested:FixtureGitHub.selected[url]=official
        if official:
            manifest=copy.deepcopy(official);sha=manifest['git_commit'];owner_id=999;owner_type='Organization'
        else:
            parts=repo.rsplit('__',1)
            if len(parts)!=2 or owner not in IDS: raise checker.Rejected('repository does not exist')
            name,version=parts[0],parts[1].replace('_','.')
            manifest=copy.deepcopy(seed[0]);manifest.update(name=name,version=version,repository_url=url,git_commit=COMMIT)
            sha=COMMIT;owner_id=IDS[owner];owner_type='User'
        source={'module.json':json.dumps(manifest),manifest['module']:'fn fixture(): int { return 1; }'}
        for descriptor in ('native','javascript'):
            if descriptor in manifest:
                source['CMakeLists.txt']='# inspection requires manual review\n'
        tree=[];blobs={}
        for name,text in source.items():
            raw=text.encode();digest=hashlib.sha1(b'blob '+str(len(raw)).encode()+b'\0'+raw).hexdigest()
            tree.append({'path':name,'type':'blob','mode':'100644','size':len(raw),'sha':digest})
            blobs[digest]={'sha':digest,'encoding':'base64','content':base64.b64encode(raw).decode()}
        if tail.startswith('/commits/'):
            ref=unquote(tail[len('/commits/'):])
            if ref not in ('main',sha): raise checker.Rejected('commit missing')
            return {'sha':sha,'commit':{'tree':{'sha':'b'*40}}}
        if tail.startswith('/git/trees/'):return {'tree':tree,'truncated':False}
        if tail.startswith('/git/blobs/'):
            blob=blobs.get(tail.rsplit('/',1)[1])
            if not blob:raise checker.Rejected('blob missing')
            return blob
        return {'html_url':url,'id':100,'owner':{'id':owner_id,'type':owner_type,'login':owner},'default_branch':'main','private':False}

real_inspect,real_identity=checker.inspect,checker.identity
def inspected(value):
    result=real_inspect(value,client_factory=FixtureGitHub)
    name=value.get('repository_url','').split('/')[-1]
    if name.startswith('unknown-scan__'):result['inspection']['status']='unknown'
    if name.startswith('wrong-scan__'):result['inspection']['git_commit']='0'*40
    if name.startswith('missing-scan__'):result.pop('inspection')
    return result
checker.inspect=inspected
checker.identity=lambda token:real_identity(token,client_factory=FixtureGitHub)
checker.Server(('0.0.0.0',8090),checker.Handler).serve_forever()
