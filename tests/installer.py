#!/usr/bin/env python3
import contextlib,http.server,json,os,pathlib,shutil,subprocess,tempfile,threading,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
INSTALL=ROOT/'scripts/install.sh'
class Quiet(http.server.SimpleHTTPRequestHandler):
 def log_message(self,*args):pass
class InstallerTest(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.home=pathlib.Path(self.temp.name)/'home';self.home.mkdir();self.dest=self.home/'.forge'
  self.env={**os.environ,'FORGE_PROFILE_ROOT':str(self.home),'FORGE_HOME':str(self.dest),'FORGE_DOWNLOAD_BASE':os.environ.get('FORGE_TEST_ORIGIN','http://localhost:18101')}
 def tearDown(self):self.temp.cleanup()
 def run_install(self,*args,env=None):return subprocess.run(['bash',str(INSTALL),*args],env=env or self.env,text=True,capture_output=True,timeout=180)
 def test_install_update_compile_and_uninstall(self):
  p=self.run_install();self.assertEqual(p.returncode,0,p.stderr)
  version=subprocess.check_output([str(self.dest/'bin/forge'),'--version'],env=self.env,text=True);self.assertIn('0.3.0-preview.2',version)
  self.assertIn('forge-pm',subprocess.check_output([str(self.dest/'bin/forge-pm'),'--version'],env=self.env,text=True))
  source=self.home/'hello.fg';source.write_text('native main {println("설치 검증");return 0;}')
  p=subprocess.run([str(self.dest/'bin/forge'),str(source),'-o',str(self.home/'hello')],env=self.env,text=True,capture_output=True);self.assertEqual(p.returncode,0,p.stderr)
  self.assertEqual(subprocess.check_output([str(self.home/'hello')],text=True).strip(),'설치 검증')
  p=self.run_install();self.assertEqual(p.returncode,0,p.stderr);self.assertEqual((self.home/'.bashrc').read_text().count('# >>> forge environment >>>'),1)
  project=self.home/'project';project.mkdir()
  def pm(*args):return subprocess.run([str(self.dest/'bin/forge-pm'),*args],cwd=project,env=self.env,text=True,capture_output=True,timeout=180)
  self.assertEqual(pm('init','hello-app').returncode,0);p=pm('run');self.assertEqual(p.returncode,0,p.stderr);self.assertIn('Hello, Forge!',p.stdout)
  self.assertEqual(pm('search','postgres').returncode,0)
  p=pm('add','forge-postgres','0.1.1');self.assertEqual(p.returncode,0,p.stdout+p.stderr);lock=json.loads((project/'forge.lock').read_text());self.assertEqual(len(lock['packages']['forge-postgres']['git_commit']),40)
  self.assertEqual(pm('remove','forge-postgres').returncode,0)
  p=self.run_install('--uninstall');self.assertEqual(p.returncode,0,p.stderr);self.assertFalse(self.dest.exists());self.assertNotIn('forge environment',(self.home/'.bashrc').read_text())
 def test_install_without_host_c_compiler(self):
  path=self.home/'bin';path.mkdir()
  for name in ('awk','bash','cat','chmod','curl','date','dirname','env','getconf','gzip','ln','ls','mkdir','mktemp','mv','realpath','rm','sha256sum','sleep','tar','touch','uname'):
   executable=shutil.which(name)
   if executable:(path/name).symlink_to(executable)
  env={**self.env,'PATH':str(path)}
  self.assertIsNone(shutil.which('cc',path=env['PATH']))
  p=self.run_install('--no-modify-path',env=env);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
  self.assertIn('forge-pm',subprocess.check_output([str(self.dest/'bin/forge-pm'),'--version'],env=env,text=True))
 def test_dependency_cycles_conflicts_preserve_lock(self):
  p=self.run_install('--no-modify-path');self.assertEqual(p.returncode,0,p.stderr)
  base=json.loads((ROOT/'backend/seed.json').read_text())[0]
  entries={}
  def entry(name,version,deps):
   value={**base,'name':name,'version':version,'dependencies':deps};entries[(name,version)]=value
  entry('cycle-aaa','1.0.0',{'cycle-bbb':'1.0.0'});entry('cycle-bbb','1.0.0',{'cycle-aaa':'1.0.0'})
  entry('left-module','1.0.0',{'shared-module':'1.0.0'});entry('right-module','1.0.0',{'shared-module':'2.0.0'})
  entry('shared-module','1.0.0',{});entry('shared-module','2.0.0',{})
  class Registry(http.server.BaseHTTPRequestHandler):
   def log_message(self,*args):pass
   def do_GET(self):
    pieces=self.path.split('/');value=entries.get((pieces[3],pieces[5])) if len(pieces)==6 else None
    body=json.dumps(value).encode();self.send_response(200 if value else 404);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
  server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Registry);worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
  project=self.home/'graph';project.mkdir();locked=b'{"existing":"preserve"}'
  try:
   env={**self.env,'FORGE_REGISTRY':f'http://localhost:{server.server_port}'}
   for dependencies,message in [({'cycle-aaa':'1.0.0'},'Dependency cycle'),({'left-module':'1.0.0','right-module':'1.0.0'},'Conflicting versions')]:
    (project/'forge.json').write_text(json.dumps({'name':'graph-app','entry':'main.fg','dependencies':dependencies}));(project/'forge.lock').write_bytes(locked)
    p=subprocess.run([str(self.dest/'bin/forge-pm'),'install'],cwd=project,env=env,text=True,capture_output=True,timeout=30)
    self.assertNotEqual(p.returncode,0);self.assertIn(message,p.stdout);self.assertEqual((project/'forge.lock').read_bytes(),locked)
  finally:server.shutdown();server.server_close();worker.join()
 def test_reject_unsafe_install_directory(self):
  for dest in ['/',str(pathlib.Path.home()),str(self.home/'..'/'unsafe')]:self.assertNotEqual(self.run_install('--prefix',dest).returncode,0)
 def test_preserve_unmanaged_directory(self):
  self.dest.mkdir();(self.dest/'precious.txt').write_text('keep');self.assertNotEqual(self.run_install().returncode,0);self.assertEqual((self.dest/'precious.txt').read_text(),'keep')
 def test_checksum_failure_preserves_current(self):
  p=self.run_install('--no-modify-path');self.assertEqual(p.returncode,0,p.stderr);before=os.readlink(self.dest/'current')
  directory=pathlib.Path(self.temp.name)/'server';release=directory/'releases/0.3.0-preview.2';release.mkdir(parents=True);archive='forge-0.3.0-preview.2-linux-x86_64.tar.gz';(release/archive).write_bytes(b'corrupt');(release/(archive+'.sha256')).write_text('0'*64+'  '+archive)
  handler=lambda *a,**kw:Quiet(*a,directory=str(directory),**kw)
  server=http.server.ThreadingHTTPServer(('127.0.0.1',0),handler);worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
  try:
   env={**self.env,'FORGE_DOWNLOAD_BASE':f'http://localhost:{server.server_port}'};p=self.run_install(env=env);self.assertNotEqual(p.returncode,0);self.assertIn('SHA-256 mismatch',p.stderr);self.assertEqual(os.readlink(self.dest/'current'),before)
  finally:server.shutdown();server.server_close();worker.join()
if __name__=='__main__':unittest.main()
