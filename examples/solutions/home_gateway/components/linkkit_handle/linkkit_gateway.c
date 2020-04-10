
/*
 * Copyright (C) 2015-2018 Alibaba Group Holding Limited
 */
#include "infra_config.h"

void HAL_Printf(const char *fmt, ...);
int HAL_Snprintf(char *str, const int len, const char *fmt, ...);

#ifdef DEPRECATED_LINKKIT
#include "solo.c"
#else
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "infra_types.h"
#include "infra_defs.h"
#include "infra_compat.h"
#include "infra_compat.h"
#ifdef INFRA_MEM_STATS
    #include "infra_mem_stats.h"
#endif
#include "dev_model_api.h"
#include "dm_wrapper.h"
#include "cJSON.h"
#ifdef ATM_ENABLED
    #include "at_api.h"
#endif

#include "zb_common.h"
#include "lightbulb.h"
#include "esp_log.h"
#include "conn_mgr.h"

static const char* TAG = "linkkit_gateway";

#define EXAMPLE_TRACE(...)                                          \
    do {                                                            \
        HAL_Printf("\033[1;32;40m%s.%d: ", __func__, __LINE__);     \
        HAL_Printf(__VA_ARGS__);                                    \
        HAL_Printf("\033[0m\r\n");                                  \
    } while (0)

#define EXAMPLE_MASTER_DEVID            (0)
#define EXAMPLE_YIELD_TIMEOUT_MS        (200)


typedef struct {
    int master_devid;
    int cloud_connected;
    int master_initialized;
    int subdev_index;
    int permit_join;
    void *g_user_dispatch_thread;
	void *run_seamhore;
    int g_user_dispatch_thread_running;
} user_example_ctx_t;



/**
 * These PRODUCT_KEY|PRODUCT_SECRET|DEVICE_NAME|DEVICE_SECRET are listed for demo only
 *
 * When you created your own devices on iot.console.com, you SHOULD replace them with what you got from console
 *
 */

char PRODUCT_KEY[IOTX_PRODUCT_KEY_LEN + 1] = {0};
char PRODUCT_SECRET[IOTX_PRODUCT_SECRET_LEN + 1] = {0};
char DEVICE_NAME[IOTX_DEVICE_NAME_LEN + 1] = {0};
char DEVICE_SECRET[IOTX_DEVICE_SECRET_LEN + 1] = {0};

static user_example_ctx_t g_user_example_ctx;

#if ENABLE_ZB_MODULE
sub_dev_addr_map_t sub_dev_am[EXAMPLE_SUBDEV_MAX_NUM];
static uint16_t active_sub_device_num = 0;

extern QueueHandle_t zbd_sudev_am_mutex;
extern QueueHandle_t intSubdevAddressMapQueue;
extern teZcbStatus eErasePersistentData(void);

tsZbDeviceInfo* zb_device_find_device_info_by_device_id(uint32_t device_id){

	for(uint16_t i=0;i<EXAMPLE_SUBDEV_MAX_NUM;i++){
       		if(sub_dev_am[i].dev_id == device_id)
				return sub_dev_am[i].zb_device_ib ;
		}
	return NULL;

}

uint16_t sub_dev_free_space_get(void ){
	uint16_t index;
	for(index=0;index<EXAMPLE_SUBDEV_MAX_NUM;index++){
		if(!sub_dev_am[index].zb_device_ib){
			break;
		}
	}
	return index;
}

uint16_t sub_dev_find_item(uint8_t *ieeeaddr){
	uint16_t index,active_num;
	for(index=0,active_num=0;index < EXAMPLE_SUBDEV_MAX_NUM && active_num < active_sub_device_num;index++){
		if(sub_dev_am[index].zb_device_ib){
			if(memcmp(ieeeaddr,(uint8_t *)&sub_dev_am[index].zb_device_ib->u64IeeeAddress,8) == 0){
				return index;
			}
			active_num++;
		}
	}
	return EXAMPLE_SUBDEV_MAX_NUM;
}


static void byte_array_2hex_string(uint8_t *src, char *dst, uint8_t len){

    char *hex = "0123456789ABCDEF";
    char *pout = dst;
	uint8_t *pin = src;
    int i = 0;
    for(; i < len - 1; ++i){
        *pout++ = hex[(*pin>>4)&0xF];
        *pout++ = hex[(*pin++)&0xF];
        *pout++ = ':';
    }
    *pout++ = hex[(*pin>>4)&0xF];
    *pout++ = hex[(*pin)&0xF];
    *pout = 0;
}


