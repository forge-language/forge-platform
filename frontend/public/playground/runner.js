let worker, timer;
function stop() { clearTimeout(timer); worker?.terminate(); worker = null; }
window.addEventListener('message', event => {
  if (event.source !== parent || event.data?.type !== 'run' || typeof event.data.javascript !== 'string') return;
  stop();
  let output = '', finished = false;
  const finish = (error) => {
    if (finished) return;
    finished = true; stop();
    parent.postMessage({type:'result', output, error}, '*');
  };
  const prelude = `let size=0;console.log=(...args)=>{const line=args.map(String).join(' ')+'\\n';size+=line.length;if(size>16000)throw new Error('Output exceeds 16,000 characters');postMessage({line});};\n`;
  const tail = `\npostMessage({done:true});`;
  const url = URL.createObjectURL(new Blob([prelude, event.data.javascript, tail], {type:'text/javascript'}));
  worker = new Worker(url); URL.revokeObjectURL(url);
  worker.onmessage = ({data}) => {
    if (data.done) finish();
    else if (typeof data.line === 'string') {
      output += data.line;
      if (output.length > 16000) finish('출력 제한을 초과했습니다.');
    }
  };
  worker.onerror = event => { event.preventDefault(); finish(event.message || '실행 오류'); };
  timer = setTimeout(() => finish('실행 시간 제한(2초)을 초과했습니다.'), 2000);
});
parent.postMessage({type:'ready'}, '*');
