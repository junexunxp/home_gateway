/* Copyright 2018 NXP */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "infra_config.h"
#include "infra_compat.h"

#include "serial_link.h"
#include "serial.h"
#include "mqtt_wrapper.h"
#include "zb_common.h"
#if ENABLE_ZB_MODULE


#define ZB_DEVICE_ENDPOINT_COUNT_DEFAULT        1

extern TimerHandle_t zbDeviceTimer;

tsZbNetworkInfo zbNetworkInfo;
tsZbDeviceInfo deviceTable[MAX_ZD_DEVICE_NUMBERS];
tsZbDeviceAttribute attributeTable[MAX_ZD_ATTRIBUTE_NUMBERS_TOTAL];
tsZbDeviceConfigReport reportHeartBeatTable[MAX_ZD_DEVICE_NUMBERS];

extern sub_dev_addr_map_t sub_dev_am[EXAMPLE_SUBDEV_MAX_NUM];

/***export function****/
extern tsZbDeviceInfo* zb_device_find_device_info_by_device_id(uint32_t device_id);

extern void user_devcount_post_property(uint16_t value);

/***local function****/
static teZcbStatus zb_device_manage_set_hearbeat_interval(uint16_t u16NodeId,
												  teZbDeviceType eDeviceType,
                                                  uint8_t u8SrcEndpoint,
                                                  uint8_t u8DstEndpoint,
                                                  uint16_t u16ClusterId,
                                                  uint16_t u16AttributeId,
                                                  uint8_t u8DataType,
                                                  uint16_t u16TimeOut,
                                                  uint64_t u64Change);



static TimerHandle_t zbd_second_timer = NULL;
static QueueHandle_t zbd_timer_event_mutex = NULL;
QueueHandle_t zbd_sudev_am_mutex = NULL;
QueueHandle_t intSubdevAddressMapQueue = NULL;
struct list_head zb_device_list_head;
struct list_head zb_device_timer_head;

typedef int (*zbd_timer_cb_fun)(void *args);

typedef struct{
	struct dlist_s *prev;
	struct dlist_s *next;
	int time_set;
	int time_left;
	zbd_timer_cb_fun cb_function;
	void *cb_args;
}zbd_timer_event_t;


static void zbd_s_timer_cb(TimerHandle_t cb_timerhdl){
	xSemaphoreTake(zbd_timer_event_mutex, portMAX_DELAY);
	if(list_empty(&zb_device_timer_head)){

		HAL_Printf("Error state, no items in the timer event list\r\n");
		xTimerStop(zbd_second_timer, 0);
		return;
	}
	zbd_timer_event_t *wte = (zbd_timer_event_t *)zb_device_timer_head.next;
	do{
		if(wte->time_left > 0){
			wte->time_left--;
	
		}else{
			int ret_cb = -1;
			if(wte->cb_function){
				
				ret_cb = wte->cb_function(wte->cb_args);
				
			}
			
			if(ret_cb == -1){
				
				list_del((dlist_t *)wte);
				vPortFree(wte);
				
			}else if(ret_cb == 0){

				wte->time_left = wte->time_set;
				
			}else{
				wte->time_left = ret_cb;
			}
			
            
			
		}
        wte= (zbd_timer_event_t *)wte->next;
		if(!wte){
			break;
		}
	}while((void *)wte != (void *)&zb_device_timer_head);

	if(list_empty(&zb_device_timer_head)){
		HAL_Printf("All timer event handled, will stop the periodic timer\r\n");
		xTimerStop(zbd_second_timer, 0);
	}
	xSemaphoreGive(zbd_timer_event_mutex);

}




static int zbd_s_timer_start(int time_s, zbd_timer_cb_fun cb_function, void *cb_args){
	
	if(list_empty(&zb_device_timer_head)){
		xTimerStart(zbd_second_timer,pdMS_TO_TICKS(1000));
	}
	
	xSemaphoreTake(zbd_timer_event_mutex, portMAX_DELAY);
	zbd_timer_event_t *wme = pvPortMalloc(sizeof(zbd_timer_event_t));
	if(!wme){
		xSemaphoreGive(zbd_timer_event_mutex);

		return - 1;
	}
	memset(wme,0,sizeof(*wme));
	wme->time_left = time_s;
	wme->time_set = time_s;
	wme->cb_args = cb_args;
	wme->cb_function = cb_function;
	list_add((dlist_t *)wme,&zb_device_timer_head);
	xSemaphoreGive(zbd_timer_event_mutex);
	return 0;

}

static void zbd_s_timer_timeout_set(void *cbargs, int time_s){
	xSemaphoreTake(zbd_timer_event_mutex, portMAX_DELAY);
	if(!list_empty(&zb_device_timer_head)){
		
		zbd_timer_event_t *wte = (zbd_timer_event_t *)zb_device_timer_head.next;
		do{
			if(wte->cb_args == cbargs){
				wte->time_left = time_s?time_s:wte->time_set;
				break;
			}
			wte= (zbd_timer_event_t *)wte->next;
			if(!wte){
				break;
			}
		}while((void *)wte != (void *)&zb_device_timer_head);
	}
	xSemaphoreGive(zbd_timer_event_mutex);



}



static void zbd_s_timer_stop(void *cbargs){
	xSemaphoreTake(zbd_timer_event_mutex, portMAX_DELAY);
	if(!list_empty(&zb_device_timer_head)){
		
		zbd_timer_event_t *wte = (zbd_timer_event_t *)zb_device_timer_head.next;
		do{
			if(wte->cb_args == cbargs){
				list_del((dlist_t *)wte);
				vPortFree(wte);
				break;
			}
            wte= (zbd_timer_event_t *)wte->next;
			if(!wte){
				break;
			}
		}while((void *)wte != (void *)&zb_device_timer_head);
		if(list_empty(&zb_device_timer_head)){
			xTimerStop(zbd_second_timer, 0);
			HAL_Printf("wm timer stopped\r\n");
		}
	}
	xSemaphoreGive(zbd_timer_event_mutex);
}


 bool bZD_ValidityCheckOfNodeId(uint16_t u16NodeId)
{
    if ((u16NodeId == ZB_DEVICE_TABLE_NULL_NODE_ID) 
        || (u16NodeId >= E_ZB_BROADCAST_ADDRESS_LOWPOWERROUTERS)) {
        HAL_Printf("Not the legal device node id\r\n");
        return false;
    }
    return true;
}



static bool bZD_ValidityCheckOfIeeeAddr(uint64_t u64IeeeAddr)
{
    if (u64IeeeAddr == ZB_DEVICE_TABLE_NULL_IEEE_ADDR) {
        HAL_Printf("Not the legal device ieee addr\r\n");
        return false;
    }
    return true;
}



static bool bZD_ValidityCheckOfEndpointId(uint8_t u8Endpoint)
{
    //Endpoint 0:   For ZDO
    //Endpoint 1-240: For Application
    //Endpoint 242: For GreenPower
    
    if ((u8Endpoint == 0) || (u8Endpoint == 241) || (u8Endpoint > 242)) {
        HAL_Printf("Not the legal endpoint id\r\n");
        return false;
    }
    return true;
}



uint8_t uZDM_FindDevTableIndexByNodeId(uint16_t u16NodeId)
{
	if (!bZD_ValidityCheckOfNodeId(u16NodeId)) {		
		return 0xFF;
	}
    
	for(uint8_t i = 0; i < MAX_ZD_DEVICE_NUMBERS; i++)
	{
		if(deviceTable[i].u16NodeId == u16NodeId) {
			return i;
		}
	}
    
	HAL_Printf("No this device exits in current device table!\r\n");
	return 0xFF;
}



tsZbDeviceInfo* tZDM_FindDeviceByIndex(uint8_t u8Index)
{
	if (u8Index > MAX_ZD_DEVICE_NUMBERS) {
		return NULL;
	}	
	return &(deviceTable[u8Index]);
}


tsZbDeviceInfo* tZDM_FindDeviceByNodeId(uint16_t u16NodeId)
{
	if (!bZD_ValidityCheckOfNodeId(u16NodeId)) {		
		return NULL;
	}
	
	for(uint8_t i = 0; i < MAX_ZD_DEVICE_NUMBERS; i++)
	{
		if(deviceTable[i].u16NodeId == u16NodeId) {
			return &(deviceTable[i]);
		}
	}
	HAL_Printf("No this device exits in current device table!\r\n");
	return NULL;
}



tsZbDeviceInfo* tZDM_FindDeviceByIeeeAddress(uint64_t u64IeeeAddr, uint16_t shortaddr)
{
	if (!bZD_ValidityCheckOfIeeeAddr(u64IeeeAddr)) {		
		return NULL;
	}

	for(uint8_t i = 0; i < MAX_ZD_DEVICE_NUMBERS; i++)
	{
		if(deviceTable[i].u64IeeeAddress == u64IeeeAddr) {
			if(shortaddr && (deviceTable[i].u16NodeId != shortaddr)){
				HAL_Printf("Short address changed... should update it!\r\n");
				deviceTable[i].u16NodeId = shortaddr;
				HAL_Kv_Set(DBM_ZD_DEVICE_TABLE_KEY,deviceTable,sizeof(tsZbDeviceInfo)*MAX_ZD_DEVICE_NUMBERS,0);
				
			}
			return &(deviceTable[i]);
		}
	}
	HAL_Printf("No this device exits in current device table!\r\n");
	return NULL;	
}



