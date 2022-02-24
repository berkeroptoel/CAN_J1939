
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "freertos/event_groups.h"
#include "protocol_examples_common.h"
#include "common.h"


static const char *TAG = "OTA_CHECK";
extern const uint8_t server_cert_pem_start[] asm("_binary_ca_cert_pem_start");
//extern const uint8_t server_cert_pem_end[] asm("_binary_ca_cert_pem_end");
#define OTA_URL_SIZE 256


#if CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
#include "esp_efuse.h"
#endif

extern EventGroupHandle_t s_wifi_event_group;


static esp_err_t validate_image_header(esp_app_desc_t *new_app_info)
{
    if (new_app_info == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_app_desc_t running_app_info;
    if (esp_ota_get_partition_description(running, &running_app_info) == ESP_OK) {
        ESP_LOGI(TAG, "Running firmware version: %s", running_app_info.version);
    }

#ifndef CONFIG_EXAMPLE_SKIP_VERSION_CHECK
    if (memcmp(new_app_info->version, running_app_info.version, sizeof(new_app_info->version)) <= 0) {
        ESP_LOGW(TAG, "No newer FW version. We will not continue the update.");
        return ESP_FAIL;
    }
#endif

#ifdef CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK
    /**
     * Secure version check from firmware image header prevents subsequent download and flash write of
     * entire firmware image. However this is optional because it is also taken care in API
     * esp_https_ota_finish at the end of OTA update procedure.
     */
    const uint32_t hw_sec_version = esp_efuse_read_secure_version();
    if (new_app_info->secure_version < hw_sec_version) {
        ESP_LOGW(TAG, "New firmware security version is less than eFuse programmed, %d < %d", new_app_info->secure_version, hw_sec_version);
        return ESP_FAIL;
    }
#endif

    return ESP_OK;
}

static esp_err_t _http_client_init_cb(esp_http_client_handle_t http_client)
{
    esp_err_t err = ESP_OK;
    /* Uncomment to add custom headers to HTTP request */
    // err = esp_http_client_set_header(http_client, "Custom-Header", "Value");
    return err;
}


void advanced_ota_example_task(void *pvParameter)
{

	  //Wait WIFI connection
	vTaskDelay( 10000 / portTICK_PERIOD_MS ); //While first connection is made wait a bit, do not try to connect

	   ESP_LOGI(TAG, "Starting Advanced OTA check");

	    esp_err_t ota_finish_err = ESP_OK;
	    esp_http_client_config_t config = {
	        .url = CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL,
	        .cert_pem = (char *)server_cert_pem_start,
	        .timeout_ms = CONFIG_EXAMPLE_OTA_RECV_TIMEOUT,
	        .keep_alive_enable = true,
	    };

	#ifdef CONFIG_EXAMPLE_FIRMWARE_UPGRADE_URL_FROM_STDIN
	    char url_buf[OTA_URL_SIZE];
	    if (strcmp(config.url, "FROM_STDIN") == 0) {
	        example_configure_stdin_stdout();
	        fgets(url_buf, OTA_URL_SIZE, stdin);
	        int len = strlen(url_buf);
	        url_buf[len - 1] = '\0';
	        config.url = url_buf;
	    } else {
	        ESP_LOGE(TAG, "Configuration mismatch: wrong firmware upgrade image url");
	        abort();
	    }
	#endif

	#ifdef CONFIG_EXAMPLE_SKIP_COMMON_NAME_CHECK
	    config.skip_cert_common_name_check = true;
	#endif

	    esp_https_ota_config_t ota_config = {
	        .http_config = &config,
	        .http_client_init_cb = _http_client_init_cb, // Register a callback to be invoked after esp_http_client is initialized
	#ifdef CONFIG_EXAMPLE_ENABLE_PARTIAL_HTTP_DOWNLOAD
	        .partial_http_download = true,
	        .max_http_request_size = CONFIG_EXAMPLE_HTTP_REQUEST_SIZE,
	#endif
	    };

	    esp_https_ota_handle_t https_ota_handle = NULL;

	    while(1)
	    {



	    	EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
	    					WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
	    					pdFALSE,
	    					pdFALSE,
	    					100 / portTICK_PERIOD_MS); /* Wait a maximum of 100ms for either bit to be set. */


	    	//No WIFI
			if(bits & WIFI_FAIL_BIT)
			{

				goto ota_end;

			}

		    esp_err_t err = esp_https_ota_begin(&ota_config, &https_ota_handle);
		    if (err != ESP_OK) {
		        ESP_LOGE(TAG, "ESP HTTPS OTA Begin failed");
		        //vTaskDelete(NULL);
		        goto ota_end;
		    }

		    esp_app_desc_t app_desc;
		    err = esp_https_ota_get_img_desc(https_ota_handle, &app_desc);
		    if (err != ESP_OK) {
		        ESP_LOGE(TAG, "esp_https_ota_read_img_desc failed");
		        goto ota_end;
		    }
		    err = validate_image_header(&app_desc);
		    if (err != ESP_OK) {
		        ESP_LOGE(TAG, "image header verification failed");
		        goto ota_end;
		    }


		    while (1) {
		        err = esp_https_ota_perform(https_ota_handle);
		        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
		            break;
		        }
		        // esp_https_ota_perform returns after every read operation which gives user the ability to
		        // monitor the status of OTA upgrade by calling esp_https_ota_get_image_len_read, which gives length of image
		        // data read so far.
		        ESP_LOGD(TAG, "Image bytes read: %d", esp_https_ota_get_image_len_read(https_ota_handle));
		    }

		    if (esp_https_ota_is_complete_data_received(https_ota_handle) != true) {
		        // the OTA image was not completely received and user can customise the response to this situation.
		        ESP_LOGE(TAG, "Complete data was not received.");
		    } else {
		        ota_finish_err = esp_https_ota_finish(https_ota_handle);
		        if ((err == ESP_OK) && (ota_finish_err == ESP_OK)) {
		            ESP_LOGI(TAG, "ESP_HTTPS_OTA upgrade successful. Rebooting ...");
		            vTaskDelay(1000 / portTICK_PERIOD_MS);
		            esp_restart();
		        } else {
		            if (ota_finish_err == ESP_ERR_OTA_VALIDATE_FAILED) {
		                ESP_LOGE(TAG, "Image validation failed, image is corrupted");
		            }
		            ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed 0x%x", ota_finish_err);
		            //vTaskDelete(NULL);
		        }
		    }

		    ota_end:
		    //vTaskDelete(NULL);
		    //esp_https_ota_abort(https_ota_handle);
		    ESP_LOGE(TAG, "ESP_HTTPS_OTA upgrade failed");
		    vTaskDelay( 60000 / portTICK_PERIOD_MS );

	    }

	    vTaskDelete(NULL);

}

