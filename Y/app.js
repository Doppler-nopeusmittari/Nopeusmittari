


/*********************************************************
 * HB100 BLE Web sovellus yhden ja kahden tutkan käytölle
 * Web Bluetooth + Firebase käsittely
 *********************************************************/


/* =================================================
   Firebase Apin määrittely = Tuodaan kirjastot yms
==================================================== */
import { initializeApp } from "https://www.gstatic.com/firebasejs/12.11.0/firebase-app.js";
import {
  getFirestore, // Yhteys tietokantaan
  collection, // Tietokantakokoelma
  addDoc, // Lisää uuden dokumentin
  serverTimestamp, // Luo ajan palvelimella
  query,
  where,
  getDocs,
  orderBy,
  limit
} from "https://www.gstatic.com/firebasejs/12.11.0/firebase-firestore.js";


const firebaseConfig = { // Konfiguraatio ja tunnistetiedot eli yhdistetään oikeaan tietokantaan
  apiKey: "YOUR_API_KEY",
  authDomain: "hb100radar.firebaseapp.com",
  projectId: "hb100radar",
  storageBucket: "hb100radar.appspot.com",
  messagingSenderId: "190497361918",
  appId: "1:190497361918:web:xxxxxxxxxxxxxxxx"
};

const firebaseApp = initializeApp(firebaseConfig); // Alustaa sovelluksen yllämainituilla tiedoilla
const db = getFirestore(firebaseApp); // Luo yhteyden tietokantaan

console.log("app.js loaded"); // debug että app.js on ladattu

/* ======================================================================================
   BLE UUIDs näiden avulla selain löytää ESP32 ja BLE palvelun nopeusdatakarakteristiikan
========================================================================================= */
const SERVICE_UUID = "00000001-0000-1000-8000-00805f9b34fb"; 
const CHAR_UUID    = "00000002-0000-1000-8000-00805f9b34fb";

/* ==============
   UI elementit
================= */
const connectRadarA = document.getElementById("connect-radarA"); // Radar A yhdistäminen
const connectRadarB  = document.getElementById("connect-radarB"); // Radar B yhdistäminen
const speedEl    = document.getElementById('nopeus');
const statusEl   = document.getElementById('status');
const maxSpeedEl = document.getElementById('max-nopeus');

// ===== UI-elementit (Kirjautuminen / Lempinimi) =====
const authBox = document.getElementById('auth-box');
const userInfoBox = document.getElementById('user-info-box');
const playerNameDisplay = document.getElementById('current-player-name');
const nicknameInput = document.getElementById('nickname-input');
const setNameBtn = document.getElementById('set-name-btn');
const logoutBtn = document.getElementById('logout-btn');

// ===== UI-elementit (Laatikot) =====
const sessioBox = document.getElementById('sessio');
const nopeinBox = document.getElementById('nopein');
const keskiarvoBox = document.getElementById('keskiarvo');

// ===== UI-elementit Statistiikka =====
const fastestTodayEl = document.getElementById("nopein-tänään");
const fastestWeekEl = document.getElementById("nopein-viikko");
const fastestAllEl = document.getElementById("nopein-koko-aika");

const avgTodayEl = document.getElementById("keskiarvo-tänään");
const avgWeekEl = document.getElementById("keskiarvo-viikko");
const avgAllEl = document.getElementById("keskiarvo-koko-aika");

const top50ListEl = document.getElementById("top50-list");
const showMoreBtn = document.getElementById("show-more-btn");

const startBtn = document.getElementById("start-session");
const stopBtn = document.getElementById("stop-session");
const sessionListEl = document.getElementById("session-list");

/* =================
   Sovelluksen tila
==================== */

// Monenko tutkan tila: "single" | "dual"
let measurementMode = "single";

let sessionActive = false;
let sessionSpeeds = [];
let sessionHitCounter = 0;

// ===== Event / lyöntitila =====
let currentEventId = null;
let currentPlayerName = null;

// BLE yhteyksien tila, tallentaa kummankin tutkan BLE laitteen ja nopeuskarakteristiikan
const radars = {
  radarA: { device: null, characteristic: null },
  radarB:  { device: null, characteristic: null }
};


/* ====================
   Avustavat funktiot
======================= */
function setStatus(text) { // Päivittää käyttöliittymän status tekstin
  statusEl.textContent = text;
}

function startNewEvent() { // Uuden tapahtuman aloitus aina kun uusi lyönti tai reset 
  currentEventId = crypto.randomUUID(); // Luo uniikin tunnisteen tapahtumalle
  
  lastAcceptedTime = {
    radarA: 0,
    radarB: 0
  };


  maxSpeedEl.textContent = "Max speed: -- km/h"; // Tyhjentää maksiminopeu denUI:ssa
  console.log("New event:", currentEventId); // Debug uudelle tapahtumalle
}

