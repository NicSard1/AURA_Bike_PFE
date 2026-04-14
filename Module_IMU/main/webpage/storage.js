/* =========================================================
   AURA Bike - storage.js
   Gestion du stockage local :
   - profil utilisateur
   - trajets
   - routes
   - paramètres
   - statistiques globales
   Compatible avec app.js / route.js / kml_gpx.js
   ========================================================= */

/* =========================
   Clés localStorage
   ========================= */
const AURA_STORAGE_KEYS = {
  PROFILE: "aura_profile_v1",
  TRIPS: "aura_trips_v1",
  ROUTES: "aura_routes_v1",
  SETTINGS: "aura_settings_v1",
  LAST_ROUTE: "aura_last_route_v1",
  SESSION: "aura_session_v1"
};

/* =========================
   Valeurs par défaut
   ========================= */
const AURA_STORAGE_DEFAULTS = {
  profile: {
    userId: null,
    name: "",
    email: "",
    phone: "",
    address: "",
    createdAt: 0,
    updatedAt: 0,
    totals: {
      distanceKm: 0,
      durationSec: 0,
      avgSpeedKmh: 0,
      maxSpeedKmh: 0
    }
  },

  settings: {
    autosync: false,
    units: "metric",
    theme: "dark",
    privacyMode: false
  },

  session: {
    bleLastDeviceName: null,
    bleLastSeenAt: null,
    lastSyncAt: null
  }
};

/* =========================
   Helpers généraux
   ========================= */
function auraStorageClone(obj) {
  return JSON.parse(JSON.stringify(obj));
}

function auraStorageNow() {
  return Date.now();
}

function auraStorageUuid() {
  if (window.crypto && crypto.randomUUID) return crypto.randomUUID();
  return "aura-" + Math.random().toString(16).slice(2) + "-" + Date.now();
}

function auraStorageSafeParse(raw, fallback) {
  try {
    return JSON.parse(raw);
  } catch {
    return fallback;
  }
}

function auraStorageRead(key, fallback) {
  try {
    const raw = localStorage.getItem(key);
    if (raw === null || raw === undefined) return fallback;
    return auraStorageSafeParse(raw, fallback);
  } catch {
    return fallback;
  }
}

function auraStorageWrite(key, value) {
  localStorage.setItem(key, JSON.stringify(value));
  return value;
}

function auraStorageRemove(key) {
  localStorage.removeItem(key);
}

function auraStorageArray(value) {
  return Array.isArray(value) ? value : [];
}

/* =========================
   Profil utilisateur
   ========================= */
function auraStorageMakeDefaultProfile() {
  const now = auraStorageNow();
  const p = auraStorageClone(AURA_STORAGE_DEFAULTS.profile);
  p.userId = auraStorageUuid();
  p.createdAt = now;
  p.updatedAt = now;
  return p;
}

function auraStorageGetProfile() {
  let profile = auraStorageRead(AURA_STORAGE_KEYS.PROFILE, null);

  if (!profile || typeof profile !== "object") {
    profile = auraStorageMakeDefaultProfile();
    auraStorageWrite(AURA_STORAGE_KEYS.PROFILE, profile);
    return profile;
  }

  // sécurisation des champs
  profile.userId = profile.userId || auraStorageUuid();
  profile.name = profile.name || "";
  profile.email = profile.email || "";
  profile.phone = profile.phone || "";
  profile.address = profile.address || "";
  profile.createdAt = Number(profile.createdAt || auraStorageNow());
  profile.updatedAt = Number(profile.updatedAt || auraStorageNow());

  if (!profile.totals || typeof profile.totals !== "object") {
    profile.totals = auraStorageClone(AURA_STORAGE_DEFAULTS.profile.totals);
  }

  profile.totals.distanceKm = Number(profile.totals.distanceKm || 0);
  profile.totals.durationSec = Number(profile.totals.durationSec || 0);
  profile.totals.avgSpeedKmh = Number(profile.totals.avgSpeedKmh || 0);
  profile.totals.maxSpeedKmh = Number(profile.totals.maxSpeedKmh || 0);

  auraStorageWrite(AURA_STORAGE_KEYS.PROFILE, profile);
  return profile;
}

