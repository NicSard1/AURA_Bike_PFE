/* =========================================================
   AURA Bike - ble.js
   Version alignée sur le prototype GPX -> ESP32
   - recherche BLE par préfixe de nom : ESP32S3
   - 1 service
   - 1 caractéristique write
   - envoi chunké
   - marqueur EOF
   ========================================================= */

const AURA_BLE_CONFIG = {
  DEVICE_NAME_PREFIX: "ESP32S3",
  SERVICE_UUID: "0000abf0-0000-1000-8000-00805f9b34fb",
  CHAR_UUID: "0000abf1-0000-1000-8000-00805f9b34fb",
  CHUNK_SIZE: 200,
  CHUNK_DELAY_MS: 20
};

class AuraBLE {
  constructor() {
    this.device = null;
    this.server = null;
    this.service = null;
    this.characteristic = null;
    this.isConnected = false;
  }

  log(msg) {
    console.log("[AURA BLE]", msg);

    const routeStatus = document.getElementById("route_status");
    if (routeStatus) {
      routeStatus.textContent = "Status : " + msg;
    }

    const bleStatus = document.getElementById("ble_status");
    if (bleStatus && !msg.startsWith("Envoi")) {
      bleStatus.textContent = "BLE : " + msg;
    }
  }

  updateBleUI(connected) {
    const dot = document.getElementById("ble_dot");
    const text = document.getElementById("ble_text");
    const status = document.getElementById("ble_status");
    const deviceTag = document.getElementById("device_tag");
    const settingsBleTag = document.getElementById("settings_ble_tag");

    if (connected) {
      if (dot) dot.style.background = "var(--ok)";
      if (text) text.textContent = "BLE • Connecté";
      if (status) status.textContent = "BLE : connecté";
      if (deviceTag) {
        deviceTag.textContent = this.device?.name
          ? `Device : ${this.device.name}`
          : "Device : ESP32S3";
      }
      if (settingsBleTag) {
        settingsBleTag.textContent = this.device?.name
          ? `BLE : ${this.device.name}`
          : "BLE : ESP32S3";
      }
    } else {
      if (dot) dot.style.background = "var(--bad)";
      if (text) text.textContent = "BLE • Déconnecté";
      if (status) status.textContent = "BLE : déconnecté";
      if (deviceTag) deviceTag.textContent = "Device : —";
      if (settingsBleTag) settingsBleTag.textContent = "BLE : --";
    }
  }

  updateRouteProgress(percent) {
    const bar = document.getElementById("route_progress");
    const txt = document.getElementById("route_progress_text");

    if (bar) bar.style.width = `${percent}%`;
    if (txt) txt.textContent = `${percent}%`;
  }

  delay(ms) {
    return new Promise(resolve => setTimeout(resolve, ms));
  }

  async connect() {
    try {
      if (!navigator.bluetooth) {
        throw new Error("Web Bluetooth n'est pas supporté sur ce navigateur.");
      }

      this.log(`Recherche d'un appareil BLE avec le préfixe "${AURA_BLE_CONFIG.DEVICE_NAME_PREFIX}"...`);

      this.device = await navigator.bluetooth.requestDevice({
        filters: [
          { namePrefix: AURA_BLE_CONFIG.DEVICE_NAME_PREFIX }
        ],
        optionalServices: [AURA_BLE_CONFIG.SERVICE_UUID]
      });

      if (!this.device) {
        throw new Error("Aucun appareil BLE sélectionné.");
      }

      this.device.addEventListener("gattserverdisconnected", () => {
        this.onDisconnected();
      });

      this.log(`Appareil trouvé : ${this.device.name || "nom inconnu"}`);

      this.log("Connexion GATT...");
      this.server = await this.device.gatt.connect();

      this.log("Récupération du service...");
      this.service = await this.server.getPrimaryService(AURA_BLE_CONFIG.SERVICE_UUID);

      this.log("Récupération de la caractéristique...");
      this.characteristic = await this.service.getCharacteristic(AURA_BLE_CONFIG.CHAR_UUID);

      this.isConnected = true;
      this.updateBleUI(true);
      this.log("Connecté avec succès.");
    } catch (err) {
      console.error("BLE connect error:", err);
      this.isConnected = false;
      this.updateBleUI(false);
      this.log("Erreur connexion: " + err.message);
    }
  }

  async disconnect() {
    try {
      if (this.device && this.device.gatt && this.device.gatt.connected) {
        this.device.gatt.disconnect();
      }
    } catch (err) {
      console.error("BLE disconnect error:", err);
    } finally {
      this.onDisconnected();
    }
  }

  onDisconnected() {
    this.isConnected = false;
    this.server = null;
    this.service = null;
    this.characteristic = null;
    this.updateBleUI(false);
    this.log("Déconnecté.");
  }

  async sendRoute(routeId, gpxText) {
    try {
      if (!this.characteristic || !this.isConnected) {
        throw new Error("Pas connecté à la caractéristique BLE.");
      }

      if (!gpxText || typeof gpxText !== "string") {
        throw new Error("GPX invalide.");
      }

      const encoder = new TextEncoder();
      const data = encoder.encode(gpxText);

      this.log(`Préparation envoi route ${routeId || "sans-id"}`);
      this.log(`Taille: ${data.length} octets`);

      this.updateRouteProgress(0);

      let offset = 0;

      while (offset < data.length) {
        const chunk = data.slice(offset, offset + AURA_BLE_CONFIG.CHUNK_SIZE);

        await this.characteristic.writeValue(chunk);

        offset += chunk.length;
        const percent = Math.floor((offset * 100) / data.length);
        this.updateRouteProgress(percent);

        const routeStatus = document.getElementById("route_status");
        if (routeStatus) {
          routeStatus.textContent = `Status : envoi... ${percent}% (${offset}/${data.length})`;
        }

        await this.delay(AURA_BLE_CONFIG.CHUNK_DELAY_MS);
      }

      this.log("Envoi du marqueur EOF...");
      await this.characteristic.writeValue(encoder.encode("EOF"));

      this.updateRouteProgress(100);
      this.log("GPX envoyé.");
    } catch (err) {
      console.error("BLE sendRoute error:", err);
      this.log("Erreur envoi: " + err.message);
      throw err;
    }
  }
}

const auraBLE = new AuraBLE();

document.addEventListener("DOMContentLoaded", () => {
  const btnConnect = document.getElementById("btn_ble_connect");
  const btnDisconnect = document.getElementById("btn_ble_disconnect");
  const btnConnect2 = document.getElementById("btn_ble_connect_2");
  const btnDisconnect2 = document.getElementById("btn_ble_disconnect_2");

  if (btnConnect) btnConnect.addEventListener("click", () => auraBLE.connect());
  if (btnDisconnect) btnDisconnect.addEventListener("click", () => auraBLE.disconnect());
  if (btnConnect2) btnConnect2.addEventListener("click", () => auraBLE.connect());
  if (btnDisconnect2) btnDisconnect2.addEventListener("click", () => auraBLE.disconnect());

  auraBLE.updateBleUI(false);
});

window.auraBLE = auraBLE;