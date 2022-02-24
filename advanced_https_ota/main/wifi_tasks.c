
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/event_groups.h"
#include "common.h"


static const char *TAG = "WIFI_RECONNECT";
extern EventGroupHandle_t s_wifi_event_group;


void wifi_reconnect_task(void *pvParameter)
{

	vTaskDelay( 10000 / portTICK_PERIOD_MS ); //While first connection is made wait a bit, do not try to connect

	while(1)
	{
		EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
				WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
				pdFALSE,
				pdFALSE,
				100 / portTICK_PERIOD_MS); /* Wait a maximum of 100ms for either bit to be set. */


		if(bits & WIFI_FAIL_BIT)
		{
			ESP_LOGI(TAG, "Trying to reconnect to ap SSID:%s password:%s",
					CONFIG_EXAMPLE_WIFI_SSID, CONFIG_EXAMPLE_WIFI_PASSWORD);

			esp_wifi_connect();

		}
		else if(bits & WIFI_CONNECTED_BIT)
		{
			ESP_LOGI(TAG, "Already connected to ap SSID:%s password:%s",
					CONFIG_EXAMPLE_WIFI_SSID, CONFIG_EXAMPLE_WIFI_PASSWORD);
		}
		else
		{
			ESP_LOGE(TAG, "WIFI RECONNECT UNEXPECTED EVENT");
		}

		vTaskDelay( 60000 / portTICK_PERIOD_MS );

	}

	// Never reach here
	vTaskDelete(NULL);

}

