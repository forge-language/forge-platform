#!/usr/bin/env python3
"""Publish existing SDK archives to Forge Storage and write atomic Nginx redirects.
Run on the deployment host; writer credentials stay in local .env files.
"""
import argparse, hashlib, http.client, json, os, pathlib, re, urllib.parse
ROOT=pathlib.Path(__file__).resolve().parents[1]

def upload(base,token,file):
 size=file.stat().st_size
 if size>256*1024*1024: raise ValueError('Object exceeds 256 MiB')
 digest=hashlib.sha256()
 with file.open('rb') as stream:
  while chunk:=stream.read(65536): digest.update(chunk)
 key=digest.hexdigest();url=urllib.parse.urlsplit(base)
 if url.scheme not in ('http','https') or not url.hostname or url.query or url.fragment:raise ValueError('Invalid storage origin')
 conn=(http.client.HTTPSConnection if url.scheme=='https' else http.client.HTTPConnection)(url.hostname,url.port,timeout=180)
 try:
  conn.putrequest('PUT',url.path.rstrip('/')+'/v1/objects/'+key)
  conn.putheader('Authorization','Bearer '+token);conn.putheader('Content-Type','application/octet-stream');conn.putheader('Content-Length',str(size));conn.endheaders()
  with file.open('rb') as stream:
   while chunk:=stream.read(65536): conn.send(chunk)
  response=conn.getresponse();body=response.read(8192)
  if response.status not in (200,201):raise RuntimeError('Storage returned HTTP '+str(response.status))
  result=json.loads(body)
  if result.get('sha256')!=key or result.get('size')!=size:raise RuntimeError('Storage metadata mismatch')
  return result
 finally:conn.close()
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--base',default='http://127.0.0.1:18104');parser.add_argument('--public',default='https://storage.forge-lang.org');args=parser.parse_args()
 env=dict(line.split('=',1) for line in (ROOT/'.env').read_text().splitlines() if '=' in line and not line.startswith('#'))
 if not re.fullmatch(r'https://[A-Za-z0-9.-]+(?::[0-9]+)?',args.public):raise ValueError('Invalid public origin')
 records={}
 for file in sorted((ROOT/'releases').glob('*/*')):
  if not file.name.endswith(('.tar.gz','.tar.gz.sha256')):continue
  metadata=upload(args.base,env['STORAGE_TOKEN'],file);metadata['url']=args.public+'/v1/objects/'+metadata['sha256'];records['/releases/'+file.relative_to(ROOT/'releases').as_posix()]=metadata
 target=ROOT/'releases/artifacts.conf';temp=target.with_suffix('.tmp')
 temp.write_text('map $uri $forge_release_url {\n default "";\n'+''.join(' "'+path+'" "'+value['url']+'";\n' for path,value in records.items())+'}\n');temp.replace(target)
 target=ROOT/'releases/artifacts.json';temp=target.with_suffix('.tmp');temp.write_text(json.dumps(records,indent=2)+'\n');temp.replace(target)
 print('Published',len(records),'SDK objects; reload the site nginx after validating its configuration.')
if __name__=='__main__':main()
