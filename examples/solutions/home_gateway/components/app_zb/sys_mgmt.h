
#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H



void sys_mgmt_load_config(void);
void sys_mgmt_mark_connected();
bool sys_mgmt_first_boot(void);
bool sys_mgmt_first_connect(void);
void sys_mgmt_clr_boot(void);

#endif /* SYSTEM_MANAGER_H */

