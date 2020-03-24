
/* Standard includes. */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "log_porting.h"
#if ENABLE_ZB_MODULE
const char *LOG_LV[] = 
{
    "-I- ",
    "-W- ",
    "-E- "
};

/*-----------------------------------------------------------*/

/*!
 * \brief Formats a string to be printed and sends it
 * to the print queue.
 *
 * Appends the message number, time (in ticks), and task
 * that called vLoggingPrintf to the beginning of each
 * print statement.
 *
 */
void vLoggingPrintf( const char *pcFormat, ... )
{
	printf(pcFormat);

}

#endif
