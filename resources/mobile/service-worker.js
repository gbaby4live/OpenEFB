const SHELL = "openefb-mobile-2";
const ASSETS = ["/", "/index.html", "/app.css?v=m2.0", "/app.js?v=m2.0", "/icon.svg", "/manifest.webmanifest"];

self.addEventListener("install", (event) => {
  event.waitUntil(caches.open(SHELL).then((cache) => cache.addAll(ASSETS)).catch(() => {}));
  self.skipWaiting();
});

self.addEventListener("activate", (event) => {
  event.waitUntil(caches.keys().then((keys) => Promise.all(keys.filter((key) => key !== SHELL).map((key) => caches.delete(key)))));
  self.clients.claim();
});

self.addEventListener("fetch", (event) => {
  const url = new URL(event.request.url);
  if (url.pathname.startsWith("/api/") || url.hostname === "tile.openstreetmap.org") return;
  event.respondWith(fetch(event.request).then((response) => {
    const copy = response.clone();
    caches.open(SHELL).then((cache) => cache.put(event.request, copy));
    return response;
  }).catch(() => caches.match(event.request).then((response) => response || caches.match("/index.html"))));
});