int gateway_sub_dev_add(tsZbDeviceInfo *devInfo, char *product_key, char *product_secret, char *device_name, char *device_secret){
	if(!devInfo || !product_key || !product_secret || !device_name || !device_secret){


		HAL_Printf("gateway sub dev add failed\r\n");
		return -1;
	}
	xSemaphoreTake(zbd_sudev_am_mutex,portMAX_DELAY);
	uint16_t item_indx = sub_dev_find_item((uint8_t *)&devInfo->u64IeeeAddress);
	
	
	if(item_indx == EXAMPLE_SUBDEV_MAX_NUM){
		item_indx = sub_dev_free_space_get();
		if(item_indx >= EXAMPLE_SUBDEV_MAX_NUM){
			HAL_Printf("Gateway run out of memory for new device's secret info, %s\r\n",device_name);
			xSemaphoreGive(zbd_sudev_am_mutex);
			return -1;
		}
		
		
		iotx_linkkit_dev_meta_info_t subdev = {0};
		int read_len = sizeof(subdev);
		char ieeaddr[24]={0};
		byte_array_2hex_string((uint8_t *)&devInfo->u64IeeeAddress,ieeaddr,8);
		int kv_saved_previous = HAL_Kv_Get(ieeaddr,&subdev, &read_len);
		int needupdate = 0;
		if(kv_saved_previous == 0){
	    if((strlen(subdev.product_key) != strlen(product_key))||(memcmp(subdev.product_key, product_key, strlen(subdev.product_key)) != 0) || 
        (strlen(subdev.product_secret) != strlen(product_secret))||(memcmp(subdev.product_secret, product_secret, strlen(subdev.product_secret)) != 0)||
        (strlen(subdev.device_name) != strlen(device_name))||(memcmp(subdev.device_name, device_name, strlen(subdev.device_name)) != 0)||
        (strlen(subdev.device_secret) != strlen(device_secret))||(memcmp(subdev.device_secret, device_secret, strlen(subdev.device_secret)) != 0))
          {
				needupdate = 1;
				memset(&subdev,0,sizeof(subdev));
			}

		}
        
        if((needupdate == 1)||(kv_saved_previous != 0))
        {
			strncpy(subdev.product_key, product_key, strlen(product_key));
			strncpy(subdev.product_secret, product_secret, strlen(product_secret));
			strncpy(subdev.device_name, device_name, strlen(device_name));
			strncpy(subdev.device_secret, device_secret, strlen(device_secret));
			int ret = HAL_Kv_Set(ieeaddr,&subdev, sizeof(subdev), 0);
			if(ret != 0){
				HAL_Printf("Add new sub device secret info to kv failed, key name %s\r\n",ieeaddr);
				xSemaphoreGive(zbd_sudev_am_mutex);
				return ret;
		         }
                        else
			{
				HAL_Printf("Add new sub device secret info to kv success, key name %s\r\n",ieeaddr);
			}
		}
		sub_dev_am[item_indx].zb_device_ib = devInfo;
		sub_dev_am[item_indx].dev_id = -1;
		sub_dev_am[item_indx].bIsOnLine = false;
		active_sub_device_num++;
		if(kv_saved_previous == 0){			
			HAL_Printf("Sub device secret info has been in kv, key name %s\r\n",ieeaddr);
			g_user_example_ctx.permit_join = 1;
		}
        //g_user_example_ctx.subdev_index = item_indx;
        if(g_user_example_ctx.permit_join == 1)
        {
			xQueueSendToBack(intSubdevAddressMapQueue,&item_indx,portMAX_DELAY);
        }
		
	}else{
		//Handle device reboot condition
		g_user_example_ctx.permit_join = 1;
		//g_user_example_ctx.subdev_index = item_indx;
		xQueueSendToBack(intSubdevAddressMapQueue,&item_indx,portMAX_DELAY);
	}

	xSemaphoreGive(zbd_sudev_am_mutex);
	return 0;
}

int gateway_delete_subdev(tsZbDeviceInfo *device){
	if(!device){

		return -1;
	}
	BaseType_t rt;
	rt = xSemaphoreTake(zbd_sudev_am_mutex,portMAX_DELAY);
	if (rt != pdPASS) {
        HAL_Printf("zbd_sudev_am_mutex take failed :%d\r\n", rt);
    }
	uint16_t item_indx = sub_dev_find_item((uint8_t *)&device->u64IeeeAddress);
	if(item_indx == EXAMPLE_SUBDEV_MAX_NUM){
		HAL_Printf("%s no items find\r\n",__func__);
		xSemaphoreGive(zbd_sudev_am_mutex);
		return -1;
		
	}
	
	int ret =  IOT_Linkkit_Report(sub_dev_am[item_indx].dev_id, ITM_MSG_LOGOUT,
								 NULL,0);
	//sub_dev_am[item_indx].dev_id = -1;
	//sub_dev_am[item_indx].zb_device_ib = NULL;
	sub_dev_am[item_indx].bIsOnLine = false;
	xSemaphoreGive(zbd_sudev_am_mutex);
	return ret;
	
}


