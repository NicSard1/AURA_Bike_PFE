/* =========================================================
   AURA Bike - app.js
   Version démo avec adresses enregistrées
   - navigation SPA
   - modal paramètres
   - stockage local
   - historique trajets démo
   - profil par défaut
   - adresses rapides : domicile / bureau / favoris
   ========================================================= */

const LS_KEYS = {
  PROFILE: "aura_profile_v1",
  TRIPS: "aura_trips_v1",
  SETTINGS: "aura_settings_v1",
  LAST_ROUTE: "aura_last_route_v1"
};

const DEFAULT_SETTINGS = {
  autosync: false
};

/* =========================
   Helpers
   ========================= */
function fmtTimeHMS(totalSec) {
  totalSec = Math.max(0, Math.floor(Number(totalSec || 0)));
  const h = Math.floor(totalSec / 3600);
  const m = Math.floor((totalSec % 3600) / 60);
  const s = totalSec % 60;

  if (h > 0) return `${h}h ${String(m).padStart(2, "0")}m`;
  return `${m}m ${String(s).padStart(2, "0")}s`;
}

function fmtDateTime(ts) {
  if (!ts) return "—";
  try {
    return new Date(ts).toLocaleString("fr-FR");
  } catch {
    return "—";
  }
}

function loadJson(key, fallback) {
  try {
    const raw = localStorage.getItem(key);
    if (!raw) return fallback;
    return JSON.parse(raw);
  } catch {
    return fallback;
  }
}

function saveJson(key, value) {
  localStorage.setItem(key, JSON.stringify(value));
}

function uid() {
  if (window.crypto && crypto.randomUUID) return crypto.randomUUID();
  return "local-" + Math.random().toString(16).slice(2) + "-" + Date.now();
}

/* =========================
   Profile / settings / trips
   ========================= */
function makeDefaultProfile() {
  return {
    userId: uid(),
    name: "Sandric Bretécher",
    email: "sandric.bretecher@esme.fr",
    phone: "+33 7 81 09 83 88",
    address: "4 rue des Vallées, 94160 Saint-Mandés",
    createdAt: Date.now(),
    savedPlaces: {
      home: {
        label: "Domicile",
        address: "4 rue des Vallées, 94160 Saint-Mandés"
      },
      work: {
        label: "Bureau",
        address: "20 Rue Bouvier, 75011 Paris"
      },
      favorites: [
        {
          id: uid(),
          label: "ESME",
          address: "38 Rue Molière, 94200 Ivry-sur-Seine"
        },
        {
          id: uid(),
          label: "Chez les parents",
          address: "43 rue du Rocher, 44430 Le Loroux-Bottereau"
        }
      ]
    },
    totals: {
      distanceKm: 842.5,
      durationSec: 132000,
      avgSpeedKmh: 21.4,
      maxSpeedKmh: 36.7
    }
  };
}

function normalizeProfile(profile) {
  const p = profile || makeDefaultProfile();

  if (!p.savedPlaces || typeof p.savedPlaces !== "object") {
    p.savedPlaces = {
      home: { label: "Domicile", address: "" },
      work: { label: "Bureau", address: "" },
      favorites: []
    };
  }

  if (!p.savedPlaces.home) {
    p.savedPlaces.home = { label: "Domicile", address: "" };
  }

  if (!p.savedPlaces.work) {
    p.savedPlaces.work = { label: "Bureau", address: "" };
  }

  if (!Array.isArray(p.savedPlaces.favorites)) {
    p.savedPlaces.favorites = [];
  }

  return p;
}

function loadProfile() {
  const profile = loadJson(LS_KEYS.PROFILE, null);
  if (profile) return normalizeProfile(profile);

  const fresh = makeDefaultProfile();
  saveProfile(fresh);
  return fresh;
}

function saveProfile(profile) {
  saveJson(LS_KEYS.PROFILE, normalizeProfile(profile));
}

function loadTrips() {
  const trips = loadJson(LS_KEYS.TRIPS, []);
  return Array.isArray(trips) ? trips : [];
}

function saveTrips(trips) {
  saveJson(LS_KEYS.TRIPS, Array.isArray(trips) ? trips : []);
}

