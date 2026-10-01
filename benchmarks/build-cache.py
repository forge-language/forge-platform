import argparse,json,os,pathlib,shutil,statistics,subprocess,tempfile,time
parser=argparse.ArgumentParser()
parser.add_argument("--toolchain",required=True)
parser.add_argument("--before",required=True)
parser.add_argument("--after",required=True)
parser.add_argument("--output",required=True)
args=parser.parse_args()
root=pathlib.Path(args.toolchain).resolve()
with tempfile.TemporaryDirectory(prefix='forge-real-build-') as temp:
 p=pathlib.Path(temp); project=p/'app';project.mkdir();tc=p/'toolchain'
 (tc/'build/bin').mkdir(parents=True);(tc/'build/lib').mkdir();shutil.copytree(root/'include',tc/'include')
 shutil.copy2(root/'build/bin/forge',tc/'build/bin/forge')
 for name in ['libforge_runtime.a','libforge_std.a']:shutil.copy2(root/'build/lib'/name,tc/'build/lib'/name)
 (project/'forge.json').write_text(json.dumps({'name':'benchmark-app','entry':'main.fg','dependencies':{}}))
 (project/'main.fg').write_text('import helper;native main { println(helper.value());return 0;}')
 (project/'helper.fg').write_text('fn value(): int { return 42; }')
 env={**os.environ,'FORGE_ROOT':str(tc),'FORGE_REGISTRY':'builtin','LD_LIBRARY_PATH':os.environ.get('LD_LIBRARY_PATH','')}
 bins={'before':str(pathlib.Path(args.before).resolve()),'after':str(pathlib.Path(args.after).resolve())}; rows=[]
 def run(name):
  start=time.perf_counter();r=subprocess.run([bins[name],'build'],cwd=project,env=env,text=True,capture_output=True,timeout=60)
  if r.returncode:raise RuntimeError(r.stdout+r.stderr)
  elapsed=time.perf_counter()-start
  assert subprocess.check_output([str(project/'build/app')],text=True)=='42\n'
  return {'variant':name,'seconds':elapsed,'cache_hit':'Build cache hit' in r.stdout}
 for name in bins:run(name)
 for i in range(7):
  for name in (['before','after'] if i%2==0 else ['after','before']):
   r=run(name);r['repeat']=i+1;rows.append(r)
 med={k:statistics.median(r['seconds'] for r in rows if r['variant']==k) for k in bins}
 report={'scope':'Actual compiler, cc and linked executable; unchanged application with one Forge import, no external native modules; toolchain and manager startup/hashing included','repeats':7,'medians_seconds':med,'speedup':med['before']/med['after'],'runs':rows}
 pathlib.Path(args.output).write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report))
