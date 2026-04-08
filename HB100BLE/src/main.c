#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "nvs_flash.h"
#include "esp_log.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

// HB100 service UUID määrittely
static const ble_uuid128_t hb100_service_uuid =  // Määrittelee GATT servicen yksilöllisen tunnisteen, web bluetooth etsii palvelun tällä UUID:lla
    BLE_UUID128_INIT(0xFB,0x34,0x9B,0x5F,0x80,0x00,0x00,0x80,
                      0x00,0x10,0x00,0x00,0x01,0x00,0x00,0x00);

// Pallon nopeus characteristic UUID määrittely
static const ble_uuid128_t ball_speed_char_uuid = // Määrittelee yksittäisen datakentän esim pallon nopeus
    BLE_UUID128_INIT(0xFB,0x34,0x9B,0x5F,0x80,0x00,0x00,0x80,
                      0x00,0x10,0x00,0x00,0x02,0x00,0x00,0x00);

static uint16_t ball_speed_char_handle; //BLE stack antaa numeron tälle, Jotta BLE tietää mihin notify lähetetään
static uint16_t conn_handle = 0; // BLE yhteyden tunniste 0 = ei yhteyttä
static bool client_subscribed = false; // Tämä kertoo, onko selain tilannut notificaationit, BLE ei lähetä dataa ennen kuin selain tilaa ja tämä muuttuu silloin trueksi
static uint8_t own_addr_type; // BLE osoitetyyppi, selvittää osoitteen yhdistäessä

static int gatt_access_cb(uint16_t conn_handle, // NimBLE vaatii tämän käyttöä vaikka tässä sitä ei käytetä. Tämä kutsuttaisiin jos sovellus yrittää READ tai WRITE
                          uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt,
                          void *arg)
{
    return 0;
}

static const struct ble_gatt_svc_def gatt_svcs[] = { // GATT rakenne eli määritellään BLE:n datamalli, kaikki mitä voidaan lukea/tilata on tässä
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY, // Tämä on pääpalvelu joka lukee selaimessa
        .uuid = &hb100_service_uuid.u, // Tämä liittää servicen aikaisemmin määriteltyyn UUID:iin
        .characteristics = (struct ble_gatt_chr_def[]) { // Servicen sisällä olevat kentät
            {
                .uuid = &ball_speed_char_uuid.u, // Pallon nopeud kenttä
                .access_cb = gatt_access_cb, // Callback sille, että jos joku yrittää suorittaa READ/WRITE
                .val_handle = &ball_speed_char_handle, // Handlen tallennus myöhempää notifyä varten
                .flags = BLE_GATT_CHR_F_NOTIFY, // Tässä sallitaan notify mutta ei sallita READ/WRTIE
            },
            { 0 } // lista loppuu nollaan
        },
    },
    { 0 }
};

static const char *TAG = "BLE_MIN"; // Logien tunniste

/* ===== GAP event handler ===== */
static int ble_gap_event(struct ble_gap_event *event, void *arg) // Kaikki BLE tapahtumat tulevat tänne
{
    switch (event->type) { //Tarkistaa mikä BLE tapahtuma tapahtui

case BLE_GAP_EVENT_SUBSCRIBE: // Subscribe event, tämä tapahtuu kun selain "painaa" enable notifications
    if (event->subscribe.attr_handle == ball_speed_char_handle) { // Tällä varmistetaan, että tilaus koskee oikeaa characteristicia
        client_subscribed = event->subscribe.cur_notify; 
        ESP_LOGI(TAG, "Notify %s",
                 client_subscribed ? "ENABLED" : "DISABLED"); // True selain haluaa dataa false selain ei halua dataa
    }
    break;

    case BLE_GAP_EVENT_CONNECT:
    if (event->connect.status == 0) {
        conn_handle = event->connect.conn_handle;
        ESP_LOGI(TAG, "Connected");
    } else {
        conn_handle = 0;
    }
    break;
    
case BLE_GAP_EVENT_DISCONNECT: // Disconnect 
    ESP_LOGI(TAG, "Disconnected");
    conn_handle = 0;
    client_subscribed = false;

    ble_gap_adv_start(
        own_addr_type,
        NULL,
        BLE_HS_FOREVER,
        NULL,
        ble_gap_event,
        NULL
    );
    break;


    default:
        break;
    }
    return 0;
}