function loadSettings() {
  const settings = loadJson(LS_KEYS.SETTINGS, DEFAULT_SETTINGS);
  return { ...DEFAULT_SETTINGS, ...(settings || {}) };
}

function saveSettings(settings) {
  saveJson(LS_KEYS.SETTINGS, settings);
}

/* =========================
   Totaux
   ========================= */
function recomputeTotals(profile, trips) {
  let dist = 0;
  let dur = 0;
  let vmax = 0;

  for (const tr of trips) {
    dist += Number(tr.distanceKm || 0);
    dur += Number(tr.durationSec || 0);
    vmax = Math.max(vmax, Number(tr.maxSpeedKmh || 0));
  }

  profile.totals.distanceKm = dist;
  profile.totals.durationSec = dur;
  profile.totals.avgSpeedKmh = dur > 0 ? dist / (dur / 3600) : 0;
  profile.totals.maxSpeedKmh = vmax;

  return profile;
}

/* =========================
   Navigation SPA
   ========================= */
function setActiveView(viewName) {
  const views = ["dashboard", "navigation", "history", "profile"];

  for (const v of views) {
    const sec = document.getElementById("view_" + v);
    const nav = document.getElementById("nav_" + v);

    if (sec) sec.classList.toggle("is-active", v === viewName);
    if (nav) nav.classList.toggle("is-active", v === viewName);
  }
}

function bindBottomNav() {
  const items = document.querySelectorAll(".nav-item");
  items.forEach(btn => {
    btn.addEventListener("click", () => {
      const view = btn.getAttribute("data-view");
      if (view) setActiveView(view);
    });
  });

  const goNav = document.getElementById("btn_go_nav");
  if (goNav) {
    goNav.addEventListener("click", () => setActiveView("navigation"));
  }
}

/* =========================
   Modal paramètres
   ========================= */
function openSettings() {
  const modal = document.getElementById("settings_modal");
  if (modal) modal.setAttribute("aria-hidden", "false");
}

function closeSettings() {
  const modal = document.getElementById("settings_modal");
  if (modal) modal.setAttribute("aria-hidden", "true");
}

function bindSettingsModal() {
  const btn = document.getElementById("btn_settings");
  const closeBtn = document.getElementById("btn_close_settings");
  const backdrop = document.getElementById("settings_backdrop");

  if (btn) btn.addEventListener("click", openSettings);
  if (closeBtn) closeBtn.addEventListener("click", closeSettings);
  if (backdrop) backdrop.addEventListener("click", closeSettings);

  document.addEventListener("keydown", e => {
    if (e.key === "Escape") closeSettings();
  });
}

/* =========================
   Profil
   ========================= */
function fillProfileForm(profile) {
  const p = normalizeProfile(profile);

  const name = document.getElementById("p_name");
  const email = document.getElementById("p_email");
  const phone = document.getElementById("p_phone");
  const address = document.getElementById("p_address");
  const info = document.getElementById("profile_info");

  const home = document.getElementById("p_home");
  const work = document.getElementById("p_work");
  const fav1Label = document.getElementById("p_fav1_label");
  const fav1Addr = document.getElementById("p_fav1_address");
  const fav2Label = document.getElementById("p_fav2_label");
  const fav2Addr = document.getElementById("p_fav2_address");
  const fav3Label = document.getElementById("p_fav3_label");
  const fav3Addr = document.getElementById("p_fav3_address");

  if (name) name.value = p.name || "";
  if (email) email.value = p.email || "";
  if (phone) phone.value = p.phone || "";
  if (address) address.value = p.address || "";

  if (home) home.value = p.savedPlaces.home?.address || "";
  if (work) work.value = p.savedPlaces.work?.address || "";

  const favs = p.savedPlaces.favorites || [];

  if (fav1Label) fav1Label.value = favs[0]?.label || "";
  if (fav1Addr) fav1Addr.value = favs[0]?.address || "";

  if (fav2Label) fav2Label.value = favs[1]?.label || "";
  if (fav2Addr) fav2Addr.value = favs[1]?.address || "";

  if (fav3Label) fav3Label.value = favs[2]?.label || "";
  if (fav3Addr) fav3Addr.value = favs[2]?.address || "";

  if (info) {
    info.textContent = `Profil créé le : ${fmtDateTime(p.createdAt)} • id: ${p.userId}`;
  }
}

