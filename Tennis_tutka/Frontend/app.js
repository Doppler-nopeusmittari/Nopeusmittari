/*************************************************
 * HB100 BLE Web Receiver
 * ESP32 (NimBLE) -> Web Bluetooth -> JS
 *************************************************/

import { // Tuodaan Firestore -kirjastosta tarvittavat toiminnot
  collection, //Firestore kokoelma
  addDoc, // Lisää uuden dokumentin
  serverTimestamp // Luo aikaleiman Firebase palvelimella
} from "https://www.gstatic.com/firebasejs/12.11.0/firebase-firestore.js";

console.log("app.js loaded"); // Nähdään että App.js on ladattu

// ===== UUID:t eli tarkat tunnisteet ESP:lle  =====
const SERVICE_UUID = '00000001-0000-1000-8000-00805f9b34fb'; // HB100
const CHAR_UUID    = '00000002-0000-1000-8000-00805f9b34fb'; // Pallon nopeus notify

// ===== UI-elementit =====
const connectBtn = document.getElementById('connect');  // HTML elementit
const speedEl    = document.getElementById('speed');
const statusEl   = document.getElementById('status');

async function saveSpeedToFirestore(speed) { // Firestoreen tallennus funktio
  try {
    if (!currentEventId) {
      concole.warn("No active event, skipping save");
      return;
    }

    await addDoc(collection(window.db, "measurements"), { // Tallentaa measurements kokoelmaan
      event_id: currentEventId,
      speed_kmh: speed,
      created_at: serverTimestamp()
    });

    console.log("Saved to Firestore:", speed, "event", currentEventId);
  } catch (err) {
    console.error("Firestore save error:", err);
  }
}

function startNewEvent() {
  currentEventId = crypto.randomUUID();
  console.log("New event started:", currentEventId);
}


// ===== BLE-tila =====
let bleDevice = null;
let bleCharacteristic = null;


// ===== Event / lyöntitila =====
let currentEventId = null;


// ==========================================
// Yhdistä BLE-laitteeseen
// ==========================================
async function connectBLE() {
  try {
    setStatus('Opening Bluetooth chooser...');

    // 1. Pyydä käyttäjää valitsemaan laite
    bleDevice = await navigator.bluetooth.requestDevice({
      acceptAllDevices: true,
      optionalServices: [SERVICE_UUID]
    });

    bleDevice.addEventListener('gattserverdisconnected', onDisconnected);

    setStatus('Connecting...');
    const server = await bleDevice.gatt.connect();

    // 2. Hae GATT service & characteristic
    const service = await server.getPrimaryService(SERVICE_UUID);
    bleCharacteristic = await service.getCharacteristic(CHAR_UUID);

    // 3. Tilaa notificationit
    await bleCharacteristic.startNotifications();

    bleCharacteristic.addEventListener(
      'characteristicvaluechanged',
      onSpeedNotification
    );

    startNewEvent();
    setStatus('Connected ✅');
  } catch (err) {
    console.error(err);
    setStatus('Bluetooth error');
  }
}

// ==========================================
// Vastaanota BLE Notifiointi (speed)
// ==========================================
function onSpeedNotification(event) {
  const dataView = event.target.value;

  // ESP32 lähettää: float (32-bit, little endian)
  const speed = dataView.getFloat32(0, true);

  // Näytä UI:ssa
  speedEl.textContent = speed.toFixed(2) + ' km/h';

  console.log('Speed received:', speed);

  // Tässä kohtaa data voidaan tallentaa Firebaseen
  // saveSpeedToFirebase(speed);

  saveSpeedToFirestore(speed)
}

// ==========================================
// Käsittele BLE-disconnect
// ==========================================
function onDisconnected() {
  setStatus('Disconnected');
  bleCharacteristic = null;
}

// ==========================================
// UI-apurit
// ==========================================
function setStatus(text) {
  if (statusEl) {
    statusEl.textContent = text;
  }
}

// ==========================================
// Event binding
// ==========================================
connect.addEventListener('click', connectBLE);