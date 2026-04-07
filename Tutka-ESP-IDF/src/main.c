#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_continuous.h" // Tarvitaan jatkuvalle ADC:lle
#include "esp_dsp.h"
#include "dsps_fft2r.h"
#include "dsps_math.h"
#include "dsps_wind.h"

static const char *TAG = "FFT_ADC";

// --- 1. FFT:n asetukset ja muuttujat (Määritelty globaalisti) ---
const uint16_t SAMPLES = 512;               // Näytteiden määrä (pakko olla 2:n potenssi)
const float SAMPLING_FREQ = 20000.0;        // 20 kHz näytteenotto

// esp-dsp vaatii lomitetun taulukon: Koko on SAMPLES * 2 (1024)
float y_cf[1024];                    
float wind_array[512];                      // Taulukko ikkunointifunktiolle

// --- 2. ADC- ja kalibrointikahvat ---
adc_continuous_handle_t adc_handle = NULL;
adc_cali_handle_t cali_handle = NULL;

void init_adc_dma(){
    // --- 4. DMA-puskurien alustus ---
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 4096, // Varattu puskuritila
        .conv_frame_size = 2048,    // Yhden puskurin koko
    };
    adc_continuous_new_handle(&adc_config, &adc_handle);

    // --- 5. ADC-kanavan asetukset ---
    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = (uint32_t)SAMPLING_FREQ,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };
    adc_digi_pattern_config_t adc_pattern[1] = {
        {
            .atten = ADC_ATTEN_DB_12,     // Jännitealue n. 0-3.3V
            .channel = ADC_CHANNEL_3,     // GPIO4 (ADC1_CH3)
            .unit = ADC_UNIT_1,
            .bit_width = ADC_BITWIDTH_12,
        }
    };
    dig_cfg.pattern_num = 1; 
    dig_cfg.adc_pattern = adc_pattern;
    adc_continuous_config(adc_handle, &dig_cfg);

    // --- 6. Laitteistotason kalibroinnin alustus ---
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_cali_create_scheme_curve_fitting(&cali_config, &cali_handle);
    printf("ADC alustettu");
}

void app_main() {
    esp_log_level_set("*", ESP_LOG_NONE);
    dsps_fft2r_init_fc32(NULL, 4096); 
    dsps_wind_hann_f32(wind_array, SAMPLES);
    init_adc_dma();   
    adc_continuous_start(adc_handle);

    // Muuttujat lukuun
    uint8_t result_buffer[2048]; // 512 näytettä * 4 tavua
    uint32_t out_len = 0;

    printf("Tutka käynnistetty...");

    // --- Ikuinen silmukka ---
    while (1) {
        // 1. Luetaan dataa ADC:ltä (odotetaan kunnes puskuri täynnä)
        esp_err_t ret = adc_continuous_read(adc_handle, result_buffer, 2048, &out_len, portMAX_DELAY);

        if (ret == ESP_OK) {
            // 2. Siirretään raakadata y_cf-taulukkoon ja kalibroidaan
            for (int i = 0; i < out_len; i += 4) {
                adc_digi_output_data_t *p = (adc_digi_output_data_t *)&result_buffer[i];
                int voltage_mv = 0;
                adc_cali_raw_to_voltage(cali_handle, p->type2.data, &voltage_mv);

                int idx = i / 4;
                y_cf[idx * 2] = ((float)voltage_mv / 1000.0f) * wind_array[idx]; // Reaali + Ikkunointi
                y_cf[idx * 2 + 1] = 0; // Imaginaari
            }

            // 3. FFT-Pipeline
            dsps_fft2r_fc32(y_cf, SAMPLES);
            dsps_bit_rev_fc32(y_cf, SAMPLES);
            dsps_cplx2reC_fc32(y_cf, SAMPLES);

            // 4. Nopeuden etsintä
            float max_magnitude = 0.0;
            int peak_index = 0;

            for (int i = 1; i < SAMPLES / 2; i++) {
                if (y_cf[i] > max_magnitude) {
                    max_magnitude = y_cf[i];
                    peak_index = i;
                }
            }

            float nopeus_kmh = 0.0;
            const float KOHINAKYNNYS = 2.0;

            if (max_magnitude > KOHINAKYNNYS) {
                float taajuus_hz = (float)peak_index * (SAMPLING_FREQ / SAMPLES);
                nopeus_kmh = taajuus_hz / 19.49f;
                printf("Nopeus: %.1f km/h (Voimakkuus: %.2f)\n", nopeus_kmh, max_magnitude);
                
            }
        }
        
        // Pieni viive, jotta watchdog-ajastin ei suutu (FreeRTOS vaatimus)
        vTaskDelay(pdMS_TO_TICKS(10)); 
    }
}

