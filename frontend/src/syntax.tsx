import {Title, Link, repo, type Go} from './home';
import {useText} from './i18n';

export const syntaxExamples = [
  {id:'output', en:'Print text and values', ko:'텍스트와 값 출력',
    explainEn:'println adds a newline and supports typed values. Import io to use print for text without a newline in native programs. The browser Playground supports println; print requires the native compiler. Multiple println arguments are written without inserted spaces.',
    explainKo:'println은 줄바꿈을 추가하며 여러 타입의 값을 출력합니다. 네이티브 프로그램에서 io를 import하면 줄바꿈 없이 텍스트를 출력하는 print를 사용할 수 있습니다. 브라우저 Playground에서는 println을 사용하세요. print는 네이티브 컴파일러가 필요합니다. println의 여러 인자 사이에는 공백이 자동으로 추가되지 않습니다.',
    source:`import io;
native main {
  print("Hello, ");
  println("Forge!");
  println("Answer: ", 42);
  return 0;
}`, output:'Hello, Forge!\nAnswer: 42'},
  {id:'variables', en:'Variables and constants', ko:'변수와 상수',
    explainEn:'Declare a typed variable with let name: type = value; and change it with assignment. Constants use const NAME = value;. Statements end with semicolons.',
    explainKo:'let 이름: 타입 = 값;으로 변수를 선언하고 대입으로 값을 변경합니다. 상수는 const 이름 = 값;으로 선언합니다. 문장은 세미콜론으로 끝납니다.',
    source:`const LIMIT = 3;
native main {
  let count: int = 1;
  let label: string = "count: ";
  count = count + LIMIT;
  println(label, count);
  return 0;
}`, output:'count: 4'},
  {id:'conditions', en:'if and else', ko:'if와 else 조건문',
    explainEn:'Put the condition in parentheses and each branch inside braces. Integer comparisons produce boolean conditions.',
    explainKo:'조건은 괄호 안에, 각 분기는 중괄호 안에 작성합니다. 정수 비교식을 조건으로 사용할 수 있습니다.',
    source:`native main {
  let score: int = 80;
  if (score >= 60) {
    println("pass");
  } else {
    println("retry");
  }
  return 0;
}`, output:'pass'},
  {id:'for', en:'for loops', ko:'for 반복문',
    explainEn:'The current preview uses a C-style for loop: initialization; condition; update. Range syntax such as for i in 0..5 is not the current supported syntax. break exits a loop; continue skips to its next iteration.',
    explainKo:'현재 preview는 초기화; 조건; 갱신으로 구성된 C 스타일 for문을 사용합니다. for i in 0..5 같은 범위 문법은 현재 지원하는 문법이 아닙니다. break는 반복을 종료하고 continue는 다음 반복으로 넘어갑니다.',
    source:`native main {
  for (let i: int = 0; i < 3; i = i + 1) {
    println(i);
  }
  return 0;
}`, output:'0\n1\n2'},
  {id:'while', en:'while loops', ko:'while 반복문',
    explainEn:'while repeats its body as long as the condition holds. Update the value used by the condition so the loop can finish.',
    explainKo:'while은 조건이 참인 동안 본문을 반복합니다. 반복이 끝나도록 조건에 사용하는 값을 갱신하세요.',
    source:`native main {
  let remaining: int = 3;
  while (remaining > 0) {
    println(remaining);
    remaining = remaining - 1;
  }
  return 0;
}`, output:'3\n2\n1'},
  {id:'functions', en:'Functions and return values', ko:'함수와 반환값',
    explainEn:'Functions use fn name(argument: type): return_type. Use return to provide a result. native main is the entry point of a native program.',
    explainKo:'함수는 fn 이름(인자: 타입): 반환타입으로 선언합니다. return으로 결과를 반환합니다. 네이티브 프로그램의 진입점은 native main입니다.',
    source:`fn square(value: int): int {
  return value * value;
}
native main {
  println(square(12));
  return 0;
}`, output:'144'},
  {id:'modules', en:'Import a standard module', ko:'표준 모듈 가져오기',
    explainEn:'import selects a module. Standard strings helpers are available as functions such as str_len and str_concat. String lengths count UTF-8 bytes.',
    explainKo:'import로 모듈을 가져옵니다. strings 표준 모듈은 str_len과 str_concat 같은 함수를 제공합니다. 문자열 길이는 UTF-8 바이트 수입니다.',
    source:`import strings;
native main {
  let message: string = str_concat("Hello, ", "Forge!");
  println(message);
  println(str_len(message));
  return 0;
}`, output:'Hello, Forge!\n13'},
];

