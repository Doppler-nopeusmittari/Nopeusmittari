#include <Arduino.h>
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "arduinoFFT.h"

// --- 1. FFT:n asetukset ja muuttujat ---
const uint16_t samples = 512;               // Näytteiden määrä (pakko olla 2:n potenssi)
const double samplingFrequency = 20000.0;   // 20 kHz näytteenotto
double vReal[samples];                      // Taulukko mitatuille jännitteille
double vImag[samples];                      // Taulukko imaginaariarvoille (nollia)

ArduinoFFT<double> FFT = ArduinoFFT<double>(vReal, vImag, samples, samplingFrequency);

// --- 2. ADC- ja kalibrointikahvat ---
adc_continuous_handle_t adc_handle = NULL;
adc_cali_handle_t cali_handle = NULL;

void setup() {
    Serial.begin(115200);

    // --- 3. DMA-puskurien alustus ---
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 4096, // Varattu puskuritila
        .conv_frame_size = 2048,    // yhden puskurin koko
    };
    adc_continuous_new_handle(&adc_config, &adc_handle);

    // --- 4. ADC-kanavan asetukset ---
    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = 20000,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    adc_digi_pattern_config_t adc_pattern[1] = {
        {
            .atten = ADC_ATTEN_DB_12,     // Jännitealue n. 0-3.3V
            .channel = ADC_CHANNEL_3,     // GPIO4
            .unit = ADC_UNIT_1,
            .bit_width = ADC_BITWIDTH_12,
        }
    };
    dig_cfg.pattern_num = 1;
    dig_cfg.adc_pattern = adc_pattern;
    adc_continuous_config(adc_handle, &dig_cfg);

    // --- 5. Laitteistotason kalibroinnin alustus ---
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);

    // --- 6. Käynnistetään jatkuva mittaus ---
    adc_continuous_start(adc_handle);
    Serial.println("ADC DMA ALUSTETTU");
}

void loop() {
    uint8_t tulos_puskuri[2048];
    uint32_t luettu_maara = 0;

    // Yritetään lukea puskuri (viimeinen 0 = ei jäädä odottamaan, jos data ei ole valmista)
    esp_err_t ret = adc_continuous_read(adc_handle, tulos_puskuri, 2048, &luettu_maara, 0);

    // Jos dataa saatiin ja puskuri on täysi
    if (ret == ESP_OK && luettu_maara == 2048) {
        
        int sample_idx = 0; // Seuraa monesko näyte (0-511) tallennetaan FFT-taulukkoon

        // --- 7. Datan purku ja kalibrointi ---
        for (int i = 0; i < luettu_maara; i += sizeof(adc_digi_output_data_t)) {
            adc_digi_output_data_t *p = (adc_digi_output_data_t*)&tulos_puskuri[i];
            uint16_t raaka_arvo = p->type2.data;
            
            int jannite_mv = 0;
            adc_cali_raw_to_voltage(cali_handle, raaka_arvo, &jannite_mv);
            
            vReal[sample_idx] = (double)jannite_mv;
            vImag[sample_idx] = 0.0; 
            sample_idx++;
        }
        double summa = 0;
        for (int i = 0; i < samples; i++) {
            summa += vReal[i];
        }
        double keskiarvo = summa / samples;
        for (int i = 0; i < samples; i++) {
            vReal[i] -= keskiarvo;
        }

        // --- 8. Suoritetaan FFT-analyysi ---
        FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD); // Ikkunointi
        FFT.compute(FFT_FORWARD);                        // Laskenta
        FFT.complexToMagnitude();                        // Amplitudiksi

        // --- 9. Etsitään taajuus ja lasketaan nopeus ---
        double huippu = FFT.majorPeak();           // Doppler-siirtymä (Hz)
        double nopeus_kmh = huippu / 19.49;        // Muunnos km/h

        // --- 10. Suodatetaan kohina ja tulostetaan ---
        if (nopeus_kmh > 3.0) {
            Serial.printf("Doppler: %.2f Hz | Pallon nopeus: %.2f km/h\n", huippu, nopeus_kmh);
        }
    }
    
    // Pieni tauko antaa prosessorille aikaa hengittää (esim. WiFi/Bluetooth-taustatehtäviin)
    delay(5); 
}