int gateway_delete_subdev_complete(tsZbDeviceInfo *device){
	if(!device){

		return -1;
	}
	BaseType_t rt;
	rt = xSemaphoreTake(zbd_sudev_am_mutex,portMAX_DELAY);
    if (rt != pdPASS) {
        HAL_Printf("zbd_sudev_am_mutex take failed :%d\r\n", rt);
    }
	uint16_t item_indx = sub_dev_find_item((uint8_t *)&device->u64IeeeAddress);
	if(item_indx == EXAMPLE_SUBDEV_MAX_NUM){
            HAL_Printf("%s no items find\r\n",__func__);
            xSemaphoreGive(zbd_sudev_am_mutex);
            return -1;
		
	}
	
	int ret =  IOT_Linkkit_Report(sub_dev_am[item_indx].dev_id, ITM_MSG_LOGOUT,
								 NULL,0);
	//delete device id
	IOT_Linkkit_Close(sub_dev_am[item_indx].dev_id);
	sub_dev_am[item_indx].dev_id = -1;
	sub_dev_am[item_indx].zb_device_ib = NULL;
	sub_dev_am[item_indx].bIsOnLine = false;
	active_sub_device_num--;
	xSemaphoreGive(zbd_sudev_am_mutex);
	return ret;	
}


int gateway_add_subdev(uint16_t index,user_example_ctx_t *user_example_ctx){
	if(index > EXAMPLE_SUBDEV_MAX_NUM){

		return -1;
	}
	xSemaphoreTake(zbd_sudev_am_mutex,portMAX_DELAY);
	if(sub_dev_am[index].bIsOnLine == true){		
		HAL_Printf("Device already reported to cloud, device id %d\r\n",sub_dev_am[index].dev_id);
		xSemaphoreGive(zbd_sudev_am_mutex);
		return -1;
	}
	
	int res = 0, devid = -1;
	iotx_linkkit_dev_meta_info_t ldm = {0};
	char ieeaddr[24]={0};
	byte_array_2hex_string((uint8_t *)&sub_dev_am[index].zb_device_ib->u64IeeeAddress,ieeaddr,8);
	int buflen = sizeof(ldm);
	if(HAL_Kv_Get(ieeaddr, &ldm, &buflen) != 0){
		HAL_Printf("Device security items can't find in kv\r\n");
		xSemaphoreGive(zbd_sudev_am_mutex);
		return -1;
	}

	if(sub_dev_am[index].dev_id == -1)
        {
          sub_dev_am[index].dev_id = IOT_Linkkit_Open(IOTX_LINKKIT_DEV_TYPE_SLAVE, &ldm);
          devid = sub_dev_am[index].dev_id;
          if (devid == FAIL_RETURN) {
              EXAMPLE_TRACE("subdev open Failed\n");
			  xSemaphoreGive(zbd_sudev_am_mutex);
              return FAIL_RETURN;
          }
          EXAMPLE_TRACE("subdev open susseed, devid = %d\n", devid);
        }
        else
        {
            devid = sub_dev_am[index].dev_id;
        }
    
	res = IOT_Linkkit_Connect(devid);
	if (res == FAIL_RETURN) {
		EXAMPLE_TRACE("subdev connect Failed\n");
		xSemaphoreGive(zbd_sudev_am_mutex);
		return res;
	}
	EXAMPLE_TRACE("subdev connect success: devid = %d\n", devid);

	res = IOT_Linkkit_Report(devid, ITM_MSG_LOGIN, NULL, 0);
	if (res == FAIL_RETURN) {
		EXAMPLE_TRACE("subdev login Failed\n");
		xSemaphoreGive(zbd_sudev_am_mutex);
		return res;
	}
	EXAMPLE_TRACE("subdev login success: devid = %d\n", devid);
        sub_dev_am[index].bIsOnLine = true;
        user_example_ctx->permit_join = 0;
	xSemaphoreGive(zbd_sudev_am_mutex);
	return res;

}
int get_user_ctx_permit_join(void)
{
    return g_user_example_ctx.permit_join;
}

