#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_adc/adc_continuous.h" //Tarvitaan nopeaan ADC-lukuun
#include "esp_adc/adc_cali.h" //Tarvitaan kalibrointiin
#include "esp_adc/adc_cali_scheme.h"
#include "dsps_fft2r.h" // Espressifin laitteistokiihdytetty dsp kirjasto
#include "ble_hb100.h"
#include "nvs_flash.h"

//Globaali "kahva", josta voidaan tarttua ADC "apulaiseen"
adc_continuous_handle_t tutka_adc_kahva = NULL;
//Globaali "kahva" kalibroinnille
adc_cali_handle_t kalibrointi_kahva = NULL;

//Taulukko, johon "apulainen" tuo keräämänsä datan (koko aiemmin määritelty 2048 tavua)
uint8_t tulopuskuri[4096]; //2048 pieniä, yhden tavun kokoisia muistilokeroita
//Taulukko, johon for silmukassa puhdistetut jännitearvot tallennetaan FFT varten
float fft_taulukko[2048];
float nopeuksia[10];
int16_t nayteMaara = 1024;
int16_t naytteNopeus = 20000;

//Funktio, joka palkkaa ja opettaa apulaisen
void alusta_tutka() {
    //Määritellään laatikoiden koot
    adc_continuous_handle_cfg_t adc_config = {

        .max_store_buf_size = 8192, //Koko varaston koko tavuina
        .conv_frame_size = 4096, //Yhden kerralla toimitettavan laatikon koko tavuina
    };

    adc_continuous_new_handle(&adc_config, &tutka_adc_kahva);

    //Määritetään näytteenoton nopeus ja tarkkuus
    adc_continuous_config_t dig_cfg = {

        .sample_freq_hz = naytteNopeus,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
    };

    //Määritetään mistä pinnistä signaali luetaan
    adc_digi_pattern_config_t adc_pattern[1] = {
        {
            .atten = ADC_ATTEN_DB_12, // Sallii 0-3.3V jännitteet 
            .channel = ADC_CHANNEL_3, // Vastaa pinniä GPIO 4
            .unit = ADC_UNIT_1,
            .bit_width = ADC_BITWIDTH_12,
        }
    };
    dig_cfg.pattern_num = 1;
    dig_cfg.adc_pattern = adc_pattern;

    adc_continuous_config(tutka_adc_kahva, &dig_cfg);
    printf("Tutkan DMA määritetty\n");

    //Koulutetaan kalibrointi muuttamaan raakadata jännitteeksi
    adc_cali_curve_fitting_config_t cali_config = {

        .unit_id = ADC_UNIT_1,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };
    adc_cali_create_scheme_curve_fitting(&cali_config, &kalibrointi_kahva);
}

float nopeudenlasku(float *fft_taulukko){
    dsps_fft2r_fc32(fft_taulukko, nayteMaara);
    dsps_bit_rev_fc32(fft_taulukko, nayteMaara); // Järjestää tulokset oikeaan suuruusjärjestykseen
    dsps_cplx2reC_fc32(fft_taulukko, nayteMaara); //Muuttaa tulokset ymmärrettäväksi voimakkuudeksi

    //Tässä vaiheessa taulukko sisältää pelkkiä taajuuksien voimaakkuuksia. Haluamme etsiä tästä 512
    //numeron listasta suurimman luvun (tämä kuvaa tennispallon aiheuttamaa voimakasta piikkiä)
    //Otetaan "tyhjä muistilappu", johon kirjoitetaan aluksi suurimmaksi luvuksi 0
    //Sitten käydään FFT taulukon tulokset läpi yksi kerrallaan. Jos löytyy suurempi luku kuin muistilapulla,
    //pyyhitään vanha pois ja kirjoitetaan uusi tilalle. Otetaan myös talteen millä paikalla(missä indeksissä)
    //tuo luku taulukossa sijaitsi

    float suurin_voimakkuus = 0.0;
    int paras_indeksi = 0;

    //Käydään tulokset läpi. Aloitetaan indeksistä 1, koska 0 on taustahälyä
    //Tutkitaan vain puolet näytteistä (512/2 = 256), koska FFT matematiikka tuottaa tuloksesta aina peilikuvan.
    for (int i = 1; i < nayteMaara/2; i++) {
        if (fft_taulukko[i] > suurin_voimakkuus) {
            suurin_voimakkuus = fft_taulukko[i];
            paras_indeksi = i;

        }
    }
    //TAAJUUDESTA NOPEUDEKSI
    //näytteenottotaajuus on 20 000Hz ja näytemäärä 512. Tämä tarkoittaa, että laitteen kuuntelema 20 000Hz
    //on jaettu tastan 512 erilliseen taajuuslokeroon. Yksi lokero on siis (20000/512=39,0625Hz).
    //Tämä luku on siis tutkan tarkkuus eli resoluutio. Jokainen lokero FFT taulukossa edustaa siis noin 39Hz
    //askelta. Lokero 1 on 39Hz, Lokero 2 78Hz...

    //Määritellään nopeus aluksi nollaksi jos mikään mitattu taajuus ei ylitä kynnysarvoa palautettaan 0 km/h
    float nopeus_kmh = 0.0;         
    if(suurin_voimakkuus > 25000.0){
        //Muutetaan indeksi taajuudeksi ja taajuus nopeudeksi
        //Saimme edellisessä osiossa voimaikkaimman signaalin lokeron (paras_indeksi). Voimme käyttää tätä taajuuden saamiseksi
        float resoluutio_hz = (float)naytteNopeus / (float)nayteMaara;
        float taajuus_hz = (float)paras_indeksi * resoluutio_hz;
        //Muunnetaan taajuus nopeudeksi tutkakohtaisella vakiolla 19.49. Tämä tarkoittaa, että 19,49Hz vastaa 1km/h
        //vakio saadaan kaavalla Fd = 2*nopeus*tutkan taajuus/valonnopeus. Jos käytetään nopeutena 1 m/s ja tutkan taajuutta 10,525GHz
        //saadaan tulos 2*1*10525000000/300000000 = 70,16Hz. Tämä muutetaan km/h jakamalla tämä 3,6 = 19,488...
        nopeus_kmh = taajuus_hz / 19.49;
        //printf("Suurin voimakkuus: %.2f\n", suurin_voimakkuus);
        //Tulostetaan tulos yhden desimaalin tarkkuudella
    }
    /*
    for (int i = 2; i < nayteMaara; i++) {
        // Tulostetaan data Teleplotin vaatimassa muodossa: >GraafinNimi:X_arvo:Y_arvo
        // i = X-akseli (taajuuden indeksi), fft_taulukko[i] = Y-akseli (voimakkuus)
        // Oletetaan että näytteenottotaajuutesi on esim 10 000 Hz ja näytteitä 512
        float resoluutio_hz = (float)naytteNopeus / (float)nayteMaara;
        float taajuus_hz = (float)paras_indeksi * resoluutio_hz;
        // Tulostetaan data Teleplotille: >FFT:taajuus:voimakkuus
        // Koska dsps_cplx2reC_fc32 on ajettu, fft_taulukko[i] on suoraan voimakkuus (Y-akseli)
        printf(">FFT:%.2f:%.2f\n", taajuus_hz, fft_taulukko[i]);
    }
    */
    printf("Suurin nopeus: %.2f\n", nopeus_kmh);
    return nopeus_kmh;
}