function readProfileForm(profile) {
  const p = normalizeProfile(profile);

  const name = document.getElementById("p_name");
  const email = document.getElementById("p_email");
  const phone = document.getElementById("p_phone");
  const address = document.getElementById("p_address");

  const home = document.getElementById("p_home");
  const work = document.getElementById("p_work");

  const fav1Label = document.getElementById("p_fav1_label");
  const fav1Addr = document.getElementById("p_fav1_address");
  const fav2Label = document.getElementById("p_fav2_label");
  const fav2Addr = document.getElementById("p_fav2_address");
  const fav3Label = document.getElementById("p_fav3_label");
  const fav3Addr = document.getElementById("p_fav3_address");

  p.name = name ? name.value.trim() : p.name;
  p.email = email ? email.value.trim() : p.email;
  p.phone = phone ? phone.value.trim() : p.phone;
  p.address = address ? address.value.trim() : p.address;

  p.savedPlaces.home = {
    label: "Domicile",
    address: home ? home.value.trim() : ""
  };

  p.savedPlaces.work = {
    label: "Bureau",
    address: work ? work.value.trim() : ""
  };

  const favorites = [];

  const favRows = [
    {
      label: fav1Label ? fav1Label.value.trim() : "",
      address: fav1Addr ? fav1Addr.value.trim() : ""
    },
    {
      label: fav2Label ? fav2Label.value.trim() : "",
      address: fav2Addr ? fav2Addr.value.trim() : ""
    },
    {
      label: fav3Label ? fav3Label.value.trim() : "",
      address: fav3Addr ? fav3Addr.value.trim() : ""
    }
  ];

  for (const row of favRows) {
    if (row.label || row.address) {
      favorites.push({
        id: uid(),
        label: row.label || "Favori",
        address: row.address || ""
      });
    }
  }

  p.savedPlaces.favorites = favorites;

  return p;
}

function bindProfileButtons() {
  const saveBtn = document.getElementById("btn_save_profile");
  const resetBtn = document.getElementById("btn_reset_profile");

  if (saveBtn) {
    saveBtn.addEventListener("click", () => {
      const profile = loadProfile();
      readProfileForm(profile);
      saveProfile(profile);
      renderAll();

      const info = document.getElementById("profile_info");
      if (info) info.textContent = "Profil enregistré";
    });
  }

  if (resetBtn) {
    resetBtn.addEventListener("click", () => {
      const ok = confirm("Réinitialiser le profil local ? (ne supprime pas l'historique)");
      if (!ok) return;

      saveProfile(makeDefaultProfile());
      renderAll();
    });
  }
}

/* =========================
   Actions rapides
   ========================= */
function createQuickDestinationButton(label, address, icon = "📍") {
  const btn = document.createElement("button");
  btn.className = "btn ghost";
  btn.type = "button";
  btn.innerHTML = `<span>${icon} ${label}</span>`;
  btn.addEventListener("click", async () => {
    if (!address) {
      alert(`Aucune adresse enregistrée pour ${label}.`);
      return;
    }

    setActiveView("navigation");

    if (!window.AURAGoogleMaps || typeof window.AURAGoogleMaps.routeFromCurrentLocationToAddress !== "function") {
      alert("La navigation n'est pas encore prête.");
      return;
    }

    try {
      await window.AURAGoogleMaps.routeFromCurrentLocationToAddress(address, label);
    } catch (err) {
      console.error(err);
      alert("Impossible de calculer l’itinéraire depuis la position actuelle.");
    }
  });

  return btn;
}