int user_zb_device_property_post_event_handler(uint8_t index, tsZbDeviceAttribute *devattr)
{
    int res = 0;
    user_example_ctx_t *user_example_ctx = &g_user_example_ctx;    
    if(!user_example_ctx->cloud_connected)
    {
        EXAMPLE_TRACE("cloud not connected\r\n");
        return -1;
    }
    char *property_payload = HAL_Malloc(128);
    if(property_payload == NULL)
    {
        return -1;
    }
    memset(property_payload,0,sizeof(*property_payload));
    switch(devattr->eDeviceType)
    {
        case ZED_DOORSENSOR:
            {
              if((devattr->u16ClusterId == E_ZB_CLUSTERID_ONOFF)&&(devattr->u16AttributeId == E_ZB_ATTRIBUTEID_ONOFF_ONOFF))
              {
                  HAL_Printf("\nDoorSensor report value = %d\r\n", (uint8_t)devattr->uData.u64Data);
                  snprintf(property_payload, 64, "{\"ContactState\":%d}", (uint8_t)devattr->uData.u64Data);  
		}
	    }
		break;
        case ZED_ALARMBUTTON:
            {
                if((devattr->u16ClusterId == E_ZB_CLUSTERID_ONOFF)&&(devattr->u16AttributeId == E_ZB_ATTRIBUTEID_ONOFF_ONOFF))
                {
                    HAL_Printf("\nAlarmButton report value = %d\r\n", (uint8_t)devattr->uData.u64Data);
                    snprintf(property_payload, 128, "{\"BinarySwitch\":%d}", (uint8_t)devattr->uData.u64Data);
		}
            }
		break;      
        case ZR_DIMMABLELIGHT:
            {
                if((devattr->u16ClusterId == E_ZB_CLUSTERID_ONOFF)&&(devattr->u16AttributeId == E_ZB_ATTRIBUTEID_ONOFF_ONOFF))
                {
                    snprintf(property_payload, 128, "{\"LightSwitch\":%d}", (uint8_t)devattr->uData.u64Data);
                }
                else if((devattr->u16ClusterId == E_ZB_CLUSTERID_LEVEL_CONTROL)&&(devattr->u16AttributeId == E_ZB_ATTRIBUTEID_LEVEL_CURRENTLEVEL))
                {
                    snprintf(property_payload, 128, "{\"Brightness\":%d}", (uint8_t)devattr->uData.u64Data);
                }
            }
            break;
        default:
            {
            HAL_Printf( "Unregistered zigbee device type.\r\n");
            }
            break;
    }
    res = IOT_Linkkit_Report(sub_dev_am[index].dev_id, ITM_MSG_POST_PROPERTY,
                             (unsigned char *)property_payload, strlen(property_payload));
    EXAMPLE_TRACE("user_zb_device_property_post_event_handler: Post Property Message ID: %d", res);	
    HAL_Free(property_payload);
    return 0;
}

void user_devcount_post_property(uint16_t value)
{
    int res = 0;
    user_example_ctx_t *user_example_ctx = &g_user_example_ctx;
    char *pp = HAL_Malloc(128);
    if(pp == NULL)
    {
        return;
    }
    memset(pp,0,sizeof(*pp));    
    snprintf(pp, 128, "{\"zbSubDev\":%d}", value);
    res = IOT_Linkkit_Report(user_example_ctx->master_devid, ITM_MSG_POST_PROPERTY, (unsigned char *)pp, strlen(pp));
    EXAMPLE_TRACE("user_devcount_post_property: Post Property Message ID: %d", res);
}

int get_index_from_queue(user_example_ctx_t *user_example_ctx)
{
   uint16_t index = 0;     
   if(xQueueReceive(intSubdevAddressMapQueue,&index,0) == pdPASS)
   {   
     // user_example_ctx->permit_join = 1;
      return (int)index;
   }
   else
   {      
      return -1;
   }
}

#endif
/** Awss Status event callback */
static int user_awss_status_event_handler(int status)
{
    EXAMPLE_TRACE("Awss Status %d", status);

    return SUCCESS_RETURN;
}

/** cloud connected event callback */
static int user_connected_event_handler(void)
{
    EXAMPLE_TRACE("Cloud Connected");
    g_user_example_ctx.cloud_connected = 1;

    return 0;
}

/** cloud connect fail event callback */
static int user_connect_fail_event_handler(void) 
{
    EXAMPLE_TRACE("Cloud Connect Fail");

    return SUCCESS_RETURN;
}

/** cloud disconnected event callback */
static int user_disconnected_event_handler(void)
{
    EXAMPLE_TRACE("Cloud Disconnected");
    g_user_example_ctx.cloud_connected = 0;

    return 0;
}

/** cloud raw_data arrived event callback */
static int user_rawdata_arrived_event_handler(const int devid, const unsigned char *request, const int request_len)
{
    EXAMPLE_TRACE("Cloud Rawdata Arrived");

    return 0;
}

/* device initialized event callback */
static int user_initialized(const int devid)
{
    EXAMPLE_TRACE("Device Initialized");
    g_user_example_ctx.master_initialized = 1;

    return 0;
}

/** recv property post response message from cloud **/
static int user_report_reply_event_handler(const int devid, const int msgid, const int code, const char *reply,
        const int reply_len)
{
    EXAMPLE_TRACE("Message Post Reply Received, Message ID: %d, Code: %d, Reply: %.*s", msgid, code,
                  reply_len,
                  (reply == NULL)? ("NULL") : (reply));
    return 0;
}

/** recv event post response message from cloud **/
static int user_trigger_event_reply_event_handler(const int devid, const int msgid, const int code, const char *eventid,
        const int eventid_len, const char *message, const int message_len)
{
    EXAMPLE_TRACE("Trigger Event Reply Received, Message ID: %d, Code: %d, EventID: %.*s, Message: %.*s",
                  msgid, code,
                  eventid_len,
                  eventid, message_len, message);

    return 0;
}


