/* =========================================================
   AURA Bike - route.js
   Gestion des routes / trajets :
   - création d'objet route
   - création d'objet trajet
   - calcul distance GPS
   - calcul vitesse moyenne / max
   - stats cumulées
   - export helpers
   ========================================================= */

/* =========================
   Config
   ========================= */
const AURA_ROUTE = {
  EARTH_RADIUS_KM: 6371.0088
};

/* =========================
   Helpers généraux
   ========================= */
function auraRouteUuid() {
  if (window.crypto && crypto.randomUUID) return crypto.randomUUID();
  return "route-" + Math.random().toString(16).slice(2) + "-" + Date.now();
}

function auraRouteToRad(deg) {
  return deg * Math.PI / 180;
}

function auraRouteClamp(v, a, b) {
  return Math.max(a, Math.min(b, v));
}

function auraRouteIsFiniteNumber(v) {
  return typeof v === "number" && Number.isFinite(v);
}

function auraRouteNowIso() {
  return new Date().toISOString();
}

/* =========================
   Validation point GPS
   Format attendu :
   { lat: Number, lon: Number, ele?: Number, ts?: Number|string }
   ========================= */
function auraRouteNormalizePoint(p) {
  if (!p) return null;

  const lat = Number(p.lat);
  const lon = Number(p.lon);
  const ele = p.ele !== undefined ? Number(p.ele) : undefined;

  if (!Number.isFinite(lat) || !Number.isFinite(lon)) return null;
  if (lat < -90 || lat > 90 || lon < -180 || lon > 180) return null;

  return {
    lat,
    lon,
    ele: Number.isFinite(ele) ? ele : undefined,
    ts: p.ts !== undefined ? p.ts : undefined
  };
}

function auraRouteNormalizePoints(points) {
  if (!Array.isArray(points)) return [];
  return points.map(auraRouteNormalizePoint).filter(Boolean);
}

/* =========================
   Distance Haversine
   ========================= */
function auraRouteDistanceKmBetween(p1, p2) {
  if (!p1 || !p2) return 0;

  const dLat = auraRouteToRad(p2.lat - p1.lat);
  const dLon = auraRouteToRad(p2.lon - p1.lon);

  const a =
    Math.sin(dLat / 2) * Math.sin(dLat / 2) +
    Math.cos(auraRouteToRad(p1.lat)) *
    Math.cos(auraRouteToRad(p2.lat)) *
    Math.sin(dLon / 2) * Math.sin(dLon / 2);

  const c = 2 * Math.atan2(Math.sqrt(a), Math.sqrt(1 - a));
  return AURA_ROUTE.EARTH_RADIUS_KM * c;
}

function auraRouteComputeDistanceKm(points) {
  const pts = auraRouteNormalizePoints(points);
  if (pts.length < 2) return 0;

  let total = 0;
  for (let i = 1; i < pts.length; i++) {
    total += auraRouteDistanceKmBetween(pts[i - 1], pts[i]);
  }
  return total;
}

/* =========================
   Temps / vitesse
   ========================= */
function auraRouteDurationSec(startedAt, endedAt) {
  const a = Number(startedAt);
  const b = Number(endedAt);
  if (!Number.isFinite(a) || !Number.isFinite(b) || b < a) return 0;
  return Math.round((b - a) / 1000);
}

function auraRouteAvgSpeedKmh(distanceKm, durationSec) {
  const d = Number(distanceKm);
  const t = Number(durationSec);
  if (!Number.isFinite(d) || !Number.isFinite(t) || t <= 0) return 0;
  return d / (t / 3600);
}

/**
 * Calcul vitesse max à partir d'échantillons live
 * samples format:
 * [
 *   { speedKmh: 21.3 },
 *   { speedKmh: 28.1 }
 * ]
 */
function auraRouteMaxSpeedFromSamples(samples) {
  if (!Array.isArray(samples) || samples.length === 0) return 0;
  let vmax = 0;
  for (const s of samples) {
    const v = Number(s && s.speedKmh);
    if (Number.isFinite(v)) vmax = Math.max(vmax, v);
  }
  return vmax;
}

/**
 * Calcul vitesse max à partir de points timestampés
 * points format:
 * [{lat, lon, ts}, ...]
 */
