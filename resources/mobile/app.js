const $ = (id) => document.getElementById(id);
const canvas = $("map");
const context = canvas.getContext("2d");

function stored(key) { try { return localStorage.getItem(key) || ""; } catch { return ""; } }
function store(key, value) { try { value ? localStorage.setItem(key, value) : localStorage.removeItem(key); } catch { /* Private browsing may deny storage. */ } }

let sessionToken = stored("openefb-session");
let snapshot = null;
let routeDraft = null;
let draftRevision = 0;
let airportData = null;
let selectedApproach = null;
let excludedFixes = new Set();
let destinationEndpoint = true;
let libraryData = { entries: [], message: "" };
let libraryFilter = "all";
let refreshing = false;
let zoom = 10;
let center = null;
let following = true;
let dragStart = null;
let toastTimer = null;
const tileCache = new Map();

function toast(message, duration = 4200) {
  clearTimeout(toastTimer);
  $("toast").textContent = message;
  $("toast").hidden = false;
  toastTimer = setTimeout(() => { $("toast").hidden = true; }, duration);
}

function setConnection(live) {
  $("connection").classList.toggle("offline", !live);
  $("connection").innerHTML = `<span></span> ${live ? "LIVE" : "OFFLINE"}`;
}

function apiUrl(path, values = {}) {
  const url = new URL(path, location.origin);
  if (sessionToken) url.searchParams.set("token", sessionToken);
  Object.entries(values).forEach(([key, value]) => url.searchParams.set(key, value));
  return url;
}

async function request(path, options = {}, values = {}, timeoutMs = 6000) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), timeoutMs);
  try {
    return await fetch(apiUrl(path, values), { cache: "no-store", ...options, signal: controller.signal });
  } finally { clearTimeout(timeout); }
}

function disconnect(message = "") {
  sessionToken = "";
  store("openefb-session", "");
  $("app").hidden = true;
  $("bottom-nav").hidden = true;
  $("pairing").hidden = false;
  $("pair-error").textContent = message;
}

function world(lat, lon, z) {
  const size = 256 * 2 ** z;
  const sin = Math.sin(Math.max(-85.0511, Math.min(85.0511, lat)) * Math.PI / 180);
  return { x: (lon + 180) / 360 * size, y: (0.5 - Math.log((1 + sin) / (1 - sin)) / (4 * Math.PI)) * size };
}

function geographic(x, y, z) {
  const size = 256 * 2 ** z;
  const lon = x / size * 360 - 180;
  const n = Math.PI - 2 * Math.PI * y / size;
  return { lat: 180 / Math.PI * Math.atan(Math.sinh(n)), lon };
}

function tile(z, x, y) {
  const count = 2 ** z;
  const wrappedX = ((x % count) + count) % count;
  if (y < 0 || y >= count) return null;
  const key = `${z}/${wrappedX}/${y}`;
  if (!tileCache.has(key)) {
    const image = new Image();
    image.crossOrigin = "anonymous";
    image.src = `https://tile.openstreetmap.org/${z}/${wrappedX}/${y}.png`;
    image.onload = drawMap;
    tileCache.set(key, image);
    if (tileCache.size > 180) tileCache.delete(tileCache.keys().next().value);
  }
  return tileCache.get(key);
}

function screenPoint(lat, lon, origin) {
  const point = world(lat, lon, zoom);
  return { x: point.x - origin.x, y: point.y - origin.y };
}

function drawAircraft(x, y, heading, color, size = 15) {
  context.save(); context.translate(x, y); context.rotate((heading || 0) * Math.PI / 180);
  context.beginPath(); context.moveTo(0, -size); context.lineTo(4, -3); context.lineTo(size, 4);
  context.lineTo(size, 8); context.lineTo(3, 5); context.lineTo(3, 12); context.lineTo(7, 16);
  context.lineTo(7, 19); context.lineTo(0, 16); context.lineTo(-7, 19); context.lineTo(-7, 16);
  context.lineTo(-3, 12); context.lineTo(-3, 5); context.lineTo(-size, 8); context.lineTo(-size, 4);
  context.lineTo(-4, -3); context.closePath(); context.fillStyle = color; context.strokeStyle = "#041018";
  context.lineWidth = 2; context.stroke(); context.fill(); context.restore();
}

