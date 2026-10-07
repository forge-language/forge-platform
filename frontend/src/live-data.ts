import {useEffect,useState} from 'react';
export function useLiveData<T>(path:string){
  const [data,setData]=useState<T|null>(null),[error,setError]=useState('');
  useEffect(()=>{let active=true,inFlight=false;const controller=new AbortController();
    async function update(){if(inFlight||document.hidden)return;inFlight=true;try{const response=await fetch(path,{cache:'no-store',signal:controller.signal});if(!response.ok)throw new Error(String(response.status));const value=await response.json();if(active){setData(value);setError('')}}catch(e){if(active&&!(e instanceof DOMException&&e.name==='AbortError'))setError('Unavailable')}finally{inFlight=false}}
    void update();const timer=window.setInterval(update,30000);const visible=()=>{if(!document.hidden)void update()};document.addEventListener('visibilitychange',visible);
    return()=>{active=false;controller.abort();window.clearInterval(timer);document.removeEventListener('visibilitychange',visible)};
  },[path]);return {data,error};
}
export type Material={id:string;title:string;path:string;file:string;format:string;source:string|null;repository:string|null;kind:string};
export type BenchmarkData={updated_at:string|null;checked_at:string;reports:Material[];errors:string[]};
export function viewerLink(file:string){return '/view?file='+encodeURIComponent(file)}
