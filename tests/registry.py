#!/usr/bin/env python3
import base64,concurrent.futures,hashlib,hmac,json,os,pathlib,sys,time,unittest,urllib.request,urllib.error
BASE=os.environ.get('REGISTRY_BASE','http://127.0.0.1:18103')
SECRET=os.environ.get('TEST_JWT_SECRET','registry-integration-only')
ROOT=pathlib.Path(__file__).resolve().parents[1]
def token(user='Helloworld0822',role='admin',expiry=None,github_id=None):
 def b64(value):return base64.urlsafe_b64encode(json.dumps(value,separators=(',',':')).encode()).rstrip(b'=')
 h=b64({'alg':'HS256','typ':'JWT'});p=b64({'sub':user,'role':role,'github_id':github_id or {'Helloworld0822':'1','TestUser':'2','OtherUser':'3'}.get(user,'4'),'exp':expiry or int(time.time())+1200});data=h+b'.'+p
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
  m['acknowledge_review']=True
  status,value=request('POST','/api/packages',m,token())
  if status not in (201,409):raise RuntimeError((status,value))
class RegistryTest(unittest.TestCase):
 @classmethod
 def setUpClass(cls):seed()
 def sample(self,name='test-module',version='0.1.0'):
  value=json.loads((ROOT/'backend/seed.json').read_text())[0];value.update(name=name,version=version,git_commit='a'*40,repository_url='https://github.com/Helloworld0822/'+name+'__'+version.replace('.','_'),acknowledge_review=True);return value
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
 def test_immutable(self):self.assertEqual(request('POST','/api/packages',{**json.loads((ROOT/'backend/seed.json').read_text())[0],'acknowledge_review':True},token())[0],409)
 def test_bad_inputs(self):
  cases={'name':'../escape','version':'1.01.0','git_commit':'main','module':'../secrets','repository_url':'file:///tmp/a','dependencies':{'evil':'^1.0.0'},'native':{'library':'--evil','cmake_target':'../evil','pkg_config':[';shell']}}
  for key,value in cases.items():
   m=self.sample('invalid-test');m[key]=value
   with self.subTest(key=key):self.assertEqual(request('POST','/api/packages',m,token())[0],400)
 def test_ownership(self):self.assertEqual(request('POST','/api/packages',self.sample('other-owner'),token('OtherUser','user'))[0],403)
 def test_owner_publish_and_conflict(self):
  m=self.sample('own-module');m['repository_url']='https://github.com/TestUser/own-module__0_1_0';auth=token('TestUser','user');self.assertEqual(request('POST','/api/packages',m,auth)[0],201);m['version']='0.2.0';m['repository_url']='https://github.com/OtherUser/own-module__0_2_0';self.assertEqual(request('POST','/api/packages',m,token('OtherUser','user'))[0],403)
 def test_numeric_version_sort(self):
  for version in ['0.9.0','0.10.0']:
   self.assertEqual(request('POST','/api/packages',self.sample('sort-module',version),token())[0],201)
  self.assertEqual(request('GET','/api/packages/sort-module')[1]['versions'][0]['version'],'0.10.0')
 def test_duplicate_concurrent(self):
  m=self.sample('concurrent-module');auth=token()
  with concurrent.futures.ThreadPoolExecutor(max_workers=8) as e:codes=list(e.map(lambda _:request('POST','/api/packages',m,auth)[0],range(8)))
  self.assertEqual(codes.count(201),1);self.assertTrue(all(code in (201,409,503) for code in codes));self.assertEqual(request('POST','/api/packages',m,auth)[0],409)
 def test_sql_literal_search(self):self.assertEqual(request('GET',"/api/packages?q=%27%3BDROP%20TABLE%20packages%3B--")[0],200);self.assertEqual(request('GET','/api/stats')[0],200)
 def test_bad_json(self):self.assertEqual(request('POST','/api/packages',auth=token(),raw=b'{broken')[0],400)
 def test_ambiguous_json_is_rejected(self):
  sample=json.dumps(self.sample('ambiguous-module'),separators=(',',':'))
  for raw in [sample[:-1]+',"name":"different-module"}',sample[:-1]+',"\\u006eame":"different-module"}',sample.replace('ambiguous-module','ambiguous-module\\u0000suffix')]:
   with self.subTest(raw=raw):self.assertEqual(request('POST','/api/packages',auth=token(),raw=raw.encode())[0],400)
 def test_unsafe_signed_claims_are_rejected(self):
  for auth in [token(role='admin\x00suffix'),token(user='Helloworld0822\x00suffix'),token(expiry=2**63),token(expiry=2**64-1)]:
   with self.subTest(auth=auth):self.assertEqual(request('GET','/api/me',auth=auth)[0],401)
 def test_github_token_authentication(self):
  status,body=request('POST','/api/auth/github/token',{'token':'fixture-user-token'})
  self.assertEqual(status,200);self.assertEqual(body['user']['github_id'],'2');self.assertEqual(body['user']['role'],'user')
  self.assertNotIn('fixture-user-token',json.dumps(body));self.assertEqual(request('GET','/api/me',auth=body['token'])[0],200)
  self.assertEqual(request('POST','/api/auth/github/token',{'token':'wrong'})[0],401)
 def test_repository_inspect_and_register(self):
  value={'repository_url':'https://github.com/TestUser/from-repo__0_1_0','ref':'main'}
  status,body=request('POST','/api/packages/inspect',value,token('TestUser','user'))
  self.assertEqual(status,200);self.assertEqual(body['manifest']['git_commit'],'a'*40)
  self.assertEqual(body['inspection']['status'],'review_required')
  self.assertEqual(request('POST','/api/packages/register',value,token('TestUser','user'))[0],400)
  value.update(ref=body['manifest']['git_commit'],acknowledge_review=True)
  self.assertEqual(request('POST','/api/packages/register',value,token('TestUser','user'))[0],201)
 def test_source_and_identity_mismatch_are_rejected(self):
  value=self.sample('forged-source');value['license']='made-up';value['repository_url']='https://github.com/TestUser/forged-source__0_1_0'
  self.assertEqual(request('POST','/api/packages',value,token('TestUser','user'))[0],422)
  self.assertEqual(request('POST','/api/packages/inspect',{'repository_url':'http://127.0.0.1/secrets'},token())[0],400)
  value=self.sample('renamed-owner');value['repository_url']='https://github.com/TestUser/renamed-owner__0_1_0'
  self.assertEqual(request('POST','/api/packages',value,token('TestUser','user'))[0],201)
  forged=token('OtherUser','user')
  # A username string cannot transfer ownership: repository and stored package use immutable ids.
  self.assertEqual(request('POST','/api/packages',value,forged)[0],403)
 def test_inspection_protocol_must_fail_closed(self):
  for name in ('unknown-scan','wrong-scan','missing-scan'):
   body={'repository_url':'https://github.com/TestUser/'+name+'__0_1_0','acknowledge_review':True}
   with self.subTest(name=name):
    self.assertEqual(request('POST','/api/packages/register',body,token('TestUser','user'))[0],503)
    self.assertEqual(request('GET','/api/packages/'+name)[0],404)
 def test_reused_login_cannot_take_numeric_owner_or_admin(self):
  body={'repository_url':'https://github.com/TestUser/reused-user__0_1_0','acknowledge_review':True}
  self.assertEqual(request('POST','/api/packages/register',body,token('TestUser','user',github_id='3'))[0],403)
  seed=json.loads((ROOT/'backend/seed.json').read_text())[0]
  self.assertEqual(request('POST','/api/packages/inspect',{'repository_url':seed['repository_url']},token(github_id='2'))[0],403)
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