tsZbDeviceInfo* tZDM_AddNewDeviceToDeviceTable(uint16_t u16NodeId, uint64_t u64IeeeAddr,teZbDeviceType eZrZedType)
{
	int status = 0;
	if (!bZD_ValidityCheckOfNodeId(u16NodeId)) {		
		return NULL;
	}

	if (!bZD_ValidityCheckOfIeeeAddr(u64IeeeAddr)) {		
		return NULL;
	}    

	for (uint8_t i = 0; i < MAX_ZD_DEVICE_NUMBERS; i++)
	{	
	    if (deviceTable[i].u16NodeId == ZB_DEVICE_TABLE_NULL_NODE_ID) {
			memset(&(deviceTable[i]), 0, sizeof(tsZbDeviceInfo));
			deviceTable[i].u16NodeId = u16NodeId;
			deviceTable[i].u64IeeeAddress = u64IeeeAddr;
			deviceTable[i].eDeviceState = E_ZB_DEVICE_STATE_NEW_JOINED;
			deviceTable[i].eZrZedType = eZrZedType;
            zbNetworkInfo.u16DeviceCount ++;
		//	user_devcount_post_property(zbNetworkInfo.u16DeviceCount);
            status = HAL_Kv_Set(DBM_ZD_DEVICE_TABLE_KEY,deviceTable,sizeof(tsZbDeviceInfo)*MAX_ZD_DEVICE_NUMBERS,0);
			if(status != 0){
				HAL_Printf("Save device table failed! status:%d\r\n",status);
			}
				
            status = HAL_Kv_Set(DBM_ZD_NETWORK_INFO_KEY,&zbNetworkInfo,sizeof(tsZbNetworkInfo),0);
			if(status != 0){
				HAL_Printf("Save network info failed! status:%d\r\n",status);
			}
			
			return &(deviceTable[i]);
		}		
	}
	HAL_Printf("The device table is full already!\r\n");
	return NULL;
}

uint16_t zb_device_child_num(void ){
	return zbNetworkInfo.u16DeviceCount;


}


tsZbDeviceAttribute* tZDM_AttributeInAttributeTable(uint16_t u16NodeId,
                                                    uint8_t u8Endpoint,
                                                    uint16_t u16ClusterId,
                                                    uint16_t u16AttributeId,
                                                    uint8_t u8DataType)								                                    
                                  
{
   for(uint16_t i = 0; i < MAX_ZD_ATTRIBUTE_NUMBERS_TOTAL; i++)
   	{
	  	if(attributeTable[i].u16NodeId ==u16NodeId)
  		{
  			if(attributeTable[i].u8Endpoint ==u8Endpoint)
  		    {
  			    if(attributeTable[i].u16ClusterId ==u16ClusterId)
  				{		  		
					if(attributeTable[i].u16AttributeId == u16AttributeId)
			        {
			            return &(attributeTable[i]);
			        }
  			    }
  			}
  		}
   	}
   return NULL;
}


tsZbDeviceAttribute* tZDM_AddNewAttributeToAttributeTable(uint16_t u16NodeId,
                                                          uint8_t u8Endpoint,
                                                          uint16_t u16ClusterId,
                                                          uint16_t u16AttributeId,
                                                          uint8_t u8DataType)
{
	tsZbDeviceAttribute* findAttributeInAttributeTable=NULL;
	int status = 0;
	if (!bZD_ValidityCheckOfNodeId(u16NodeId)) {		
		return NULL;
	}

    if (!bZD_ValidityCheckOfEndpointId(u8Endpoint)) {		
		return NULL;
	}
	
	findAttributeInAttributeTable=tZDM_AttributeInAttributeTable(u16NodeId,u8Endpoint,u16ClusterId,u16AttributeId,u8DataType);
	if(findAttributeInAttributeTable!=NULL)
	  {
		  return findAttributeInAttributeTable;
	  }


    for (uint16_t i = 0; i < MAX_ZD_ATTRIBUTE_NUMBERS_TOTAL; i++) {
  
        if (attributeTable[i].u16NodeId == ZB_DEVICE_TABLE_NULL_NODE_ID) {
            attributeTable[i].u16NodeId      = u16NodeId;
            attributeTable[i].u8Endpoint     = u8Endpoint;
            attributeTable[i].u16ClusterId   = u16ClusterId;
            attributeTable[i].u16AttributeId = u16AttributeId;
            attributeTable[i].u8DataType     = u8DataType;
            tsZbDeviceCluster* devCluster = tZDM_FindClusterEntryInDeviceTable(u16NodeId, u8Endpoint, u16ClusterId);
            devCluster->u8AttributeCount ++;
		
            status = HAL_Kv_Set(DBM_ZD_ATTRIBUTE_TABLE_KEY,attributeTable,sizeof(tsZbDeviceAttribute)*MAX_ZD_ATTRIBUTE_NUMBERS_TOTAL,0);
            if(status != 0){
             HAL_Printf("The attribute table has not been saved to flash! status : %d\r\n",status);

            }
            return &(attributeTable[i]);
        }
    }
    HAL_Printf("The attribute table is full already!\r\n");
    return NULL;
}



tsZbDeviceEndPoint* tZDM_FindEndpointEntryInDeviceTable(uint16_t u16NodeId, uint8_t u8Endpoint)
{
    if (!bZD_ValidityCheckOfEndpointId(u8Endpoint)) {		
		return NULL;
	}

    tsZbDeviceInfo* sDevice = tZDM_FindDeviceByNodeId(u16NodeId);
    for (uint8_t i = 0; i < MAX_ZD_CLUSTER_NUMBERS_PER_EP; i++)
    {
        if (sDevice->sZDEndpoint[i].u8EndpointId == u8Endpoint)
            return &(sDevice->sZDEndpoint[i]);
    }
    return NULL;
}



tsZbDeviceCluster* tZDM_FindClusterEntryInDeviceTable(uint16_t u16NodeId,
                                                      uint8_t u8Endpoint,
                                                      uint16_t u16ClusterId)
{
    tsZbDeviceEndPoint *sEndpoint = tZDM_FindEndpointEntryInDeviceTable(u16NodeId, u8Endpoint);
    for (uint8_t i = 0; i < MAX_ZD_CLUSTER_NUMBERS_PER_EP; i++) {
        if (sEndpoint->sZDCluster[i].u16ClusterId == u16ClusterId)
            return &(sEndpoint->sZDCluster[i]);
    }
    return NULL;
}



tsZbDeviceAttribute* tZDM_FindAttributeEntryByIndex(uint16_t u16Index)
{
	if (u16Index > MAX_ZD_ATTRIBUTE_NUMBERS_TOTAL) {
		return NULL;
	}	
	return &(attributeTable[u16Index]);    
}



tsZbDeviceAttribute* tZDM_FindAttributeEntryByElement(uint16_t u16NodeId,
                                                      uint8_t u8Endpoint,
                                                      uint16_t u16ClusterId,
                                                      uint16_t u16AttributeId)
{
    if (!bZD_ValidityCheckOfNodeId(u16NodeId)) 
    {
        return NULL;
    }

    if (!bZD_ValidityCheckOfEndpointId(u8Endpoint)) {		
		return NULL;
	}

    for (uint16_t i = 0; i < MAX_ZD_ATTRIBUTE_NUMBERS_TOTAL; i++) {
        if (attributeTable[i].u16NodeId == u16NodeId) {
            if (attributeTable[i].u8Endpoint == u8Endpoint) {                
                if (attributeTable[i].u16ClusterId == u16ClusterId) {
                    if (attributeTable[i].u16AttributeId == u16AttributeId) {
                        return &(attributeTable[i]);
                    }
                }
            }
        }        
    }
    return NULL;    
}



uint8_t uZDM_FindAttributeListByElement(uint16_t u16NodeId,
                                        uint8_t u8Endpoint,
                                        uint16_t u16ClusterId,
                                        uint16_t auAttrList[])
{
	if (!bZD_ValidityCheckOfNodeId(u16NodeId)) {		
		return 0xFF;
	}

    if (!bZD_ValidityCheckOfEndpointId(u8Endpoint)) {		
		return 0xFF;
	}

    uint8_t attrCnt = 0;
    for (uint16_t i = 0; i < MAX_ZD_ATTRIBUTE_NUMBERS_TOTAL; i++) {
        if (attributeTable[i].u16NodeId == u16NodeId) {
            if (attributeTable[i].u8Endpoint == u8Endpoint) {                
                if (attributeTable[i].u16ClusterId == u16ClusterId) {
                    auAttrList[attrCnt ++] = attributeTable[i].u16AttributeId; 
                }
            }
        }        
    }
    tsZbDeviceCluster * devClus = tZDM_FindClusterEntryInDeviceTable(u16NodeId,
                                                                     u8Endpoint,
                                                                     u16ClusterId);
    devClus->u8AttributeCount = attrCnt;
    return attrCnt;
}



void bZDM_EraseAttributeInfoByNodeId(uint16_t u16NodeId)
{
    for (uint16_t i = 0; i < MAX_ZD_ATTRIBUTE_NUMBERS_TOTAL; i++) {
        if (attributeTable[i].u16NodeId == u16NodeId) {
            if ((attributeTable[i].u8DataType == E_ZCL_CSTRING)
                || (attributeTable[i].u8DataType == E_ZCL_OSTRING)) {
                if(attributeTable[i].uData.sData.pData != NULL)
                {
                	vPortFree(attributeTable[i].uData.sData.pData);
                }
            }
            memset(&(attributeTable[i]), 0, sizeof(tsZbDeviceAttribute));
        }
    }
	HAL_Kv_Set(DBM_ZD_ATTRIBUTE_TABLE_KEY,attributeTable,sizeof(tsZbDeviceAttribute)*MAX_ZD_ATTRIBUTE_NUMBERS_TOTAL,0);
}