static int user_property_set_event_handler(const int devid, const char *request, const int request_len)
{
	
   
    
#if 0//ENABLE_ZB_MODULE
	int res = 0;
	EXAMPLE_TRACE("Property Set Received, Devid: %d, Request: %s", devid, request);
	cJSON *p_root,*ls;
	p_root= cJSON_Parse(request);
	if (p_root == NULL || !cJSON_IsObject(p_root)) {
	   HAL_Printf("JSON Parse Error\r\n");
	   return -1;
	}
	char *property_payload = HAL_Malloc(128);
	if(property_payload == NULL){
		return -1;
	}
	memset(property_payload,0,sizeof(*property_payload));

    ls = cJSON_GetObjectItem(p_root,"LightSwitch");
    if((ls  != NULL) && (cJSON_IsNumber(ls))){
    	zb_device_rxedcmd_process(devid,"LightSwitch",ls->valueint);
        snprintf(property_payload, 128, "{\"LightSwitch\":%d}", ls->valueint);
    }
    ls = cJSON_GetObjectItem(p_root,"Brightness");
    if((ls  != NULL) && (cJSON_IsNumber(ls))){
    	zb_device_rxedcmd_process(devid,"Brightness",ls->valueint);
        snprintf(property_payload, 128, "{\"Brightness\":%d}", ls->valueint);
    }
    ls = cJSON_GetObjectItem(p_root,"BinarySwitch");	
    if((ls  != NULL) && (cJSON_IsNumber(ls))){
    	zb_device_rxedcmd_process(devid,"BinarySwitch",ls->valueint);
	snprintf(property_payload, 128, "{\"BinarySwitch\":%d}", ls->valueint);
    }
	ls = cJSON_GetObjectItem(p_root,"startZbNet");
	if((ls	!= NULL) && (cJSON_IsNumber(ls))){
		zb_device_rxedcmd_process(ZB_DEVICE_ID_IVALID,"startZbNet",ls->valueint);
        snprintf(property_payload, 128, "{\"startZbNet\":%d}", ls->valueint);
	}
    ls = cJSON_GetObjectItem(p_root,"zbPermitJoin");
    if((ls  != NULL) && (cJSON_IsNumber(ls))){
        zb_device_rxedcmd_process(ZB_DEVICE_ID_IVALID,"zbPermitJoin",ls->valueint);
        snprintf(property_payload, 128, "{\"zbPermitJoin\":%d}", ls->valueint);
    }
	cJSON_Delete(p_root);
	res = IOT_Linkkit_Report(devid, ITM_MSG_POST_PROPERTY,
                             (unsigned char *)property_payload, strlen(property_payload));
	HAL_Free(property_payload);
	EXAMPLE_TRACE("user_property_set_event_handler: Post Property Message ID: %d", res);
    return 0;
#else
	int res = 0;
	cJSON *root = NULL, *LightSwitch = NULL, *LightColor = NULL, *ls=NULL;
	ESP_LOGI(TAG,"Property Set Received, Devid: %d, Request: %s", devid, request);

	lightbulb_set_brightness(78);
	lightbulb_set_saturation(100);

	if (!request) {
		return NULL_VALUE_ERROR;
	}

	/* Parse Root */
	root = cJSON_Parse(request);
	if (!root) {
		ESP_LOGI(TAG,"JSON Parse Error");
		return FAIL_RETURN;
	}
	ls = cJSON_GetObjectItem(root,"FactoryNew");
	if((ls	!= NULL) && (cJSON_IsBool(ls))){
		conn_mgr_reset_wifi_config();

	}

	/** Switch Lightbulb On/Off   */
	LightSwitch = cJSON_GetObjectItem(root, "LightSwitch");
	if (LightSwitch) {
		lightbulb_set_on(LightSwitch->valueint);
	} 

	/** Switch Lightbulb Hue */
	LightSwitch = cJSON_GetObjectItem(root, "RGBColor");
	if (LightSwitch) {
		LightColor = cJSON_GetObjectItem(LightSwitch, "Red");
		lightbulb_set_hue(LightColor ? LightColor->valueint : 0);
		LightColor = cJSON_GetObjectItem(LightSwitch, "Green");
		lightbulb_set_hue(LightColor ? LightColor->valueint : 120);
		LightColor = cJSON_GetObjectItem(LightSwitch, "Blue");
		lightbulb_set_hue(LightColor ? LightColor->valueint : 240);
	}

	ls = cJSON_GetObjectItem(root,"startZbNet");
	if((ls	!= NULL) && (cJSON_IsNumber(ls))){
		zb_device_rxedcmd_process(ZB_DEVICE_ID_IVALID,"startZbNet",ls->valueint);
   
	}
    ls = cJSON_GetObjectItem(root,"zbPermitJoin");
    if((ls  != NULL) && (cJSON_IsNumber(ls))){
        zb_device_rxedcmd_process(ZB_DEVICE_ID_IVALID,"zbPermitJoin",ls->valueint);

    }
    ls = cJSON_GetObjectItem(root,"zbFnReset");
    if((ls  != NULL) && (cJSON_IsNumber(ls))){
        zb_device_rxedcmd_process(ZB_DEVICE_ID_IVALID,"startZbNet",2);

    }

	cJSON_Delete(root);

	res = IOT_Linkkit_Report(EXAMPLE_MASTER_DEVID, ITM_MSG_POST_PROPERTY,
							 (unsigned char *)request, request_len);
	ESP_LOGI(TAG,"Post Property Message ID: %d", res);

	return SUCCESS_RETURN;

#endif
    
    
}