// ==========================================
// 1. KIRJAUTUMISEN HALLINTA (LocalStorage)
// ==========================================

function tarkistaKirjautuminen() {
  const tallennettuNimi = localStorage.getItem('tennisPlayerName'); // Hakee tallaennetun pelaajan nimen
  
  if (tallennettuNimi) {
    // Pelaaja löytyi muistista
    currentPlayerName = tallennettuNimi;
    playerNameDisplay.textContent = currentPlayerName;
    
    // Poistetaan hidden-luokka (tulevat näkyviin)
    authBox.classList.add('hidden'); // Kirjautumislaatikko piiloon
    userInfoBox.classList.remove('hidden');
    sessioBox.classList.remove('hidden');
    nopeinBox.classList.remove('hidden');
    keskiarvoBox.classList.remove('hidden');

    loadUserStats() // Lataa käyttäjän tiedot
    loadTop50(); // Lataa top50 kovimmat lyönnit tiedot sivulle

  } else {
    // Ei pelaajaa muistissa
    currentPlayerName = null;
    
    // Lisätään hidden-luokka (menevät piiloon)
    authBox.classList.remove('hidden'); // Kirjautumislaatikko esiin
    userInfoBox.classList.add('hidden');
    sessioBox.classList.add('hidden');
    nopeinBox.classList.add('hidden');
    keskiarvoBox.classList.add('hidden');

  }
}

// Aseta nimi
setNameBtn.addEventListener('click', () => {
  const nimi = nicknameInput.value.trim();
  if (nimi !== "") {
    localStorage.setItem('tennisPlayerName', nimi);
    tarkistaKirjautuminen(); // Päivittää käyttöliittymän
  } else {
    alert("Syötä lempinimi ensin!");
  }
});

// Vaihda pelaajaa (Uloskirjautuminen)
logoutBtn.addEventListener('click', () => {
  localStorage.removeItem('tennisPlayerName');
  nicknameInput.value = "";
  tarkistaKirjautuminen(); // Päivittää käyttöliittymän
});

// Suoritetaan heti kun sivu ladataan, jotta laatikot asettuvat oikein
tarkistaKirjautuminen();

/* ====================
   Firebase tallennus
======================= */
async function saveFinalResult(data) { //Vastaanottaa valmiin tuloksen tallennettavaksi
  try {
    await addDoc(collection(db, "events"), { // Lisää uuden dokumentin Firestore kokoelmaan "events"
      ...data, // Levittää datan
      event_id: currentEventId, // Lisää tapahtuma ID
      player_name: currentPlayerName,
      created_at: serverTimestamp() // Lisää aikaleiman
    });

    console.log("Saved event:", data); //Debug tallennukselle
  } catch (err) {
    console.error("Firestore error:", err); //Tallennuksen epäonnistuminen
  }
}

/* ========================
   Yhden tutkan käsittely
=========================== */
function handleSingleRadar(speed) { // Kutsutaan kun esp lähettää yhden tutkan nopeuden
  if (!sessionActive) return;
  console.log("Single radar speed:", speed); // Debug saadulle nopeudelle

  if (speed > 0) { // Vain oikeat yli 0 kmh lyönnit tallennetaan
    saveFinalResult({ // Firebaseen tallennus
      mode: "single",
      final_speed: speed
    });
  }

  sessionSpeeds.push(speed);

  sessionHitCounter++; // Listaa sen hetkisen session lyönnit

  const li = document.createElement("li"); // Määrittely että mitä haetaan databasesta
  li.textContent = `#${sessionHitCounter}: (${new Date().toLocaleTimeString()}): ${speed.toFixed(1)} km/h`;
  sessionListEl.appendChild(li);

}

/* ==================================================================================
  Kahden tutkan tila, joka säilyttää kummankin tutkan lähettämän lopullisen nopeuden.
  Nollataan jokaisen lyönnin jälkeen 
=====================================================================================*/
let dualShot = { 
  radarA: null,
  radarB: null
};

