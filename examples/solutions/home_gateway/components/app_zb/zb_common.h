#ifndef ZB_COMMON_H_
#define ZB_COMMON_H_
#include <stdbool.h>

#include "gw_macro.h"
#include "zcb.h"
#include "zb_dev.h"
#include "cmd.h"
#include "sys_mgmt.h"


int HAL_Kv_Set(const char *key, const void *val, int len, int sync);
int HAL_Kv_Get(const char *key, void *val, int *buffer_len);
int HAL_Kv_Del(const char *key);
void HAL_Printf(const char *fmt, ...);

int gateway_delete_subdev(tsZbDeviceInfo *device);
int gateway_sub_dev_add(tsZbDeviceInfo *devInfo, char *product_key, char *product_secret, char *device_name, char *device_secret);
int gateway_delete_subdev_complete(tsZbDeviceInfo *device);

void user_devcount_post_property(uint16_t value);
int user_zb_device_property_post_event_handler(uint8_t index, tsZbDeviceAttribute *devattr);



#endif