static int user_property_desired_get_reply_event_handler(const char *serviceid, const int serviceid_len)
{
    ESP_LOGI(TAG, "ITE_PROPERTY_DESIRED_GET_REPLY");

    return SUCCESS_RETURN;
}

static int user_property_get_event_handler(const int devid, const char *serviceid, const int serviceid_len, char **response, int *response_len)
{
    ESP_LOGI(TAG,"Get Property Message ID: %d", devid);

    return SUCCESS_RETURN;
}


static int user_service_request_event_handler(const int devid, const char *serviceid, const int serviceid_len,
        const char *request, const int request_len,
        char **response, int *response_len)
{
    int contrastratio = 0, to_cloud = 0;
    cJSON *root = NULL, *item_transparency = NULL, *item_from_cloud = NULL;
    ESP_LOGI(TAG,"Service Request Received, Devid: %d, Service ID: %.*s, Payload: %s", devid, serviceid_len,
                  serviceid,
                  request);

    /* Parse Root */
    root = cJSON_Parse(request);
    if (root == NULL || !cJSON_IsObject(root)) {
        ESP_LOGI(TAG,"JSON Parse Error");
        return -1;
    }

    if (strlen("Custom") == serviceid_len && memcmp("Custom", serviceid, serviceid_len) == 0) {
        /* Parse Item */
        const char *response_fmt = "{\"Contrastratio\":%d}";
        item_transparency = cJSON_GetObjectItem(root, "transparency");
        if (item_transparency == NULL || !cJSON_IsNumber(item_transparency)) {
            cJSON_Delete(root);
            return -1;
        }
        ESP_LOGI(TAG,"transparency: %d", item_transparency->valueint);
        contrastratio = item_transparency->valueint + 1;

        /* Send Service Response To Cloud */
        *response_len = strlen(response_fmt) + 10 + 1;
        *response = malloc(*response_len);
        if (*response == NULL) {
            ESP_LOGW(TAG,"Memory Not Enough");
            return -1;
        }
        memset(*response, 0, *response_len);
        snprintf(*response, *response_len, response_fmt, contrastratio);
        *response_len = strlen(*response);
    } else if (strlen("SyncService") == serviceid_len && memcmp("SyncService", serviceid, serviceid_len) == 0) {
        /* Parse Item */
        const char *response_fmt = "{\"ToCloud\":%d}";
        item_from_cloud = cJSON_GetObjectItem(root, "FromCloud");
        if (item_from_cloud == NULL || !cJSON_IsNumber(item_from_cloud)) {
            cJSON_Delete(root);
            return -1;
        }
        ESP_LOGI(TAG,"FromCloud: %d", item_from_cloud->valueint);
        to_cloud = item_from_cloud->valueint + 1;

        /* Send Service Response To Cloud */
        *response_len = strlen(response_fmt) + 10 + 1;
        *response = malloc(*response_len);
        if (*response == NULL) {
            ESP_LOGW(TAG,"Memory Not Enough");
            return -1;
        }
        memset(*response, 0, *response_len);
        snprintf(*response, *response_len, response_fmt, to_cloud);
        *response_len = strlen(*response);
    }
    cJSON_Delete(root);

    return 0;
}

static int user_timestamp_reply_event_handler(const char *timestamp)
{
    EXAMPLE_TRACE("Current Timestamp: %s", timestamp);

    return SUCCESS_RETURN;
}

static int user_toplist_reply_event_handler(const int devid, const int msgid, const int code, const char *eventid, const int eventid_len)
{
    EXAMPLE_TRACE("ITE_TOPOLIST_REPLY");

    return SUCCESS_RETURN;
}

int user_permit_join_event_handler(const char *product_key, const int time)
{
    user_example_ctx_t *user_example_ctx = &g_user_example_ctx;

    EXAMPLE_TRACE("Product Key: %s, Time: %d", product_key, time);

    user_example_ctx->permit_join = 1;
#if ENABLE_ZB_MODULE
	eSetPermitJoining(time);//zb permit join set
#endif
    return 0;
}


/** fota event handler **/
static int user_fota_event_handler(int type, const char *version)
{
    char buffer[1024] = {0};
    int buffer_length = 1024;

    /* 0 - new firmware exist, query the new firmware */
    if (type == 0) {
        EXAMPLE_TRACE("New Firmware Version: %s", version);

        IOT_Linkkit_Query(EXAMPLE_MASTER_DEVID, ITM_MSG_QUERY_FOTA_DATA, (unsigned char *)buffer, buffer_length);
    }

    return 0;
}