function handleDualRadar(speed, radarId) {   // Kutsutaan, kun jompikumpi tutka lähettää tuloksen
  console.log(`${radarId} speed:`, speed); // Debug: Kumpi tutka ja mikä nopeus?

  // Tallentaa nopeuden oikealle tutkalle
  if (speed > 0) {
    dualShot[radarId] = speed;
  }

  // Odottaa, että molemmat tutkat ovat antaneet arvon
  if (dualShot.radarA !== null && dualShot.radarB !== null) {
    const finalSpeed = Math.max( // Valitsee suurimman nopeuden
      dualShot.radarA,
      dualShot.radarB
    );

    saveFinalResult({ // Tallentaa suurimman nopeuden 
      mode: "dual",
      final_speed: finalSpeed
    });

    // Resetoi seuraavaa lyöntiä varten
    dualShot = { radarA: null, radarB: null };

    sessionSpeeds.push(finalSpeed);

    sessionHitCounter++; // Listaa sen hetkisen session lyönnit

    const li = document.createElement("li"); // Määritellään mitä haetaan databasesta
    li.textContent = `#${sessionHitCounter}: (${new Date().toLocaleTimeString()}): ${finalSpeed.toFixed(1)} km/h`;
    sessionListEl.appendChild(li);
  }
}

const HIT_COOLDOWN_MS = 200; // 200ms viive ettei tule tuplamittauksia
const MIN_VALID_SPEED = 30; // km/h nopeus minimissään

let lastAcceptedTime = { // Nollaa aikasuodattimet, jotta vanha piikki ei vaikuta uuteen lyöntiin
  radarA: 0,
  radarB: 0
};

/* ==================
   BLE Yhdistäminen
===================== */
async function connectRadar(radarId) { // Funktio molempien tutkien yhdistämiseen
  try {
    setStatus(`Yhdistetään ${radarId} ...`); // Päivittää UI:n kertomaan yhdistämisestä

    const device = await navigator.bluetooth.requestDevice({ // Avaa selaimen laitteenvalinnan
      acceptAllDevices: true,
      optionalServices: [SERVICE_UUID]
    });

    const server = await device.gatt.connect(); // Yhdistää laitteeseen
    const service = await server.getPrimaryService(SERVICE_UUID); // Hakee oikean BLE palvelun
    const characteristic = await service.getCharacteristic(CHAR_UUID); // Hakee oikean karakteristiikan

    await characteristic.startNotifications(); // Aloittaa BLE notifikaatiot


/*=======================
    BLE datan käsittely
=========================*/
characteristic.addEventListener( // Ajetaan aina kun ESP lähettää datapaketin selaimelle
  "characteristicvaluechanged",
  e => {
    const now = Date.now(); // Ajankohta millisekunteina

    const speed = e.target.value.getFloat32(0, true); // BLE datapuskuri lukee 32bit liukulukuja

    
    if (speed < MIN_VALID_SPEED) return; // Jos nopeus on alle sallitun minimin niin funktiosta poistutaan heti 

    
    if (now - lastAcceptedTime[radarId] < HIT_COOLDOWN_MS) return; // Aikasuodatus jolla estetään tuplamittaukset

    lastAcceptedTime[radarId] = now; // Tallentaa hyväksytyn mittauksen ajan

    
    if (!sessionActive) return; // Estää mittaukset jos ei olla painettu aloita painiketta

    console.log(`${radarId} ACCEPTED speed:`, speed); // Debuggaus tuloste

    speedEl.textContent = speed.toFixed(2) + " km/h"; // Näyttää nopeuden 2 desimaalin tarkkuudella

    if (measurementMode === "single") { // Valitsee mittaustavan
      handleSingleRadar(speed); // Yhdellä tutkalla nopeuas tallennetaan suoraa
    } else {
      handleDualRadar(speed, radarId); // Kahdella tutkalla valitaan suurempi mittaus
    }
  }
);

/*===========================
  Yhdistetyn tutkan tallennus
=============================*/
    radars[radarId] = { device, characteristic }; // Tallentaa tutkan tilan muistiin

    if (!currentEventId) startNewEvent(); // Jos ei ole aktiivista tapahtumaa aloitetaan uusi

    
// Jos molemmat tutkat ovat yhditetty niin silloin ollaan "dual" tilassa
    if (radars.radarA.device && radars.radarB.device) {
      measurementMode = "dual";
}


    setStatus(`${radarId} Tutka yhdistetty`); // Päivittää UI:n
  } catch (err) {
    console.error(err);
    setStatus("BLE error");
  }
}

/*==========================================
  Statistiikan käsittely (TOP50 lyönnit yms)
============================================*/

function startOfToday() { // PÄivän kovimmat lyönnit
  const d = new Date();
  d.setHours(0, 0, 0, 0);
  return d;
}

function startOfWeek() { // Viikon kovimmat lyönnit
  const d = new Date();
  const day = d.getDay() || 7; // maanantai = 1
  d.setDate(d.getDate() - day + 1);
  d.setHours(0, 0, 0, 0);
  return d;
}

