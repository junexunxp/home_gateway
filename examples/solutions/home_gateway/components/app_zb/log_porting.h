#ifndef LOG_PORTING_H
#define LOG_PORTING_H
#define INFO                           0
#define WARN                           1
#define ERR                            2
#define OFF                            3

//#include "freertos/portmacro.h"
extern const char *LOG_LV[];

void vLoggingPrintf( const char *pcFormat, ... );

//BaseType_t xLoggingTaskInitialize( uint16_t usStackSize, UBaseType_t uxPriority, UBaseType_t uxQueueLength );

/* Sets the length of the buffers into which logging messages are written - so
 * also defines the maximum length of each log message. */
#define configLOGGING_MAX_MESSAGE_LENGTH            128

/* Set to 1 to prepend each log message with a message number, the task name,
 * and a time stamp. */
#define configLOGGING_INCLUDE_TIME_AND_TASK_NAME    0

#define CFG_WIFI_DEBUG_EN	1
#define CFG_SYS_DEBUG_EN	1

#define CFG_SHELL_DEBUG_EN	1
#define CFG_ZBSERIAL_DEBUG_EN	1
#define CFG_ZCB_DEBUG_EN	1
#define CFG_ZB_CMD_DEBUG_EN	1
#define CFG_ZCB_DEBUG_EN	1
#define CFG_ZB_DEV_MGT_DEBUG_EN	1
#define CFG_GATEWAY_DEBUG_EN	1
#define CFG_USBH_DEBUG_EN	1
#define CFG_USBD_DEBUG_EN	1

#define LOG(NAME, LEVEL, format, ...)        NAME##_LOG(LEVEL, NAME format, ##__VA_ARGS__)

#define _print_(LEVEL, format, ...) \
    do { \
        if (1) { \
            vLoggingPrintf("%s"format, LOG_LV[LEVEL], ##__VA_ARGS__); \
        } \
    } while(0)

#if defined (CFG_WIFI_DEBUG_EN) && (CFG_WIFI_DEBUG_EN > 0)
    #define WIFI                            "[WIFI]: "
    #define WIFI_LOG                        _print_
#else
    #define WIFI_LOG(...)
#endif     
      
#if defined (CFG_L2BOOT_DEBUG_EN) && (CFG_L2BOOT_DEBUG_EN > 0)
    #define L2BOOT                            "[L2BOOT]: "
    #define L2BOOT_LOG                        _print_
#else
    #define L2BOOT_LOG(...)
#endif      

#if defined (CFG_SYS_DEBUG_EN) && (CFG_SYS_DEBUG_EN > 0)
    #define SYS                            "[SYS]: "
    #define SYS_LOG                        _print_
#else
    #define SYS_LOG(...)
#endif        

#if defined (CFG_SHELL_DEBUG_EN) && (CFG_SHELL_DEBUG_EN > 0)
    #define SHELL                            "[SHELL]: "
    #define SHELL_LOG                        _print_
#else
    #define SHELL_LOG(...)
#endif


#if defined (CFG_ZBSERIAL_DEBUG_EN) && (CFG_ZBSERIAL_DEBUG_EN > 0)
    #define ZBSERIAL                          "[ZB-SERIAL]: "               
    #define ZBSERIAL_LOG                      _print_
#else    
    #define ZBSERIAL_LOG(...)
#endif


#if defined (CFG_ZCB_DEBUG_EN) && (CFG_ZCB_DEBUG_EN > 0)
    #define ZCB                               "[ZB-CB]: "
    #define ZCB_LOG                           _print_
#else    
    #define ZCB_LOG(...)
#endif


#if defined (CFG_ZB_CMD_DEBUG_EN) && (CFG_ZB_CMD_DEBUG_EN > 0)
    #define ZBCMD                             "[ZB-CMD]: "
    #define ZBCMD_LOG                         _print_
#else     
    #define ZBCMD_LOG(...)
#endif


#if defined (CFG_ZB_DEV_MGT_DEBUG_EN) && (CFG_ZB_DEV_MGT_DEBUG_EN > 0)
    #define ZDM                               "[ZB-DM]: "
    #define ZDM_LOG                           _print_
#else    
    #define ZDM_LOG(...)
#endif


#if defined (CFG_GATEWAY_DEBUG_EN) && (CFG_GATEWAY_DEBUG_EN > 0)
    #define GW                             "[GW]: "
    #define GW_LOG                         _print_
#else
    #define GW_LOG(...)
#endif


#if defined (CFG_USBH_DEBUG_EN) && (CFG_USBH_DEBUG_EN > 0)
    #define USBHOST                        "[USBH]: "
    #define USBHOST_LOG                     _print_
#else
    #define USBHOST_LOG(...)
#endif

#if defined (CFG_USBD_DEBUG_EN) && (CFG_USBD_DEBUG_EN > 0)
    #define USBDEVICE                      "[USBD]: "
    #define USBDEVICE_LOG                  _print_
#else
    #define USBDEVICE_LOG(...)
#endif
#endif