void bZDM_EraseHeartBeatAttributeInfoByNodeId(uint16_t u16NodeId)
{
   for(uint16_t i=0;i<MAX_ZD_DEVICE_NUMBERS;i++)
   {
		if(reportHeartBeatTable[i].u16NodeId == u16NodeId) {	
				
		 	memset(&(reportHeartBeatTable[i]), 0, sizeof(tsZbDeviceConfigReport));
			break;

		}
   }
   HAL_Kv_Set(DBM_ZD_HEARTBEAT_TABLE_KEY,reportHeartBeatTable,sizeof(tsZbDeviceConfigReport)*MAX_ZD_DEVICE_NUMBERS,0);
   
}



bool bZDM_EraseDeviceFromDeviceTable(uint64_t u64IeeeAddr)
{
	if (!bZD_ValidityCheckOfIeeeAddr(u64IeeeAddr)) {		
		return NULL;
	}  

	for (uint8_t i = 0; i < MAX_ZD_DEVICE_NUMBERS; i++)
	{
		if (deviceTable[i].u64IeeeAddress == u64IeeeAddr) {
		    bZDM_EraseAttributeInfoByNodeId(deviceTable[i].u16NodeId);
			bZDM_EraseHeartBeatAttributeInfoByNodeId(deviceTable[i].u16NodeId);
			memset(&(deviceTable[i]), 0, sizeof(tsZbDeviceInfo));
			HAL_Kv_Set(DBM_ZD_DEVICE_TABLE_KEY,deviceTable,sizeof(tsZbDeviceInfo)*MAX_ZD_DEVICE_NUMBERS,0);			
			return true;
		}
	}
	HAL_Printf("No device can be erased!\r\n");
	return false;
}

bool bZDM_EraseDeviceFromNetworkInfo(void)
{
	zbNetworkInfo.u16DeviceCount--;
	HAL_Kv_Set(DBM_ZD_NETWORK_INFO_KEY,&zbNetworkInfo,sizeof(tsZbNetworkInfo),0);
	if(zbNetworkInfo.u16DeviceCount < 0)
		{
			HAL_Printf("zbNetworkInfo.u16DeviceCount has been error!\r\n");
			return false;
		}
	else
		{return true;}
}

tsZbDeviceConfigReport* tZDM_FindHeartBeatElementByNodeId(uint16_t u16NodeId)
{
	if (!bZD_ValidityCheckOfNodeId(u16NodeId)) {		
		return NULL;
	}
	
	for(uint8_t i = 0; i < MAX_ZD_DEVICE_NUMBERS; i++)
	{
		if(reportHeartBeatTable[i].u16NodeId == u16NodeId) {
			return &(reportHeartBeatTable[i]);
		}
	}
	HAL_Printf("No this Heartbeat Element exits in current Heartbeat table!\r\n");
	return NULL;
}


tsZbDeviceConfigReport* tZDM_ReportAttributeInHeartBeatTable(uint16_t u16NodeId,
                                                    uint8_t u8SrcEndpoint,
                                                    uint8_t u8DstEndpoint,
                                                    uint16_t u16ClusterId,
                                                    uint16_t u16AttributeId,
                                                    uint8_t u8DataType)								                                    
                                  
{
	if (!bZD_ValidityCheckOfNodeId(u16NodeId)) {		
		return NULL;
	}


   for(uint16_t i = 0; i < MAX_ZD_DEVICE_NUMBERS; i++)
   	{
	  	if(reportHeartBeatTable[i].u16NodeId ==u16NodeId)
  		{
		  	return &(reportHeartBeatTable[i]);
  		}
   	}
   return NULL;
}


tsZbDeviceConfigReport* tZDM_AddNewReportAttributeToHeartBeatTable(uint16_t u16NodeId,
                                                          uint8_t u8SrcEndpoint,
                                                          uint8_t u8DstEndpoint,
                                                          uint16_t u16ClusterId,
                                                          uint16_t u16AttributeId,
                                                          uint8_t u8DataType,
                                                          uint16_t u16MinIntv,
														  uint16_t u16MaxIntv,
														  uint16_t u16TimeOut,
														  uint64_t u64Change)
{
	tsZbDeviceConfigReport* findReportAttributeInHeartBeatTable=NULL;
	int status =0;
	if (!bZD_ValidityCheckOfNodeId(u16NodeId)) {		
		return NULL;
	}

    if (!bZD_ValidityCheckOfEndpointId(u8SrcEndpoint)) {		
		return NULL;
	}
	
	findReportAttributeInHeartBeatTable = tZDM_ReportAttributeInHeartBeatTable(u16NodeId,u8SrcEndpoint,u8DstEndpoint,u16ClusterId,u16AttributeId,u8DataType);
	if(findReportAttributeInHeartBeatTable!=NULL)
	  {
		  return findReportAttributeInHeartBeatTable;
	  }


    for (uint16_t i = 0; i < MAX_ZD_DEVICE_NUMBERS; i++) {
  
        if (reportHeartBeatTable[i].u16NodeId == ZB_DEVICE_TABLE_NULL_NODE_ID) {
            reportHeartBeatTable[i].u16NodeId      = u16NodeId;
            reportHeartBeatTable[i].u8DstEndpoint    = u8DstEndpoint;
            reportHeartBeatTable[i].u8SrcEndpoint   = u8SrcEndpoint;
            reportHeartBeatTable[i].u16AttributeId = u16AttributeId;
			reportHeartBeatTable[i].u16ClusterId = u16ClusterId;
            reportHeartBeatTable[i].u8DataType     = u8DataType;
			reportHeartBeatTable[i].u16MinIntv   = u16MinIntv;
            reportHeartBeatTable[i].u16MaxIntv = u16MaxIntv;
			reportHeartBeatTable[i].u16TimeOut = u16TimeOut;
            reportHeartBeatTable[i].u64Change  = u64Change;
			status = HAL_Kv_Set(DBM_ZD_HEARTBEAT_TABLE_KEY,reportHeartBeatTable,sizeof(tsZbDeviceConfigReport)*MAX_ZD_DEVICE_NUMBERS,0);
			if(status != 0){
				HAL_Printf("Save report HeartBeat table failed!\r\n");
			}
            return &(reportHeartBeatTable[i]);
        }
    }
    HAL_Printf("The heart beat attribute table is full already!\r\n");
    return NULL;
}




void vZDM_ClearAllDeviceTables()
{
    memset(deviceTable, 0, sizeof(tsZbDeviceInfo) * MAX_ZD_DEVICE_NUMBERS);
    memset(attributeTable, 0, sizeof(tsZbDeviceAttribute) * MAX_ZD_ATTRIBUTE_NUMBERS_TOTAL);
    memset(reportHeartBeatTable, 0, sizeof(tsZbDeviceConfigReport) * MAX_ZD_DEVICE_NUMBERS);
    memset(&zbNetworkInfo, 0, sizeof(tsZbNetworkInfo));
    HAL_Kv_Set(DBM_ZD_DEVICE_TABLE_KEY,deviceTable,sizeof(tsZbDeviceInfo)*MAX_ZD_DEVICE_NUMBERS,0);
    HAL_Kv_Set(DBM_ZD_NETWORK_INFO_KEY,&zbNetworkInfo,sizeof(tsZbNetworkInfo),0);
    HAL_Kv_Set(DBM_ZD_HEARTBEAT_TABLE_KEY,reportHeartBeatTable,sizeof(tsZbDeviceConfigReport)*MAX_ZD_DEVICE_NUMBERS,0);
    HAL_Kv_Set(DBM_ZD_ATTRIBUTE_TABLE_KEY,attributeTable,sizeof(tsZbDeviceAttribute)*MAX_ZD_ATTRIBUTE_NUMBERS_TOTAL,0);

}



void vZbDeviceTable_Init()
{
    if(zbd_second_timer == NULL){
            zbd_second_timer = xTimerCreate("wm_second_timer", pdMS_TO_TICKS(1000), pdTRUE, NULL, (TimerCallbackFunction_t)zbd_s_timer_cb);
            if(zbd_timer_event_mutex == NULL){
                    zbd_timer_event_mutex = (QueueHandle_t )xSemaphoreCreateMutex();
                    if(zbd_timer_event_mutex == NULL){

                            HAL_Printf("zbd_timer_event_mutex create failed\r\n");
                    }
            }

            list_init(&zb_device_list_head);
            list_init(&zb_device_timer_head);
    }
    
    if(zbd_sudev_am_mutex == NULL){
        zbd_sudev_am_mutex = (QueueHandle_t )xSemaphoreCreateMutex();
        if(zbd_sudev_am_mutex == NULL){
           HAL_Printf("zbd_sudev_am_mutex create failed\r\n");
        }
    }
    if(intSubdevAddressMapQueue == NULL){
    
         intSubdevAddressMapQueue = xQueueCreate(EXAMPLE_SUBDEV_MAX_NUM, sizeof(uint16_t));
         if(intSubdevAddressMapQueue == NULL){
            HAL_Printf("intSubdevAddressMapQueue create failed\r\n"); 
         }
    }
   
 //   vZDM_ClearAllDeviceTables();
}

