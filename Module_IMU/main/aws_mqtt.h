#ifndef AWS_MQTT_H_
#define AWS_MQTT_H_

#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void aws_mqtt_init(void);
void aws_mqtt_start_if_needed(void);
void aws_mqtt_stop(void);
bool aws_mqtt_is_connected(void);
const char *aws_mqtt_get_topic(void);
const char *aws_mqtt_get_device_id(void);
int aws_mqtt_get_last_msg_id(void);
void aws_mqtt_task(void *pvParameters);

#endif