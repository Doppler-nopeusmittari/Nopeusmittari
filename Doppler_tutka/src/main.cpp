//arduinon työkalut
#include <Arduino.h>
//ESP-IDF puolelta tuleva ajuri, joka lukee tutkan signaalia taustalla ilman, että prosessorin tarvitsee tehdä mitään
#include "driver/i2s.h"

//Lisätään silta espidf ja arduino välille, koska ohjelma odottaa espidf mukaista aloitusta
extern "C" void app_main() {
    initArduino();
    setup();
    while (true) {
        loop();
    }
}
void setup() {

    Serial.begin(115200);
    Serial.println("Tutka on hereillä!");
}

void loop() {}