/* ===== Advertising ===== */
static void ble_app_advertise(void) // Täällä luodaan mainosdata, konfiguroidaan mainostus ja käynnistetään mainostus
{
    struct ble_gap_adv_params adv_params; // Miten sanotaan
    struct ble_hs_adv_fields fields; // Mitä sanotaan

    memset(&fields, 0, sizeof(fields)); // Rakenteiden nollaaminen, jolla vältetään satunnaiset arvot
    memset(&adv_params, 0, sizeof(adv_params));

    fields.name = (uint8_t *)"HB100radar"; // Laitteen nimi 
    fields.name_len = strlen("HB100radar");
    fields.name_is_complete = 1; // Tällä määritetään ettei nimeä lyhennetä vaan sen näkyy kokonaan

    ble_gap_adv_set_fields(&fields); // Antaa mainosdatan NimBLE stackille

    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND; // Kertoo laitteen olevan yhdistettävissä
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; // Kertoo, että laite on normaali BLE laite

    ble_gap_adv_start( // Tässä määritellään, että käytetään oikeaa BLE osoitetta, maiostetaan ikuisesti, rekisteröi GAP-event handlerin
        own_addr_type,
        NULL,
        BLE_HS_FOREVER,
        &adv_params,
        ble_gap_event,
        NULL
    );

    ESP_LOGI("BLE", "Advertising started"); // Debuggaukseen, että tiedetään "mainostuksen alkaneen"
}

static void ble_app_on_sync(void) // Tämä kutsutaan, kun BLE stack on täysin valmis ja voidaan aloittaa advertising
{
    int rc;

    // Oikean osoitetyypin selvittäminen public vai random, tämä tallennetaan sitten sinne own_addr_typeen
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        return;
    }

    ble_app_advertise(); // MAINOSTUKSEN ALOITUS aikaisemmat alustusta tähän
}

void ble_notify_ball_speed(float speed_kmh) // Tämä on paikka jossa data oikeasti "lähtee" HB100 mittaustuloksia siirretään tällä
{
    if (!client_subscribed || conn_handle == 0) { // Tämä varmistaa, että yhteys on olemassa ja selain on valmis vastaanottamaan
        return;
    }

    struct os_mbuf *om =
        ble_hs_mbuf_from_flat(&speed_kmh, sizeof(speed_kmh)); // Tämä pakkaa float bytestreamiksi BLE:tä varten

    int rc = ble_gatts_notify_custom(conn_handle, ball_speed_char_handle, om); // Rivin 25 numeroa käytetään täällä, ilman kyseistä mumeroa BLE ei tietäisi mihin data lähetetään
    if (rc != 0) {
        ESP_LOGW(TAG, "Notify failed: %d", rc);
    }
}
void ble_host_task(void *param) // BLE-stackin ydin
{
    nimble_port_run(); // BLE pyörii täällä, tapahtumat tulevat tätä kautta
    nimble_port_freertos_deinit(); // Jos taski loppuu siivotaan
}

void test_notify_task(void *param)
{
    float test_speed = 10.0f;

    while (1) {
        ESP_LOGI("TEST", "Sending test speed: %.2f ", test_speed);
        ble_notify_ball_speed(test_speed);
        test_speed += 1.0f;
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ===== main ===== */
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init()); // Flash muistin käynnistys

    nimble_port_init(); // NimBLE:n alustus

    ble_svc_gap_init(); // Gap palvelun rekisteröinti
    ble_svc_gatt_init(); 
    
    ble_gatts_count_cfg(gatt_svcs);
    ble_gatts_add_svcs(gatt_svcs);

    ble_svc_gap_device_name_set("HB100"); // Virallisen device name:n asettaminen

    ble_hs_cfg.sync_cb = ble_app_on_sync; //kun BLE valmiina käytettäväksi tämä kutsutaan

    nimble_port_freertos_init(ble_host_task); // FreeRTOS taskin luominen ja BLE stack alkaa toimimaan

    xTaskCreate(
    test_notify_task,   // Task-funktio
    "test_notify",      // Nimi
    4096,               // Stack size
    NULL,               // Parametrit
    5,                  // Prioriteetti
    NULL                // Task handle (ei tarvita)
);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000)); // Varmistaa ettei app_main lopu ja, että ohjelma jää eloon
    }

}