void app_main(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    printf("Tutkaohjelma käynnistyy!\n");

    // KORJAUS 3: Vaihdettu 4096 -> 512. 
    // 2048 tavua / 4 tavua per näyte = 512 näytettä. FFT:n koon on täsmättävä tähän!
    dsps_fft2r_init_fc32(NULL, nayteMaara);

    // Palkataan apulainen täällä, vain kerran
    alusta_tutka();

    adc_continuous_start(tutka_adc_kahva);
    ble_hb100_init();

    while (1) {
        // Ylempi silmukka käyttää muuttujaa 'i'
        //for (int i = 0; i < 10; i++) {
        uint32_t luettu_maara = 0;
            
            // Annetaan prosessorille tauko watchdogia varten
        vTaskDelay(pdMS_TO_TICKS(10));

            // Lukukäsky funktio datan hakemiseen
        esp_err_t tila = adc_continuous_read(tutka_adc_kahva, tulopuskuri, 4096, &luettu_maara, portMAX_DELAY);

        // Tarkistetaan, onko tila ok
        if (tila == ESP_OK) {
            // KORJAUS 1: Käytetään muuttujaa 'j' sisemmässä silmukassa!
            for (int j = 0; j < luettu_maara; j += 4) {
                    
                adc_digi_output_data_t *kirjekuori = (adc_digi_output_data_t *)&tulopuskuri[j];
                uint32_t raaka_arvo = kirjekuori->type2.data;

                int jannite_mv = 0;
                adc_cali_raw_to_voltage(kalibrointi_kahva, raaka_arvo, &jannite_mv);

                // j/2 kaava toimii hienosti!
                fft_taulukko[j/2] = (float)jannite_mv;
                fft_taulukko[(j/2) + 1] = 0.0f; // f-pääte varmistaa, että kyseessä on float
            }
            
            // Lasketaan nopeus TÄÄLLÄ, kun kaikki taulukon arvot on täytetty
            float nopeus = nopeudenlasku(fft_taulukko);
            ble_notify_ball_speed(nopeus);

        } else {
            // KORJAUS 2: Tämä else kuuluu tänne, jos tila EI ole ESP_OK!
            printf("Datan hakemisessa oli ongelma. Tila: %d\n", tila);
            // Nollataan tämän kierroksen nopeus, jotta emme käytä vanhaa haamudataa
        }
    }

        // Haetaan 10 mittaustuloksesta suurin nopeus ja lähetetään se bluetoothilla.
        //float nopein = 0.0;
        // Tässä voidaan taas käyttää 'i'-muuttujaa turvallisesti, koska aiempi for-silmukka on jo päättynyt.
        /*for(int i = 0; i < 10; i++) {
            if(nopeuksia[i] > nopein) {
                nopein = nopeuksia[i];
            }
        }*/       
    }