function drawMap() {
  const ratio = devicePixelRatio || 1;
  const bounds = canvas.getBoundingClientRect();
  const width = Math.max(1, Math.round(bounds.width * ratio));
  const height = Math.max(1, Math.round(bounds.height * ratio));
  if (canvas.width !== width || canvas.height !== height) { canvas.width = width; canvas.height = height; }
  context.setTransform(ratio, 0, 0, ratio, 0, 0);
  context.fillStyle = "#dbe7e2"; context.fillRect(0, 0, bounds.width, bounds.height);
  if (!center) return;
  const centerWorld = world(center.lat, center.lon, zoom);
  const origin = { x: centerWorld.x - bounds.width / 2, y: centerWorld.y - bounds.height / 2 };
  const firstX = Math.floor(origin.x / 256), firstY = Math.floor(origin.y / 256);
  const lastX = Math.floor((origin.x + bounds.width) / 256), lastY = Math.floor((origin.y + bounds.height) / 256);
  for (let y = firstY; y <= lastY; y++) for (let x = firstX; x <= lastX; x++) {
    const image = tile(zoom, x, y);
    if (image?.complete && image.naturalWidth) context.drawImage(image, x * 256 - origin.x, y * 256 - origin.y, 256, 256);
  }
  const route = (snapshot?.route?.legs || []).filter((leg) => Number.isFinite(leg.lat) && Number.isFinite(leg.lon) && Math.abs(leg.lat) <= 90 && Math.abs(leg.lon) <= 180 && (leg.lat !== 0 || leg.lon !== 0));
  if (route.length > 1) {
    context.beginPath(); route.forEach((leg, index) => { const p = screenPoint(leg.lat, leg.lon, origin); index ? context.lineTo(p.x, p.y) : context.moveTo(p.x, p.y); });
    context.strokeStyle = "#ff24d2"; context.lineWidth = 5; context.lineJoin = "round"; context.shadowColor = "#300024"; context.shadowBlur = 3; context.stroke(); context.shadowBlur = 0;
  }
  for (const target of snapshot?.traffic || []) {
    const p = screenPoint(target.lat, target.lon, origin);
    if (p.x > -20 && p.y > -20 && p.x < bounds.width + 20 && p.y < bounds.height + 20) drawAircraft(p.x, p.y, target.headingDeg, "#ffd43b", 9);
  }
  const ownship = snapshot?.telemetry;
  if (ownship?.available) { const p = screenPoint(ownship.lat, ownship.lon, origin); drawAircraft(p.x, p.y, ownship.headingDeg, "#27d9ff", 15); }
  context.fillStyle = "rgba(3,12,18,.82)"; context.fillRect(10, 10, 98, 27);
  context.fillStyle = "#ecf8fb"; context.font = "700 12px system-ui"; context.fillText(`ZOOM ${zoom}`, 21, 28);
}

