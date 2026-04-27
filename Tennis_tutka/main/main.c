#include <stdio.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "dsps_fft2r.h"
#include "ble_hb100.h"
#include "nvs_flash.h"

// --- GLOBAALIT ---
adc_continuous_handle_t tutka_adc_kahva = NULL;
adc_cali_handle_t kalibrointi_kahva = NULL;

uint8_t tulopuskuri[4096];
float fft_taulukko[2048];

int16_t nayteMaara = 1024;
int16_t naytteNopeus = 20000; // SUOSITUS

// --- ADC INIT ---
void alusta_tutka() {

    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = 8192,
        .conv_frame_size = 4096,
    };

    adc_continuous_new_handle(&adc_config, &tutka_adc_kahva);

    adc_continuous_config_t dig_cfg = {
        .sample_freq_hz = naytteNopeus,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };

    adc_digi_pattern_config_t adc_pattern[1] = {
        {
            .atten = ADC_ATTEN_DB_12,
            .channel = ADC_CHANNEL_3, // GPIO4
            .unit = ADC_UNIT_1,
            .bit_width = ADC_BITWIDTH_12,
        }
    };

    dig_cfg.pattern_num = 1;
    dig_cfg.adc_pattern = adc_pattern;

    adc_continuous_config(tutka_adc_kahva, &dig_cfg);

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    adc_cali_create_scheme_curve_fitting(&cali_config, &kalibrointi_kahva);

    printf("Tutka alustettu\n");
}

// --- FFT + NOPEUS ---
float nopeudenlasku(float *fft_taulukko) {

    // 1. DC OFFSET POIS
    float keskiarvo = 0.0f;
    for (int i = 0; i < nayteMaara; i++) {
        keskiarvo += fft_taulukko[2 * i];
    }
    keskiarvo /= nayteMaara;

    for (int i = 0; i < nayteMaara; i++) {
        fft_taulukko[2 * i] -= keskiarvo;
    }

    // 2. HIGH PASS
    float prev = fft_taulukko[0];
    for (int i = 1; i < nayteMaara; i++) {
        float curr = fft_taulukko[2 * i];
        fft_taulukko[2 * i] = curr - prev;
        prev = curr;
    }

    // 3. HANNING WINDOW
    for (int i = 0; i < nayteMaara; i++) {
        float w = 0.5f * (1.0f - cosf(2.0f * M_PI * i / (nayteMaara - 1)));
        fft_taulukko[2 * i] *= w;
    }

    // 4. FFT
    dsps_fft2r_fc32(fft_taulukko, nayteMaara);
    dsps_bit_rev_fc32(fft_taulukko, nayteMaara);
    dsps_cplx2reC_fc32(fft_taulukko, nayteMaara);

    float resoluutio_hz = (float)naytteNopeus / (float)nayteMaara;

    // 5. TAAJUUSRAJAT
    int min_indeksi = (int)(800.0f / resoluutio_hz);
    int max_indeksi = (int)(3500.0f / resoluutio_hz);

    float kohinan_summa = 0.0f;
    float paras_arvo = 0.0f;
    int paras_indeksi = -1;

    int bin_maara = max_indeksi - min_indeksi + 1;

    for (int i = min_indeksi; i <= max_indeksi; i++) {

        float re = fft_taulukko[2 * i];
        float im = fft_taulukko[2 * i + 1];
        float mag = sqrtf(re * re + im * im);

        kohinan_summa += mag;

        if (mag > paras_arvo) {
            paras_arvo = mag;
            paras_indeksi = i;
        }
    }

    float kohinataso = kohinan_summa / bin_maara;

    //  HAAMUSUODATUS 
    if (paras_arvo < kohinataso * 3.5f) {
        return 0.0f;
    }

    float kynnysarvo = kohinataso * 4.0f;
    if (kynnysarvo < 15.0f) kynnysarvo = 15.0f;

    printf("Noise: %.1f | Th: %.1f | Peak: %.1f\n",
        kohinataso, kynnysarvo, paras_arvo);

    if (paras_arvo < kynnysarvo || paras_indeksi == -1) {
        return 0.0f;
    }

    float taajuus = paras_indeksi * resoluutio_hz;

    printf(">>> HIT: %.1f Hz\n", taajuus);

    float nopeus_kmh = taajuus / 19.49f;

    printf("DEBUG | noise: %.1f | peak: %.1f | speed: %.1f km/h\n",
       kohinataso,
       paras_arvo,
       nopeus_kmh);

    return nopeus_kmh;
    }

// --- MAIN ---
void app_main(void) {

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    dsps_fft2r_init_fc32(NULL, nayteMaara);

    alusta_tutka();
    adc_continuous_start(tutka_adc_kahva);
    ble_hb100_init();

    int perakkaiset_osumat = 0;
    float lennon_huippunopeus = 0.0;
    bool pallo_lennossa = false;

    const int VAADITUT_OSUMAT = 2;

    while (1) {

        uint32_t luettu_maara = 0;

        esp_err_t tila = adc_continuous_read(
            tutka_adc_kahva,
            tulopuskuri,
            4096,
            &luettu_maara,
            portMAX_DELAY
        );

        if (tila == ESP_OK) {

            int indeksi = 0;

            for (int j = 0; j < luettu_maara && indeksi < nayteMaara; j += 4) {

                adc_digi_output_data_t *kirjekuori = (adc_digi_output_data_t *)&tulopuskuri[j];

                uint32_t raaka_arvo = kirjekuori->type2.data;

                int jannite_mv = 0;
                adc_cali_raw_to_voltage(kalibrointi_kahva, raaka_arvo, &jannite_mv);

                fft_taulukko[indeksi * 2] = (float)jannite_mv;
                fft_taulukko[indeksi * 2 + 1] = 0.0f;

                indeksi++;
            }

            float nykyinen_nopeus = nopeudenlasku(fft_taulukko);

            // suodata järjettömät
            if (nykyinen_nopeus > 300.0f) nykyinen_nopeus = 0.0f;

            if (nykyinen_nopeus >= 15.0f) {

                pallo_lennossa = true;
                perakkaiset_osumat++;

                if (nykyinen_nopeus > lennon_huippunopeus) {
                    lennon_huippunopeus = nykyinen_nopeus;
                }

                printf("Tracking: %.1f km/h (%d)\n",
                       nykyinen_nopeus,
                       perakkaiset_osumat);

            } else {

                if (pallo_lennossa) {

                    if (perakkaiset_osumat >= VAADITUT_OSUMAT) {

                        printf("\n>>> VALID HIT <<<\n");
                        printf("Speed: %.2f km/h\n\n", lennon_huippunopeus);
                        printf("BLE SEND: %.2f km/h\n", lennon_huippunopeus);

                        ble_notify_ball_speed(lennon_huippunopeus);

                    } else {
                        printf("Rejected noise\n");
                    }

                    pallo_lennossa = false;
                    perakkaiset_osumat = 0;
                    lennon_huippunopeus = 0.0f;
                }
            }

        } else {
            printf("ADC virhe: %d\n", tila);
        }
    }
}