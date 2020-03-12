#ifndef AHT10_H_
#define AHT10_H_



#define eAHT10Address_default  0x38


void aht_init(void );
void aht_reset(void);
float aht_get_dew_point(float humidity, float temperature);

float aht_get_tem(void);
double aht_get_humidity(void );

void aht_demo_task(void );

#endif