function auraStorageSaveProfile(profile) {
  const current = auraStorageGetProfile();
  const merged = {
    ...current,
    ...(profile || {}),
    totals: {
      ...current.totals,
      ...((profile && profile.totals) || {})
    },
    updatedAt: auraStorageNow()
  };

  auraStorageWrite(AURA_STORAGE_KEYS.PROFILE, merged);
  return merged;
}

function auraStorageUpdateProfileFields(fields = {}) {
  const profile = auraStorageGetProfile();

  const updated = {
    ...profile,
    name: fields.name !== undefined ? String(fields.name || "").trim() : profile.name,
    email: fields.email !== undefined ? String(fields.email || "").trim() : profile.email,
    phone: fields.phone !== undefined ? String(fields.phone || "").trim() : profile.phone,
    address: fields.address !== undefined ? String(fields.address || "").trim() : profile.address,
    updatedAt: auraStorageNow()
  };

  auraStorageWrite(AURA_STORAGE_KEYS.PROFILE, updated);
  return updated;
}

function auraStorageResetProfile() {
  const profile = auraStorageMakeDefaultProfile();
  auraStorageWrite(AURA_STORAGE_KEYS.PROFILE, profile);
  return profile;
}

/* =========================
   Paramètres
   ========================= */
function auraStorageGetSettings() {
  const settings = auraStorageRead(
    AURA_STORAGE_KEYS.SETTINGS,
    auraStorageClone(AURA_STORAGE_DEFAULTS.settings)
  );

  const merged = {
    ...auraStorageClone(AURA_STORAGE_DEFAULTS.settings),
    ...(settings || {})
  };

  auraStorageWrite(AURA_STORAGE_KEYS.SETTINGS, merged);
  return merged;
}

function auraStorageSaveSettings(settings = {}) {
  const current = auraStorageGetSettings();
  const merged = {
    ...current,
    ...(settings || {})
  };
  auraStorageWrite(AURA_STORAGE_KEYS.SETTINGS, merged);
  return merged;
}

function auraStorageToggleAutosync() {
  const s = auraStorageGetSettings();
  s.autosync = !s.autosync;
  auraStorageWrite(AURA_STORAGE_KEYS.SETTINGS, s);
  return s;
}

/* =========================
   Session
   ========================= */
function auraStorageGetSession() {
  const session = auraStorageRead(
    AURA_STORAGE_KEYS.SESSION,
    auraStorageClone(AURA_STORAGE_DEFAULTS.session)
  );

  const merged = {
    ...auraStorageClone(AURA_STORAGE_DEFAULTS.session),
    ...(session || {})
  };

  auraStorageWrite(AURA_STORAGE_KEYS.SESSION, merged);
  return merged;
}

function auraStorageSaveSession(session = {}) {
  const current = auraStorageGetSession();
  const merged = {
    ...current,
    ...(session || {})
  };
  auraStorageWrite(AURA_STORAGE_KEYS.SESSION, merged);
  return merged;
}

/* =========================
   Routes
   ========================= */
function auraStorageGetRoutes() {
  return auraStorageArray(auraStorageRead(AURA_STORAGE_KEYS.ROUTES, []));
}

function auraStorageSaveRoutes(routes) {
  const safeRoutes = auraStorageArray(routes);
  auraStorageWrite(AURA_STORAGE_KEYS.ROUTES, safeRoutes);
  return safeRoutes;
}

