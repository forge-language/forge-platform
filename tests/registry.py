#!/usr/bin/env python3
import base64,concurrent.futures,hashlib,hmac,json,os,pathlib,sys,time,unittest,urllib.request,urllib.error
BASE=os.environ.get('REGISTRY_BASE','http://127.0.0.1:18103')
SECRET=os.environ.get('TEST_JWT_SECRET','registry-integration-only')
ROOT=pathlib.Path(__file__).resolve().parents[1]
def token(user='Helloworld0822',role='admin',expiry=None):
 def b64(value):return base64.urlsafe_b64encode(json.dumps(value,separators=(',',':')).encode()).rstrip(b'=')
 h=b64({'alg':'HS256','typ':'JWT'});p=b64({'sub':user,'role':role,'exp':expiry or int(time.time())+1200});data=h+b'.'+p
 return (data+b'.'+base64.urlsafe_b64encode(hmac.new(SECRET.encode(),data,hashlib.sha256).digest()).rstrip(b'=')).decode()
def request(method,path,value=None,auth=None,raw=None):
 headers={'Content-Type':'application/json'}
 if auth:headers['Authorization']='Bearer '+auth
 body=json.dumps(value).encode() if value is not None else raw
 req=urllib.request.Request(BASE+path,data=body,headers=headers,method=method)
 try:r=urllib.request.urlopen(req,timeout=30)
 except urllib.error.HTTPError as e:r=e
 with r:return r.status,json.loads(r.read())
def seed():
 for m in json.loads((ROOT/'backend/seed.json').read_text()):
  status,value=request('POST','/api/packages',m,token())
  if status not in (201,409):raise RuntimeError((status,value))
class RegistryTest(unittest.TestCase):
 @classmethod
 def setUpClass(cls):seed()
 def sample(self,name='test-module',version='0.1.0'):
  value=json.loads((ROOT/'backend/seed.json').read_text())[0];value.update(name=name,version=version);return value
 def test_health(self):self.assertEqual(request('GET','/api/health')[1]['language'],'Forge')
 def test_real_seed(self):
  status,data=request('GET','/api/packages');self.assertEqual(status,200);self.assertIn('forge-postgres',[v['name'] for v in data])
 def test_search(self):self.assertTrue(all('postgres' in v['name'] or 'postgres' in v['description'].lower() for v in request('GET','/api/packages?q=postgres')[1]))
 def test_stats(self):self.assertGreaterEqual(request('GET','/api/stats')[1]['packages'],2)
 def test_detail_exact_commit(self):
  detail=request('GET','/api/packages/forge-web')[1];release=detail['versions'][0];self.assertEqual(len(release['git_commit']),40);self.assertEqual(request('GET','/api/packages/forge-web/versions/'+release['version'])[1],{k:v for k,v in release.items() if k!='published_at'})
 def test_publish_without_auth(self):self.assertEqual(request('POST','/api/packages',self.sample())[0],401)
 def test_expired_token(self):self.assertEqual(request('POST','/api/packages',self.sample(),token(expiry=int(time.time())-5))[0],401)
 def test_invalid_role(self):self.assertEqual(request('POST','/api/packages',self.sample(),token(role='oauth'))[0],401)
 def test_immutable(self):self.assertEqual(request('POST','/api/packages',self.sample('forge-postgres'),token())[0],409)
 def test_bad_inputs(self):
  cases={'name':'../escape','version':'1.01.0','git_commit':'main','module':'../secrets','repository_url':'file:///tmp/a','dependencies':{'evil':'^1.0.0'},'native':{'library':'--evil','cmake_target':'../evil','pkg_config':[';shell']}}
  for key,value in cases.items():
   m=self.sample('invalid-test');m[key]=value
   with self.subTest(key=key):self.assertEqual(request('POST','/api/packages',m,token())[0],400)
 def test_ownership(self):self.assertEqual(request('POST','/api/packages',self.sample('other-owner'),token('OtherUser','user'))[0],403)
 def test_owner_publish_and_conflict(self):
  m=self.sample('own-module');m['repository_url']='https://github.com/TestUser/module';auth=token('TestUser','user');self.assertEqual(request('POST','/api/packages',m,auth)[0],201);m['version']='0.2.0';m['repository_url']='https://github.com/OtherUser/module';self.assertEqual(request('POST','/api/packages',m,token('OtherUser','user'))[0],403)
 def test_numeric_version_sort(self):
  for version in ['0.9.0','0.10.0']:
   self.assertEqual(request('POST','/api/packages',self.sample('sort-module',version),token())[0],201)
  self.assertEqual(request('GET','/api/packages/sort-module')[1]['versions'][0]['version'],'0.10.0')
 def test_duplicate_concurrent(self):
  m=self.sample('concurrent-module');auth=token()
  with concurrent.futures.ThreadPoolExecutor(max_workers=8) as e:codes=list(e.map(lambda _:request('POST','/api/packages',m,auth)[0],range(8)))
  self.assertEqual(codes.count(201),1);self.assertEqual(codes.count(409),7)
 def test_sql_literal_search(self):self.assertEqual(request('GET',"/api/packages?q=%27%3BDROP%20TABLE%20packages%3B--")[0],200);self.assertEqual(request('GET','/api/stats')[0],200)
 def test_bad_json(self):self.assertEqual(request('POST','/api/packages',auth=token(),raw=b'{broken')[0],400)
 def test_unknown_routes(self):self.assertEqual(request('GET','/api/packages/nonexistent')[0],404)
 def test_parallel_reads(self):
  with concurrent.futures.ThreadPoolExecutor(max_workers=20) as e:self.assertTrue(all(s==200 for s in e.map(lambda _:request('GET','/api/packages')[0],range(80))))
if __name__=='__main__':
 if '--seed-site' in sys.argv:
  # This is the newly created Forge site configuration, never another stack's .env.
  config=dict(line.split('=',1) for line in (ROOT/'.env').read_text().splitlines() if '=' in line)
  SECRET=config['JWT_SECRET'];BASE='http://127.0.0.1:'+config.get('SITE_PORT','18101');seed();print('Registry seed installed')
 elif '--seed' in sys.argv:seed()
 else:unittest.main()
