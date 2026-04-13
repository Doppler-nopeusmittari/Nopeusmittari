#ifndef BLE_HB100_H
#define BLE_HB100_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void ble_hb100_init(void);


void ble_notify_ball_speed(float speed_kmh);

void ble_host_task(void *param);

void test_notify_task(void *param);
#ifdef __cplusplus
}
#endif

#endif // BLE_HB100_H