function renderQuickDestinations(profile) {
  const container = document.getElementById("quick_destinations");
  if (!container) return;

  container.innerHTML = "";

  const p = normalizeProfile(profile);

  if (p.savedPlaces.home?.address) {
    container.appendChild(
      createQuickDestinationButton("Domicile", p.savedPlaces.home.address, "🏠")
    );
  }

  if (p.savedPlaces.work?.address) {
    container.appendChild(
      createQuickDestinationButton("Bureau", p.savedPlaces.work.address, "🏢")
    );
  }

  for (const fav of p.savedPlaces.favorites || []) {
    if (!fav?.address) continue;
    container.appendChild(
      createQuickDestinationButton(fav.label || "Favori", fav.address, "⭐")
    );
  }

  if (!container.children.length) {
    const hint = document.createElement("div");
    hint.className = "hint";
    hint.textContent = "Ajoute un domicile, un bureau ou des favoris dans le profil pour les retrouver ici.";
    container.appendChild(hint);
  }
}

/* =========================
   Render stats
   ========================= */
function renderTotals(profile) {
  const td = document.getElementById("total_distance");
  const ts = document.getElementById("total_time");
  const av = document.getElementById("avg_speed");
  const mx = document.getElementById("max_speed");

  const ptd = document.getElementById("p_total_distance");
  const pdu = document.getElementById("p_total_duration");
  const pmx = document.getElementById("p_max_speed");

  if (td) td.textContent = profile.totals.distanceKm.toFixed(1);
  if (ts) ts.textContent = fmtTimeHMS(profile.totals.durationSec);
  if (av) av.textContent = profile.totals.avgSpeedKmh.toFixed(1) + " km/h";
  if (mx) mx.textContent = profile.totals.maxSpeedKmh.toFixed(1);

  if (ptd) ptd.textContent = profile.totals.distanceKm.toFixed(1);
  if (pdu) pdu.textContent = fmtTimeHMS(profile.totals.durationSec);
  if (pmx) pmx.textContent = profile.totals.maxSpeedKmh.toFixed(1);
}

/* =========================
   Historique trajets
   ========================= */
function makeTripKpi(label, value) {
  const d = document.createElement("div");
  d.className = "trip-kpi";

  const l = document.createElement("div");
  l.className = "lbl";
  l.textContent = label;

  const v = document.createElement("div");
  v.className = "val";
  v.textContent = value;

  d.appendChild(l);
  d.appendChild(v);
  return d;
}

function renderTrips(trips) {
  const list = document.getElementById("trips_list");
  const count = document.getElementById("trips_count");
  const status = document.getElementById("history_status");

  if (count) count.textContent = String(trips.length);
  if (!list) return;

  list.innerHTML = "";

  if (!trips.length) {
    const empty = document.createElement("div");
    empty.className = "hint";
    empty.textContent = "Aucun trajet enregistré pour le moment.";
    list.appendChild(empty);
    if (status) status.textContent = "Status : vide";
    return;
  }

  const sorted = trips.slice().sort((a, b) => (b.endedAt || 0) - (a.endedAt || 0));

  for (const tr of sorted) {
    const card = document.createElement("div");
    card.className = "trip";

    const top = document.createElement("div");
    top.className = "trip-top";

    const left = document.createElement("div");

    const title = document.createElement("div");
    title.className = "trip-title";
    title.textContent = tr.name || "Trajet";

    const date = document.createElement("div");
    date.className = "trip-date";
    date.textContent = fmtDateTime(tr.endedAt || tr.startedAt);

    left.appendChild(title);
    left.appendChild(date);

    const del = document.createElement("button");
    del.className = "chip";
    del.type = "button";
    del.textContent = "🗑️";
    del.title = "Supprimer";
    del.addEventListener("click", () => {
      const ok = confirm("Supprimer ce trajet ?");
      if (!ok) return;

      const next = loadTrips().filter(x => x.id !== tr.id);
      saveTrips(next);
      renderAll();
    });

    top.appendChild(left);
    top.appendChild(del);

    const kpis = document.createElement("div");
    kpis.className = "trip-kpis";
    kpis.appendChild(makeTripKpi("Distance", `${Number(tr.distanceKm || 0).toFixed(2)} km`));
    kpis.appendChild(makeTripKpi("Durée", fmtTimeHMS(tr.durationSec || 0)));
    kpis.appendChild(makeTripKpi("V moy.", `${Number(tr.avgSpeedKmh || 0).toFixed(1)} km/h`));
    kpis.appendChild(makeTripKpi("V max", `${Number(tr.maxSpeedKmh || 0).toFixed(1)} km/h`));

    card.appendChild(top);
    card.appendChild(kpis);
    list.appendChild(card);
  }

  if (status) status.textContent = "Status : OK";
}