export function Syntax({go}: {go: Go}) {
  const t = useText();
  return <section className="content py-14">
    <Title label="DOCUMENTATION" title={t('Forge syntax','Forge 문법')}>
      <p>{t('Learn the current preview with complete, executable examples. These examples are checked against the compiler; the language is still evolving.','완전한 실행 예제로 현재 preview 문법을 배우세요. 이 예제들은 컴파일러로 검증되었으며 언어는 계속 개발 중입니다.')}</p>
    </Title>
    <div className="grid md:grid-cols-[200px_1fr] gap-12">
      <aside className="text-sm"><nav aria-label={t('Syntax topics','문법 주제')} className="space-y-3 md:sticky md:top-6">
        {syntaxExamples.map(example => <a key={example.id} className="block link" href={`#${example.id}`}>{t(example.en, example.ko)}</a>)}
        <Link to="/docs" go={go}>{t('Installation guide →','설치 안내 →')}</Link>
      </nav></aside>
      <article className="min-w-0 max-w-3xl space-y-14">
        <div className="border-y border-line py-5 text-sm leading-7">
          <p>{t('Save an example as main.fg, then compile and run it:','예제를 main.fg로 저장한 뒤 컴파일하고 실행하세요:')}</p>
          <pre className="code mt-3"><code>forge main.fg -o hello{`\n`}./hello</code></pre>
          <p className="mt-3"><Link to="/play" go={go}>{t('Try the browser Playground →','브라우저 Playground에서 실행 →')}</Link></p>
        </div>
        {syntaxExamples.map(example => <section key={example.id} id={example.id} className="scroll-mt-8">
          <h2 className="text-2xl font-semibold mb-4">{t(example.en, example.ko)}</h2>
          <p className="text-muted leading-7 mb-5">{t(example.explainEn, example.explainKo)}</p>
          <pre className="code"><code>{example.source}</code></pre>
          <p className="text-xs text-muted mt-4 mb-2">{t('Output','출력')}</p>
          <pre className="font-mono text-sm whitespace-pre-wrap border-l-2 border-line pl-4">{example.output}</pre>
        </section>)}
        <section className="border-t border-line pt-8">
          <h2 className="text-2xl font-semibold mb-4">{t('Go further','더 알아보기')}</h2>
          <p className="text-muted leading-7 mb-4">{t('Native compilation uses a C compiler. The browser supports a smaller subset and does not provide processes, coroutines, filesystem access, or arbitrary native modules. Ownership and concurrency support are experimental; a complete borrow checker remains planned.','네이티브 컴파일에는 C 컴파일러가 필요합니다. 브라우저에서는 프로세스, 코루틴, 파일시스템 접근 및 임의의 네이티브 모듈을 제공하지 않습니다. 소유권과 동시성은 실험 단계이며 완전한 borrow checker는 개발 계획에 있습니다.')}</p>
          <a href={`${repo}/blob/main/LANGUAGE_SPEC.md`} className="link" target="_blank" rel="noreferrer">{t('Language specification ↗','언어 명세 ↗')}</a>
          <p className="mt-4"><a href={`${repo}/tree/main/examples`} className="link" target="_blank" rel="noreferrer">{t('More executable examples ↗','실행 가능한 예제 더 보기 ↗')}</a></p>
        </section>
      </article>
    </div>
  </section>;
}