function showSnapshot(data) {
  snapshot = data;
  const t = data.telemetry;
  if (t.available && (following || !center)) center = { lat: t.lat, lon: t.lon };
  $("aircraft").textContent = t.aircraft || "Unknown aircraft";
  $("position").textContent = t.available ? `${t.lat.toFixed(4)}, ${t.lon.toFixed(4)}` : "No live position";
  $("altitude").textContent = t.available ? Math.round(t.altitudeFt).toLocaleString() : "--";
  $("speed").textContent = t.available ? Math.round(t.groundSpeedKt) : "--";
  $("heading").textContent = t.available ? String(Math.round(t.headingDeg)).padStart(3, "0") : "--";
  $("vertical-speed").textContent = t.available ? Math.round(t.verticalSpeedFpm).toLocaleString() : "--";
  const legs = data.route.legs || [];
  const routeName = legs.length > 1 ? `${legs[0].id} to ${legs.at(-1).id}` : "No flight plan";
  $("route-title").textContent = routeName;
  $("plan-title").textContent = routeName;
  $("route-distance").textContent = data.progress.available ? `${data.progress.destinationDistanceNm.toFixed(1)} NM` : "-- NM";
  $("route").replaceChildren(...(legs.length ? legs.map((leg) => {
    const element = document.createElement("div"); element.className = `leg ${leg.active ? "active" : ""}`; element.textContent = leg.id || "FIX"; return element;
  }) : [Object.assign(document.createElement("span"), { className: "leg", textContent: "Waiting for route" })]));
  $("dep-weather-title").textContent = data.weather.departure.id || "--";
  $("dep-weather").textContent = data.weather.departure.metar || "No METAR available";
  $("dest-weather-title").textContent = data.weather.destination.id || "--";
  $("dest-weather").textContent = data.weather.destination.metar || "No METAR available";
  if (!routeDraft) renderRoute(legs, false);
  else {
    const conflict = data.route.revision !== draftRevision;
    $("plan-revision").textContent = conflict ? "X-Plane changed this route. Discard and reopen the draft before applying." : `Draft based on route revision ${draftRevision}`;
    $("plan-revision").style.color = conflict ? "var(--danger)" : "";
    $("apply-route").disabled = conflict;
  }
  drawMap();
}

function renderRoute(legs, editing) {
  const source = editing ? routeDraft : legs;
  $("draft-route").replaceChildren();
  source.forEach((leg, index) => {
    const row = document.createElement("div");
    const locked = index === 0 || index === source.length - 1;
    row.className = `draft-leg ${locked || !editing ? "locked" : ""}`;
    const number = document.createElement("span"); number.className = "index"; number.textContent = index + 1;
    const name = document.createElement("strong"); name.textContent = leg.id || "FIX";
    const up = document.createElement("button"); up.textContent = "↑"; up.disabled = index <= 1; up.onclick = () => moveDraft(index, -1);
    const down = document.createElement("button"); down.textContent = "↓"; down.disabled = index >= source.length - 2; down.onclick = () => moveDraft(index, 1);
    const remove = document.createElement("button"); remove.textContent = "×"; remove.className = "remove"; remove.onclick = () => { routeDraft.splice(index, 1); renderRoute(routeDraft, true); };
    row.append(number, name, up, down, remove); $("draft-route").append(row);
  });
  $("edit-route").hidden = editing;
  $("draft-add").hidden = !editing;
  $("draft-actions").hidden = !editing;
  if (!editing) $("plan-revision").textContent = snapshot?.route?.available ? `Live route revision ${snapshot.route.revision}` : "Waiting for X-Plane";
}

function moveDraft(index, delta) {
  const target = index + delta;
  if (!routeDraft || index <= 0 || index >= routeDraft.length - 1 || target <= 0 || target >= routeDraft.length - 1) return;
  [routeDraft[index], routeDraft[target]] = [routeDraft[target], routeDraft[index]];
  renderRoute(routeDraft, true);
}

async function refresh(pairingAttempt = false) {
  if (!sessionToken || refreshing) return false;
  refreshing = true;
  try {
    const response = await request("/api/v2/snapshot");
    if (response.status === 401) { disconnect("The pairing session expired. Enter the new code from OpenEFB."); return false; }
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = await response.json();
    if (!data.telemetry || !data.route) throw new Error("Incomplete simulator data");
    showSnapshot(data); setConnection(true);
    $("pairing").hidden = true; $("app").hidden = false; $("bottom-nav").hidden = false; $("pair-error").textContent = "";
    return true;
  } catch (error) {
    setConnection(false);
    if (pairingAttempt) $("pair-error").textContent = "The page loaded, but live data could not be reached. Keep X-Plane running and try again.";
    return false;
  } finally { refreshing = false; }
}