/* cota event handler */
static int user_cota_event_handler(int type, const char *config_id, int config_size, const char *get_type,
                                   const char *sign, const char *sign_method, const char *url)
{
    char buffer[128] = {0};
    int buffer_length = 128;

    /* type = 0, new config exist, query the new config */
    if (type == 0) {
        EXAMPLE_TRACE("New Config ID: %s", config_id);
        EXAMPLE_TRACE("New Config Size: %d", config_size);
        EXAMPLE_TRACE("New Config Type: %s", get_type);
        EXAMPLE_TRACE("New Config Sign: %s", sign);
        EXAMPLE_TRACE("New Config Sign Method: %s", sign_method);
        EXAMPLE_TRACE("New Config URL: %s", url);

        IOT_Linkkit_Query(EXAMPLE_MASTER_DEVID, ITM_MSG_QUERY_COTA_DATA, (unsigned char *)buffer, buffer_length);
    }

    return 0;
}

static int user_mqtt_connect_succ_event_handler(void)
{
    EXAMPLE_TRACE("ITE_MQTT_CONNECT_SUCC");
    
    return SUCCESS_RETURN;
}
void tem_sensor_property_post(float temp, float hum){

    int res = 0;

    char property_payload[128] = {0};
    HAL_Snprintf(property_payload, sizeof(property_payload), "{\"CurrentTemperature\": %f,\"CurrentHumidity\": %f}", temp,hum);
	//cnt += HAL_Snprintf(property_payload + cnt, sizeof(property_payload) - cnt, "{\"CurrentHumidity\": %f}", hum);

    res = IOT_Linkkit_Report(g_user_example_ctx.master_devid, ITM_MSG_POST_PROPERTY,
                             (unsigned char *)property_payload, strlen(property_payload));

    EXAMPLE_TRACE("Post Property Message ID: %d", res);

}

static void user_post_property(void)
{
    static int cnt = 0;
    int res = 0;

    char property_payload[30] = {0};
    HAL_Snprintf(property_payload, sizeof(property_payload), "{\"Counter\": %d}", cnt++);

    res = IOT_Linkkit_Report(EXAMPLE_MASTER_DEVID, ITM_MSG_POST_PROPERTY,
                             (unsigned char *)property_payload, strlen(property_payload));

    EXAMPLE_TRACE("Post Property Message ID: %d", res);
}

static void user_post_event(void)
{
    int res = 0;
    char *event_id = "HardwareError";
    char *event_payload = "{\"ErrorCode\": 0}";

    res = IOT_Linkkit_TriggerEvent(EXAMPLE_MASTER_DEVID, event_id, strlen(event_id),
                                   event_payload, strlen(event_payload));
    EXAMPLE_TRACE("Post Event Message ID: %d", res);
}

static void user_deviceinfo_update(void)
{
    int res = 0;
    char *device_info_update = "[{\"attrKey\":\"abc\",\"attrValue\":\"hello,world\"}]";

    res = IOT_Linkkit_Report(EXAMPLE_MASTER_DEVID, ITM_MSG_DEVICEINFO_UPDATE,
                             (unsigned char *)device_info_update, strlen(device_info_update));
    EXAMPLE_TRACE("Device Info Update Message ID: %d", res);
}

static void user_deviceinfo_delete(void)
{
    int res = 0;
    char *device_info_delete = "[{\"attrKey\":\"abc\"}]";

    res = IOT_Linkkit_Report(EXAMPLE_MASTER_DEVID, ITM_MSG_DEVICEINFO_DELETE,
                             (unsigned char *)device_info_delete, strlen(device_info_delete));
    EXAMPLE_TRACE("Device Info Delete Message ID: %d", res);
}

bool linkkit_cloud_connected(void ){

	return (g_user_example_ctx.cloud_connected == 1);

}