function auraStorageAddRoute(route) {
  if (!route || typeof route !== "object") return null;

  const routes = auraStorageGetRoutes();

  const newRoute = {
    id: route.id || auraStorageUuid(),
    name: route.name || "Route AURA",
    createdAt: Number(route.createdAt || auraStorageNow()),
    updatedAt: Number(route.updatedAt || auraStorageNow()),
    source: route.source || "APP",
    fileName: route.fileName || null,
    routeType: route.routeType || "navigation",
    points: Array.isArray(route.points) ? route.points : [],
    pointsCount: Number(route.pointsCount || (Array.isArray(route.points) ? route.points.length : 0)),
    distanceKmEstimate: Number(route.distanceKmEstimate || 0),
    gpxText: typeof route.gpxText === "string" ? route.gpxText : null,
    metadata: route.metadata || {}
  };

  routes.push(newRoute);
  auraStorageSaveRoutes(routes);
  return newRoute;
}

function auraStorageUpdateRoute(routeId, patch = {}) {
  const routes = auraStorageGetRoutes();
  const idx = routes.findIndex(r => r.id === routeId);
  if (idx < 0) return null;

  routes[idx] = {
    ...routes[idx],
    ...(patch || {}),
    updatedAt: auraStorageNow()
  };

  auraStorageSaveRoutes(routes);
  return routes[idx];
}

function auraStorageDeleteRoute(routeId) {
  const routes = auraStorageGetRoutes();
  const next = routes.filter(r => r.id !== routeId);
  auraStorageSaveRoutes(next);
  return next;
}

function auraStorageGetRouteById(routeId) {
  return auraStorageGetRoutes().find(r => r.id === routeId) || null;
}

/* =========================
   Last route snapshot
   ========================= */
function auraStorageSaveLastRoute(route) {
  if (!route || typeof route !== "object") return null;

  const snapshot = {
    id: route.id || route.routeId || null,
    routeId: route.routeId || route.id || null,
    name: route.name || "Route AURA",
    pointsCount: Number(route.pointsCount || 0),
    distKm: Number(route.distanceKmEstimate || route.distKm || 0),
    loadedAt: Number(route.loadedAt || auraStorageNow())
  };

  auraStorageWrite(AURA_STORAGE_KEYS.LAST_ROUTE, snapshot);
  return snapshot;
}

function auraStorageGetLastRoute() {
  return auraStorageRead(AURA_STORAGE_KEYS.LAST_ROUTE, null);
}

function auraStorageClearLastRoute() {
  auraStorageRemove(AURA_STORAGE_KEYS.LAST_ROUTE);
}

/* =========================
   Trajets
   ========================= */
function auraStorageGetTrips() {
  return auraStorageArray(auraStorageRead(AURA_STORAGE_KEYS.TRIPS, []));
}

function auraStorageSaveTrips(trips) {
  const safeTrips = auraStorageArray(trips);
  auraStorageWrite(AURA_STORAGE_KEYS.TRIPS, safeTrips);
  auraStorageRefreshProfileTotals();
  return safeTrips;
}

function auraStorageAddTrip(trip) {
  if (!trip || typeof trip !== "object") return null;

  const trips = auraStorageGetTrips();

  const newTrip = {
    id: trip.id || auraStorageUuid(),
    name: trip.name || "Trajet AURA",
    startedAt: Number(trip.startedAt || auraStorageNow()),
    endedAt: Number(trip.endedAt || auraStorageNow()),
    durationSec: Number(trip.durationSec || 0),
    distanceKm: Number(trip.distanceKm || 0),
    avgSpeedKmh: Number(trip.avgSpeedKmh || 0),
    maxSpeedKmh: Number(trip.maxSpeedKmh || 0),
    source: trip.source || "BLE",
    routeId: trip.routeId || null,
    gpsPoints: Array.isArray(trip.gpsPoints) ? trip.gpsPoints : [],
    speedSamples: Array.isArray(trip.speedSamples) ? trip.speedSamples : [],
    createdAt: Number(trip.createdAt || auraStorageNow()),
    metadata: trip.metadata || {}
  };

  trips.push(newTrip);
  auraStorageSaveTrips(trips);
  return newTrip;
}

