# Speed Radar Project

This project utilizes an HB100 Doppler radar to detect speeds. The system measures frequency shifts between 800 Hz and 3500 Hz—corresponding to speeds of 50 to 200 km/h—and streams the data to a real-time web interface.

## Features
* **Accurate Speed Detection:** Measures target speeds ranging from 50 km/h to 200 km/h.
* **Real-time Data:** Streams speed data instantly via Bluetooth Low Energy (NimBLE).
* **Wireless Web UI:** A browser-based interface that connects directly to the ESP32.
* **Cloud Logging:** Automatically saves measured speeds and timestamps to a Firebase Database for tracking and analysis.
* **Custom Hardware:** PCB designed for integrated ESP32-S3, HB100 sensor, and signal amplification.

## Hardware & Circuit

* ESP32-S3-WROOM1-N8R8
* HB100
* OPA2350

<p align="center">
  <img src="https://github.com/user-attachments/assets/b5c4189e-4d58-45c8-89a2-462b95a89837" alt="Working PCB" width="400"/>
  <br>
  <em>PCB Layot designed in KiCAD</em>
</p>

<p align="center">
  <img src="https://github.com/user-attachments/assets/dd95163b-00fb-45d2-9450-a6dd59393946" alt="PCB in KiCAD" width="400"/>
  <br>
  <em>The assembled and working PCB</em>
</p>

## Embedded Software

[![Espressif](https://img.shields.io/badge/ESP--IDF-E7352C?style=for-the-badge&logo=espressif&logoColor=white)](https://developer.espressif.com/tags/esp-idf/)

[![C](https://img.shields.io/badge/C-00599C?style=for-the-badge&logo=c&logoColor=white)](https://en.cppreference.com/w/c)

[![NimBLE](https://img.shields.io/badge/NimBLE-2C8EBB?style=for-the-badge&logo=bluetooth&logoColor=white)](https://h2zero.github.io/NimBLE-Arduino/)

## User Interface

[![HTML](https://img.shields.io/badge/HTML5-E34F26?style=for-the-badge&logo=html5&logoColor=white)](#)

[![CSS](https://img.shields.io/badge/CSS3-1572B6?style=for-the-badge&logo=css3&logoColor=white)](#)

[![JavaScript](https://img.shields.io/badge/JavaScript-F7DF1E?style=for-the-badge&logo=javascript&logoColor=black)](#)

[![Firebase](https://img.shields.io/badge/Firebase-FFCA28?style=for-the-badge&logo=firebase&logoColor=black)](https://firebase.google.com/)

## How It Works (Architecture)
1. The **HB100** radar outputs a very small Intermediate Frequency (IF) signal based on the Doppler shift.
2. The **OPA2350** op-amp amplifies and filters this signal.
3. The **ESP32-S3** reads the amplified signal via its ADC, processes the data in C to calculate the frequency using FFT (and thus speed), and acts as a BLE server using **NimBLE**.
4. The **Web UI** connects to the ESP32 via Web Bluetooth API, updating the speed dynamically on the screen.
5. The system pushes the recorded data to a **Firebase Database**, allowing data storage and historical data visualization.

## Acknowledgments
* [Vehicle Speed Measurement Using Doppler Effect (Thesis)](https://www.theseus.fi/bitstream/handle/10024/496044/Thesis.pdf?sequence=2)