static int linkkit_thread(void *paras)
{
    int res = 0;
    iotx_linkkit_dev_meta_info_t master_meta_info;
    int domain_type = 0, dynamic_register = 0, post_reply_need = 0;
	int subdev_index = -1;

#ifdef ATM_ENABLED
    if (IOT_ATM_Init() < 0) {
        EXAMPLE_TRACE("IOT ATM init failed!\n");
        return -1;
    }
#endif

    memset(&g_user_example_ctx, 0, sizeof(user_example_ctx_t));
	g_user_example_ctx.subdev_index = -1;

    HAL_GetProductKey(PRODUCT_KEY);
    HAL_GetProductSecret(PRODUCT_SECRET);
    HAL_GetDeviceName(DEVICE_NAME);
    HAL_GetDeviceSecret(DEVICE_SECRET);
    memset(&master_meta_info, 0, sizeof(iotx_linkkit_dev_meta_info_t));
    memcpy(master_meta_info.product_key, PRODUCT_KEY, strlen(PRODUCT_KEY));
    memcpy(master_meta_info.product_secret, PRODUCT_SECRET, strlen(PRODUCT_SECRET));
    memcpy(master_meta_info.device_name, DEVICE_NAME, strlen(DEVICE_NAME));
    memcpy(master_meta_info.device_secret, DEVICE_SECRET, strlen(DEVICE_SECRET));

    /* Register Callback */
    IOT_RegisterCallback(ITE_AWSS_STATUS, user_awss_status_event_handler);
    IOT_RegisterCallback(ITE_CONNECT_SUCC, user_connected_event_handler);
    IOT_RegisterCallback(ITE_CONNECT_FAIL, user_connect_fail_event_handler);
    IOT_RegisterCallback(ITE_DISCONNECTED, user_disconnected_event_handler);
    IOT_RegisterCallback(ITE_RAWDATA_ARRIVED, user_rawdata_arrived_event_handler);
    IOT_RegisterCallback(ITE_SERVICE_REQUEST, user_service_request_event_handler);
    IOT_RegisterCallback(ITE_PROPERTY_SET, user_property_set_event_handler);
    IOT_RegisterCallback(ITE_PROPERTY_GET, user_property_get_event_handler);
    IOT_RegisterCallback(ITE_PROPERTY_DESIRED_GET_REPLY, user_property_desired_get_reply_event_handler);
    IOT_RegisterCallback(ITE_REPORT_REPLY, user_report_reply_event_handler);
    IOT_RegisterCallback(ITE_TRIGGER_EVENT_REPLY, user_trigger_event_reply_event_handler);
    IOT_RegisterCallback(ITE_TIMESTAMP_REPLY, user_timestamp_reply_event_handler);
    IOT_RegisterCallback(ITE_TOPOLIST_REPLY, user_toplist_reply_event_handler);
    IOT_RegisterCallback(ITE_PERMIT_JOIN, user_permit_join_event_handler);
    IOT_RegisterCallback(ITE_INITIALIZE_COMPLETED, user_initialized);
    IOT_RegisterCallback(ITE_FOTA, user_fota_event_handler);
    IOT_RegisterCallback(ITE_COTA, user_cota_event_handler);
    IOT_RegisterCallback(ITE_MQTT_CONNECT_SUCC, user_mqtt_connect_succ_event_handler);
	

    domain_type = IOTX_CLOUD_REGION_SHANGHAI;
    IOT_Ioctl(IOTX_IOCTL_SET_DOMAIN, (void *)&domain_type);

    /* Choose Login Method */
    dynamic_register = 0;
    IOT_Ioctl(IOTX_IOCTL_SET_DYNAMIC_REGISTER, (void *)&dynamic_register);

    /* post reply doesn't need */
    post_reply_need = 1;
    IOT_Ioctl(IOTX_IOCTL_RECV_EVENT_REPLY, (void *)&post_reply_need);

    /* Create Master Device Resources */
    g_user_example_ctx.master_devid = IOT_Linkkit_Open(IOTX_LINKKIT_DEV_TYPE_MASTER, &master_meta_info);
    if (g_user_example_ctx.master_devid < 0) {
        EXAMPLE_TRACE("IOT_Linkkit_Open Failed\n");
        return -1;
    }

    /* Start Connect Aliyun Server */
    res = IOT_Linkkit_Connect(g_user_example_ctx.master_devid);
    if (res < 0) {
        EXAMPLE_TRACE("IOT_Linkkit_Connect Failed\n");
        IOT_Linkkit_Close(g_user_example_ctx.master_devid);
        return -1;
    }

#if ENABLE_ZB_MODULE
	sys_mgmt_load_config();
	if (sys_mgmt_first_connect()) {
		eZCB_Init();
		if (eErasePersistentData() != E_ZCB_OK) {
			printf("Erase Zigbee PD failed\r\n");
		}
        vZDM_ClearAllDeviceTables();
		/* wait a while to avoid zigbee data transaction when operate FLASH */
		vTaskDelay(1000);
		sys_mgmt_mark_connected();

	}else{
       vZDM_GetAllDeviceTable();
       vZDM_SetAllDeviceOffLine();  
       vZDM_SetAllAttributeDataPointerToNull();  
       eZCB_Init();
	}
#endif

    while (1) {
        IOT_Linkkit_Yield(EXAMPLE_YIELD_TIMEOUT_MS);
		/* Add subdev */
#if ENABLE_ZB_MODULE

        subdev_index = get_index_from_queue(&g_user_example_ctx);
        //if(user_example_ctx->permit_join == 1)
        {
            if ( subdev_index < active_sub_device_num )
            {
                  if (subdev_index < EXAMPLE_SUBDEV_MAX_NUM)
                  {
                      /* Add subdev from queue*/
                      if(subdev_index != -1)
                      {
                         gateway_add_subdev(subdev_index,&g_user_example_ctx);
                      // user_example_ctx->permit_join = 0;
                      }                                              
                  }              
            }
        }
#endif
    }

    IOT_Linkkit_Close(g_user_example_ctx.master_devid);

    IOT_DumpMemoryStats(IOT_LOG_DEBUG);
    IOT_SetLogLevel(IOT_LOG_NONE);
    return 0;
}

void linkkit_main(void *paras)
{
    while (1) {
        linkkit_thread(NULL);
    }
}
#endif