function bindTripsButtons() {
  const exportBtn = document.getElementById("btn_export_trips");
  const clearBtn = document.getElementById("btn_clear_trips");

  if (exportBtn) {
    exportBtn.addEventListener("click", () => {
      const trips = loadTrips();
      const blob = new Blob([JSON.stringify(trips, null, 2)], { type: "application/json" });
      const url = URL.createObjectURL(blob);
      const a = document.createElement("a");
      a.href = url;
      a.download = "aura_trips.json";
      a.click();
      URL.revokeObjectURL(url);
    });
  }

  if (clearBtn) {
    clearBtn.addEventListener("click", () => {
      const ok = confirm("Supprimer tout l'historique des trajets ?");
      if (!ok) return;

      saveTrips([]);
      renderAll();
    });
  }
}

/* =========================
   Dernière route
   ========================= */
function renderLastRouteSnapshot() {
  const snap = loadJson(LS_KEYS.LAST_ROUTE, null);

  const lastName = document.getElementById("last_route_name");
  const lastMeta = document.getElementById("last_route_meta");
  const lastTime = document.getElementById("last_route_time");

  if (!snap) {
    if (lastName) lastName.textContent = "Aucune";
    if (lastMeta) lastMeta.textContent = "—";
    if (lastTime) lastTime.textContent = "—";
    return;
  }

  if (lastName) lastName.textContent = snap.name || "—";
  if (lastMeta) lastMeta.textContent = `${snap.pointsCount} points • ~${Number(snap.distKm || 0).toFixed(2)} km`;
  if (lastTime) lastTime.textContent = fmtDateTime(snap.loadedAt);
}

/* =========================
   BLE helpers UI
   ========================= */
function setDeviceTag(name) {
  const tag = document.getElementById("device_tag");
  const settingsTag = document.getElementById("settings_ble_tag");

  if (tag) tag.textContent = name ? `Device : ${name}` : "Device : —";
  if (settingsTag) settingsTag.textContent = name ? `BLE : ${name}` : "BLE : --";
}

/* =========================
   Paramètres / sync
   ========================= */
function bindSettingsButtons() {
  const btnSync = document.getElementById("btn_sync");
  const btnSyncNow = document.getElementById("btn_sync_now");
  const btnAuto = document.getElementById("btn_toggle_autosync");
  const syncTag = document.getElementById("sync_tag");

  function renderAuto() {
    const s = loadSettings();
    if (btnAuto) btnAuto.textContent = `Auto-sync : ${s.autosync ? "ON" : "OFF"}`;
    if (syncTag) syncTag.textContent = `Sync : ${s.autosync ? "auto" : "manuel"}`;
  }

  function doSync() {
    const lastUpdate = document.getElementById("dash_last_update");
    const dashStatus = document.getElementById("dash_status");

    if (lastUpdate) {
      lastUpdate.textContent = "Dernière mise à jour : " + new Date().toLocaleTimeString("fr-FR");
    }
    if (dashStatus) {
      dashStatus.textContent = "Status : synchronisé";
    }
  }

  if (btnSync) btnSync.addEventListener("click", doSync);
  if (btnSyncNow) btnSyncNow.addEventListener("click", doSync);

  if (btnAuto) {
    btnAuto.addEventListener("click", () => {
      const s = loadSettings();
      s.autosync = !s.autosync;
      saveSettings(s);
      renderAuto();
    });
  }

  renderAuto();
}

/* =========================
   Debug
   ========================= */