async function sendCommand(values) {
  const response = await request("/api/v2/commands", { method: "POST", headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" }, body: new URLSearchParams(values) }, {}, 8000);
  if (response.status === 401) { disconnect("The pairing session expired."); throw new Error("Pairing session expired"); }
  const accepted = await response.json().catch(() => ({}));
  if (!response.ok || !accepted.commandId) throw new Error(accepted.error || "OpenEFB did not accept the command");
  for (let attempt = 0; attempt < 30; attempt++) {
    await new Promise((resolve) => setTimeout(resolve, 200));
    const statusResponse = await request("/api/v2/command", {}, { id: accepted.commandId });
    const status = await statusResponse.json();
    if (status.done) { toast(status.message); if (!status.success) throw new Error(status.message); return status; }
  }
  throw new Error("X-Plane did not finish the command in time");
}

async function loadApproaches() {
  const airport = $("approach-airport").value.trim().toUpperCase();
  if (!/^[A-Z0-9]{2,7}$/.test(airport)) { toast("Enter a valid airport code"); return; }
  $("approach-status").textContent = `Loading ${airport} from installed X-Plane data...`;
  try {
    await sendCommand({ action: "search_airport", airport });
    for (let attempt = 0; attempt < 40; attempt++) {
      await new Promise((resolve) => setTimeout(resolve, 300));
      const response = await request("/api/v2/airport");
      airportData = await response.json();
      if (airportData.id === airport && ["ready", "not_found", "error"].includes(airportData.state)) break;
    }
    renderApproaches();
  } catch (error) { $("approach-status").textContent = error.message; }
}

function currentTransition() {
  if (!selectedApproach) return null;
  const id = $("transition-list").value;
  return selectedApproach.transitions.find((item) => item.id === id) || null;
}

function procedureLegs() {
  return [...(currentTransition()?.legs || []), ...(selectedApproach?.finalLegs || [])];
}

function renderApproachDetails() {
  $("approach-detail").hidden = !selectedApproach;
  if (!selectedApproach) return;
  $("transition-list").replaceChildren(new Option("Vectors / common legs", ""), ...selectedApproach.transitions.map((item) => new Option(item.id, item.id)));
  renderFixes();
}

function renderFixes() {
  $("approach-fixes").replaceChildren();
  procedureLegs().forEach((leg) => {
    const key = `${leg.id}#${leg.sequence}`;
    const button = document.createElement("button");
    button.textContent = `${leg.id}${leg.altitudeFt > 0 ? ` ${leg.altitudeFt} FT` : ""}`;
    button.classList.toggle("excluded", excludedFixes.has(key));
    button.onclick = () => { excludedFixes.has(key) ? excludedFixes.delete(key) : excludedFixes.add(key); renderFixes(); };
    $("approach-fixes").append(button);
  });
}

function renderApproaches() {
  selectedApproach = null; excludedFixes = new Set(); $("approach-detail").hidden = true; $("approach-list").replaceChildren();
  if (!airportData || airportData.state !== "ready") { $("approach-status").textContent = airportData?.message || "Airport procedures were not found."; return; }
  $("approach-status").textContent = `${airportData.id} - ${airportData.name || "Airport"} - ${airportData.approaches.length} approaches`;
  airportData.approaches.forEach((approach) => {
    const button = document.createElement("button"); button.className = "choice";
    button.innerHTML = `<strong>${escapeHtml(approach.name || approach.id)}</strong><small>${escapeHtml(approach.id)}${approach.runway ? ` - Runway ${escapeHtml(approach.runway)}` : ""}</small>`;
    button.onclick = () => {
      selectedApproach = approach; excludedFixes = new Set();
      $("approach-list").querySelectorAll(".choice").forEach((item) => item.classList.toggle("selected", item === button));
      renderApproachDetails();
    };
    $("approach-list").append(button);
  });
}

async function refreshLibrary() {
  try {
    const response = await request("/api/v2/library");
    if (!response.ok) throw new Error("Library unavailable");
    libraryData = await response.json(); renderLibrary();
  } catch (error) { $("library-status").textContent = error.message; }
}

function renderLibrary() {
  $("library-status").textContent = libraryData.message || `${libraryData.entries.length} local files available`;
  $("library-list").replaceChildren();
  const entries = libraryData.entries.filter((entry) => libraryFilter === "all" || (libraryFilter === "chart" ? entry.category === "chart" : entry.category === "document"));
  entries.forEach((entry) => {
    const button = document.createElement("button"); button.className = "library-entry";
    button.innerHTML = `<span class="doc-icon">${entry.category === "chart" ? "CHART" : "PDF"}</span><strong>${escapeHtml(entry.name)}</strong><small>Open</small>`;
    button.onclick = () => {
      $("document-title").textContent = entry.name;
      $("document-frame").src = apiUrl("/api/v2/library/file", { id: entry.id });
      $("document-viewer").showModal();
    };
    $("library-list").append(button);
  });
  if (!entries.length) { const empty = document.createElement("p"); empty.className = "helper"; empty.textContent = "No matching files are available yet."; $("library-list").append(empty); }
}

function escapeHtml(value) { const element = document.createElement("span"); element.textContent = value ?? ""; return element.innerHTML; }

const pageTitles = { map: "Moving Map", plan: "Flight Plan", weather: "Weather", briefing: "Briefing", settings: "Settings" };
function showPage(page) {
  document.querySelectorAll(".page").forEach((item) => item.classList.toggle("active", item.id === `page-${page}`));
  document.querySelectorAll(".bottom-nav button").forEach((item) => item.classList.toggle("selected", item.dataset.page === page));
  $("page-title").textContent = pageTitles[page];
  if (page === "map") requestAnimationFrame(drawMap);
  if (page === "briefing") refreshLibrary();
  if (page === "plan" && snapshot?.route?.legs?.length) {
    const legs = snapshot.route.legs; $("approach-airport").value = destinationEndpoint ? legs.at(-1).id : legs[0].id;
  }
}

$("pair-form").addEventListener("submit", async (event) => {
  event.preventDefault();
  const code = $("pair-code").value.trim();
  if (!/^\d{6}$/.test(code)) { $("pair-error").textContent = "Enter all six digits."; return; }
  const button = $("connect-button"); button.disabled = true; button.textContent = "Connecting..."; $("pair-error").textContent = "";
  try {
    const response = await request("/api/v2/session", {}, { code });
    if (response.status === 401) throw new Error("That pairing code was not accepted.");
    if (!response.ok) throw new Error("OpenEFB pairing is unavailable.");
    const paired = await response.json();
    if (!paired.token) throw new Error("OpenEFB returned an invalid session.");
    sessionToken = paired.token; store("openefb-session", sessionToken);
    if (!(await refresh(true))) throw new Error($("pair-error").textContent || "Live data could not be reached.");
  } catch (error) { $("pair-error").textContent = error.message; sessionToken = ""; store("openefb-session", ""); }
  finally { button.disabled = false; button.textContent = "Connect to OpenEFB"; }
});

$("edit-route").onclick = () => {
  if (!snapshot?.route?.legs?.length) { toast("No active route is available"); return; }
  routeDraft = snapshot.route.legs.map((leg) => ({ ...leg })); draftRevision = snapshot.route.revision;
  $("plan-revision").style.color = ""; renderRoute(routeDraft, true);
};
$("discard-route").onclick = () => { routeDraft = null; renderRoute(snapshot?.route?.legs || [], false); };
$("add-waypoint").onclick = () => {
  if (!routeDraft || routeDraft.length < 2) return;
  const id = $("draft-waypoint").value.trim().toUpperCase();
  if (!/^[A-Z0-9]{1,16}$/.test(id)) { toast("Enter a valid waypoint identifier"); return; }
  const position = snapshot?.telemetry || {};
  routeDraft.splice(routeDraft.length - 1, 0, { id, kind: 5, altitudeFt: 0, lat: position.lat || 0, lon: position.lon || 0 });
  $("draft-waypoint").value = ""; renderRoute(routeDraft, true);
};
$("apply-route").onclick = async () => {
  if (!routeDraft) return;
  const route = routeDraft.map((leg) => [leg.id, leg.kind ?? 5, Math.round(leg.altitudeFt || 0), Number(leg.lat || 0).toFixed(7), Number(leg.lon || 0).toFixed(7)].join("\t")).join("\n");
  $("apply-route").disabled = true;
  try { await sendCommand({ action: "apply_route", revision: draftRevision, route }); routeDraft = null; await refresh(); }
  catch (error) { toast(error.message); }
  finally { $("apply-route").disabled = false; }
};
$("endpoint-dep").onclick = () => { destinationEndpoint = false; $("endpoint-dep").classList.add("selected"); $("endpoint-dest").classList.remove("selected"); if (snapshot?.route?.legs?.length) $("approach-airport").value = snapshot.route.legs[0].id; };
$("endpoint-dest").onclick = () => { destinationEndpoint = true; $("endpoint-dest").classList.add("selected"); $("endpoint-dep").classList.remove("selected"); if (snapshot?.route?.legs?.length) $("approach-airport").value = snapshot.route.legs.at(-1).id; };
$("load-approaches").onclick = loadApproaches;
$("transition-list").onchange = () => { excludedFixes = new Set(); renderFixes(); };
$("apply-approach").onclick = async () => {
  if (!selectedApproach || !snapshot) return;
  $("apply-approach").disabled = true;
  try {
    await sendCommand({ action: "apply_approach", revision: snapshot.route.revision, airport: airportData.id, approach: selectedApproach.id, transition: $("transition-list").value, destination: destinationEndpoint ? "1" : "0", excluded: [...excludedFixes].join(",") });
    await refresh();
  } catch (error) { toast(error.message); }
  finally { $("apply-approach").disabled = false; }
};
$("refresh-library").onclick = async () => { try { await sendCommand({ action: "refresh_library" }); setTimeout(refreshLibrary, 1200); } catch (error) { toast(error.message); } };
[["library-all", "all"], ["library-charts", "chart"], ["library-docs", "document"]].forEach(([id, filter]) => { $(id).onclick = () => { libraryFilter = filter; ["library-all", "library-charts", "library-docs"].forEach((item) => $(item).classList.toggle("selected", item === id)); renderLibrary(); }; });
$("close-document").onclick = () => { $("document-viewer").close(); $("document-frame").src = "about:blank"; };
$("disconnect").onclick = () => disconnect("This phone was disconnected. Enter the current pairing code to reconnect.");
document.querySelectorAll(".bottom-nav button").forEach((button) => { button.onclick = () => showPage(button.dataset.page); });
$("zoom-in").onclick = () => { zoom = Math.min(19, zoom + 1); drawMap(); };
$("zoom-out").onclick = () => { zoom = Math.max(3, zoom - 1); drawMap(); };
$("recenter").onclick = () => { following = true; if (snapshot?.telemetry?.available) center = { lat: snapshot.telemetry.lat, lon: snapshot.telemetry.lon }; drawMap(); };
canvas.addEventListener("pointerdown", (event) => { canvas.setPointerCapture(event.pointerId); dragStart = { x: event.clientX, y: event.clientY, center: world(center.lat, center.lon, zoom) }; });
canvas.addEventListener("pointermove", (event) => { if (!dragStart) return; following = false; center = geographic(dragStart.center.x - (event.clientX - dragStart.x), dragStart.center.y - (event.clientY - dragStart.y), zoom); drawMap(); });
canvas.addEventListener("pointerup", () => { dragStart = null; });
window.addEventListener("resize", drawMap);

if ("serviceWorker" in navigator) navigator.serviceWorker.register("/service-worker.js").catch(() => {});
const nativePairCode = new URL(location.href).searchParams.get("nativePairCode") || "";
if (/^\d{6}$/.test(nativePairCode) && !sessionToken) {
  $("pair-code").value = nativePairCode;
  history.replaceState({}, "", location.pathname);
  $("pair-form").requestSubmit();
} else if (sessionToken) refresh(true); else disconnect("");
setInterval(() => refresh(false), 1000);
