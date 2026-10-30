import {createContext, useContext, useEffect, useState, type ReactNode} from 'react';

export type Language = 'en' | 'ko';
type LanguageState = {language: Language; setLanguage: (language: Language) => void};
const LanguageContext = createContext<LanguageState>({language: 'en', setLanguage: () => {}});
const storageKey = 'forge_language';

export function LanguageProvider({children}: {children: ReactNode}) {
  const [language, setLanguage] = useState<Language>(() => {
    try {return localStorage.getItem(storageKey) === 'ko' ? 'ko' : 'en';}
    catch {return 'en';}
  });
  useEffect(() => {
    document.documentElement.lang = language;
    try {localStorage.setItem(storageKey, language);} catch { /* Storage may be disabled. */ }
  }, [language]);
  return <LanguageContext.Provider value={{language, setLanguage}}>{children}</LanguageContext.Provider>;
}

export function useLanguage() {return useContext(LanguageContext);}
export function useText() {
  const {language} = useLanguage();
  return (english: string, korean: string) => language === 'ko' ? korean : english;
}

export function LanguageSwitch() {
  const {language, setLanguage} = useLanguage();
  return <label className="text-sm flex items-center gap-2"><span className="sr-only">Language / 언어</span>
    <select aria-label="Language / 언어" className="border border-line bg-paper px-2 py-1" value={language} onChange={event => setLanguage(event.target.value === 'ko' ? 'ko' : 'en')}>
      <option value="en">English</option><option value="ko">한국어</option>
    </select>
  </label>;
}