function auraRouteMaxSpeedFromPoints(points) {
  const pts = auraRouteNormalizePoints(points);
  if (pts.length < 2) return 0;

  let vmax = 0;

  for (let i = 1; i < pts.length; i++) {
    const p1 = pts[i - 1];
    const p2 = pts[i];

    const t1 = Number(p1.ts);
    const t2 = Number(p2.ts);

    if (!Number.isFinite(t1) || !Number.isFinite(t2) || t2 <= t1) continue;

    const dtSec = (t2 - t1) / 1000;
    if (dtSec <= 0) continue;

    const dKm = auraRouteDistanceKmBetween(p1, p2);
    const vKmh = dKm / (dtSec / 3600);

    if (Number.isFinite(vKmh)) vmax = Math.max(vmax, vKmh);
  }

  return vmax;
}

/* =========================
   Création objet ROUTE
   Une route = destination/navigation
   ========================= */
function auraRouteCreate(data = {}) {
  const points = auraRouteNormalizePoints(data.points || []);

  return {
    id: data.id || auraRouteUuid(),
    name: data.name || "Route AURA",
    createdAt: data.createdAt || Date.now(),
    updatedAt: data.updatedAt || Date.now(),
    source: data.source || "APP",
    fileName: data.fileName || null,
    routeType: data.routeType || "navigation",
    points,
    pointsCount: points.length,
    distanceKmEstimate: auraRouteIsFiniteNumber(data.distanceKmEstimate)
      ? Number(data.distanceKmEstimate)
      : auraRouteComputeDistanceKm(points),
    gpxText: typeof data.gpxText === "string" ? data.gpxText : null,
    metadata: data.metadata || {}
  };
}

function auraRouteUpdate(route, patch = {}) {
  const merged = { ...(route || {}), ...(patch || {}) };
  merged.updatedAt = Date.now();

  if (patch.points) {
    merged.points = auraRouteNormalizePoints(patch.points);
    merged.pointsCount = merged.points.length;
    merged.distanceKmEstimate = auraRouteComputeDistanceKm(merged.points);
  }

  return merged;
}

/* =========================
   Création objet TRAJET
   Un trajet = historique réel roulé
   ========================= */
function auraTripCreate(data = {}) {
  const startedAt = Number(data.startedAt || Date.now());
  const endedAt = Number(data.endedAt || startedAt);
  const durationSec = auraRouteIsFiniteNumber(data.durationSec)
    ? Number(data.durationSec)
    : auraRouteDurationSec(startedAt, endedAt);

  const gpsPoints = auraRouteNormalizePoints(data.gpsPoints || []);
  const distanceKm = auraRouteIsFiniteNumber(data.distanceKm)
    ? Number(data.distanceKm)
    : auraRouteComputeDistanceKm(gpsPoints);

  let maxSpeedKmh = 0;
  if (auraRouteIsFiniteNumber(data.maxSpeedKmh)) {
    maxSpeedKmh = Number(data.maxSpeedKmh);
  } else if (Array.isArray(data.speedSamples) && data.speedSamples.length > 0) {
    maxSpeedKmh = auraRouteMaxSpeedFromSamples(data.speedSamples);
  } else {
    maxSpeedKmh = auraRouteMaxSpeedFromPoints(gpsPoints);
  }

  const avgSpeedKmh = auraRouteIsFiniteNumber(data.avgSpeedKmh)
    ? Number(data.avgSpeedKmh)
    : auraRouteAvgSpeedKmh(distanceKm, durationSec);

  return {
    id: data.id || auraRouteUuid(),
    name: data.name || "Trajet AURA",
    startedAt,
    endedAt,
    durationSec,
    distanceKm,
    avgSpeedKmh,
    maxSpeedKmh,
    source: data.source || "BLE",
    routeId: data.routeId || null,
    gpsPoints,
    speedSamples: Array.isArray(data.speedSamples) ? data.speedSamples : [],
    createdAt: data.createdAt || Date.now(),
    metadata: data.metadata || {}
  };
}

/* =========================
   Stats globales sur trajets
   ========================= */
function auraTripsComputeTotals(trips) {
  const list = Array.isArray(trips) ? trips : [];

  let totalDistanceKm = 0;
  let totalDurationSec = 0;
  let maxSpeedKmh = 0;

  for (const tr of list) {
    totalDistanceKm += Number(tr.distanceKm || 0);
    totalDurationSec += Number(tr.durationSec || 0);
    maxSpeedKmh = Math.max(maxSpeedKmh, Number(tr.maxSpeedKmh || 0));
  }

  const avgSpeedKmh = auraRouteAvgSpeedKmh(totalDistanceKm, totalDurationSec);

  return {
    count: list.length,
    totalDistanceKm,
    totalDurationSec,
    avgSpeedKmh,
    maxSpeedKmh
  };
}