int  vZDM_GetAllDeviceTable()
{
    int status = 0;
	int len =0;
	
	len = sizeof(tsZbDeviceInfo)*MAX_ZD_DEVICE_NUMBERS;
	status = HAL_Kv_Get(DBM_ZD_DEVICE_TABLE_KEY,deviceTable,&len);
	if(status != 0 )
	{
		HAL_Printf("dbManagerRead deviceTable fail\r\n");
	}

	len = sizeof(tsZbNetworkInfo);
	status = HAL_Kv_Get(DBM_ZD_NETWORK_INFO_KEY,&zbNetworkInfo,&len);
	if(status != 0 )
	{
		HAL_Printf("dbManagerRead zbNetworkInfo fail\r\n");
	}

	len = sizeof(tsZbDeviceAttribute)*MAX_ZD_ATTRIBUTE_NUMBERS_TOTAL;
	status = HAL_Kv_Get(DBM_ZD_ATTRIBUTE_TABLE_KEY,attributeTable,&len);
	if(status != 0 )
	{
		HAL_Printf("dbManagerRead attributeTable fail\r\n");
	}

	len = sizeof(tsZbDeviceConfigReport)*MAX_ZD_DEVICE_NUMBERS;
	status = HAL_Kv_Get(DBM_ZD_HEARTBEAT_TABLE_KEY,reportHeartBeatTable,&len);
	if(status != 0 )
	{
		HAL_Printf("dbManagerRead reportHeartBeatTable fail\r\n");
	}
	return status;
}

void vZDM_SetAllDeviceOffLine()
{
	for (uint16_t i = 0; i < MAX_ZD_DEVICE_NUMBERS; i++) {
		if(deviceTable[i].u64IeeeAddress != NULL){
		deviceTable[i].eDeviceState = E_ZB_DEVICE_STATE_OFF_LINE;
		
		}
	}
        HAL_Kv_Set(DBM_ZD_DEVICE_TABLE_KEY,deviceTable,sizeof(tsZbDeviceInfo)*MAX_ZD_DEVICE_NUMBERS,0);
}

void vZDM_SetAllAttributeDataPointerToNull()
{
	for (uint16_t i = 0; i < MAX_ZD_ATTRIBUTE_NUMBERS_TOTAL; i++) {
	if ((attributeTable[i].u8DataType == E_ZCL_CSTRING)
                || (attributeTable[i].u8DataType == E_ZCL_OSTRING)) {
				attributeTable[i].uData.sData.pData = NULL;
		  }
		
	}
      HAL_Kv_Set(DBM_ZD_ATTRIBUTE_TABLE_KEY,attributeTable,sizeof(tsZbDeviceAttribute)*MAX_ZD_ATTRIBUTE_NUMBERS_TOTAL,0);
}