function maxOf(arr) { // Tekee taulukon josta palautetaan suurin arvo
  return arr.length ? Math.max(...arr) : null;
}

function avgOf(arr) { // Laskee keskiarvon
  if (!arr.length) return null;
  return arr.reduce((a, b) => a + b, 0) / arr.length;
}

async function loadUserStats() { // Käyttäjän omien tietojen lataus tietokannasta
  if (!currentPlayerName) return;

  const snap = await getDocs( // Hakee max 500 viimeisintä lyöntiä
    query(
      collection(db, "events"),
      where("player_name", "==", currentPlayerName),
      orderBy("created_at", "desc"),
      limit(500)
    )
  );

  const todayStart = startOfToday(); // Rajat ajalle päivä
  const weekStart = startOfWeek(); // viikko


  const all = []; // Kaikki tulokset
  const today = []; // Päivän tulokset
  const week = []; // Viikon tulokset

  snap.forEach(doc => { 
    const data = doc.data(); // Haetaan data 
    if (!data.created_at) return;

    const t = data.created_at.toDate(); // muunnetaan firestore  timestamp JS dateksi
    all.push(data.final_speed); // Lisätään data aina all

    if (t >= todayStart) today.push(data.final_speed); // Jos tänään niin lisä tään tämän päivän
    if (t >= weekStart) week.push(data.final_speed); // Ja jos viikon niin lisätään viikon
  });

  // Päivitä UI, jos dataa niin näytä nopeus, jos ei niin --
  fastestTodayEl.textContent = maxOf(today)?.toFixed(1) ?? "--";
  fastestWeekEl.textContent = maxOf(week)?.toFixed(1) ?? "--";
  fastestAllEl.textContent = maxOf(all)?.toFixed(1) ?? "--";

  avgTodayEl.textContent = avgOf(today)?.toFixed(1) ?? "--";
  avgWeekEl.textContent = avgOf(week)?.toFixed(1) ?? "--";
  avgAllEl.textContent = avgOf(all)?.toFixed(1) ?? "--";
}

async function loadTop50() { // Top 50 kaikkien koviummat lyönnit
  top50ListEl.innerHTML = "<li>Ladataan...</li>";

  try {
    const snap = await getDocs( // Hakee 50 kovinta lyöntiä tietokannasta
      query(
        collection(db, "events"),
        orderBy("final_speed", "desc"),
        limit(50)
      )
    );

    top50ListEl.innerHTML = "";

    let rank = 1; // Numerointi alkaa ykkösestä

    snap.forEach(doc => { 
      const data = doc.data(); // Käy tulokset läpi


      const date = data.created_at?.toDate(); // Muuttaa firestore timestampin  päivämääräksi
      const dateStr = date
        ? date.toLocaleDateString("fi-FI") // Suomalaiseen muotoon
        : "–";

      const li = document.createElement("li"); // Listarivin luonti
      li.textContent =
        `${rank}. ${data.player_name} – ` +
        `${data.final_speed.toFixed(1)} km/h ` +
        `(${dateStr})`;

      top50ListEl.appendChild(li);
      rank++;
    });

    if (rank === 1) { // Tyhjän listan käsittely
      top50ListEl.innerHTML = "<li>Ei vielä tuloksia</li>";
    }

  } catch (err) { // Debug
    console.error("Top50 error:", err);
    top50ListEl.innerHTML = "<li>Virhe ladattaessa</li>";
  }
}

/* ==============
  UI Tapahtumat
================= */
connectRadarA.addEventListener("click", () => { // Tutka A:n yhdistämisen painike ja "single" tila
  measurementMode = "single";
  connectRadar("radarA");
});

connectRadarB.addEventListener("click", () => { // Tutka B:n yhdistämisen painike ja "dual" tila
  measurementMode = "dual";
  connectRadar("radarB");
});

startBtn.addEventListener("click", () => { // Mittauksen aloitusnappi
  sessionActive = true;
  sessionSpeeds = [];
  sessionHitCounter = 0; 

  sessionListEl.innerHTML = "";

  startBtn.disabled = true;
  stopBtn.disabled = false;

  startNewEvent();
  console.log("Session started");
});

stopBtn.addEventListener("click", () => { // Mittauksen lopetusnappi
  sessionActive = false;
  sessionSpeeds = [];
  sessionHitCounter = 0; // NOLLAUS

  sessionListEl.innerHTML = "";

  startBtn.disabled = false;
  stopBtn.disabled = true;

  loadUserStats();

  console.log("Session stopped");
});
