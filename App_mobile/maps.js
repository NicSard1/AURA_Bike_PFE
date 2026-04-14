/* =========================================================
   AURA Bike - maps.js
   - autocomplete Google Places navigation
   - autocomplete Google Places profil
   - alias locaux : domicile / bureau / favoris
   - suggestion "Ma position actuelle" pour départ et arrivée
   - calcul itinéraire vélo
   - affichage carte
   - génération GPX
   - calcul direct depuis position actuelle
   ========================================================= */

(function () {
  "use strict";

  const AURAMaps = {
    map: null,
    directionsService: null,
    directionsRenderer: null,
    originAutocomplete: null,
    destinationAutocomplete: null,
    profileAutocompletes: [],
    originPlace: null,
    destinationPlace: null,
    currentRouteResult: null,
    currentRoutePoints: [],
    currentRouteName: null,
    currentRouteDistanceMeters: 0,
    currentRouteDurationSec: 0,
    mapReady: false,
    useCurrentLocationAsOrigin: false,
    useCurrentLocationAsDestination: false
  };

  /* =========================
     Helpers DOM / texte
     ========================= */
  function setText(id, text) {
    const el = document.getElementById(id);
    if (el) el.textContent = text;
  }

  function setStatus(text) {
    setText("gmaps_route_status", text);
  }

  function fmtDistanceMeters(meters) {
    const m = Number(meters || 0);
    if (m >= 1000) return `${(m / 1000).toFixed(2)} km`;
    return `${Math.round(m)} m`;
  }

  function fmtDurationSec(sec) {
    const total = Math.max(0, Math.round(Number(sec || 0)));
    const h = Math.floor(total / 3600);
    const m = Math.floor((total % 3600) / 60);
    const s = total % 60;

    if (h > 0) return `${h}h ${String(m).padStart(2, "0")}m`;
    if (m > 0) return `${m} min ${String(s).padStart(2, "0")} s`;
    return `${s} s`;
  }

  function xmlEscape(str) {
    return String(str || "")
      .replaceAll("&", "&amp;")
      .replaceAll("<", "&lt;")
      .replaceAll(">", "&gt;")
      .replaceAll('"', "&quot;")
      .replaceAll("'", "&apos;");
  }

  function getOriginInput() {
    return document.getElementById("origin_input");
  }

  function getDestinationInput() {
    return document.getElementById("destination_input");
  }

  function getOriginQuickPick() {
    return document.getElementById("origin_quick_pick");
  }

  function getDestinationQuickPick() {
    return document.getElementById("destination_quick_pick");
  }

  /* =========================
     Profil / lieux enregistrés
     ========================= */
  function getProfileSavedPlaces() {
    try {
      const raw = localStorage.getItem("aura_profile_v1");
      if (!raw) return null;

      const profile = JSON.parse(raw);
      return profile?.savedPlaces || null;
    } catch {
      return null;
    }
  }

  function resolveCustomAddressAlias(text) {
    const q = String(text || "").trim().toLowerCase();
    if (!q) return null;

    if (
      q === "ma position" ||
      q === "ma position actuelle" ||
      q === "position" ||
      q === "position actuelle" ||
      q === "current location"
    ) {
      return {
        type: "current_location",
        label: "Ma position",
        address: "Ma position"
      };
    }

    const savedPlaces = getProfileSavedPlaces();
    if (!savedPlaces) return null;

    const aliases = [];

    if (savedPlaces.home?.address) {
      aliases.push({
        keys: ["domicile", "maison", "home", "chez moi"],
        address: savedPlaces.home.address,
        label: savedPlaces.home.label || "Domicile"
      });
    }

    if (savedPlaces.work?.address) {
      aliases.push({
        keys: ["bureau", "travail", "work", "esme"],
        address: savedPlaces.work.address,
        label: savedPlaces.work.label || "Bureau"
      });
    }

    for (const fav of savedPlaces.favorites || []) {
      if (!fav?.address) continue;

      const keys = [];
      if (fav.label) keys.push(String(fav.label).trim().toLowerCase());
      if (fav.address) keys.push(String(fav.address).trim().toLowerCase());

      aliases.push({
        keys,
        address: fav.address,
        label: fav.label || "Favori"
      });
    }

    for (const item of aliases) {
      for (const key of item.keys) {
        if (key && q === key) {
          return item;
        }
      }
    }

    return null;
  }

  /* =========================
     Géolocalisation
     ========================= */
  function getCurrentPositionPromise() {
    return new Promise((resolve, reject) => {
      if (!navigator.geolocation) {
        reject(new Error("Géolocalisation non supportée"));
        return;
      }

      navigator.geolocation.getCurrentPosition(
        (pos) => {
          resolve({
            lat: pos.coords.latitude,
            lng: pos.coords.longitude
          });
        },
        (err) => reject(err),
        {
          enableHighAccuracy: true,
          timeout: 10000,
          maximumAge: 10000
        }
      );
    });
  }

  /* =========================
     Etat route
     ========================= */
  function clearRouteStateOnly() {
    AURAMaps.currentRouteResult = null;
    AURAMaps.currentRoutePoints = [];
    AURAMaps.currentRouteName = null;
    AURAMaps.currentRouteDistanceMeters = 0;
    AURAMaps.currentRouteDurationSec = 0;
  }

  function updateLastRouteCard(name, pointsCount, distanceMeters) {
    const snap = {
      routeId: (window.crypto && crypto.randomUUID) ? crypto.randomUUID() : "route-" + Date.now(),
      name: name || "Route Google Maps",
      pointsCount: Number(pointsCount || 0),
      distanceKmEstimate: Number(distanceMeters || 0) / 1000,
      loadedAt: Date.now()
    };

    window.AuraRouteState = window.AuraRouteState || {};
    window.AuraRouteState.routeId = snap.routeId;
    window.AuraRouteState.name = snap.name;
    window.AuraRouteState.pointsCount = snap.pointsCount;
    window.AuraRouteState.distanceKmEstimate = snap.distanceKmEstimate;
    window.AuraRouteState.loadedAt = snap.loadedAt;

    try {
      localStorage.setItem("aura_last_route_v1", JSON.stringify({
        id: snap.routeId,
        name: snap.name,
        pointsCount: snap.pointsCount,
        distKm: snap.distanceKmEstimate,
        loadedAt: snap.loadedAt
      }));
    } catch (_) {}

    setText("last_route_name", snap.name);
    setText("last_route_meta", `${snap.pointsCount} points • ~${snap.distanceKmEstimate.toFixed(2)} km`);
    setText("last_route_time", new Date(snap.loadedAt).toLocaleString("fr-FR"));
  }

  /* =========================
     Extraction géométrie
     ========================= */
  function extractDensePointsFromRoute(route) {
    const points = [];

    if (!route) return points;

    if (Array.isArray(route.legs)) {
      for (const leg of route.legs) {
        if (!leg || !Array.isArray(leg.steps)) continue;

        for (const step of leg.steps) {
          if (!step) continue;

          const path = step.path || step.lat_lngs || [];
          for (const p of path) {
            const lat = typeof p.lat === "function" ? p.lat() : p.lat;
            const lng = typeof p.lng === "function" ? p.lng() : p.lng;

            if (Number.isFinite(lat) && Number.isFinite(lng)) {
              const last = points[points.length - 1];
              if (!last || last.lat !== lat || last.lon !== lng) {
                points.push({ lat, lon: lng });
              }
            }
          }
        }
      }
    }

    if (points.length > 1) return points;

    if (Array.isArray(route.overview_path)) {
      for (const p of route.overview_path) {
        const lat = typeof p.lat === "function" ? p.lat() : p.lat;
        const lng = typeof p.lng === "function" ? p.lng() : p.lng;

        if (Number.isFinite(lat) && Number.isFinite(lng)) {
          points.push({ lat, lon: lng });
        }
      }
    }

    return points;
  }

  /* =========================
     GPX
     ========================= */
  function buildSimpleGpx(points, name) {
    const routeName = xmlEscape(name || "AURA Google Route");
    const now = new Date();

    let trkpts = "";
    points.forEach((p, i) => {
      const t = new Date(now.getTime() + i * 1000).toISOString();
      trkpts += `      <trkpt lat="${Number(p.lat).toFixed(7)}" lon="${Number(p.lon).toFixed(7)}"><time>${t}</time></trkpt>\n`;
    });

    return `<?xml version="1.0" encoding="UTF-8"?>
<gpx version="1.1" creator="AURA Bike"
  xmlns="http://www.topografix.com/GPX/1/1"
  xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
  xsi:schemaLocation="http://www.topografix.com/GPX/1/1 http://www.topografix.com/GPX/1/1/gpx.xsd">
  <metadata>
    <name>${routeName}</name>
    <time>${now.toISOString()}</time>
  </metadata>
  <trk>
    <name>${routeName}</name>
    <trkseg>
${trkpts.trimEnd()}
    </trkseg>
  </trk>
</gpx>`;
  }

  function updateAuraRouteState(name, points, distanceMeters, durationSec) {
    const gpxText = buildSimpleGpx(points, name);
    const routeId = (window.crypto && crypto.randomUUID) ? crypto.randomUUID() : "gmaps-" + Date.now();

    window.AuraRouteState = {
      routeId,
      name,
      points: Array.isArray(points) ? points : [],
      pointsCount: Array.isArray(points) ? points.length : 0,
      distanceKmEstimate: Number(distanceMeters || 0) / 1000,
      durationSec: Number(durationSec || 0),
      loadedAt: Date.now(),
      gpxText
    };

    updateLastRouteCard(name, points.length, distanceMeters);
  }

  /* =========================
     Suggestion custom "Ma position actuelle"
     ========================= */
  function bindCurrentLocationQuickPick(inputEl, quickPickEl, type) {
    if (!inputEl || !quickPickEl) return;

    function shouldShowQuickPick() {
      const v = inputEl.value.trim().toLowerCase();
      return (
        v === "" ||
        "ma position".includes(v) ||
        "ma position actuelle".includes(v) ||
        "position".includes(v) ||
        "position actuelle".includes(v)
      );
    }

    function showQuickPick() {
      if (shouldShowQuickPick()) {
        quickPickEl.hidden = false;
      }
    }

    function hideQuickPickDelayed() {
      setTimeout(() => {
        quickPickEl.hidden = true;
      }, 150);
    }

    inputEl.addEventListener("focus", showQuickPick);
    inputEl.addEventListener("click", showQuickPick);
    inputEl.addEventListener("input", showQuickPick);
    inputEl.addEventListener("blur", hideQuickPickDelayed);

    quickPickEl.addEventListener("mousedown", (e) => {
      e.preventDefault();

      inputEl.value = "Ma position";

      if (type === "origin") {
        AURAMaps.useCurrentLocationAsOrigin = true;
        AURAMaps.originPlace = null;
        setText("origin_summary", "Ma position actuelle");
      } else {
        AURAMaps.useCurrentLocationAsDestination = true;
        AURAMaps.destinationPlace = null;
        setText("destination_summary", "Ma position actuelle");
      }

      quickPickEl.hidden = true;
    });
  }

  /* =========================
     Google Autocomplete navigation
     ========================= */
  function bindAutocomplete() {
    const originInput = getOriginInput();
    const destinationInput = getDestinationInput();

    if (!originInput || !destinationInput) {
      setStatus("Status : champs adresse introuvables");
      return;
    }

    const options = {
      fields: ["formatted_address", "geometry", "name", "place_id", "types"],
      componentRestrictions: { country: ["fr"] }
    };

    AURAMaps.originAutocomplete = new google.maps.places.Autocomplete(originInput, options);
    AURAMaps.destinationAutocomplete = new google.maps.places.Autocomplete(destinationInput, options);

    const bounds = new google.maps.LatLngBounds(
      new google.maps.LatLng(48.70, 2.20),
      new google.maps.LatLng(48.92, 2.50)
    );

    AURAMaps.originAutocomplete.setBounds(bounds);
    AURAMaps.destinationAutocomplete.setBounds(bounds);

    AURAMaps.originAutocomplete.addListener("place_changed", () => {
      const place = AURAMaps.originAutocomplete.getPlace();
      AURAMaps.originPlace = place || null;
      AURAMaps.useCurrentLocationAsOrigin = false;

      if (place && place.geometry && place.geometry.location) {
        const label = place.formatted_address || place.name || "Départ";
        const typeTxt = Array.isArray(place.types) && place.types.length
          ? ` • ${place.types[0]}`
          : "";

        setText("origin_summary", `${label}${typeTxt}`);
      } else {
        setText("origin_summary", "Départ invalide");
      }
    });

    AURAMaps.destinationAutocomplete.addListener("place_changed", () => {
      const place = AURAMaps.destinationAutocomplete.getPlace();
      AURAMaps.destinationPlace = place || null;
      AURAMaps.useCurrentLocationAsDestination = false;

      if (place && place.geometry && place.geometry.location) {
        const label = place.formatted_address || place.name || "Destination";
        const typeTxt = Array.isArray(place.types) && place.types.length
          ? ` • ${place.types[0]}`
          : "";

        setText("destination_summary", `${label}${typeTxt}`);
      } else {
        setText("destination_summary", "Destination invalide");
      }
    });

    originInput.addEventListener("blur", () => {
      const resolved = resolveCustomAddressAlias(originInput.value);
      if (!resolved) return;

      if (resolved.type === "current_location") {
        AURAMaps.useCurrentLocationAsOrigin = true;
        AURAMaps.originPlace = null;
        originInput.value = "Ma position";
        setText("origin_summary", "Ma position actuelle");
        return;
      }

      setText("origin_summary", `${resolved.label} • ${resolved.address}`);
      AURAMaps.originPlace = null;
      AURAMaps.useCurrentLocationAsOrigin = false;
    });

    destinationInput.addEventListener("blur", () => {
      const resolved = resolveCustomAddressAlias(destinationInput.value);
      if (!resolved) return;

      if (resolved.type === "current_location") {
        AURAMaps.useCurrentLocationAsDestination = true;
        AURAMaps.destinationPlace = null;
        destinationInput.value = "Ma position";
        setText("destination_summary", "Ma position actuelle");
        return;
      }

      setText("destination_summary", `${resolved.label} • ${resolved.address}`);
      AURAMaps.destinationPlace = null;
      AURAMaps.useCurrentLocationAsDestination = false;
    });

    bindCurrentLocationQuickPick(originInput, document.getElementById("origin_quick_pick"), "origin");
    bindCurrentLocationQuickPick(destinationInput, document.getElementById("destination_quick_pick"), "destination");
  }

  /* =========================
     Google Autocomplete profil
     ========================= */
  function bindProfileAutocomplete() {
    const fieldIds = [
      "p_address",
      "p_home",
      "p_work",
      "p_fav1_address",
      "p_fav2_address",
      "p_fav3_address"
    ];

    const bounds = new google.maps.LatLngBounds(
      new google.maps.LatLng(48.70, 2.20),
      new google.maps.LatLng(48.92, 2.50)
    );

    const options = {
      fields: ["formatted_address", "geometry", "name", "place_id", "types"],
      componentRestrictions: { country: ["fr"] }
    };

    for (const id of fieldIds) {
      const input = document.getElementById(id);
      if (!input) continue;

      const autocomplete = new google.maps.places.Autocomplete(input, options);
      autocomplete.setBounds(bounds);

      autocomplete.addListener("place_changed", () => {
        const place = autocomplete.getPlace();
        if (!place) return;

        const bestLabel = place.formatted_address || place.name || input.value || "";
        input.value = bestLabel;
      });

      AURAMaps.profileAutocompletes.push(autocomplete);
    }
  }

  /* =========================
     Initialisation carte
     ========================= */
  async function initMap() {
    try {
      setText("gmap_status", "Carte : chargement…");

      await Promise.all([
        google.maps.importLibrary("maps"),
        google.maps.importLibrary("places")
      ]);

      const mapEl = document.getElementById("map");
      if (!mapEl) {
        setText("gmap_status", "Carte : élément introuvable");
        return;
      }

      AURAMaps.map = new google.maps.Map(mapEl, {
        center: { lat: 48.8566, lng: 2.3522 },
        zoom: 12,
        mapTypeControl: false,
        streetViewControl: false,
        fullscreenControl: true,
        clickableIcons: false
      });

      AURAMaps.directionsService = new google.maps.DirectionsService();
      AURAMaps.directionsRenderer = new google.maps.DirectionsRenderer({
        map: AURAMaps.map,
        suppressMarkers: false,
        preserveViewport: false
      });

      bindAutocomplete();
      bindProfileAutocomplete();

      AURAMaps.mapReady = true;
      setText("gmap_status", "Carte : prête");
      setStatus("Status : carte initialisée");
    } catch (err) {
      console.error("Google Maps init error:", err);
      setText("gmap_status", "Carte : erreur API");
      setStatus("Status : échec chargement Google Maps");
    }
  }

  /* =========================
     Calcul classique
     ========================= */
  async function calculateBikeRoute() {
    try {
      if (!AURAMaps.mapReady || !AURAMaps.directionsService || !AURAMaps.directionsRenderer) {
        setStatus("Status : carte non prête");
        return;
      }

      const originInput = getOriginInput();
      const destinationInput = getDestinationInput();

      let originText = originInput ? originInput.value.trim() : "";
      let destinationText = destinationInput ? destinationInput.value.trim() : "";

      const resolvedOrigin = resolveCustomAddressAlias(originText);
      const resolvedDestination = resolveCustomAddressAlias(destinationText);

      if (resolvedOrigin) {
        if (resolvedOrigin.type === "current_location") {
          AURAMaps.useCurrentLocationAsOrigin = true;
          AURAMaps.originPlace = null;
          originText = "Ma position";
          if (originInput) originInput.value = "Ma position";
        } else {
          originText = resolvedOrigin.address;
          AURAMaps.useCurrentLocationAsOrigin = false;
          if (originInput) originInput.value = resolvedOrigin.label;
        }
      }

      if (resolvedDestination) {
        if (resolvedDestination.type === "current_location") {
          AURAMaps.useCurrentLocationAsDestination = true;
          AURAMaps.destinationPlace = null;
          destinationText = "Ma position";
          if (destinationInput) destinationInput.value = "Ma position";
        } else {
          destinationText = resolvedDestination.address;
          AURAMaps.useCurrentLocationAsDestination = false;
          if (destinationInput) destinationInput.value = resolvedDestination.label;
        }
      }

      if (!originText || !destinationText) {
        setStatus("Status : renseigne le départ et la destination");
        return;
      }

      setStatus("Status : calcul en cours…");

      let originRequest;
      let destinationRequest;

      if (AURAMaps.useCurrentLocationAsOrigin || originText.toLowerCase() === "ma position") {
        setStatus("Status : récupération position actuelle (départ)…");
        originRequest = await getCurrentPositionPromise();
      } else {
        originRequest =
          AURAMaps.originPlace && AURAMaps.originPlace.geometry
            ? AURAMaps.originPlace.geometry.location
            : originText;
      }

      if (AURAMaps.useCurrentLocationAsDestination || destinationText.toLowerCase() === "ma position") {
        setStatus("Status : récupération position actuelle (destination)…");
        destinationRequest = await getCurrentPositionPromise();
      } else {
        destinationRequest =
          AURAMaps.destinationPlace && AURAMaps.destinationPlace.geometry
            ? AURAMaps.destinationPlace.geometry.location
            : destinationText;
      }

      setStatus("Status : calcul en cours…");

      const result = await AURAMaps.directionsService.route({
        origin: originRequest,
        destination: destinationRequest,
        travelMode: google.maps.TravelMode.BICYCLING,
        provideRouteAlternatives: false
      });

      if (!result || !result.routes || !result.routes.length) {
        setStatus("Status : aucun itinéraire trouvé");
        return;
      }

      AURAMaps.currentRouteResult = result;
      AURAMaps.directionsRenderer.setDirections(result);

      const route = result.routes[0];
      const leg = route.legs && route.legs[0] ? route.legs[0] : null;

      const points = extractDensePointsFromRoute(route);
      const routeName = `${originInput?.value?.trim() || "Départ"} → ${destinationInput?.value?.trim() || "Destination"}`;
      const distanceMeters = leg?.distance?.value ? Number(leg.distance.value) : 0;
      const durationSec = leg?.duration?.value ? Number(leg.duration.value) : 0;

      AURAMaps.currentRoutePoints = points;
      AURAMaps.currentRouteName = routeName;
      AURAMaps.currentRouteDistanceMeters = distanceMeters;
      AURAMaps.currentRouteDurationSec = durationSec;

      setText("origin_summary", leg?.start_address || originText);
      setText("destination_summary", leg?.end_address || destinationText);
      setText("gmaps_distance", leg?.distance?.text || fmtDistanceMeters(distanceMeters));
      setText("gmaps_duration", leg?.duration?.text || fmtDurationSec(durationSec));

      updateAuraRouteState(routeName, points, distanceMeters, durationSec);

      setStatus(`Status : itinéraire calculé ✅ (${points.length} points GPX)`);
    } catch (err) {
      console.error("Route error:", err);
      setStatus("Status : erreur calcul itinéraire");
    }
  }

  /* =========================
     Calcul depuis position actuelle
     ========================= */
  async function routeFromCurrentLocationToAddress(address, label = "Destination rapide") {
    try {
      if (!AURAMaps.mapReady || !AURAMaps.directionsService || !AURAMaps.directionsRenderer) {
        setStatus("Status : carte non prête");
        return;
      }

      const originInput = getOriginInput();
      const destinationInput = getDestinationInput();

      if (destinationInput) destinationInput.value = label || address;
      if (originInput) originInput.value = "Ma position";

      AURAMaps.useCurrentLocationAsOrigin = true;
      AURAMaps.originPlace = null;

      setStatus("Status : récupération position actuelle…");

      const currentPos = await getCurrentPositionPromise();

      setStatus("Status : calcul en cours…");

      const result = await AURAMaps.directionsService.route({
        origin: currentPos,
        destination: address,
        travelMode: google.maps.TravelMode.BICYCLING,
        provideRouteAlternatives: false
      });

      if (!result || !result.routes || !result.routes.length) {
        setStatus("Status : aucun itinéraire trouvé");
        return;
      }

      AURAMaps.currentRouteResult = result;
      AURAMaps.directionsRenderer.setDirections(result);

      const route = result.routes[0];
      const leg = route.legs && route.legs[0] ? route.legs[0] : null;

      const points = extractDensePointsFromRoute(route);
      const routeName = `Ma position → ${label}`;
      const distanceMeters = leg?.distance?.value ? Number(leg.distance.value) : 0;
      const durationSec = leg?.duration?.value ? Number(leg.duration.value) : 0;

      AURAMaps.currentRoutePoints = points;
      AURAMaps.currentRouteName = routeName;
      AURAMaps.currentRouteDistanceMeters = distanceMeters;
      AURAMaps.currentRouteDurationSec = durationSec;

      setText("origin_summary", leg?.start_address || "Ma position");
      setText("destination_summary", leg?.end_address || address);
      setText("gmaps_distance", leg?.distance?.text || fmtDistanceMeters(distanceMeters));
      setText("gmaps_duration", leg?.duration?.text || fmtDurationSec(durationSec));

      updateAuraRouteState(routeName, points, distanceMeters, durationSec);

      setStatus(`Status : itinéraire calculé ✅ (${points.length} points GPX)`);
    } catch (err) {
      console.error("routeFromCurrentLocationToAddress error:", err);
      setStatus("Status : impossible de calculer depuis la position actuelle");
      throw err;
    }
  }

  /* =========================
     Clear
     ========================= */
  function clearGoogleRoute() {
    const originInput = getOriginInput();
    const destinationInput = getDestinationInput();

    if (originInput) originInput.value = "";
    if (destinationInput) destinationInput.value = "";

    setText("origin_summary", "Aucun départ sélectionné");
    setText("destination_summary", "Aucune destination sélectionnée");
    setText("gmaps_distance", "--");
    setText("gmaps_duration", "--");

    AURAMaps.originPlace = null;
    AURAMaps.destinationPlace = null;
    AURAMaps.useCurrentLocationAsOrigin = false;
    AURAMaps.useCurrentLocationAsDestination = false;
    clearRouteStateOnly();

    if (AURAMaps.directionsRenderer) {
      AURAMaps.directionsRenderer.setDirections({ routes: [] });
    }

    window.AuraRouteState = null;

    setStatus("Status : itinéraire effacé");
  }

  /* =========================
     Envoi BLE
     ========================= */
  async function sendGoogleRouteToBle() {
    try {
      if (!window.AuraRouteState || !window.AuraRouteState.gpxText) {
        setStatus("Status : calcule d’abord un itinéraire");
        return;
      }

      if (!window.auraBLE || !window.auraBLE.isConnected || typeof window.auraBLE.sendRoute !== "function") {
        setStatus("Status : BLE non connecté");
        return;
      }

      setStatus("Status : envoi GPX via BLE…");

      await window.auraBLE.sendRoute(
        window.AuraRouteState.routeId || ("gmaps-" + Date.now()),
        window.AuraRouteState.gpxText
      );

      setStatus("Status : GPX envoyé au cintre ✅");
    } catch (err) {
      console.error("Send Google route error:", err);
      setStatus("Status : échec envoi GPX");
    }
  }

  /* =========================
     Boutons
     ========================= */
  function bindButtons() {
    const calcBtn = document.getElementById("btn_calc_route");
    const clearBtn = document.getElementById("btn_clear_google_route");
    const sendBtn = document.getElementById("btn_send_google_route");

    if (calcBtn) calcBtn.addEventListener("click", calculateBikeRoute);
    if (clearBtn) clearBtn.addEventListener("click", clearGoogleRoute);
    if (sendBtn) sendBtn.addEventListener("click", sendGoogleRouteToBle);
  }

  /* =========================
     Init
     ========================= */
  document.addEventListener("DOMContentLoaded", async () => {
    bindButtons();
    await initMap();
  });

  window.AURAGoogleMaps = {
    initMap,
    calculateBikeRoute,
    clearGoogleRoute,
    sendGoogleRouteToBle,
    routeFromCurrentLocationToAddress,
    getCurrentRoutePoints: () => AURAMaps.currentRoutePoints.slice(),
    getCurrentRouteGpx: () => (window.AuraRouteState ? window.AuraRouteState.gpxText : null)
  };
})();