/* =========================
   Helpers UI / affichage
   ========================= */
function auraRouteFormatDuration(sec) {
  sec = Math.max(0, Math.floor(Number(sec || 0)));
  const h = Math.floor(sec / 3600);
  const m = Math.floor((sec % 3600) / 60);
  const s = sec % 60;

  if (h > 0) return `${h}h ${String(m).padStart(2, "0")}m ${String(s).padStart(2, "0")}s`;
  return `${m}m ${String(s).padStart(2, "0")}s`;
}

function auraRouteFormatDistance(km) {
  const v = Number(km || 0);
  return `${v.toFixed(2)} km`;
}

function auraRouteFormatSpeed(kmh) {
  const v = Number(kmh || 0);
  return `${v.toFixed(1)} km/h`;
}

/* =========================
   Export
   ========================= */
function auraRouteExportJson(data, fileName = "aura_export.json") {
  const blob = new Blob([JSON.stringify(data, null, 2)], {
    type: "application/json"
  });

  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = fileName;
  a.click();
  URL.revokeObjectURL(url);
}

function auraTripToCsvRow(trip) {
  const cols = [
    trip.id,
    trip.name,
    trip.startedAt,
    trip.endedAt,
    trip.durationSec,
    trip.distanceKm,
    trip.avgSpeedKmh,
    trip.maxSpeedKmh,
    trip.source,
    trip.routeId
  ];

  return cols
    .map(v => `"${String(v ?? "").replaceAll('"', '""')}"`)
    .join(",");
}

function auraTripsExportCsv(trips, fileName = "aura_trips.csv") {
  const list = Array.isArray(trips) ? trips : [];
  const header = [
    "id",
    "name",
    "startedAt",
    "endedAt",
    "durationSec",
    "distanceKm",
    "avgSpeedKmh",
    "maxSpeedKmh",
    "source",
    "routeId"
  ].join(",");

  const rows = list.map(auraTripToCsvRow);
  const csv = [header, ...rows].join("\n");

  const blob = new Blob([csv], { type: "text/csv;charset=utf-8;" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = fileName;
  a.click();
  URL.revokeObjectURL(url);
}

/* =========================
   Intégration simple avec localStorage
   (optionnelle, compatible avec app.js)
   ========================= */
function auraTripsLoadFromStorage(storageKey = "aura_trips_v1") {
  try {
    const raw = localStorage.getItem(storageKey);
    if (!raw) return [];
    const data = JSON.parse(raw);
    return Array.isArray(data) ? data : [];
  } catch {
    return [];
  }
}

function auraTripsSaveToStorage(trips, storageKey = "aura_trips_v1") {
  localStorage.setItem(storageKey, JSON.stringify(Array.isArray(trips) ? trips : []));
}

function auraTripsAddToStorage(trip, storageKey = "aura_trips_v1") {
  const trips = auraTripsLoadFromStorage(storageKey);
  trips.push(trip);
  auraTripsSaveToStorage(trips, storageKey);
  return trips;
}

/* =========================
   Exposition globale
   ========================= */
window.AURARoute = {
  createRoute: auraRouteCreate,
  updateRoute: auraRouteUpdate,
  createTrip: auraTripCreate,
  computeDistanceKm: auraRouteComputeDistanceKm,
  distanceKmBetween: auraRouteDistanceKmBetween,
  durationSec: auraRouteDurationSec,
  avgSpeedKmh: auraRouteAvgSpeedKmh,
  maxSpeedFromSamples: auraRouteMaxSpeedFromSamples,
  maxSpeedFromPoints: auraRouteMaxSpeedFromPoints,
  computeTotals: auraTripsComputeTotals,
  formatDuration: auraRouteFormatDuration,
  formatDistance: auraRouteFormatDistance,
  formatSpeed: auraRouteFormatSpeed,
  exportJson: auraRouteExportJson,
  exportTripsCsv: auraTripsExportCsv,
  loadTrips: auraTripsLoadFromStorage,
  saveTrips: auraTripsSaveToStorage,
  addTrip: auraTripsAddToStorage
};