function auraStorageUpdateTrip(tripId, patch = {}) {
  const trips = auraStorageGetTrips();
  const idx = trips.findIndex(t => t.id === tripId);
  if (idx < 0) return null;

  trips[idx] = {
    ...trips[idx],
    ...(patch || {})
  };

  auraStorageSaveTrips(trips);
  return trips[idx];
}

function auraStorageDeleteTrip(tripId) {
  const trips = auraStorageGetTrips();
  const next = trips.filter(t => t.id !== tripId);
  auraStorageSaveTrips(next);
  return next;
}

function auraStorageClearTrips() {
  auraStorageWrite(AURA_STORAGE_KEYS.TRIPS, []);
  auraStorageRefreshProfileTotals();
  return [];
}

function auraStorageGetTripById(tripId) {
  return auraStorageGetTrips().find(t => t.id === tripId) || null;
}

/* =========================
   Stats / Totaux
   ========================= */
function auraStorageComputeTripsTotals(trips) {
  const list = auraStorageArray(trips);

  let totalDistanceKm = 0;
  let totalDurationSec = 0;
  let maxSpeedKmh = 0;

  for (const trip of list) {
    totalDistanceKm += Number(trip.distanceKm || 0);
    totalDurationSec += Number(trip.durationSec || 0);
    maxSpeedKmh = Math.max(maxSpeedKmh, Number(trip.maxSpeedKmh || 0));
  }

  const avgSpeedKmh = totalDurationSec > 0
    ? totalDistanceKm / (totalDurationSec / 3600)
    : 0;

  return {
    count: list.length,
    distanceKm: totalDistanceKm,
    durationSec: totalDurationSec,
    avgSpeedKmh,
    maxSpeedKmh
  };
}

function auraStorageRefreshProfileTotals() {
  const trips = auraStorageGetTrips();
  const totals = auraStorageComputeTripsTotals(trips);
  const profile = auraStorageGetProfile();

  profile.totals.distanceKm = totals.distanceKm;
  profile.totals.durationSec = totals.durationSec;
  profile.totals.avgSpeedKmh = totals.avgSpeedKmh;
  profile.totals.maxSpeedKmh = totals.maxSpeedKmh;
  profile.updatedAt = auraStorageNow();

  auraStorageWrite(AURA_STORAGE_KEYS.PROFILE, profile);
  return profile;
}

/* =========================
   Import / Export
   ========================= */
function auraStorageExportAll() {
  return {
    profile: auraStorageGetProfile(),
    trips: auraStorageGetTrips(),
    routes: auraStorageGetRoutes(),
    settings: auraStorageGetSettings(),
    lastRoute: auraStorageGetLastRoute(),
    session: auraStorageGetSession(),
    exportedAt: auraStorageNow()
  };
}

function auraStorageImportAll(data) {
  if (!data || typeof data !== "object") return false;

  if (data.profile) auraStorageWrite(AURA_STORAGE_KEYS.PROFILE, data.profile);
  if (Array.isArray(data.trips)) auraStorageWrite(AURA_STORAGE_KEYS.TRIPS, data.trips);
  if (Array.isArray(data.routes)) auraStorageWrite(AURA_STORAGE_KEYS.ROUTES, data.routes);
  if (data.settings) auraStorageWrite(AURA_STORAGE_KEYS.SETTINGS, data.settings);
  if (data.lastRoute) auraStorageWrite(AURA_STORAGE_KEYS.LAST_ROUTE, data.lastRoute);
  if (data.session) auraStorageWrite(AURA_STORAGE_KEYS.SESSION, data.session);

  auraStorageRefreshProfileTotals();
  return true;
}

