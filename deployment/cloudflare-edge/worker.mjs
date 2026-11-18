const policy = "default-src 'self'; script-src 'self' 'wasm-unsafe-eval' https://storage.googleapis.com/forge-lang-514802-frontend/; worker-src 'self'; frame-src 'self'; style-src 'self' https://storage.googleapis.com/forge-lang-514802-frontend/; connect-src 'self' https://storage.googleapis.com/forge-lang-514802-frontend/ https://storage.googleapis.com/forge-lang-514802-releases/; img-src 'self' https://avatars.githubusercontent.com; object-src 'none'; base-uri 'self'; frame-ancestors 'none'";
const runnerPolicy = "default-src 'none'; script-src 'self' blob:; worker-src blob:; connect-src 'none'; frame-ancestors 'self'; sandbox allow-scripts";
const bucket = "https://storage.googleapis.com/forge-lang-514802-frontend";
async function staticContent(request, env, url) {
  if (request.method !== "GET" && request.method !== "HEAD") return env.ASSETS.fetch(request);
  const channel = url.hostname === "gcp-preview.forge-lang.org" ? "preview" : "current";
  try {
    const pointer = await fetch(bucket + "/origin-cd/" + channel + ".json", {
      cf: { cacheEverything: true, cacheTtlByStatus: { "200": 15, "400-599": 0 } }
    });
    if (pointer.ok) {
      const release = await pointer.json();
      let path;
      try { path = decodeURIComponent(url.pathname); } catch { return env.ASSETS.fetch(request); }
      const item = /^\/assets\/[a-zA-Z0-9_.-]+$/.test(path) ? path.slice(1) :
        Object.hasOwn(release.files, path) ? release.files[path] : release.files["/index.html"];
      if (typeof item !== "string" || !/^(assets\/[a-zA-Z0-9_.-]+|origin-cd\/blobs\/[a-f0-9]{64}\.[a-zA-Z0-9]+)$/.test(item)) {
        return env.ASSETS.fetch(request);
      }
      const headers = new Headers();
      for (const name of ["Range", "If-None-Match", "If-Modified-Since", "If-Range"]) {
        if (request.headers.has(name)) headers.set(name, request.headers.get(name));
      }
      const file = await fetch(bucket + "/" + item, { method: request.method, headers });
      if (file.ok || file.status === 304 || file.status === 416) {
        const retained = new Headers();
        for (const name of ["Content-Type", "Content-Length", "ETag", "Last-Modified", "Accept-Ranges", "Content-Range", "Content-Encoding"]) {
          if (file.headers.has(name)) retained.set(name, file.headers.get(name));
        }
        retained.set("X-Forge-Site-Revision", release.image);
        return new Response(file.body, { status: file.status, headers: retained });
      }
    }
  } catch { /* Keep the previous verified static site available during storage outages. */ }
  return env.ASSETS.fetch(request);
}
export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (url.hostname === "storage.forge-lang.org") {
      const object = /^\/v1\/objects\/([a-f0-9]{64})$/.exec(url.pathname);
      if (!object || !["GET", "HEAD"].includes(request.method)) return new Response(null, { status: 405 });
      const headers = new Headers();
      for (const name of ["Range", "If-None-Match", "If-Modified-Since"]) {
        if (request.headers.has(name)) headers.set(name, request.headers.get(name));
      }
      return fetch("https://storage.googleapis.com/forge-lang-514802-releases/package-objects/" + object[1],
                   { method: request.method, headers });
    }
    if (url.protocol === "http:" || url.hostname === "www.forge-lang.org" || (url.hostname === "play.forge-lang.org" || url.hostname === "packages.forge-lang.org") && url.pathname === "/") {
      if (url.hostname === "play.forge-lang.org") url.pathname = "/play";
      if (url.hostname === "packages.forge-lang.org") url.pathname = "/packages";
      url.hostname = "forge-lang.org";
      url.protocol = "https:";
      return Response.redirect(url.toString(), 308);
    }
    if (url.pathname.startsWith("/releases/")) {
      const target = new URL("https://storage.googleapis.com/forge-lang-514802-releases/" + url.pathname.slice("/releases/".length));
      target.search = url.search;
      return Response.redirect(target.toString(), 307);
    }
    const proxy = ["/api/", "/live/", "/materials/"].some((prefix) => url.pathname.startsWith(prefix)) || ["/api", "/github/callback", "/install.sh", "/activity.json", "/benchmarks.json"].includes(url.pathname);
    if (proxy) {
      const origin = new URL(env.ORIGIN);
      const host = url.host;
      url.protocol = origin.protocol;
      url.host = origin.host;
      const upstream = new Request(url, new Request(request, { redirect: "manual" }));
      upstream.headers.set("Host", origin.host);
      upstream.headers.set("X-Forwarded-Host", host);
      upstream.headers.set("X-Forwarded-Proto", "https");
      return fetch(upstream);
    }
    const response = await staticContent(request, env, url);
    const headers = new Headers(response.headers);
    headers.set("X-Content-Type-Options", "nosniff");
    headers.set("Referrer-Policy", "strict-origin-when-cross-origin");
    headers.set("Permissions-Policy", "camera=(), microphone=(), geolocation=()");
    headers.set("Strict-Transport-Security", "max-age=31536000");
    headers.set("Content-Security-Policy", url.pathname === "/playground/runner.html" ? runnerPolicy : policy);
    headers.set("Cache-Control", url.pathname.startsWith("/assets/") ? "public,max-age=31536000,immutable" : url.pathname.startsWith("/playground/") ? "public,max-age=3600,must-revalidate" : "no-cache");
    return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
  }
};

