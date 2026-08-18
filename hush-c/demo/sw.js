/* Hush UI service worker: cache the shell, never intercept /api/. */
const HUSH_CACHE = "hush-ui-v6";
const HUSH_PRECACHE = [
  "/",
  "/index.html",
  "/manifest.webmanifest",
  "/icon-192.png",
  "/icon-512.png",
  "/apple-touch-icon.png"
];

self.addEventListener("install", (event) => {
  event.waitUntil(
    caches.open(HUSH_CACHE).then((cache) => cache.addAll(HUSH_PRECACHE)).then(() => self.skipWaiting())
  );
});

self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys().then((keys) =>
      Promise.all(keys.filter((k) => k !== HUSH_CACHE).map((k) => caches.delete(k)))
    ).then(() => self.clients.claim())
  );
});

self.addEventListener("fetch", (event) => {
  const req = event.request;
  if (req.method !== "GET")
    return;
  const url = new URL(req.url);
  if (url.origin !== self.location.origin)
    return;
  if (url.pathname.indexOf("/api/") === 0)
    return;
  event.respondWith(
    fetch(req).then((res) => {
      const copy = res.clone();
      caches.open(HUSH_CACHE).then((cache) => cache.put(req, copy));
      return res;
    }).catch(() => caches.match(req).then((hit) => hit || caches.match("/")))
  );
});