void vZDM_NewDeviceQualifyProcess(tsZbDeviceInfo* device)
{
    uint8_t i,j,k;
    bool loop = true;
    uint16_t au16AttrList[1] = {E_ZB_ATTRIBUTEID_BASIC_MODEL_ID};
    static uint8_t epArrayIndex = 0;
    uint16_t clusterId;
    while (loop) {
        switch (device->eDeviceState)
        {
            case E_ZB_DEVICE_STATE_NEW_JOINED:
            {
                /*
                  After sending device announce notification to the host, the coordinator will 
                  process PDM saving of EndDevice timeout value. If the host send the uart data
                  to the coordinator at this time, it would cause the coordinator stack dump and
                  watchdog reset.
                  It may be a bug of JN5189 zigbee stack, add some delay here to avoid seen issue.
                */
                vTaskDelay (pdMS_TO_TICKS(100));
                for (i = 0; i < 2; i++)
                {
                    if (eActiveEndpointRequest(device->u16NodeId) != E_ZCB_OK)
                    {
                        HAL_Printf("Sending active endpoint request fail\r\n");
                        if (i == 1) {
                            device->u8EndpointCount = ZB_DEVICE_ENDPOINT_COUNT_DEFAULT;
                            device->eDeviceState = E_ZB_DEVICE_STATE_GET_CLUSTER;
                        } else {
                            vTaskDelay (pdMS_TO_TICKS(50));
                        }
                    }
                    else
                    {
                        //wait 1s for the active endpoint response
                        if (eSL_MessageWait(E_SL_MSG_ACTIVE_ENDPOINT_RESPONSE, 1000, NULL, NULL) != E_SL_OK)
                        {
                            HAL_Printf("No active endpoint response is received\r\n");
                            if (i == 1) {
                                device->u8EndpointCount = ZB_DEVICE_ENDPOINT_COUNT_DEFAULT;
                                device->eDeviceState = E_ZB_DEVICE_STATE_GET_CLUSTER;
                            } else {
                                vTaskDelay (pdMS_TO_TICKS(50));
                            }
                        } else {
                            loop = false;
                            break;
                        }
                    }
                }
            }
                break;
                
            case E_ZB_DEVICE_STATE_GET_CLUSTER:
                epArrayIndex ++;
                for (i = 0; i < 2; i++)
                {                   
                    if (eSimpleDescriptorRequest(device->u16NodeId, device->sZDEndpoint[epArrayIndex - 1].u8EndpointId) != E_ZCB_OK)
                    {
                        HAL_Printf( "Sending simple descriptor request fail\n");                        
                    }
                    else
                    {
                        //wait 1s for the simple descriptor response
                        if (eSL_MessageWait(E_SL_MSG_SIMPLE_DESCRIPTOR_RESPONSE, 1000, NULL, NULL) != E_SL_OK) {
                            HAL_Printf("No simple descriptor response is received\n");
                        } else {
                            break;
                        }
                    }
                    vTaskDelay (pdMS_TO_TICKS(50));
                }
                loop = false;
                break;
                
            case E_ZB_DEVICE_STATE_READ_ATTRIBUTE:
            {
                for (i = 0; i < device->u8EndpointCount; i++)
                {
                    for (j = 0; j < device->sZDEndpoint[i].u8ClusterCount; j++)
                    {
                        clusterId = device->sZDEndpoint[i].sZDCluster[j].u16ClusterId;
                        switch (clusterId)
                        {
                            case E_ZB_CLUSTERID_BASIC:
                            {                
                                tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
                                                                     device->sZDEndpoint[i].u8EndpointId,
                                                                     clusterId,
                                                                     E_ZB_ATTRIBUTEID_BASIC_MODEL_ID,
                                                                     E_ZB_ATTRIBUTE_STRING_TYPE);                

                                for (k = 0; k < 2; k++)
                                {
                                    if (eReadAttributeRequest(E_ZD_ADDRESS_MODE_SHORT, 
                                                              device->u16NodeId, 
                                                              ZB_ENDPOINT_SRC_DEFAULT, 
                                                              device->sZDEndpoint[i].u8EndpointId, 
                                                              E_ZB_CLUSTERID_BASIC,  
                                                              ZB_MANU_CODE_DEFAULT, 
                                                              MANUFACTURER_SPECIFIC_FALSE,
                                                              1, 
                                                              au16AttrList) != E_ZCB_OK)
                                    {
                                        HAL_Printf("Sending basic model id read request fail\r\n");
                                        if (k == 1) {
                                            /*eMgmtLeaveRequst(device->u16NodeId, device->u64IeeeAddress, 0, 1);
                                            if (bZDM_EraseDeviceFromDeviceTable(device->u64IeeeAddress)) {
                                                ZCB_DEBUG( "Erase Device Successfullly\r\n");
                                            }*/
                                        } else {
                                            vTaskDelay (pdMS_TO_TICKS(50));
                                        }
                                    }
                                    else
                                    {
                                        //wait 1s for the basic mode id response
                                        if (eSL_MessageWait(E_SL_MSG_READ_ATTRIBUTE_RESPONSE, 1000, NULL, NULL) != E_SL_OK)
                                        {
                                           HAL_Printf("No basic model id response is received\r\n");
                                            if (k == 1) {
                                                /*eMgmtLeaveRequst(device->u16NodeId, device->u64IeeeAddress, 0, 1);
                                                if (bZDM_EraseDeviceFromDeviceTable(device->u64IeeeAddress)) {
                                                    ZCB_DEBUG( "Erase Device Successfullly\r\n");
                                                }*/
                                            } else {
                                                vTaskDelay (pdMS_TO_TICKS(50));
                                            }
                                        } else {
                                            loop = false;
                                            break;
                                        }                       
                                    }
                                }
                            }
                                break;
                                
                            default:
                                break;
                        }
                    }
                }
            }
                loop = false;
                break;
                
            case E_ZB_DEVICE_STATE_BIND_CLUSTER:
            {
                for (i = 0; i < device->u8EndpointCount; i++)
                {
                    for (j = 0; j < device->sZDEndpoint[i].u8ClusterCount; j++)
                    {
                        uint16_t clusterId = device->sZDEndpoint[i].sZDCluster[j].u16ClusterId;
                        switch (clusterId)
                        {
                            case E_ZB_CLUSTERID_BASIC:                            
                                break;
                                
                            case E_ZB_CLUSTERID_ONOFF:
                            {
                                tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
                                                                     device->sZDEndpoint[i].u8EndpointId,
                                                                     clusterId,
                                                                     E_ZB_ATTRIBUTEID_ONOFF_ONOFF,
                                                                     E_ZB_ATTRIBUTE_UINT64_TYPE);
                                eSendBindUnbindCommand(device->u64IeeeAddress,
                                                       device->sZDEndpoint[i].u8EndpointId,
                                                       E_ZB_CLUSTERID_ONOFF,
                                                       SEND_BIND_REQUEST_COMMAND);
                                vTaskDelay (pdMS_TO_TICKS(50));
                            }
                                break;
                                
                            case E_ZB_CLUSTERID_LEVEL_CONTROL:
                            {
                                tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
                                                                     device->sZDEndpoint[i].u8EndpointId,
                                                                     clusterId,
                                                                     E_ZB_ATTRIBUTEID_LEVEL_CURRENTLEVEL,
                                                                     E_ZB_ATTRIBUTE_UINT64_TYPE); 
                                eSendBindUnbindCommand(device->u64IeeeAddress, 
                                                       device->sZDEndpoint[i].u8EndpointId, 
                                                       E_ZB_CLUSTERID_LEVEL_CONTROL,
                                                       SEND_BIND_REQUEST_COMMAND);
                                vTaskDelay (pdMS_TO_TICKS(50));
                            }
                                break; 
                                
                            case E_ZB_CLUSTERID_COLOR_CONTROL:
                            {
                                switch (device->sZDEndpoint[i].u16DeviceType)
                                {
                                    case E_ZB_DEVICEID_LIGHT_COLOR_TEMP:
                                    {
                                        tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
                                                                             device->sZDEndpoint[i].u8EndpointId,
                                                                             clusterId,
                                                                             E_ZB_ATTRIBUTEID_COLOUR_COLOURTEMPERATURE,
                                                                             E_ZB_ATTRIBUTE_UINT64_TYPE);
                                        eSendBindUnbindCommand(device->u64IeeeAddress,
                                                               device->sZDEndpoint[i].u8EndpointId,
                                                               E_ZB_CLUSTERID_COLOR_CONTROL,
                                                               SEND_BIND_REQUEST_COMMAND);
                                        vTaskDelay (pdMS_TO_TICKS(50));
                                    }

                                        break;
                                        
                                    case E_ZB_DEVICEID_LIGHT_COLOR_EXT:
                                    {
                                        tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
                                                                             device->sZDEndpoint[i].u8EndpointId,
                                                                             clusterId,
                                                                             E_ZB_ATTRIBUTEID_COLOUR_COLOURTEMPERATURE,
                                                                             E_ZB_ATTRIBUTE_UINT64_TYPE);
                                        tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
                                                                             device->sZDEndpoint[i].u8EndpointId,
                                                                             clusterId,
                                                                             E_ZB_ATTRIBUTEID_COLOUR_CURRENTX,
                                                                             E_ZB_ATTRIBUTE_UINT64_TYPE);
                                        tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
                                                                             device->sZDEndpoint[i].u8EndpointId,
                                                                             clusterId,
                                                                             E_ZB_ATTRIBUTEID_COLOUR_CURRENTY,
                                                                             E_ZB_ATTRIBUTE_UINT64_TYPE);                                        
                                        eSendBindUnbindCommand(device->u64IeeeAddress,
                                                               device->sZDEndpoint[i].u8EndpointId,
                                                               E_ZB_CLUSTERID_COLOR_CONTROL,
                                                               SEND_BIND_REQUEST_COMMAND);
                                        vTaskDelay (pdMS_TO_TICKS(50));
                                    }
                                        break;
                                        
                                    default:
                                        break;
                                    
                                }
                            }
                                break;

                            case E_ZB_CLUSTERID_MEASUREMENTSENSING_TEMP:
                            {
                                tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
                                                                     device->sZDEndpoint[i].u8EndpointId,
                                                                     clusterId,
                                                                     E_ZB_ATTRIBUTEID_MS_TEMP_MEASURED,
                                                                     E_ZB_ATTRIBUTE_UINT64_TYPE);
                                eSendBindUnbindCommand(device->u64IeeeAddress,
                                                       device->sZDEndpoint[i].u8EndpointId,
                                                       E_ZB_CLUSTERID_MEASUREMENTSENSING_TEMP,
                                                       SEND_BIND_REQUEST_COMMAND);
                            }
                                break;
                                
                            case E_ZB_CLUSTERID_MEASUREMENTSENSING_HUM:
                            {
                                tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
                                                                     device->sZDEndpoint[i].u8EndpointId,
                                                                     clusterId,
                                                                     E_ZB_ATTRIBUTEID_MS_HUM_MEASURED,
                                                                     E_ZB_ATTRIBUTE_UINT64_TYPE);
                                eSendBindUnbindCommand(device->u64IeeeAddress,
                                                       device->sZDEndpoint[i].u8EndpointId,
                                                       E_ZB_CLUSTERID_MEASUREMENTSENSING_HUM,
                                                       SEND_BIND_REQUEST_COMMAND);                                
                            }
                                break;
                                
                            case E_ZB_CLUSTERID_MEASUREMENTSENSING_ILLUM:
                            {
                                tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
                                                                     device->sZDEndpoint[i].u8EndpointId,
                                                                     clusterId,
                                                                     E_ZB_ATTRIBUTEID_MS_ILLUM_MEASURED,
                                                                     E_ZB_ATTRIBUTE_UINT64_TYPE);
                                eSendBindUnbindCommand(device->u64IeeeAddress,
                                                       device->sZDEndpoint[i].u8EndpointId,
                                                       E_ZB_CLUSTERID_MEASUREMENTSENSING_ILLUM,
                                                       SEND_BIND_REQUEST_COMMAND);
                                vTaskDelay (pdMS_TO_TICKS(50));
                            }
                                break;

                            case E_ZB_CLUSTERID_OCCUPANCYSENSING:
                            {
                                tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
                                                                     device->sZDEndpoint[i].u8EndpointId,
                                                                     clusterId,
                                                                     E_ZB_ATTRIBUTEID_MS_OCC_OCCUPANCY,
                                                                     E_ZB_ATTRIBUTE_UINT64_TYPE);                                                                         
                            }
                                break;
                                
                            default:
                                break;
                        }
                    }

                }
                
                device->eDeviceState = E_ZB_DEVICE_STATE_ACTIVE;
                loop = false;
            }
                break;
            case E_ZB_DEVICE_STATE_CFG_ATTRIBUTE:
                loop = false;
                break;

            case E_ZB_DEVICE_STATE_ACTIVE:
                epArrayIndex = 0;
                loop = false;
                break;
                
            default:
                loop = false;
                break;
        }
        
    }

    if (device->eDeviceState == E_ZB_DEVICE_STATE_ACTIVE) {
        epArrayIndex = 0;
        vZDM_cJSON_DeviceCreate(device);
        if (eSetPermitJoining(0) != E_ZCB_OK)
            HAL_Printf( "Setting permit joining fail\r\n");        
    }

}
static teZcbStatus zb_device_manage_cluster_bind(tsZbDeviceInfo *device){
	int i,j;
	uint16_t clusterId;
	teZcbStatus ret = E_ZCB_ERROR;

	for (i = 0; i < device->u8EndpointCount; i++)
	{
		for (j = 0; j < device->sZDEndpoint[i].u8ClusterCount; j++)
		{
			uint16_t clusterId = device->sZDEndpoint[i].sZDCluster[j].u16ClusterId;
			switch (clusterId)
			{
				case E_ZB_CLUSTERID_BASIC:							  
                                {
                                  tZDM_AddNewAttributeToAttributeTable( device->u16NodeId,
									device->sZDEndpoint[i].u8EndpointId,
									clusterId,
                                                                        E_ZB_ATTRIBUTEID_BASIC_ZCL_VERSION,
									E_ZB_ATTRIBUTE_UINT8_TYPE);
                                  ret = eSendBindUnbindCommand( device->u64IeeeAddress,
                                                                device->sZDEndpoint[i].u8EndpointId,
								E_ZB_CLUSTERID_BASIC,
								SEND_BIND_REQUEST_COMMAND);
                                  }
					break;
					
				case E_ZB_CLUSTERID_ONOFF:
				{
                                    switch (device->sZDEndpoint[i].u16DeviceType)
					{
						case E_ZB_DEVICEID_ONOFF_SENSOR:
						{
					tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
														 device->sZDEndpoint[i].u8EndpointId,
														 clusterId,
														 E_ZB_ATTRIBUTEID_ONOFF_ONOFF,
														 E_ZB_ATTRIBUTE_BOOL_TYPE);
					ret = eSendBindUnbindCommand(device->u64IeeeAddress,
										   device->sZDEndpoint[i].u8EndpointId,
										   E_ZB_CLUSTERID_ONOFF,
										   SEND_BIND_REQUEST_COMMAND);
						}
							break;
						case E_ZB_DEVICEID_ONOFF_BUTTON:
						{
							tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
											device->sZDEndpoint[i].u8EndpointId,
											clusterId,
                                                                                        E_ZB_ATTRIBUTEID_ONOFF_ONOFF,
										        E_ZB_ATTRIBUTE_BOOL_TYPE);
							ret = eSendBindUnbindCommand(device->u64IeeeAddress,
										   device->sZDEndpoint[i].u8EndpointId,
                                                          E_ZB_CLUSTERID_ONOFF,
										   SEND_BIND_REQUEST_COMMAND);
						}
							break;
                                                case E_ZB_DEVICEID_LIGHT_DIMMER:
						{
							tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
											device->sZDEndpoint[i].u8EndpointId,
											clusterId,
                                                          E_ZB_ATTRIBUTEID_ONOFF_ONOFF,
										        E_ZB_ATTRIBUTE_BOOL_TYPE);
							ret = eSendBindUnbindCommand(device->u64IeeeAddress,
										   device->sZDEndpoint[i].u8EndpointId,
										   E_ZB_CLUSTERID_ONOFF,
										   SEND_BIND_REQUEST_COMMAND);
						}
							break;
						default:
							break;
					}
										zb_device_manage_set_hearbeat_interval(device->u16NodeId,
														  device->eZrZedType,
                                                          ZB_ENDPOINT_SRC_DEFAULT,
                                                          ZB_ENDPOINT_DST_DEFAULT,
                                                          E_ZB_CLUSTERID_ONOFF,
                                                          E_ZB_ATTRIBUTEID_ONOFF_ONOFF,
                                                          E_ZB_ATTRIBUTE_BOOL_TYPE,
                                                          ZCL_HEARTBEAT_TIMEOUT_VALUE,
                                                          ZCL_HEARTBEAT_CHANGE_VALUE);
				}
					break;
					
				case E_ZB_CLUSTERID_LEVEL_CONTROL:
				{
                                    switch (device->sZDEndpoint[i].u16DeviceType)
					{
						case E_ZB_DEVICEID_LIGHT_DIMMER:
						{
					tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
														 device->sZDEndpoint[i].u8EndpointId,
														 clusterId,
														 E_ZB_ATTRIBUTEID_LEVEL_CURRENTLEVEL,
														 E_ZB_ATTRIBUTE_UINT8_TYPE); 
					ret = eSendBindUnbindCommand(device->u64IeeeAddress, 
										   device->sZDEndpoint[i].u8EndpointId, 
										   E_ZB_CLUSTERID_LEVEL_CONTROL,
										   SEND_BIND_REQUEST_COMMAND);
						}
                                                break;
						default:
						break;
					}
					zb_device_manage_set_hearbeat_interval(device->u16NodeId,
                                    					  device->eZrZedType,
                                                          ZB_ENDPOINT_SRC_DEFAULT,
                                                          ZB_ENDPOINT_DST_DEFAULT,
                                                          E_ZB_CLUSTERID_LEVEL_CONTROL,
                                                          E_ZB_ATTRIBUTEID_LEVEL_CURRENTLEVEL,
                                                          E_ZB_ATTRIBUTE_UINT8_TYPE,
                                                          ZCL_HEARTBEAT_TIMEOUT_VALUE,
                                                          ZCL_HEARTBEAT_CHANGE_VALUE);
				}
					break; 
					
				case E_ZB_CLUSTERID_COLOR_CONTROL:
				{
					switch (device->sZDEndpoint[i].u16DeviceType)
					{
						case E_ZB_DEVICEID_LIGHT_COLOR_TEMP:
						{
							tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
																 device->sZDEndpoint[i].u8EndpointId,
																 clusterId,
																 E_ZB_ATTRIBUTEID_COLOUR_COLOURTEMPERATURE,
																 E_ZB_ATTRIBUTE_UINT16_TYPE);
							ret = eSendBindUnbindCommand(device->u64IeeeAddress,
												   device->sZDEndpoint[i].u8EndpointId,
												   E_ZB_CLUSTERID_COLOR_CONTROL,
												   SEND_BIND_REQUEST_COMMAND);
							
						}

							break;
							
						case E_ZB_DEVICEID_LIGHT_COLOR_EXT:
						{
							tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
																 device->sZDEndpoint[i].u8EndpointId,
																 clusterId,
																 E_ZB_ATTRIBUTEID_COLOUR_COLOURTEMPERATURE,
																 E_ZB_ATTRIBUTE_UINT16_TYPE);
							tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
																 device->sZDEndpoint[i].u8EndpointId,
																 clusterId,
																 E_ZB_ATTRIBUTEID_COLOUR_CURRENTX,
																 E_ZB_ATTRIBUTE_UINT64_TYPE);
							tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
																 device->sZDEndpoint[i].u8EndpointId,
																 clusterId,
																 E_ZB_ATTRIBUTEID_COLOUR_CURRENTY,
																 E_ZB_ATTRIBUTE_UINT64_TYPE);										 
							ret = eSendBindUnbindCommand(device->u64IeeeAddress,
												   device->sZDEndpoint[i].u8EndpointId,
												   E_ZB_CLUSTERID_COLOR_CONTROL,
												   SEND_BIND_REQUEST_COMMAND);
						}
							break;
							
						default:
							break;
						
					}
					zb_device_manage_set_hearbeat_interval(device->u16NodeId,
						                                  device->eZrZedType,
                                                          ZB_ENDPOINT_SRC_DEFAULT,
                                                          ZB_ENDPOINT_DST_DEFAULT,
                                                          E_ZB_CLUSTERID_COLOR_CONTROL,
                                                          E_ZB_ATTRIBUTEID_COLOUR_COLOURTEMPERATURE,
                                                          E_ZB_ATTRIBUTE_UINT16_TYPE,
                                                          ZCL_HEARTBEAT_TIMEOUT_VALUE,
                                                          ZCL_HEARTBEAT_CHANGE_VALUE);
				}
					break;

				case E_ZB_CLUSTERID_MEASUREMENTSENSING_TEMP:
				{
					tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
														 device->sZDEndpoint[i].u8EndpointId,
														 clusterId,
														 E_ZB_ATTRIBUTEID_MS_TEMP_MEASURED,
														 E_ZB_ATTRIBUTE_INT16_TYPE);
					ret = eSendBindUnbindCommand(device->u64IeeeAddress,
										   device->sZDEndpoint[i].u8EndpointId,
										   E_ZB_CLUSTERID_MEASUREMENTSENSING_TEMP,
										   SEND_BIND_REQUEST_COMMAND);
					zb_device_manage_set_hearbeat_interval(device->u16NodeId,
						                              device->eZrZedType,
                                                      ZB_ENDPOINT_SRC_DEFAULT,
                                                      ZB_ENDPOINT_DST_DEFAULT,
                                                      E_ZB_CLUSTERID_MEASUREMENTSENSING_TEMP,
                                                      E_ZB_ATTRIBUTEID_MS_TEMP_MEASURED,
                                                      E_ZB_ATTRIBUTE_INT16_TYPE,
                                                      ZCL_HEARTBEAT_TIMEOUT_VALUE,
                                                      ZCL_HEARTBEAT_CHANGE_VALUE);
				}
					break;
					
				case E_ZB_CLUSTERID_MEASUREMENTSENSING_HUM:
				{
					tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
														 device->sZDEndpoint[i].u8EndpointId,
														 clusterId,
														 E_ZB_ATTRIBUTEID_MS_HUM_MEASURED,
														 E_ZB_ATTRIBUTE_UINT16_TYPE);
					ret = eSendBindUnbindCommand(device->u64IeeeAddress,
										   device->sZDEndpoint[i].u8EndpointId,
										   E_ZB_CLUSTERID_MEASUREMENTSENSING_HUM,
										   SEND_BIND_REQUEST_COMMAND);
					zb_device_manage_set_hearbeat_interval(device->u16NodeId,
						                              device->eZrZedType,
                                                      ZB_ENDPOINT_SRC_DEFAULT,
                                                      ZB_ENDPOINT_DST_DEFAULT,
                                                      E_ZB_CLUSTERID_MEASUREMENTSENSING_HUM,
                                                      E_ZB_ATTRIBUTEID_MS_HUM_MEASURED,
                                                      E_ZB_ATTRIBUTE_UINT16_TYPE,
                                                      ZCL_HEARTBEAT_TIMEOUT_VALUE,
                                                      ZCL_HEARTBEAT_CHANGE_VALUE);
				}
					break;
					
				case E_ZB_CLUSTERID_MEASUREMENTSENSING_ILLUM:
				{
					tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
														 device->sZDEndpoint[i].u8EndpointId,
														 clusterId,
														 E_ZB_ATTRIBUTEID_MS_ILLUM_MEASURED,
														 E_ZB_ATTRIBUTE_UINT16_TYPE);
					ret = eSendBindUnbindCommand(device->u64IeeeAddress,
										   device->sZDEndpoint[i].u8EndpointId,
										   E_ZB_CLUSTERID_MEASUREMENTSENSING_ILLUM,
										   SEND_BIND_REQUEST_COMMAND);
					zb_device_manage_set_hearbeat_interval(device->u16NodeId,
						                              device->eZrZedType,
                                                      ZB_ENDPOINT_SRC_DEFAULT,
                                                      ZB_ENDPOINT_DST_DEFAULT,
                                                      E_ZB_CLUSTERID_MEASUREMENTSENSING_ILLUM,
                                                      E_ZB_ATTRIBUTEID_MS_ILLUM_MEASURED,
                                                      E_ZB_ATTRIBUTE_UINT16_TYPE,
                                                      ZCL_HEARTBEAT_TIMEOUT_VALUE,
                                                      ZCL_HEARTBEAT_CHANGE_VALUE);
				}
					break;

				case E_ZB_CLUSTERID_OCCUPANCYSENSING:
				{
					tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
														 device->sZDEndpoint[i].u8EndpointId,
														 clusterId,
														 E_ZB_ATTRIBUTEID_MS_OCC_OCCUPANCY,
														 E_ZB_ATTRIBUTE_MAP8_TYPE);	
					ret = eSendBindUnbindCommand(device->u64IeeeAddress,
										   device->sZDEndpoint[i].u8EndpointId,
										   E_ZB_CLUSTERID_OCCUPANCYSENSING,
										   SEND_BIND_REQUEST_COMMAND);
					zb_device_manage_set_hearbeat_interval(device->u16NodeId,
						                              device->eZrZedType,
                                                      ZB_ENDPOINT_SRC_DEFAULT,
                                                      ZB_ENDPOINT_DST_DEFAULT,
                                                      E_ZB_CLUSTERID_OCCUPANCYSENSING,
                                                      E_ZB_ATTRIBUTEID_MS_OCC_OCCUPANCY,
                                                      E_ZB_ATTRIBUTE_MAP8_TYPE,
                                                      ZCL_HEARTBEAT_TIMEOUT_VALUE,
                                                      ZCL_HEARTBEAT_CHANGE_VALUE);
				}
					break;
					
				default:
					break;
			}
		}

	}
	
	device->eDeviceState = E_ZB_DEVICE_BIND_OVER;
    return ret;
}

