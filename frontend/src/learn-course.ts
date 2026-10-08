import {useEffect,useState} from 'react';
export type Translation = {en:string;ko:string};
export type Lesson = {id:string;title:Translation;summary:Translation;explanation:Translation;source:string;expected_output:string;challenge:Translation};
export type Course = {schema_version:1;lessons:Lesson[]};
export function parseCourse(value:unknown):Course {
 if(!value||typeof value!=='object')throw new Error('Invalid learning course.');
 const course=value as Course;
 if(course.schema_version!==1||!Array.isArray(course.lessons)||!course.lessons.length||course.lessons.length>40)throw new Error('Unsupported learning course.');
 const ids=new Set<string>();
 for(const lesson of course.lessons){
  if(!lesson||typeof lesson.id!=='string'||! /^[a-z0-9-]{1,40}$/.test(lesson.id)||ids.has(lesson.id))throw new Error('Invalid lesson identifier.');
  ids.add(lesson.id);
  for(const key of ['title','summary','explanation','challenge'] as const){if(typeof lesson[key]?.en!=='string'||typeof lesson[key]?.ko!=='string')throw new Error('Missing lesson translation.');}
  if(typeof lesson.source!=='string'||new TextEncoder().encode(lesson.source).length>65536||typeof lesson.expected_output!=='string'||lesson.expected_output.length>16000)throw new Error('Invalid lesson source or output.');
 }
 return course;
}
export function useCourse(){
 const[course,setCourse]=useState<Course|null>(null);const[error,setError]=useState('');
 useEffect(()=>{const abort=new AbortController();fetch('/learn/course.json',{signal:abort.signal}).then(async response=>{if(!response.ok)throw new Error(`Course request failed (HTTP ${response.status}).`);return parseCourse(await response.json());}).then(setCourse).catch(error=>{if(!abort.signal.aborted)setError(String(error.message||error));});return()=>abort.abort();},[]);
 return{course,error};
}
export function outputMatches(actual:string,expected:string){return actual.replace(/\r\n/g,'\n').trimEnd()===expected.replace(/\r\n/g,'\n').trimEnd();}
