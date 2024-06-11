/* TWAI Network Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/twai.h" // Update from V4.2
#include "twai.h"
#include "cJSON.h"
//#include "init_malloci.h"

static const char *TAG = "TWAI";

extern QueueHandle_t xQueue_http_client;
extern QueueHandle_t xQueue_twai_tx;

extern TOPIC_t *publish;
extern int16_t npublish;
int str_iterator = 0;
extern char* twai_string_buf;

//extern esp_err_t init_twai_read_malloc();

void dump_table(TOPIC_t *topics, int16_t ntopic);

void twai_task(void *pvParameters)
{
	ESP_LOGI(TAG,"task start");
	dump_table(publish, npublish);
	twai_message_t rx_msg;
	twai_message_t tx_msg;
	FRAME_t frameBuf;
	char* ftype;
	//char* fdata = (char*)malloc(32*sizeof(char));
	//int iterator = 0;
	char flags[4];
	char identifier[4];
	while (1) {
		esp_err_t ret = twai_receive(&rx_msg, pdMS_TO_TICKS(10));
		if (ret == ESP_OK) {
			ESP_LOGD(TAG,"twai_receive identifier=0x%"PRIx32" flags=0x%"PRIx32" data_length_code=%d",
				rx_msg.identifier, rx_msg.flags, rx_msg.data_length_code);

			int ext = rx_msg.flags & 0x01;
			int rtr = rx_msg.flags & 0x02;
			ESP_LOGD(TAG, "ext=%x rtr=%x", ext, rtr);

#if CONFIG_ENABLE_PRINT
			if (ext == STANDARD_FRAME) {
				printf("Standard ID: 0x%03"PRIx32, rx_msg.identifier);
			} else {
				printf("Extended ID: 0x%08"PRIx32, rx_msg.identifier);
			}
			printf(" DLC: %d  Data: ", rx_msg.data_length_code);

			printf("\n");
#endif
// JSON fun fun fun output for da api
			if (ext == STANDARD_FRAME) {
				ftype = "STANDARD_FRAME";
			} else {
				ftype = "EXTENDED_FRAME";
			}

			cJSON *twai_root = cJSON_CreateObject();
			
			int charslen_each = 6;
			int data_len_chars = rx_msg.data_length_code*charslen_each;
			char tmp_buf[charslen_each+2];
			char fdata[data_len_chars];

			if (rx_msg.data_length_code != 0) {
				for (int i = 0; i < rx_msg.data_length_code; i++) {
					snprintf(tmp_buf, charslen_each, "0x%02x ",rx_msg.data[i]);
					strcat(fdata, tmp_buf);
				}
				fdata[strlen(fdata)-1] = '\0';
				cJSON_AddStringToObject(twai_root, "data", fdata);
			}
			snprintf(flags, sizeof(rx_msg.flags), "0x%"PRIx32, rx_msg.flags);
			snprintf(identifier, sizeof(rx_msg.identifier), "0x%"PRIx32, rx_msg.identifier);

			cJSON_AddStringToObject(twai_root, "type", ftype);
			cJSON_AddStringToObject(twai_root, "id", identifier);
			cJSON_AddStringToObject(twai_root, "flags", flags);
			cJSON_AddNumberToObject(twai_root, "ext", ext);
			cJSON_AddNumberToObject(twai_root, "rtr", rtr);
			cJSON_AddNumberToObject(twai_root, "dlc",rx_msg.data_length_code);

			char *string = cJSON_Print(twai_root);
			char br[] = "<br>\n";
			
			strcat(twai_string_buf,string);
			strcat(twai_string_buf,br);

			cJSON_free(string);
			cJSON_Delete(twai_root);
// end json buffer funssss

			for(int index=0;index<npublish;index++) {
				if (publish[index].frame != ext) continue;
				if (publish[index].canid == rx_msg.identifier) {
					ESP_LOGI(TAG, "publish[%d] frame=%d canid=0x%"PRIx32" topic=[%s] topic_len=%d",
					index, publish[index].frame, publish[index].canid, publish[index].topic, publish[index].topic_len);
					strcpy(frameBuf.topic, publish[index].topic);
					frameBuf.command = CMD_RECEIVE;
					frameBuf.topic_len = publish[index].topic_len;
					frameBuf.canid = rx_msg.identifier;
					frameBuf.ext = ext;
					frameBuf.rtr = rtr;
					if (rtr == 0) {
						frameBuf.data_len = rx_msg.data_length_code;
					} else {
						frameBuf.data_len = 0;
					}
					for(int i=0;i<frameBuf.data_len;i++) {
						frameBuf.data[i] = rx_msg.data[i];
					}
					if (xQueueSend(xQueue_http_client, &frameBuf, portMAX_DELAY) != pdPASS) {
						ESP_LOGE(TAG, "xQueueSend Fail");
					}
				}
			}

		} else if (ret == ESP_ERR_TIMEOUT) {
			if (xQueueReceive(xQueue_twai_tx, &tx_msg, 0) == pdTRUE) {
				ESP_LOGI(TAG, "tx_msg.identifier=[0x%"PRIx32"] tx_msg.extd=%d", tx_msg.identifier, tx_msg.extd);
				twai_status_info_t status_info;
				twai_get_status_info(&status_info);
				ESP_LOGD(TAG, "status_info.state=%d",status_info.state);
				if (status_info.state != TWAI_STATE_RUNNING) {
					ESP_LOGE(TAG, "TWAI driver not running %d", status_info.state);
					continue;
				}
				ESP_LOGD(TAG, "status_info.msgs_to_tx=%"PRIu32,status_info.msgs_to_tx);
				ESP_LOGD(TAG, "status_info.msgs_to_rx=%"PRIu32,status_info.msgs_to_rx);
				//esp_err_t ret = twai_transmit(&tx_msg, pdMS_TO_TICKS(10));
				esp_err_t ret = twai_transmit(&tx_msg, 0);
				if (ret == ESP_OK) {
					ESP_LOGI(TAG, "twai_transmit success");
				} else {
					ESP_LOGE(TAG, "twai_transmit Fail %s", esp_err_to_name(ret));
				}
			}

		} else {
			ESP_LOGE(TAG, "twai_receive Fail %s", esp_err_to_name(ret));
		}
	}

	// Never reach here
	vTaskDelete(NULL);
}

