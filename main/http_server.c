/* HTTP Server Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs.h"
#include "esp_spiffs.h"
#include "esp_http_server.h"
#include "esp_chip_info.h"
#include "cJSON.h"
#include "driver/twai.h"
#include "twai.h"

#define SCRATCH_BUFSIZE (1024)

#define TAG	"HTTP_SERVER"

int pointer = 0;
static char *base_path = "/spiffs";  

extern QueueHandle_t xQueue_twai_tx;
extern char* twai_string_buf;
extern int twai_allocation_size;
extern char* file_read_buf;
extern int allocation_size;

typedef struct rest_server_context {
	char base_path[ESP_VFS_PATH_MAX + 1]; // Not used in this project
	char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

esp_err_t readFile(char *ifile) {
	int c;
	ESP_LOGI(TAG, "File read llocated memory confirmed!: %d bytes", allocation_size);
	char *spiffspath = "/spiffs";
	int buflen = strlen(spiffspath)+strlen(ifile)+1;
	char buf[buflen];
	snprintf(buf, sizeof(buf), "%s%s", spiffspath, ifile);
	ESP_LOGI(TAG, "buflen: %d, spiffspath: %s, ifile: %s, buf: %s, base_path: %s",buflen,spiffspath,ifile,buf,base_path);
    FILE* f = fopen(buf, "r");
	ESP_LOGI(TAG, "Begin reading file");
	int i = 0;
	while ((c = fgetc(f)) != EOF) {
		file_read_buf[i] = c;
		i++;
	};
	file_read_buf[i] = '\0';
	fclose(f);
	ESP_LOGI(TAG, "Read File - Iterations: %d", i);
	return ESP_OK;
}

/* Handler for roor get handler */
esp_err_t root_get_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "root_get_handler req->uri=[%s]", req->uri);
	readFile("/index.html");
	esp_err_t sendcheck = httpd_resp_send(req, file_read_buf, HTTPD_RESP_USE_STRLEN);
	//httpd_resp_sendstr_chunk(req, NULL);

	if(sendcheck == ESP_OK) {
		//free(file_read_buf);
		memset(twai_string_buf, '\0', allocation_size);
		ESP_LOGI(TAG, "malloc freed");
		return ESP_OK;
	} else {
		return ESP_ERR_HTTPD_RESP_SEND;
	} 

	return ESP_OK;
}

static esp_err_t twai_read_handler(httpd_req_t *req)
{
	httpd_resp_sendstr(req, twai_string_buf);
	// Buffers returned by cJSON_Print must be freed by the caller.
	// Please use the proper API (cJSON_free) rather than directly
	//twai_string_buf = {0};
	memset(twai_string_buf, '\0', twai_allocation_size);
	return ESP_OK;
}

/* Handler for getting system information handler */
// curl 'http://esp32-can-server:8000/api/system/info' | python -m json.tool
static esp_err_t system_info_get_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "system_info_get_handler req->uri=[%s]", req->uri);
	httpd_resp_set_type(req, "application/json");
	cJSON *root = cJSON_CreateObject();
	esp_chip_info_t chip_info;
	esp_chip_info(&chip_info);
	cJSON_AddStringToObject(root, "version", IDF_VER);
	cJSON_AddNumberToObject(root, "cores", chip_info.cores);
	//const char *sys_info = cJSON_Print(root);
	char *sys_info = cJSON_Print(root);
	httpd_resp_sendstr(req, sys_info);
	// Buffers returned by cJSON_Print must be freed by the caller.
	// Please use the proper API (cJSON_free) rather than directly calling stdlib free.
	cJSON_free(sys_info);
	cJSON_Delete(root);
	return ESP_OK;
}


// Create array
cJSON *Create_array_of_anything(cJSON **objects,int array_num)
{
	cJSON *prev = 0;
	cJSON *root;
	root = cJSON_CreateArray();
	for (int i=0;i<array_num;i++) {
		if (!i)	{
			root->child=objects[i];
		} else {
			prev->next=objects[i];
			objects[i]->prev=prev;
		}
		prev=objects[i];
	}
	return root;
}