static teZcbStatus zb_device_manage_attr_read(tsZbDeviceInfo *device){
	int i,j;
	uint16_t clusterId;
	teZcbStatus ret = E_ZCB_ERROR;
	uint16_t attribute_id = E_ZB_ATTRIBUTEID_BASIC_MODEL_ID;
	for (i = 0; i < device->u8EndpointCount; i++)
	{
	    for (j = 0; j < device->sZDEndpoint[i].u8ClusterCount; j++)
	    {
	        clusterId = device->sZDEndpoint[i].sZDCluster[j].u16ClusterId;

			if(clusterId == E_ZB_CLUSTERID_BASIC){

				tZDM_AddNewAttributeToAttributeTable(device->u16NodeId,
													 device->sZDEndpoint[i].u8EndpointId,
													 clusterId,
													 attribute_id,
													 E_ZB_ATTRIBUTE_STRING_TYPE);

				ret = eReadAttributeRequest(E_ZD_ADDRESS_MODE_SHORT, 
	                                      device->u16NodeId, 
	                                      ZB_ENDPOINT_SRC_DEFAULT, 
	                                      device->sZDEndpoint[i].u8EndpointId, 
	                                      E_ZB_CLUSTERID_BASIC,  
	                                      ZB_MANU_CODE_DEFAULT, 
                                          MANUFACTURER_SPECIFIC_FALSE,
	                                      1, 
	                                      &attribute_id);

			}
	        
	    }
	}
	return ret;
}