function bindDebugButtons() {
  const dump = document.getElementById("btn_dump_storage");
  const reset = document.getElementById("btn_reset_app");

  if (dump) {
    dump.addEventListener("click", () => {
      console.log("PROFILE", loadProfile());
      console.log("TRIPS", loadTrips());
      console.log("SETTINGS", loadSettings());
      console.log("LAST_ROUTE", loadJson(LS_KEYS.LAST_ROUTE, null));
      alert("Dump dans la console.");
    });
  }

  if (reset) {
    reset.addEventListener("click", () => {
      const ok = confirm("Reset app = supprime profil + trajets + settings. Continuer ?");
      if (!ok) return;

      localStorage.removeItem(LS_KEYS.PROFILE);
      localStorage.removeItem(LS_KEYS.TRIPS);
      localStorage.removeItem(LS_KEYS.SETTINGS);
      localStorage.removeItem(LS_KEYS.LAST_ROUTE);

      renderAll();
      alert("App reset");
    });
  }
}

/* =========================
   Démo trajets
   ========================= */
function ensureDemoTripsIfEmpty() {
  const trips = loadTrips();
  if (trips.length > 0) return;

  const now = Date.now();

  const demo = [
    {
      id: uid(),
      name: "Maison → ESME",
      startedAt: now - (2 * 24 * 3600 * 1000) - (32 * 60 * 1000),
      endedAt: now - (2 * 24 * 3600 * 1000),
      durationSec: 32 * 60,
      distanceKm: 8.4,
      avgSpeedKmh: 15.8,
      maxSpeedKmh: 31.6,
      source: "DEMO"
    },
    {
      id: uid(),
      name: "Boucle Ivry / Seine",
      startedAt: now - (24 * 3600 * 1000) - (54 * 60 * 1000),
      endedAt: now - (24 * 3600 * 1000),
      durationSec: 54 * 60,
      distanceKm: 18.7,
      avgSpeedKmh: 20.8,
      maxSpeedKmh: 37.9,
      source: "DEMO"
    },
    {
      id: uid(),
      name: "Villejuif → Bastille",
      startedAt: now - (6 * 3600 * 1000) - (41 * 60 * 1000),
      endedAt: now - (6 * 3600 * 1000),
      durationSec: 41 * 60,
      distanceKm: 11.9,
      avgSpeedKmh: 17.4,
      maxSpeedKmh: 33.8,
      source: "DEMO"
    },
    {
      id: uid(),
      name: "Sortie du soir",
      startedAt: now - (90 * 60 * 1000),
      endedAt: now - (35 * 60 * 1000),
      durationSec: 55 * 60,
      distanceKm: 21.3,
      avgSpeedKmh: 23.2,
      maxSpeedKmh: 41.5,
      source: "DEMO"
    }
  ];

  saveTrips(demo);
}

/* =========================
   Render global
   ========================= */
function renderAll() {
  let profile = loadProfile();
  const trips = loadTrips();

  profile = recomputeTotals(profile, trips);
  saveProfile(profile);

  fillProfileForm(profile);
  renderTotals(profile);
  renderTrips(trips);
  renderLastRouteSnapshot();
  renderQuickDestinations(profile);

  const buildInfo = document.getElementById("build_info");
  if (buildInfo) {
    buildInfo.textContent = "Build : " + new Date().toLocaleDateString("fr-FR");
  }

  const dashStatus = document.getElementById("dash_status");
  if (dashStatus && dashStatus.textContent.trim() === "Status : --") {
    dashStatus.textContent = "Status : prêt";
  }

  try {
    if (window.auraBLE && window.auraBLE.device && window.auraBLE.isConnected) {
      setDeviceTag(window.auraBLE.device.name || "ESP32");
    } else {
      setDeviceTag(null);
    }
  } catch {
    setDeviceTag(null);
  }
}

/* =========================
   Init
   ========================= */
document.addEventListener("DOMContentLoaded", () => {
  console.log("AURA app.js chargé");

  ensureDemoTripsIfEmpty();

  bindBottomNav();
  bindSettingsModal();
  bindProfileButtons();
  bindTripsButtons();
  bindSettingsButtons();
  bindDebugButtons();

  setActiveView("dashboard");
  renderAll();

  setInterval(() => {
    try {
      if (window.auraBLE && window.auraBLE.device && window.auraBLE.isConnected) {
        setDeviceTag(window.auraBLE.device.name || "ESP32");
      } else {
        setDeviceTag(null);
      }
    } catch (_) {}
  }, 1500);
});