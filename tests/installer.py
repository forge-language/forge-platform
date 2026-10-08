#!/usr/bin/env python3
import contextlib,hashlib,http.server,io,json,os,pathlib,shutil,subprocess,tarfile,tempfile,threading,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
INSTALL=ROOT/'scripts/install.sh'
class Quiet(http.server.SimpleHTTPRequestHandler):
 def log_message(self,*args):pass
class InstallerTest(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.home=pathlib.Path(self.temp.name)/'home';self.home.mkdir();self.dest=self.home/'.forge'
  self.env={**os.environ,'FORGE_PROFILE_ROOT':str(self.home),'FORGE_HOME':str(self.dest),'FORGE_DOWNLOAD_BASE':os.environ.get('FORGE_TEST_ORIGIN','http://localhost:18101')}
 def tearDown(self):self.temp.cleanup()
 def run_install(self,*args,env=None):return subprocess.run(['bash',str(INSTALL),*args],env=env or self.env,text=True,capture_output=True,timeout=1050 if self.env['FORGE_DOWNLOAD_BASE'].startswith('https://') else 180)
 def test_install_update_compile_and_uninstall(self):
  p=self.run_install();self.assertEqual(p.returncode,0,p.stderr)
  version=subprocess.check_output([str(self.dest/'bin/forge'),'--version'],env=self.env,text=True);self.assertIn('0.3.0-preview.6',version)
  self.assertIn('forge-pm',subprocess.check_output([str(self.dest/'bin/forge-pm'),'--version'],env=self.env,text=True))
  source=self.home/'hello.fg';source.write_text('native main {println("설치 검증");return 0;}')
  p=subprocess.run([str(self.dest/'bin/forge'),str(source),'-o',str(self.home/'hello')],env=self.env,text=True,capture_output=True);self.assertEqual(p.returncode,0,p.stderr)
  self.assertEqual(subprocess.check_output([str(self.home/'hello')],text=True).strip(),'설치 검증')
  p=self.run_install();self.assertEqual(p.returncode,0,p.stderr);self.assertEqual((self.home/'.bashrc').read_text().count('# >>> forge environment >>>'),1)
  project=self.home/'project';project.mkdir()
  def pm(*args):return subprocess.run([str(self.dest/'bin/forge-pm'),*args],cwd=project,env=self.env,text=True,capture_output=True,timeout=180)
  self.assertEqual(pm('init','hello-app').returncode,0);project=project/'hello-app';p=pm('run');self.assertEqual(p.returncode,0,p.stderr);self.assertIn('Hello, Forge!',p.stdout)
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
 def test_site_origin_rewrite_preserves_trailing_slash_normalization(self):
  deployed=self.home/'install.sh';deployed.write_bytes(INSTALL.read_bytes())
  nginx=self.home/'nginx.conf';nginx.write_text((ROOT/'frontend/nginx.conf').read_text())
  entrypoint=(ROOT/'frontend/40-forge.sh').read_text().replace('/usr/share/nginx/html/install.sh',str(deployed)).replace('/etc/nginx/conf.d/default.conf',str(nginx)).replace('/etc/nginx/forge',str(self.home/'nginx-internal')).replace('/srv/releases/artifacts.conf',str(self.home/'releases-artifacts.conf'))
  origin=self.env['FORGE_DOWNLOAD_BASE'].rstrip('/')+'/'
  for _ in range(2):
   p=subprocess.run(['sh','-c',entrypoint],env={**self.env,'FORGE_DOWNLOAD_BASE':origin},text=True,capture_output=True)
   self.assertEqual(p.returncode,0,p.stderr)
   self.assertIn('BASE_URL=${BASE_URL%/}',deployed.read_text())
  env={**self.env};env.pop('FORGE_DOWNLOAD_BASE')
  p=subprocess.run(['bash',str(deployed),'--no-modify-path'],env=env,text=True,capture_output=True,timeout=1050 if origin.startswith('https://') else 180)
  self.assertEqual(p.returncode,0,p.stdout+p.stderr)
  self.assertIn('0.3.0-preview.6',subprocess.check_output([str(self.dest/'bin/forge'),'--version'],text=True))
 def test_site_release_map_cold_start_and_read_only_source(self):
  deployed=self.home/'install.sh';deployed.write_bytes(INSTALL.read_bytes())
  nginx=self.home/'nginx.conf';nginx.write_text((ROOT/'frontend/nginx.conf').read_text())
  directory=self.home/'nginx-internal';source=self.home/'artifacts.conf'
  entrypoint=(ROOT/'frontend/40-forge.sh').read_text().replace('/usr/share/nginx/html/install.sh',str(deployed)).replace('/etc/nginx/conf.d/default.conf',str(nginx)).replace('/etc/nginx/forge',str(directory)).replace('/srv/releases/artifacts.conf',str(source))
  def start():
   process=subprocess.run(['sh','-c',entrypoint],env=self.env,text=True,capture_output=True)
   self.assertEqual(process.returncode,0,process.stderr)
   return (directory/'release-artifacts.conf').read_text()
  self.assertEqual(start(),'map $uri $forge_release_url { default ""; }\n')
  self.assertFalse(source.exists())
  published='map $uri $forge_release_url { default ""; /releases/old.tar.gz "https://example.org/old.tar.gz"; }\n'
  source.write_text(published);source.chmod(0o444)
  self.assertEqual(start(),published);self.assertEqual(source.read_text(),published)
  self.assertEqual(source.stat().st_mode & 0o777,0o444)
  source.unlink()
  self.assertEqual(start(),'map $uri $forge_release_url { default ""; }\n')
  self.assertFalse((directory/'release-artifacts.conf.new').exists())
 def test_preserve_unmanaged_directory(self):
  self.dest.mkdir();(self.dest/'precious.txt').write_text('keep');self.assertNotEqual(self.run_install().returncode,0);self.assertEqual((self.dest/'precious.txt').read_text(),'keep')
 def test_checksum_failure_preserves_current(self):
  p=self.run_install('--no-modify-path');self.assertEqual(p.returncode,0,p.stderr);before=os.readlink(self.dest/'current')
  directory=pathlib.Path(self.temp.name)/'server';release=directory/'releases/0.3.0-preview.6';release.mkdir(parents=True);archive='forge-0.3.0-preview.6-linux-x86_64.tar.gz';(release/archive).write_bytes(b'corrupt');(release/(archive+'.sha256')).write_text('0'*64+'  '+archive)
  handler=lambda *a,**kw:Quiet(*a,directory=str(directory),**kw)
  server=http.server.ThreadingHTTPServer(('127.0.0.1',0),handler);worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
  try:
   env={**self.env,'FORGE_DOWNLOAD_BASE':f'http://localhost:{server.server_port}'};p=self.run_install(env=env);self.assertNotEqual(p.returncode,0);self.assertIn('SHA-256 mismatch',p.stderr);self.assertEqual(os.readlink(self.dest/'current'),before)
  finally:server.shutdown();server.server_close();worker.join()
 def test_download_retries_resume_and_preserve_current_on_failure(self):
  archive='forge-0.3.0-preview.6-linux-x86_64.tar.gz'
  content=(ROOT/'releases/0.3.0-preview.6'/archive).read_bytes()
  checksum=(hashlib.sha256(content).hexdigest()+'  '+archive+'\n').encode()
  self.dest.mkdir();(self.dest/'.forge-install').write_text('forge-install-v1\n')
  old=self.dest/'old';old.mkdir();current=self.dest/'current';current.symlink_to(old)
  for scenario in ['interrupted','no-ranges','transient','missing','exhausted','changed']:
   with self.subTest(scenario=scenario):
    current.unlink();current.symlink_to(old)
    requests=[];checksum_requests=[]
    class Download(http.server.BaseHTTPRequestHandler):
     def log_message(self,*args):pass
     def do_GET(self):
      if self.path.endswith('.sha256'):
       checksum_requests.append(self.path)
       if scenario=='transient' and len(checksum_requests)==1:
        self.send_error(503);return
       self.send_response(200);self.send_header('Content-Length',str(len(checksum)));self.end_headers();self.wfile.write(checksum);return
      if not self.path.endswith('/'+archive):self.send_error(404);return
      requested=self.headers.get('Range');requests.append(requested)
      if scenario=='missing':self.send_error(404);return
      if scenario=='transient' and len(requests)==1:self.send_error(503);return
      start=int(requested.removeprefix('bytes=').removesuffix('-')) if requested else 0
      if scenario=='no-ranges':start=0
      self.send_response(206 if start else 200)
      self.send_header('Content-Length',str(len(content)-start))
      if start:self.send_header('Content-Range',f'bytes {start}-{len(content)-1}/{len(content)}')
      self.end_headers()
      body=content[start:]
      if scenario=='exhausted' or (scenario in ['interrupted','no-ranges','changed'] and len(requests)==1):body=body[:131072]
      elif scenario=='changed':body=bytes([body[0]^1])+body[1:]
      try:self.wfile.write(body);self.wfile.flush()
      except (BrokenPipeError,ConnectionResetError):pass
      self.close_connection=True
    server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Download);worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
    try:
     env={**self.env,'FORGE_DOWNLOAD_BASE':f'http://localhost:{server.server_port}'}
     p=self.run_install('--no-modify-path',env=env)
     if scenario in ['interrupted','no-ranges','transient']:
      self.assertEqual(p.returncode,0,p.stdout+p.stderr);self.assertNotEqual(os.readlink(current),str(old))
      self.assertIn('0.3.0-preview.6',subprocess.check_output([str(self.dest/'bin/forge'),'--version'],text=True))
     else:
      self.assertNotEqual(p.returncode,0,p.stdout+p.stderr);self.assertEqual(os.readlink(current),str(old))
      self.assertIn('SHA-256 mismatch' if scenario=='changed' else 'Release download failed',p.stderr)
     expected={
      'interrupted':[None,'bytes=131072-'],
      'no-ranges':[None,'bytes=131072-',None],
      'transient':[None,None],
      'missing':[None],
      'exhausted':[None,'bytes=131072-','bytes=262144-'],
      'changed':[None,'bytes=131072-'],
     }
     self.assertEqual(requests,expected[scenario]);self.assertFalse(list(self.dest.glob('.install.*')))
     self.assertEqual(len(checksum_requests),2 if scenario=='transient' else 1)
    finally:server.shutdown();server.server_close();worker.join()
 def test_reject_managed_symlinks_before_writes(self):
  self.dest.mkdir();outside=self.home/'outside';outside.mkdir();marker=outside/'marker';marker.write_text('preserve')
  for name in ['bin','toolchains','.forge-install','env']:
   with self.subTest(name=name):
    link=self.dest/name;link.symlink_to(outside if name in ['bin','toolchains'] else marker)
    p=self.run_install('--no-modify-path');self.assertNotEqual(p.returncode,0);self.assertEqual(marker.read_text(),'preserve');link.unlink()
 def test_uninstall_cannot_dereference_root_symlink(self):
  outside=self.home/'outside';outside.mkdir();(outside/'.forge-install').write_text('forge-install-v1\n');precious=outside/'precious';precious.write_text('keep');self.dest.symlink_to(outside)
  for suffix in ['', '/', '//', '/.']:
   with self.subTest(suffix=suffix):
    p=self.run_install('--prefix',str(self.dest)+suffix,'--uninstall');self.assertNotEqual(p.returncode,0);self.assertEqual(precious.read_text(),'keep')
 def test_uninstall_rejects_critical_path_spellings(self):
  for path in ['//','/./','/usr/','/usr//local/',str(pathlib.Path.home())+'/']:
   with self.subTest(path=path):self.assertNotEqual(self.run_install('--prefix',path,'--uninstall').returncode,0)
 def test_unsafe_archives_preserve_installation(self):
  self.dest.mkdir();(self.dest/'.forge-install').write_text('forge-install-v1\n');old=self.dest/'old';old.mkdir();(self.dest/'current').symlink_to(old)
  directory=pathlib.Path(self.temp.name)/'bad-archive';release=directory/'releases/0.3.0-preview.6';release.mkdir(parents=True)
  archive=release/'forge-0.3.0-preview.6-linux-x86_64.tar.gz'
  handler=lambda *a,**kw:Quiet(*a,directory=str(directory),**kw)
  server=http.server.ThreadingHTTPServer(('127.0.0.1',0),handler);worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
  env={**self.env,'FORGE_DOWNLOAD_BASE':f'http://localhost:{server.server_port}'}
  try:
   for kind in ['symlink','hardlink','fifo','setuid','traversal','corrupt','owner-size','missing-loader']:
    with self.subTest(kind=kind):
     if kind=='corrupt':archive.write_bytes(b'not a tarball')
     elif kind=='owner-size':
      sparse=directory/'oversized'
      with sparse.open('wb') as stream:stream.truncate(1073741825)
      subprocess.run(['tar','--sparse','--owner=a 0 b','--group=c','-czf',str(archive),'-C',str(directory),'oversized'],check=True)
     elif kind=='missing-loader':
      with tarfile.open(archive,'w:gz') as tar:
       for name in ['build/bin/forge','libexec/forge-pm']:
        entry=tarfile.TarInfo(name);entry.mode=0o755;script=b'#!/bin/sh\nexit 0\n';entry.size=len(script);tar.addfile(entry,io.BytesIO(script))
     else:
      with tarfile.open(archive,'w:gz') as tar:
       entry=tarfile.TarInfo('../escape' if kind=='traversal' else 'unsafe');entry.mode=0o4755 if kind=='setuid' else 0o644
       if kind in ['symlink','hardlink']:entry.type=tarfile.SYMTYPE if kind=='symlink' else tarfile.LNKTYPE;entry.linkname=str(self.home/'outside')
       elif kind=='fifo':entry.type=tarfile.FIFOTYPE
       else:entry.size=3
       tar.addfile(entry,io.BytesIO(b'bad') if entry.isreg() else None)
     archive.with_name(archive.name+'.sha256').write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+'  '+archive.name+'\n')
     p=self.run_install('--no-modify-path',env=env);self.assertNotEqual(p.returncode,0,p.stdout+p.stderr)
     self.assertIn('Release is incomplete' if kind=='missing-loader' else 'archive',p.stderr);self.assertEqual(os.readlink(self.dest/'current'),str(old));self.assertFalse((self.home/'escape').exists());self.assertFalse((self.home/'outside').exists())
  finally:server.shutdown();server.server_close();worker.join()
if __name__=='__main__':
 if os.environ.get('FORGE_TEST_ORIGIN'):unittest.main()
 else:
  import importlib.util
  spec=importlib.util.spec_from_file_location('installed_cache',ROOT/'tests/installed-cache.py');assets=importlib.util.module_from_spec(spec);spec.loader.exec_module(assets)
  with assets.local_release_origin() as origin:
   os.environ['FORGE_TEST_ORIGIN']=origin;os.environ['FORGE_REGISTRY']='builtin';os.environ['GIT_MASTER']='1';unittest.main()
