# Speed Radar Project

This project utilizes an HB100 Doppler radar to detect speeds. The system measures frequency shifts between 800 Hz and 3500 Hz—corresponding to speeds of 50 to 200 km/h—and streams the data to a real-time web interface.

## Hardware & Circuit
* ESP32-S3-WROOM1-N8R8
* HB100
* OPA2350

![PCB in KiCA](https://github.com/user-attachments/assets/b5c4189e-4d58-45c8-89a2-462b95a89837)
*PCB layout designed in KiCAD*

![Working PCB](https://github.com/user-attachments/assets/dd95163b-00fb-45d2-9450-a6dd59393946)
*The assembled and working PCB*

## Embedded Software
* [ESP-IDF](https://developer.espressif.com/tags/esp-idf/)
* C
* [NimBLE](https://h2zero.github.io/NimBLE-Arduino/)

## User Interface
* HTML
* CSS
* JavaScript

## Acknowledgments
* [Vehicle Speed Measurement Using Doppler Effect (Thesis)](https://www.theseus.fi/bitstream/handle/10024/496044/Thesis.pdf?sequence=2)
