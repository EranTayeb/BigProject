#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "xgpio.h"
#include "xttcps.h"
#include "dht.h"

#define GPIO_CHANNEL 1
#define TICKS_IN_US  50

typedef struct Dht_ {
	XGpio * gpio;
	u32 dhtPin;
	bool isRunning;
	float lastTemperature;
	float lastHumidity;
	XTtcPs ttc;
} Dht;

static void Dht_enable(Dht * dht);
static void Dht_disable(Dht * dht);

static u32 timeDiffData[40];

Dht * Dht_init(XGpio * gpio, u32 dhtPin)
{
	static Dht dht;

	dht.gpio = gpio;
	dht.dhtPin = dhtPin;
	dht.isRunning = false;
	dht.lastTemperature = 0.0f;
	dht.lastHumidity = 0.0f;

	Dht_disable(&dht);

	// Configure TTC timer
	XTtcPs * ttcPtr = &(dht.ttc);
	XTtcPs_Config * ttcConfig = XTtcPs_LookupConfig(XPAR_PSU_TTC_8_DEVICE_ID);

	XTtcPs_CfgInitialize(ttcPtr, ttcConfig, ttcConfig->BaseAddress);

	XTtcPs_SetOptions(ttcPtr, XTTCPS_OPTION_INTERVAL_MODE | XTTCPS_OPTION_WAVE_DISABLE);
	XTtcPs_SetPrescaler(ttcPtr, 0);
	XTtcPs_SetInterval(ttcPtr, 0xFFFFFFFF);

	return &dht;
}

bool Dht_takeData(Dht * dht)
{
	u8 dhtData[5] = { 0 };
	u8 sum = 0;
	u32 timeStamp;
	u32 timeDiff;

	configASSERT(dht != NULL);

	if (dht->isRunning) {
		// don't allow to run DHT if it's already running
		return false;
	}
	Dht_enable(dht);

	vTaskDelay(pdMS_TO_TICKS(20));

	// set DHT output pin to high and change output to input
	XGpio_DiscreteSet(dht->gpio, GPIO_CHANNEL, dht->dhtPin);
	u32 dir = XGpio_GetDataDirection(dht->gpio, GPIO_CHANNEL);
	XGpio_SetDataDirection(dht->gpio, GPIO_CHANNEL, dir | dht->dhtPin);

	XTtcPs_ResetCounterValue(&(dht->ttc));
	XTtcPs_Start(&(dht->ttc));

	// wait for start getting data
	timeStamp = XTtcPs_GetCounterValue(&(dht->ttc));
	while (XGpio_DiscreteRead(dht->gpio, GPIO_CHANNEL) & dht->dhtPin)
	{
		timeDiff = (XTtcPs_GetCounterValue(&(dht->ttc)) - timeStamp) / TICKS_IN_US;
		if (timeDiff > 1000) {
			// DHT didn't start to send data
			Dht_disable(dht);
			return false;
		}
	}
	// skip first bit
	while ((XGpio_DiscreteRead(dht->gpio, GPIO_CHANNEL) & dht->dhtPin) == 0)
	{
		timeDiff = (XTtcPs_GetCounterValue(&(dht->ttc)) - timeStamp) / TICKS_IN_US;
		if (timeDiff > 1000) {
			// DHT didn't start to send data
			Dht_disable(dht);
			return false;
		}
	}
	while (XGpio_DiscreteRead(dht->gpio, GPIO_CHANNEL) & dht->dhtPin)
	{
		timeDiff = (XTtcPs_GetCounterValue(&(dht->ttc)) - timeStamp) / TICKS_IN_US;
		if (timeDiff > 1000) {
			// DHT didn't start to send data
			Dht_disable(dht);
			return false;
		}
	}

	// get 40 bit of data
	for (int i = 0; i < 40; i++)
	{
		timeStamp = XTtcPs_GetCounterValue(&(dht->ttc));

		while ((XGpio_DiscreteRead(dht->gpio, GPIO_CHANNEL) & dht->dhtPin) == 0) {
			timeDiff = (XTtcPs_GetCounterValue(&(dht->ttc)) - timeStamp) / TICKS_IN_US;
			if (timeDiff > 100) {
				// data read failure
				Dht_disable(dht);
				return false;
			}
		}

		while ((XGpio_DiscreteRead(dht->gpio, GPIO_CHANNEL) & dht->dhtPin) != 0) {
			timeDiff = (XTtcPs_GetCounterValue(&(dht->ttc)) - timeStamp) / TICKS_IN_US;
			if (timeDiff > 200) {
				// data read failure
				Dht_disable(dht);
				return false;
			}
		}

		timeDiff = (XTtcPs_GetCounterValue(&(dht->ttc)) - timeStamp) / TICKS_IN_US;

		timeDiffData[i] = timeDiff;
		if (timeDiff > 100) {
			dhtData[i / 8] |= 1 << (7 - i % 8);
		}
	}

	sum = (u8)(dhtData[0] + dhtData[1] + dhtData[2] + dhtData[3]);
	if (sum != dhtData[4])
	{
		// invalid check sum
		Dht_disable(dht);
		return false;
	}


	dht->lastHumidity = dhtData[0] + dhtData[1] / 10.0f;
	dht->lastTemperature = dhtData[2] + dhtData[3] / 10.0f;

	Dht_disable(dht);

	return true;
}

void Dht_getResult(Dht * dht, int * temperature, int * humidity)
{
	configASSERT(dht != NULL);
	configASSERT(temperature != NULL);
	configASSERT(humidity != NULL);

	*temperature = (int)dht->lastTemperature;
	*humidity = (int)dht->lastHumidity;
}

static void Dht_disable(Dht * dht)
{
	u32 dir = XGpio_GetDataDirection(dht->gpio, GPIO_CHANNEL);
	XGpio_SetDataDirection(dht->gpio, GPIO_CHANNEL, dir & ~dht->dhtPin);

	// Set output pin to high to disable DHT
	XGpio_DiscreteSet(dht->gpio, GPIO_CHANNEL, dht->dhtPin);

	dht->isRunning = false;
}

static void Dht_enable(Dht * dht)
{
	dht->isRunning = true;

	u32 dir = XGpio_GetDataDirection(dht->gpio, GPIO_CHANNEL);
	XGpio_SetDataDirection(dht->gpio, GPIO_CHANNEL, dir & ~dht->dhtPin);

	// set DHT output pin to low to enable DHT
	XGpio_DiscreteClear(dht->gpio, GPIO_CHANNEL, dht->dhtPin);
}