void zb_device_rxedcmd_process(uint32_t device_id, char *cmd, uint8_t value)
{
	tsZbDeviceInfo*  device = NULL;
    if(strstr(cmd, "LightSwitch"))
    {
		device = zb_device_find_device_info_by_device_id(device_id);
        if(device != NULL)
        {
				eOnOff(E_ZB_ADDRESS_MODE_SHORT,
                                       device->u16NodeId, //deviceTable[device_id].u16NodeId
                                       1,
                                       1,
                                       value);
			}
    }
    else if(strstr(cmd, "Brightness"))
    {
        device = zb_device_find_device_info_by_device_id(device_id);
        if(device != NULL)
        {
            eLevelControlMoveToLevel(E_ZB_ADDRESS_MODE_SHORT,
                                     device->u16NodeId, //deviceTable[device_id].u16NodeId
                                     1,
                                     1,
                                     1,         //with onoff
                                     value,     //0x00-0xff(0-255)
                                     0x10);     //transitionTime 1.6s
        }
    }
    else if(strstr(cmd, "startZbNet"))
    {
		switch (value){

            case 0:
            {
				eResetDevice();

			}
			break;
			
            case 1:
            {
				eSetChannelMask(ZB_CHANNEL_DEFAULT);
				eSetExPANID(ZB_EXTPANID_DEFAULT);
				eStartNetwork();
			}
			break;
			
            case 2:
            {
				eErasePersistentData();
				eResetDevice();
			}
			break;
			
            default:
            {
				HAL_Printf("ZB network configure with invalid parameters %d\r\n",value);
			}
			break;
        }
		}

    else if(strstr(cmd, "zbPermitJoin"))
    {
        if (value == 1)
        {
            eSetPermitJoining(30);
            if (eSetPermitJoining(30) != E_ZCB_OK)
            HAL_Printf("Setting permit joining fail\r\n");  
        }
        else
        {
            eSetPermitJoining(0);
            if (eSetPermitJoining(0) != E_ZCB_OK)
            HAL_Printf("Setting permit joining fail\r\n");  
	}
        }


}




static teZcbStatus zb_device_manage_set_hearbeat_interval(uint16_t u16NodeId,
												  teZbDeviceType eDeviceType,
                                                  uint8_t u8SrcEndpoint,
                                                  uint8_t u8DstEndpoint,
                                                  uint16_t u16ClusterId,
                                                  uint16_t u16AttributeId,
                                                  uint8_t u8DataType,                                                 
                                                  uint16_t u16TimeOut,
                                                  uint64_t u64Change){


	tsZbDeviceConfigReport* findReportAttributeInHeartBeatTable = NULL;
	uint16_t u16MinIntv;
	uint16_t u16MaxIntv;
	if(eDeviceType == ZR)
	{
		u16MinIntv = ZCL_HEARTBEAT_ZR_MIN_REPORT_INTERVAL;
		u16MaxIntv = ZCL_HEARTBEAT_ZR_MAX_REPORT_INTERVAL;
	}
	if(eDeviceType == ZED)
	{
		u16MinIntv = ZCL_HEARTBEAT_ZED_MIN_REPORT_INTERVAL;
        u16MaxIntv = ZCL_HEARTBEAT_ZED_MAX_REPORT_INTERVAL;
	}

	findReportAttributeInHeartBeatTable = tZDM_ReportAttributeInHeartBeatTable(u16NodeId,u8SrcEndpoint,u8DstEndpoint,u16ClusterId,u16AttributeId,u8DataType);
	if(findReportAttributeInHeartBeatTable == NULL)
	{
	   if(eConfigureReportingCommand(E_ZD_ADDRESS_MODE_SHORT,
								   u16NodeId,
								   u8SrcEndpoint,
								   u8DstEndpoint,
								   u16ClusterId,
								   ZB_MANU_CODE_DEFAULT,
								   u8DataType,
								   u16AttributeId,
								   u16MinIntv,
								   u16MaxIntv,
								   u64Change) != E_ZCB_OK){
		   HAL_Printf("Sending set heartbeat command fail\r\n");
		   return E_ZCB_COMMS_FAILED;

	   }
		tZDM_AddNewReportAttributeToHeartBeatTable(u16NodeId,u8SrcEndpoint,u8DstEndpoint,u16ClusterId,u16AttributeId,u8DataType,u16MinIntv,u16MaxIntv,u16TimeOut,u64Change);
	}

	return E_ZCB_OK;

}

static int zb_device_iot_se_req_timeoutcb(void *args){

	zb_device_iot_se_req_t *zbdi = (zb_device_iot_se_req_t *)args;//((xTIMER *)thdl)->pvTimerID;
	int ret = 10;
	if(!zbdi){
		HAL_Printf("Invalid parameters\r\n");
		return -1;
	}

	if(zbdi->timeout == 0){
		ret = -1;
		HAL_Printf("Secrety request timeout\r\n");
		goto zbd_done;
	}
	zbdi->timeout--;
	HAL_Printf("items get seq:%d\n",zbdi->items_get);
	switch(zbdi->items_get){
		
		case ZB_DEVICE_MANAGE_ACTIVE_EP_REQ:{
			teZcbStatus st = eActiveEndpointRequest(zbdi->devinfo->u16NodeId);		
			HAL_Printf("Active endpoint request , dev 0x%x status 0x%x\r\n",zbdi->devinfo->u16NodeId,st);
		}
		break;
		
		case ZB_DEVICE_MANAGE_SIMPLE_DESC_REQ:{
			uint8_t epArrayIndex;
			teZcbStatus st = E_ZCB_ERROR;
			for(epArrayIndex=0;epArrayIndex<zbdi->devinfo->u8EndpointCount;epArrayIndex++){
				st = eSimpleDescriptorRequest(zbdi->devinfo->u16NodeId, zbdi->devinfo->sZDEndpoint[epArrayIndex].u8EndpointId);
			}
			HAL_Printf("Simple descriptor request result 0x%x\r\n",st);
			
		}
		break;

		
		case ZB_DEVICE_MANAGE_ATTRIBUTE_READ:{
			teZcbStatus st = zb_device_manage_attr_read(zbdi->devinfo);
			HAL_Printf("Device manage attribute read result 0x%x\r\n",st);
		}
		break;


		case ZB_DEVICE_MANAGE_CLUSTER_BIND:{
			teZcbStatus st = zb_device_manage_cluster_bind(zbdi->devinfo);
			HAL_Printf("Cluster bind result 0x%x\r\n",st);
		}
		break;

		case ZB_DEVICE_MANAGE_PKEY_REQ:{
			uint16_t au16AttrList = E_ZB_ATTRIBUTEID_ALIIOTSECURITY_PRODUCTKEY;
            tZDM_AddNewAttributeToAttributeTable(zbdi->devinfo->u16NodeId,
                                                    ZB_ENDPOINT_SRC_DEFAULT,
                                                    E_ZB_CLUSTERID_ALIIOTSECURITY,
                                                    E_ZB_ATTRIBUTEID_ALIIOTSECURITY_PRODUCTKEY,
                                                    E_ZB_ATTRIBUTE_STRING_TYPE);
			eReadAttributeRequest(E_ZD_ADDRESS_MODE_SHORT, 
                                  zbdi->devinfo->u16NodeId, 
                                  ZB_ENDPOINT_SRC_DEFAULT, 
                                  ZB_ENDPOINT_DST_ALIIOTSEC, 
                                  E_ZB_CLUSTERID_ALIIOTSECURITY,  
                                  ZB_MANU_CODE_DEFAULT, 
                                  MANUFACTURER_SPECIFIC_TRUE,
                                  1, 
                                  &au16AttrList);
			HAL_Printf("Request product key\r\n");
			
		}
		break;
		
		case ZB_DEVICE_MANAGE_PSECRET_REQ:{
			uint16_t au16AttrList = E_ZB_ATTRIBUTEID_ALIIOTSECURITY_PRODUCTSECRET;
            tZDM_AddNewAttributeToAttributeTable(zbdi->devinfo->u16NodeId,
                                                ZB_ENDPOINT_SRC_DEFAULT,
                                                E_ZB_CLUSTERID_ALIIOTSECURITY,
                                                E_ZB_ATTRIBUTEID_ALIIOTSECURITY_PRODUCTSECRET,
                                                E_ZB_ATTRIBUTE_STRING_TYPE);
			eReadAttributeRequest(E_ZD_ADDRESS_MODE_SHORT, 
                                  zbdi->devinfo->u16NodeId, 
                                  ZB_ENDPOINT_SRC_DEFAULT, 
                                  ZB_ENDPOINT_DST_ALIIOTSEC, 
                                  E_ZB_CLUSTERID_ALIIOTSECURITY,  
                                  ZB_MANU_CODE_DEFAULT, 
                                  MANUFACTURER_SPECIFIC_TRUE,
                                  1, 
                                  &au16AttrList);
			HAL_Printf("Request product secret\r\n");

		}
		break;
		
		case ZB_DEVICE_MANAGE_DNAME_REQ:{
			uint16_t au16AttrList = E_ZB_ATTRIBUTEID_ALIIOTSECURITY_DEVICENAME;
            tZDM_AddNewAttributeToAttributeTable(zbdi->devinfo->u16NodeId,
                                                ZB_ENDPOINT_SRC_DEFAULT,
                                                E_ZB_CLUSTERID_ALIIOTSECURITY,
                                                E_ZB_ATTRIBUTEID_ALIIOTSECURITY_DEVICENAME,
                                                E_ZB_ATTRIBUTE_STRING_TYPE);
			eReadAttributeRequest(E_ZD_ADDRESS_MODE_SHORT, 
                                  zbdi->devinfo->u16NodeId, 
                                  ZB_ENDPOINT_SRC_DEFAULT, 
                                  ZB_ENDPOINT_DST_ALIIOTSEC, 
                                  E_ZB_CLUSTERID_ALIIOTSECURITY,  
                                  ZB_MANU_CODE_DEFAULT, 
                                  MANUFACTURER_SPECIFIC_TRUE,
                                  1, 
                                  &au16AttrList);
			HAL_Printf("Request device name\r\n");


		}
		break;
		
		case ZB_DEVICE_MANAGE_DSECRET_REQ:{
			uint16_t au16AttrList = E_ZB_ATTRIBUTEID_ALIIOTSECURITY_DEVICESECRET;
            tZDM_AddNewAttributeToAttributeTable(zbdi->devinfo->u16NodeId,
                                                ZB_ENDPOINT_SRC_DEFAULT,
                                                E_ZB_CLUSTERID_ALIIOTSECURITY,
                                                E_ZB_ATTRIBUTEID_ALIIOTSECURITY_DEVICESECRET,
                                                E_ZB_ATTRIBUTE_STRING_TYPE);
			eReadAttributeRequest(E_ZD_ADDRESS_MODE_SHORT, 
                                  zbdi->devinfo->u16NodeId, 
                                  ZB_ENDPOINT_SRC_DEFAULT, 
                                  ZB_ENDPOINT_DST_ALIIOTSEC, 
                                  E_ZB_CLUSTERID_ALIIOTSECURITY,  
                                  ZB_MANU_CODE_DEFAULT, 
                                  MANUFACTURER_SPECIFIC_TRUE,
                                  1, 
                                  &au16AttrList);
			HAL_Printf("Request device secret\r\n");

		}
		break;
		
		case ZB_DEVICE_MANAGE_COMPLETE:{
			HAL_Printf("IoT security request success, report device to cloud\r\n");
			gateway_sub_dev_add(zbdi->devinfo, (char *)zbdi->product_key,(char *)zbdi->product_secret,(char *)zbdi->device_name,(char *)zbdi->device_secret);
            zbdi->devinfo->eDeviceState = E_ZB_DEVICE_STATE_ACTIVE;
			xTimerStart(zbDeviceTimer,pdMS_TO_TICKS(1000));
			ret = -1;
		}
		
		default:{
			ret = -1;
		}
		break;
	}
	zbd_done:
	if(ret == -1){
		list_del((dlist_t *)zbdi);
		vPortFree(zbdi);
	}
	
	return ret;

}