char *JSON_Types(int type) {
	if (type == cJSON_Invalid) return ("cJSON_Invalid");
	if (type == cJSON_False) return ("cJSON_False");
	if (type == cJSON_True) return ("cJSON_True");
	if (type == cJSON_NULL) return ("cJSON_NULL");
	if (type == cJSON_Number) return ("cJSON_Number");
	if (type == cJSON_String) return ("cJSON_String");
	if (type == cJSON_Array) return ("cJSON_Array");
	if (type == cJSON_Object) return ("cJSON_Object");
	if (type == cJSON_Raw) return ("cJSON_Raw");
	return NULL;
}

/* Handler for twai send handler */
// curl -X POST -H "Content-Type: application/json" -d '{"canid": 513, "frame": "standard", "data": [11, 12, 13, 14]}' http://esp32-server.local:8000/api/twai/send
static esp_err_t twai_send_handler(httpd_req_t *req)
{
	ESP_LOGI(TAG, "twai_send_handler req->uri=[%s]", req->uri);
	int total_len = req->content_len;
	int cur_len = 0;
	char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
	int received = 0;
	if (total_len >= SCRATCH_BUFSIZE) {
		/* Respond with 500 Internal Server Error */
		httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
		return ESP_FAIL;
	}
	while (cur_len < total_len) {
		received = httpd_req_recv(req, buf + cur_len, total_len);
		if (received <= 0) {
			/* Respond with 500 Internal Server Error */
			httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
			return ESP_FAIL;
		}
		cur_len += received;
	}
	buf[total_len] = '\0';
	ESP_LOGI(TAG, "buf=[%s]", buf);


	bool parse = true;
	cJSON *root = cJSON_Parse(buf);

	// Search canid item
	int32_t canid = 0;
	cJSON* state = cJSON_GetObjectItem(root, "canid");
	if (state) {
		canid = cJSON_GetObjectItem(root, "canid")->valueint;
		ESP_LOGI(TAG, "canid=%"PRIx32, canid);
	} else {
		ESP_LOGE(TAG, "canid item not found");
		parse = false;
	}

	// Search frame item
	char frameStr[12];
	uint16_t frame = 0;
	state = cJSON_GetObjectItem(root, "frame");
	if (state) {
		strcpy(frameStr, cJSON_GetObjectItem(root,"frame")->valuestring);
		ESP_LOGI(TAG, "frameStr=[%s]", frameStr);
		if (strcmp(frameStr, "standard") != 0 && strcmp(frameStr, "extended") != 0 ) {
			ESP_LOGE(TAG, "frame item not correct");
			parse = false;
		} else {
			if (strcmp(frameStr, "standard") == 0) frame = STANDARD_FRAME;
			if (strcmp(frameStr, "extended") == 0) frame = EXTENDED_FRAME;
		}
	} else {
		ESP_LOGE(TAG, "frame item not found");
		parse = false;
	}

	// Search data item
	int16_t data_len;
	char data_value[8];
	state = cJSON_GetObjectItem(root, "data");
	if (state) {
		cJSON *data_array = cJSON_GetObjectItem(root,"data");
		ESP_LOGI(TAG, "data_array->type=%s", JSON_Types(data_array->type));
		if (data_array->type == cJSON_Array) {
			int data_array_size = cJSON_GetArraySize(data_array);
			ESP_LOGI(TAG, "data_array_size=%d", data_array_size);
			bool data_valid = true;
			data_len = data_array_size;
			if (data_array_size > 8) {
				ESP_LOGW(TAG, "Too many data arrays : %d", data_array_size);
				data_len = 8;
			} 
			for (int i=0;i<data_len;i++) {
				cJSON *array = cJSON_GetArrayItem(data_array,i);
				//ESP_LOGI(TAG, "array->type=%s", JSON_Types(array->type));
				uint16_t data_int = array->valueint;
				ESP_LOGI(TAG, "data_int[%d]=%x", i, data_int);
				if (data_int <= 0xff) {
					data_value[i] = data_int;
				} else {
					ESP_LOGE(TAG, "Too large data value : %x", data_int);
					data_valid = false;
				}
			} // end for
			if (data_valid == false) {
				parse = false;
			}
		} else {
			ESP_LOGE(TAG, "data item not array");
			parse = false;
		} // end if


	} else {
		ESP_LOGE(TAG, "data item not found");
		parse = false;
	}

	cJSON_Delete(root);

	// JSON parse success. Send twai data.
	if (parse) {
		ESP_LOGI(TAG, "twai_send_handler frame=%d canid=%"PRIx32" data_len=%d", frame, canid, data_len);
		ESP_LOG_BUFFER_HEX(TAG, data_value, data_len);
		twai_message_t tx_msg;
		tx_msg.extd = frame;
		tx_msg.ss = 1;
		tx_msg.self = 0;
		tx_msg.dlc_non_comp = 0;
		tx_msg.identifier = canid;
		tx_msg.data_length_code = data_len;
		for (int i=0;i<data_len;i++) {
			tx_msg.data[i] = data_value[i];
		}
		if (xQueueSend(xQueue_twai_tx, &tx_msg, portMAX_DELAY) != pdPASS) {
			ESP_LOGE(TAG, "xQueueSend Fail");
		}
		httpd_resp_sendstr(req, "CAN tx sent successfully");
	} else {
		ESP_LOGE(TAG, "Request parameter not correct");
		httpd_resp_sendstr(req, "Request parameter not correct");
	}
	return ESP_OK;
}

