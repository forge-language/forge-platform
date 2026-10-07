import createCompiler from './compiler.mjs';
self.onmessage = async ({data}) => {
  if (typeof data.source !== 'string' || new TextEncoder().encode(data.source).length > 65536) {
    self.postMessage({error: '코드는 64 KiB 이하로 입력하세요.'}); return;
  }
  let diagnostics = '';
  const report = line => { diagnostics = (diagnostics + line + '\n').slice(0, 8000); };
  try {
    const compiler = await createCompiler({noInitialRun:true, print:report, printErr:report});
    compiler.FS.writeFile('/input.fg', data.source);
    let status = 0;
    try { status = compiler.callMain([]) || 0; }
    catch (error) { if (error.name === 'ExitStatus') status = error.status; else throw error; }
    if (status) throw new Error(diagnostics || `Compiler exited ${status}`);
    self.postMessage({javascript:compiler.FS.readFile('/output.js', {encoding:'utf8'})});
  } catch (error) { self.postMessage({error:diagnostics || String(error.message || error)}); }
};
