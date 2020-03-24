/* Copyright 2018 NXP */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"

#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"
#include "zb_common.h"


/*******************************************************************************
 * Definitions
 ******************************************************************************/

/*******************************************************************************
 * Prototypes
 ******************************************************************************/

/*******************************************************************************
 * Variables
 ******************************************************************************/

static bool s_IsFirstBoot = false;
static bool s_IsFirstConnectCloud = false;
static const uint32_t BOOT_MAGICNUM = 0x12839845;
static const uint32_t CONNECT_MAGICNUM = 0x43219923;
#define systemFILE_NAME_BOOTCONFIG            "BootConifg.dat"

/*******************************************************************************
 * Code
 ******************************************************************************/

void sys_mgmt_load_config(void)
{
    uint32_t kvalue = 0;
    int32_t size   = 4;
    if (HAL_Kv_Get(systemFILE_NAME_BOOTCONFIG,(uint8_t *)&kvalue, &size) == 0) {

        if (kvalue == BOOT_MAGICNUM) {
            
            s_IsFirstBoot = false;
            s_IsFirstConnectCloud = true;

        } else if (kvalue == CONNECT_MAGICNUM) {
        
            s_IsFirstBoot = false;
            s_IsFirstConnectCloud = false;
            
        }

    } else {
        s_IsFirstBoot = true;
        s_IsFirstConnectCloud = true;
        HAL_Kv_Set(systemFILE_NAME_BOOTCONFIG,(uint8_t *)&BOOT_MAGICNUM, sizeof(uint32_t),0);
        printf("First time Boot!\r\n");
    }
}

void sys_mgmt_mark_connected(void )
{
    HAL_Kv_Set(systemFILE_NAME_BOOTCONFIG, (uint8_t *)&CONNECT_MAGICNUM, sizeof(uint32_t),0);
}

bool sys_mgmt_first_boot(void)
{
    return s_IsFirstBoot;
}

bool sys_mgmt_first_connect(void)
{
    return s_IsFirstConnectCloud;
}

void sys_mgmt_clr_boot(void)
{
   
    
    int rt = HAL_Kv_Del(systemFILE_NAME_BOOTCONFIG);
    assert(rt == 0);
//    /* any value != BOOTFLAGMN treat as clean */
//    uint32_t value = 0x00000000;
//    rt = kv_item_set(systemFILE_NAME_BOOTCONFIG, (uint8_t *)&value, sizeof(uint32_t));
//    assert(rt == 0);
//    
//    uint32_t kvalue = 0;
//    rt = kv_item_get(systemFILE_NAME_BOOTCONFIG, (uint8_t *)&kvalue, &size);
//    assert(rt == 0);
//    
//    assert(kvalue == value);
}