/* Function to start the file server */
esp_err_t start_server(const char *base_path, int port)
{
	rest_server_context_t *rest_context = calloc(1, sizeof(rest_server_context_t));
	if (rest_context == NULL) {
		ESP_LOGE(TAG, "No memory for rest context");
		while(1) { vTaskDelay(1); }
	}

	httpd_handle_t server = NULL;
	httpd_config_t config = HTTPD_DEFAULT_CONFIG();
	config.server_port = port;

	/* Use the URI wildcard matching function in order to
	 * allow the same handler to respond to multiple different
	 * target URIs which match the wildcard scheme */
	config.uri_match_fn = httpd_uri_match_wildcard;

	ESP_LOGD(TAG, "Starting HTTP Server on port: '%d'", config.server_port);
	if (httpd_start(&server, &config) != ESP_OK) {
		ESP_LOGE(TAG, "Failed to start file server!");
		return ESP_FAIL;
	}

	/* URI handler for root */
	httpd_uri_t root = {
		.uri	   = "/",	// Match all URIs of type /path/to/file
		.method    = HTTP_GET,
		.handler   = root_get_handler,
		//.user_ctx  = server_data	// Pass server data as context
	};
	httpd_register_uri_handler(server, &root);

	/* URI handler for getting system info */
	httpd_uri_t system_info_get_uri = {
		.uri       = "/api/system/info",
		.method    = HTTP_GET,
		.handler   = system_info_get_handler,
		.user_ctx  = rest_context
	};
	httpd_register_uri_handler(server, &system_info_get_uri);

	/* URI handler for send twai */
	httpd_uri_t twai_send_post_uri = {
		.uri       = "/api/twai/send",
		.method    = HTTP_POST,
		.handler   = twai_send_handler,
		.user_ctx  = rest_context
	};
	httpd_register_uri_handler(server, &twai_send_post_uri);

	/* URI handler for send twai */
	httpd_uri_t twai_read = {
		.uri       = "/api/twai/read",
		.method    = HTTP_GET,
		.handler   = twai_read_handler,
		//.user_ctx  = rest_context
	};
	httpd_register_uri_handler(server, &twai_read);

	return ESP_OK;
}

void http_server_task(void *pvParameters)
{
	char *task_parameter = (char *)pvParameters;
	ESP_LOGI(TAG, "Start task_parameter=%s", task_parameter);
	char url[64];
	sprintf(url, "http://%s:%d", task_parameter, CONFIG_WEB_PORT);
	ESP_LOGI(TAG, "Starting server on %s", url);

#if 0
	// Create Semaphore
	// This Semaphore is used for locking
	ctrl_task_sem = xSemaphoreCreateBinary();
	configASSERT( ctrl_task_sem );
	xSemaphoreGive(ctrl_task_sem);
#endif

	ESP_ERROR_CHECK(start_server("/spiffs", CONFIG_WEB_PORT));
	
	while(1) {
		// Nothing to do
		vTaskDelay(1);
	}

	// Never reach here
	ESP_LOGI(TAG, "finish");
	vTaskDelete(NULL);
}
