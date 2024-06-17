#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "xil_printf.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "xuartps.h"
#include "FreeRTOS_CLI.h"
#include "dht.h"
#include "XGpio.h"
#include "xparameters.h"
#include "queue.h"
#include "event_groups.h"
#include "string.h"

#define GPIO_DEVICE_ID XPAR_GPIO_0_DEVICE_ID
#define LED1 (0x01 << 20)
#define LED2 (0x01 << 21)
#define DHT_PIN (0x01 << 27)



int Temperature =0,Humidity=0;
int TEMP_HIGH_LIMIT = 30 ,TEMP_LOW_LIMIT = 25 ;
Dht * dhtSensor;

#define CHANNEL 1
#define DeviceId 0
XUartPs InstancePtrUART;
QueueHandle_t xQueue;
XGpio Gpio;
TaskHandle_t initmainID ;
TaskHandle_t TaskCLIID ;
TaskHandle_t TaskUartID ;
TaskHandle_t MonitoringTaskID ;
TaskHandle_t AlarmTaskID ;
SemaphoreHandle_t TemperatureMutex;

TimerHandle_t xTimerMonitoring;
TimerHandle_t xTimerblink ;

SemaphoreHandle_t MutexTaskCLI;
SemaphoreHandle_t MutexTaskUart;
SemaphoreHandle_t MutexAlarmTask;



char  BufferPtr[1000] ;
char  BufferPtr2[1000] ;
char  BufferPtrCLI[1000] ;

int Status;


TaskStatus_t pxTaskStatusArray [10];

// Generic task function for printing

void TaskUart(void *pvParameters);
void TaskCLI(void *pvParameters);
void TaskMain(void *pvParameters);
void AlarmTask(void *pvParameters);
void checktemp(void *pvParameters);
void blink(void *pvParameters);
BaseType_t get_temp(char * writeBuffer, size_t bufferLen, const char *pcCommandString);
BaseType_t list(char * writeBuffer, size_t bufferLen, const char *pcCommandString);
BaseType_t stat(char * writeBuffer, size_t bufferLen, const char *pcCommandString);
BaseType_t set_low(char * writeBuffer, size_t bufferLen, const char *pcCommandString);
BaseType_t set_high(char * writeBuffer, size_t bufferLen, const char *pcCommandString);


static const CLI_Command_Definition_t listCommand =
{
"list",
"print list of task names one per line\r\n",
list,
0 // expects exactly one parameter
};
static const CLI_Command_Definition_t statCommand =
{
"stat",
"print run-time statistics\r\n",
stat,
0 // expects exactly one parameter
};
static const CLI_Command_Definition_t set_highCommand =
{
"set_high",
"sets TEMP_HIGH_LIMIT\r\n",
set_high,
1 // expects exactly one parameter
};
static const CLI_Command_Definition_t set_lowCommand =
{
"set_low",
"sets TEMP_LOW_LIMIT\r\n",
set_low ,
1 // expects exactly one parameter
};

static const CLI_Command_Definition_t get_tempCommand =
{
"get_temp",
" print temperature and humidity\r\n",
get_temp,
0 // expects exactly one parameter
};


int main(void) {

    xTaskCreate(TaskMain, "initmain", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, &initmainID);

    vTaskStartScheduler();

while (1);
return 0;

}
void TaskMain(void *pvParameters) {



	MutexTaskCLI = xSemaphoreCreateMutex();
	MutexTaskUart = xSemaphoreCreateMutex();
	MutexAlarmTask = xSemaphoreCreateMutex();

	xSemaphoreTake(MutexTaskCLI, portMAX_DELAY);
	xSemaphoreTake(MutexTaskUart, portMAX_DELAY);
	xSemaphoreTake(MutexAlarmTask, portMAX_DELAY);

	//Initialize GPIO
	int Status = XGpio_Initialize(&Gpio, XPAR_GPIO_0_DEVICE_ID);
	if (Status != XST_SUCCESS) {
			xil_printf("Gpio Initialization Failed\r\n");
		}
	XGpio_SetDataDirection(&Gpio, CHANNEL, ~LED1);

    dhtSensor = Dht_init(&Gpio, DHT_PIN);

	xQueue = xQueueCreate( 10 , 100*sizeof( char ) );

	TemperatureMutex = xSemaphoreCreateMutex();

    XUartPs_Config  *Config =  XUartPs_LookupConfig(DeviceId);

    Status = XUartPs_CfgInitialize( &InstancePtrUART, Config, Config->BaseAddress);

	xTimerMonitoring = xTimerCreate("xTimerMonitoring", pdMS_TO_TICKS(3000), pdTRUE,NULL,(TimerCallbackFunction_t)checktemp);
	xTimerblink = xTimerCreate("xTimerMonitoring", pdMS_TO_TICKS(300), pdTRUE,NULL,(TimerCallbackFunction_t)blink);




    FreeRTOS_CLIRegisterCommand(&listCommand);
    FreeRTOS_CLIRegisterCommand(&statCommand);
    FreeRTOS_CLIRegisterCommand(&set_highCommand);
    FreeRTOS_CLIRegisterCommand(&set_lowCommand);
    FreeRTOS_CLIRegisterCommand(&get_tempCommand);

    xTaskCreate(TaskCLI, "TaskCLI", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, &TaskCLIID);
    xTaskCreate(TaskUart, "TaskUart", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, &TaskUartID);
    xTaskCreate(AlarmTask, "AlarmTask", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 4,&AlarmTaskID);

	xSemaphoreGive(MutexTaskCLI);
	xSemaphoreGive(MutexTaskUart);
	xSemaphoreGive(MutexAlarmTask);

	xTimerStart(xTimerMonitoring, 0);

	vTaskDelay(portMAX_DELAY);
}


