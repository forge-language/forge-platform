export type RegistryUser = {sub: string; role: string; exp?: number};
export const tokenKey = 'forge_registry_token';
export class ApiError extends Error {
  constructor(message: string, public readonly body: unknown) { super(message); }
}
export async function api<T>(path: string, options: RequestInit = {}): Promise<T> {
  const token = sessionStorage.getItem(tokenKey);
  const response = await fetch(path, {...options, headers: {
    'Content-Type': 'application/json', ...(token ? {Authorization: `Bearer ${token}`} : {}), ...options.headers,
  }});
  let body: unknown;
  try {body = await response.json();}
  catch {throw new ApiError(`HTTP ${response.status}: 서버 응답이 JSON 형식이 아닙니다.`, null);}
  if (!response.ok) {
    const error = body && typeof body === 'object' ? body as {message?: unknown; error?: unknown} : {};
    const message = typeof error.message === 'string' ? error.message : typeof error.error === 'string' ? error.error : `HTTP ${response.status}`;
    throw new ApiError(message, body);
  }
  return body as T;
}