zb_device_iot_se_req_t *zb_device_get_iot_se_req_data(tsZbDeviceInfo *devinfo){
	zb_device_iot_se_req_t *wte = (zb_device_iot_se_req_t *)zb_device_list_head.next;
	do{
		if(wte->devinfo  == devinfo){
			return wte;
		}
		wte= (zb_device_iot_se_req_t *)wte->next;
		if(!wte){
			break;
		}
	}while((void *)wte != (void *)&zb_device_list_head);
	return NULL;
}

void zb_device_handle_zb_response(tsZbDeviceInfo *devinfo, zb_device_mange_st_e msgtype)
{

	zb_device_iot_se_req_t *zbdi = zb_device_get_iot_se_req_data(devinfo);
	
	if(!zbdi){
		HAL_Printf("Error to locate the iot security request primitive\r\n");
		return;
	}
	int ret = 0;
	HAL_Printf("Rxed msg type 0x%x\r\n",msgtype);
	if(zbdi->items_get == msgtype){
		zbdi->items_get++;
	}
	ret = zb_device_iot_se_req_timeoutcb(zbdi);
	if(ret == -1){

		zbd_s_timer_stop(zbdi);

	}else{
		zbd_s_timer_timeout_set(zbdi, ret);

	}

}

void zb_device_handle_iot_se_response(tsZbDeviceAttribute *attrv, tsZbDeviceInfo *devinfo){
	zb_device_iot_se_req_t *zbdi = zb_device_get_iot_se_req_data(devinfo);
	
	if(!zbdi){
		HAL_Printf("Can't locate the iot security request primitive\r\n");
		return;
	}
	int ret = 0;
	switch(attrv->u16AttributeId){
		case E_ZB_ATTRIBUTEID_ALIIOTSECURITY_PRODUCTKEY:{
			HAL_Printf("Rxed product key %s\r\n",attrv->uData.sData.pData);
			memcpy(zbdi->product_key,attrv->uData.sData.pData,attrv->uData.sData.u8Length);
            if(zbdi->items_get == ZB_DEVICE_MANAGE_PKEY_REQ){
				zbdi->items_get++;
			}
			ret = zb_device_iot_se_req_timeoutcb(zbdi);
		}
		break;
		case E_ZB_ATTRIBUTEID_ALIIOTSECURITY_PRODUCTSECRET:{
			HAL_Printf("Rxed product secret %s\r\n",attrv->uData.sData.pData);
			memcpy(zbdi->product_secret,attrv->uData.sData.pData,attrv->uData.sData.u8Length);
		
            if(zbdi->items_get == ZB_DEVICE_MANAGE_PSECRET_REQ){
				zbdi->items_get++;
			}
			ret = zb_device_iot_se_req_timeoutcb(zbdi);

		}
		break;
		case E_ZB_ATTRIBUTEID_ALIIOTSECURITY_DEVICENAME:{
			HAL_Printf("Rxed device name %s\r\n",attrv->uData.sData.pData);
			memcpy(zbdi->device_name,attrv->uData.sData.pData,attrv->uData.sData.u8Length);
            if(zbdi->items_get == ZB_DEVICE_MANAGE_DNAME_REQ){
				zbdi->items_get++;
			}
			ret = zb_device_iot_se_req_timeoutcb(zbdi);



		}
		break;
		case E_ZB_ATTRIBUTEID_ALIIOTSECURITY_DEVICESECRET:{
			HAL_Printf("Rxed device secrit %s\r\n",attrv->uData.sData.pData);
			memcpy(zbdi->device_secret,attrv->uData.sData.pData,attrv->uData.sData.u8Length);
                if(zbdi->items_get == ZB_DEVICE_MANAGE_DSECRET_REQ){
				zbdi->items_get++;
			}
			ret = zb_device_iot_se_req_timeoutcb(zbdi);

		}
		break;
		default:{


			HAL_Printf("Unknown attribute response received 0x%x\r\n",attrv->u16AttributeId);
			
		}
		break;
	}
	if(ret == -1){

		zbd_s_timer_stop(zbdi);

	}
}


static const char product_key[] = "a1WYUuiqS61";
static const char product_secret[] = "8OsWmZ4P7gujrwQY"; 
static const char device_name[] = "test_01";  
static const char device_secret[] = "T6JeIZjZoogkFnSiUVxELbVuEJeVGQK3";




void zb_device_request_IoT_security(tsZbDeviceInfo *devinfo, bool new_join){
#if 0
	gateway_sub_dev_add(devinfo,product_key,product_secret,device_name,device_secret);


#else
	zb_device_iot_se_req_t *zbdi = pvPortMalloc(sizeof(zb_device_iot_se_req_t));
	if(zbdi){
		memset(zbdi,0,sizeof(*zbdi));
		zbdi->devinfo = devinfo;
		zbdi->timeout = IOT_SE_REQ_TIMEOUT_S;
		zbdi->items_get = new_join?ZB_DEVICE_MANAGE_ACTIVE_EP_REQ:ZB_DEVICE_MANAGE_PKEY_REQ;
		if(zbd_s_timer_start(2,zb_device_iot_se_req_timeoutcb,zbdi) != 0){
			vPortFree(zbdi);
			//TODO: Need send leave cmd to the newly joined device
			return;
		}
		list_add((dlist_t *)zbdi,&zb_device_list_head);
		
	}
#endif

}



/*
 * Create the cJSON Format New Device Info Struct to the server
 */ 
void vZDM_cJSON_DeviceCreate(tsZbDeviceInfo *device)
{

	HAL_Printf("Add device to cloud\r\n");
	zb_device_request_IoT_security(device,false);

}

void vZDM_cJSON_DeviceDelete(tsZbDeviceInfo *device)
{
	gateway_delete_subdev_complete(device);

}




#endif


// ------------------------------------------------------------------
// END OF FILE
// -----------------------------

