/*************************************************
 * HB100 BLE Web Receiver
 * ESP32 (NimBLE) -> Web Bluetooth -> JS
 *************************************************/

import { initializeApp } from "https://www.gstatic.com/firebasejs/12.11.0/firebase-app.js";
import {
  getFirestore,
  collection,
  addDoc,
  serverTimestamp
} from "https://www.gstatic.com/firebasejs/12.11.0/firebase-firestore.js";

console.log("app.js loaded"); // Nähdään että App.js on ladattu

// ===== UUID:t eli tarkat tunnisteet ESP:lle  =====
const SERVICE_UUID = '00000001-0000-1000-8000-00805f9b34fb'; // HB100
const CHAR_UUID    = '00000002-0000-1000-8000-00805f9b34fb'; // Pallon nopeus notify

const firebaseConfig = {
  apiKey: "AIzaSyAMpIsInCMi_OeLk3KKLladZuR-DFGnQUo",
  authDomain: "hb100radar.firebaseapp.com",
  projectId: "hb100radar",
  storageBucket: "hb100radar.appspot.com",
  messagingSenderId: "190497361918",
  appId: "1:190497361918:web:b3fad99750190b9a18baaf"
};

const firebaseApp = initializeApp(firebaseConfig);
const db = getFirestore(firebaseApp);


// ===== UI-elementit =====
const connectBtn = document.getElementById('connect');  // HTML elementit
const speedEl    = document.getElementById('speed');
const statusEl   = document.getElementById('status');
const maxSpeedEl = document.getElementById('max-speed');
const resetBtn   = document.getElementById('reset');



async function saveSpeedToFirestore(speed) { // Firestoreen tallennus funktio, tallentaa yhden mittauksen
  try {
    if (!currentEventId) { // Tarkistetaan  että event id on olemassa 
      console.warn("No active event, skipping save"); // Estetään datan tallennus jos ei tiedetä mihin mittaus kuuluu
      return;
    }

    await addDoc(collection(db, "measurements"), { // Tallentaa measurements kokoelmaan tai se luodaan automaattisesti jos sitä ei ole
      event_id: currentEventId, // Tapahtuman id
      speed_kmh: speed, // Nopeus
      created_at: serverTimestamp() // Aikaleima
    });

    console.log("Saved to Firestore:", speed, "event", currentEventId); // Logi tallennuksen varmistamiseksi
  } catch (err) {
    console.error("Firestore save error:", err); // error logi
  }
}

function startNewEvent() { // Luo uuden lyönnin tunnisteen 
  currentEventId = crypto.randomUUID(); // Tekee uniikin tunnisteen 
  console.log("New event started:", currentEventId); // Logi 
  huippunopeus = null;
}

function resetMaxSpeed() {
  huippunopeus = null;
  maxSpeedEl.textContent = 'Max speed: -- km/h';
  startNewEvent();
  console.log("Max speed reset & new event started");
}

// ===== BLE-tila =====
let bleDevice = null;
let bleCharacteristic = null;


// ===== Event / lyöntitila =====
let currentEventId = null;
let huippunopeus = null;

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


  // Päivitä huippunopeus
  if (huippunopeus === null || speed > huippunopeus) {
    huippunopeus = speed;
    maxSpeedEl.textContent = 'Max speed: ' + huippunopeus.toFixed(2) + ' km/h';
  }



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
connectBtn.addEventListener('click', connectBLE);
resetBtn.addEventListener('click', resetMaxSpeed);