function auraStorageDownloadJson(fileName = "aura_storage_export.json") {
  const data = auraStorageExportAll();
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

/* =========================
   Reset global
   ========================= */
function auraStorageResetAll() {
  auraStorageRemove(AURA_STORAGE_KEYS.PROFILE);
  auraStorageRemove(AURA_STORAGE_KEYS.TRIPS);
  auraStorageRemove(AURA_STORAGE_KEYS.ROUTES);
  auraStorageRemove(AURA_STORAGE_KEYS.SETTINGS);
  auraStorageRemove(AURA_STORAGE_KEYS.LAST_ROUTE);
  auraStorageRemove(AURA_STORAGE_KEYS.SESSION);

  // recrée structure minimale
  auraStorageGetProfile();
  auraStorageGetSettings();
  auraStorageGetSession();
  auraStorageWrite(AURA_STORAGE_KEYS.TRIPS, []);
  auraStorageWrite(AURA_STORAGE_KEYS.ROUTES, []);

  return true;
}

/* =========================
   Compatibilité avec route.js
   ========================= */
function auraStorageAddTripFromRouteModule(tripData) {
  if (window.AURARoute && typeof window.AURARoute.createTrip === "function") {
    const trip = window.AURARoute.createTrip(tripData || {});
    return auraStorageAddTrip(trip);
  }
  return auraStorageAddTrip(tripData || {});
}

function auraStorageAddRouteFromRouteModule(routeData) {
  if (window.AURARoute && typeof window.AURARoute.createRoute === "function") {
    const route = window.AURARoute.createRoute(routeData || {});
    return auraStorageAddRoute(route);
  }
  return auraStorageAddRoute(routeData || {});
}

/* =========================
   Initialisation
   ========================= */
function auraStorageInit() {
  auraStorageGetProfile();
  auraStorageGetSettings();
  auraStorageGetSession();

  if (!localStorage.getItem(AURA_STORAGE_KEYS.TRIPS)) {
    auraStorageWrite(AURA_STORAGE_KEYS.TRIPS, []);
  }

  if (!localStorage.getItem(AURA_STORAGE_KEYS.ROUTES)) {
    auraStorageWrite(AURA_STORAGE_KEYS.ROUTES, []);
  }

  auraStorageRefreshProfileTotals();
  return true;
}

auraStorageInit();

/* =========================
   Exposition globale
   ========================= */
window.AURAStorage = {
  keys: AURA_STORAGE_KEYS,

  init: auraStorageInit,

  getProfile: auraStorageGetProfile,
  saveProfile: auraStorageSaveProfile,
  updateProfileFields: auraStorageUpdateProfileFields,
  resetProfile: auraStorageResetProfile,

  getSettings: auraStorageGetSettings,
  saveSettings: auraStorageSaveSettings,
  toggleAutosync: auraStorageToggleAutosync,

  getSession: auraStorageGetSession,
  saveSession: auraStorageSaveSession,

  getRoutes: auraStorageGetRoutes,
  saveRoutes: auraStorageSaveRoutes,
  addRoute: auraStorageAddRoute,
  addRouteFromRouteModule: auraStorageAddRouteFromRouteModule,
  updateRoute: auraStorageUpdateRoute,
  deleteRoute: auraStorageDeleteRoute,
  getRouteById: auraStorageGetRouteById,

  saveLastRoute: auraStorageSaveLastRoute,
  getLastRoute: auraStorageGetLastRoute,
  clearLastRoute: auraStorageClearLastRoute,

  getTrips: auraStorageGetTrips,
  saveTrips: auraStorageSaveTrips,
  addTrip: auraStorageAddTrip,
  addTripFromRouteModule: auraStorageAddTripFromRouteModule,
  updateTrip: auraStorageUpdateTrip,
  deleteTrip: auraStorageDeleteTrip,
  clearTrips: auraStorageClearTrips,
  getTripById: auraStorageGetTripById,

  computeTripsTotals: auraStorageComputeTripsTotals,
  refreshProfileTotals: auraStorageRefreshProfileTotals,

  exportAll: auraStorageExportAll,
  importAll: auraStorageImportAll,
  downloadJson: auraStorageDownloadJson,

  resetAll: auraStorageResetAll
};