void TaskCLI(void *pvParameters) {
	xSemaphoreTake(MutexTaskCLI, portMAX_DELAY);

		while (1) {
			Status = xQueueReceive(xQueue, BufferPtr2, portMAX_DELAY);
			FreeRTOS_CLIProcessCommand(BufferPtr2, BufferPtrCLI,100);
			xil_printf("%s\n\r",BufferPtrCLI);
		}
}


void TaskUart(void *pvParameters){
	xSemaphoreTake(MutexTaskUart, portMAX_DELAY);

	char ch;
	u32 NumBytes = 1 ;
	int count =0 ;
	while (1) {
		Status = XUartPs_Recv(&InstancePtrUART, (u8*)&ch, NumBytes);
		if (Status != 0) {
			Status = XUartPs_Send(&InstancePtrUART, (u8*)&ch, NumBytes);
			BufferPtr[count] = ch;
			count++;
			if ((BufferPtr[count - 1] == '\n')
					|| (BufferPtr[count - 1] == '\r')) {
				xil_printf("\r\n");
				BufferPtr[count - 1] = '\0';
				count = 0;

					Status = xQueueSendToBack(xQueue, BufferPtr, 0);
					if (Status != pdPASS) {
						xil_printf("Queue is FULL.\r\n");
				}
				taskYIELD();
			}
		}
	}
}



void AlarmTask(void *pvParameters) {
	xSemaphoreTake(MutexAlarmTask, portMAX_DELAY);

	 TickType_t counttimein ;
	TickType_t counttimeout ;

	uint32_t receivedValue;
	while (1) {
		xTaskNotifyWait(0, 0, &receivedValue, portMAX_DELAY);
		xil_printf("in alarmtask\n\r");
 		counttimein =  xTaskGetTickCount();
 		counttimein = counttimein*portTICK_PERIOD_MS;
		xTimerStart(xTimerblink, 0);
		while (1) {
			counttimeout =  xTaskGetTickCount();
			counttimeout = counttimeout*portTICK_PERIOD_MS;

			if ((counttimeout-counttimein) > 3000){
		 		counttimein =  xTaskGetTickCount();
		 		counttimein = counttimein*portTICK_PERIOD_MS;
				xil_printf("error Temperature = %d\n\r",Temperature);
			}
			if (xSemaphoreTake(TemperatureMutex, portMAX_DELAY) == pdTRUE) {

				if (Temperature < TEMP_LOW_LIMIT) {
					xTimerStop(xTimerblink, 0);
					break;
				}
				xSemaphoreGive(TemperatureMutex);
			}

		}

	}

}


BaseType_t get_temp(char * writeBuffer, size_t bufferLen,
		const char *pcCommandString) {
	int Humiditycheck ;
	int Temperaturecheck ;

	Dht_takeData(dhtSensor);
	Dht_getResult(dhtSensor, &Temperaturecheck, &Humiditycheck);
	if (xSemaphoreTake(TemperatureMutex, portMAX_DELAY) == pdTRUE) {
		Temperature=Temperaturecheck;
		Humidity=Humiditycheck;
		sprintf(writeBuffer, "Temperature - %d \n\r ", Temperature);
		xil_printf("%s", writeBuffer);
		xSemaphoreGive(TemperatureMutex);
	} else {
		xil_printf("TemperatureMutex is FULL.\r\n");
	}
	writeBuffer[0] = '\0';

	return pdFALSE;
}

BaseType_t list(char * writeBuffer, size_t bufferLen, const char *pcCommandString) {
	vTaskList(writeBuffer);
	return pdFALSE;
}

BaseType_t stat(char * writeBuffer, size_t bufferLen, const char *pcCommandString) {
	vTaskGetRunTimeStats(writeBuffer);
	return pdFALSE;
}

BaseType_t set_high(char * writeBuffer, size_t bufferLen, const char *pcCommandString) {
	BaseType_t parameterStringLength;
	int num ;
	const char * parameter = FreeRTOS_CLIGetParameter(  pcCommandString, 1, &parameterStringLength);

	num = atoi(parameter);
	TEMP_HIGH_LIMIT = num ;
	sprintf(writeBuffer," the TEMP_HIGH_LIMIT = %d \n\r" ,TEMP_HIGH_LIMIT );

	return pdFALSE;
}

BaseType_t set_low(char * writeBuffer, size_t bufferLen, const char *pcCommandString) {
	BaseType_t parameterStringLength;
	int num ;
	const char * parameter = FreeRTOS_CLIGetParameter(  pcCommandString, 1, &parameterStringLength);
	num = atoi(parameter);
	TEMP_LOW_LIMIT = num ;
	sprintf(writeBuffer," the TEMP_LOW_LIMIT = %d \n\r" ,TEMP_LOW_LIMIT );

	return pdFALSE;
}

void checktemp(void *pvParameters) {
	int Humiditycheck1 ;
	int Temperaturecheck1 ;
	Dht_takeData(dhtSensor);
	Dht_getResult(dhtSensor, &Temperaturecheck1, &Humiditycheck1);
	if (xSemaphoreTake(TemperatureMutex, portMAX_DELAY) == pdTRUE) {
		Temperature=Temperaturecheck1;
		Humidity=Humiditycheck1;

		if (Temperature > TEMP_HIGH_LIMIT) {
			xTaskNotify(AlarmTaskID, 0, eNoAction); // Notify DHT task to start reading
		}
		xSemaphoreGive(TemperatureMutex);

	}

}
void blink(void *pvParameters){
//	xil_printf("in blinky: \r\n");
        u32 val = XGpio_DiscreteRead(&Gpio, CHANNEL);
        val ^=LED1;
        XGpio_DiscreteWrite(&Gpio, CHANNEL, val);

}


