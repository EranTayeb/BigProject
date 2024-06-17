#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "xil_printf.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include "xuartps.h"
#include "FreeRTOS_CLI.h"
#include "XGpio.h"
#include "xparameters.h"
#include "queue.h"
#include "event_groups.h"
#include "string.h"
#include "xiic.h"
#include "xiicps.h"
#include "XScuGic.h"

#define CHANNEL 1
#define DeviceId 0
#define GPIO_DEVICE_ID XPAR_GPIO_0_DEVICE_ID
#define LED1 (0x01 << 20)
#define LED2 (0x01 << 21)
#define MASK_BUTTON (0x01 << 17)
#define DeviceIdRTC XPAR_AXI_IIC_0_DEVICE_ID
extern XScuGic xInterruptController;

QueueHandle_t xQueue;
TaskHandle_t TaskMainID ;
TaskHandle_t TasklogID ;
TaskHandle_t TaskRTCID ;
TaskHandle_t TaskCLIID;
TaskHandle_t TaskMesID;

XIic instancePtrRTC;
XUartPs InstancePtrUART;
XIicPs I2cInst;


u8  BufferPtr[100] ;
char  BufferPtrUART[100] ;
char  BufferPtrCLI[100] ;
int checkrecv = 0 ;
int checksend = 0 ;
int Status;
int system_verbosity_level = 0 ;

SemaphoreHandle_t levelMutex;
SemaphoreHandle_t MutexRTC;
SemaphoreHandle_t MutexTaskMes;
SemaphoreHandle_t MutexTIME;
SemaphoreHandle_t Mutexlog;
SemaphoreHandle_t MutexCLI;
SemaphoreHandle_t MutexTASKRTC;
SemaphoreHandle_t Mutexverbosity;



typedef enum LogVerbosity_ {
LOG_DEBUG = 0,
LOG_INFO,
LOG_WARNING,
LOG_ERROR,
} LogVerbosity;

typedef struct {
	char message[100];
	LogVerbosity verbosity;
} struct_Data;

typedef struct {
	int year ;
	int month ;
	int week ;
	int day ;
	int hour ;
	int Min ;
	int sec ;

} struct_time;

void TaskMain(void *pvParameters);
void logg(LogVerbosity verbosity, const char * message);
void logisr(LogVerbosity verbosity, const char * message ,BaseType_t *pxHigherPriorityTaskWoken );
void Tasklog(void *pvParameters);
void TaskCLI(void *pvParameters);
void TaskRTC(void *pvParameters);
void TaskMes(void *pvParameters);
void RecvHandler ();
void SendHandler ();
void initbutton();
void vButtonISR(void *CallBackRef);
BaseType_t set_time(char * writeBuffer, size_t bufferLen, const char *pcCommandString);
BaseType_t set_date(char * writeBuffer, size_t bufferLen, const char *pcCommandString);
BaseType_t date_time(char * writeBuffer, size_t bufferLen, const char *pcCommandString);
BaseType_t set_verbosity(char * writeBuffer, size_t bufferLen, const char *pcCommandString);

struct_time Time = {0,0,0,0,0,0,0};
u8 slaveAddr = 0x51 ;
XGpio Gpio;


int num = 0;
static const CLI_Command_Definition_t set_timeCommand =
{
"set_time",
"set_time \r\n",
set_time,
1 // expects exactly one parameter
};

static const CLI_Command_Definition_t set_dateCommand =
{
"set_date",
"set_date \r\n",
set_date,
1 // expects exactly one parameter
};
static const CLI_Command_Definition_t date_timeCommand =
{
"date_time",
"get date_time\r\n",
date_time,
0 // expects exactly one parameter
};
static const CLI_Command_Definition_t set_verbosityCommand =
{
"set_verbosity",
"set_verbosity\r\n",
set_verbosity,
1 // expects exactly one parameter
};



int main(void) {

    xTaskCreate(TaskMain, "TaskMain", configMINIMAL_STACK_SIZE*10, NULL, tskIDLE_PRIORITY + 4, &TaskMainID);

    vTaskStartScheduler();

while (1);
return 0;
}

void TaskMain(void *pvParameters) {

	int AddressType = XII_ADDR_TO_SEND_TYPE ;


	xQueue = xQueueCreate( 10 , sizeof( struct_Data ) );

	levelMutex = xSemaphoreCreateMutex();
	MutexRTC = xSemaphoreCreateMutex();
	MutexTIME = xSemaphoreCreateMutex();
	Mutexlog = xSemaphoreCreateMutex();
	MutexCLI = xSemaphoreCreateMutex();
	MutexTASKRTC = xSemaphoreCreateMutex();
	Mutexverbosity = xSemaphoreCreateMutex();
	MutexTaskMes = xSemaphoreCreateMutex();


	xSemaphoreTake(Mutexlog, portMAX_DELAY);
	xSemaphoreTake(MutexCLI, portMAX_DELAY);
	xSemaphoreTake(MutexTASKRTC, portMAX_DELAY);
	xSemaphoreTake(MutexTaskMes, portMAX_DELAY);


	int led1_2 = LED1 | LED2;
    // Initialize GPIO
    int Status = XGpio_Initialize(&Gpio, GPIO_DEVICE_ID);
    if (Status != XST_SUCCESS) {
        xil_printf("GPIO Initialization Failed\r\n");
    }
    XGpio_SetDataDirection(&Gpio, 1, ~led1_2);
    initbutton();

    XUartPs_Config  *Config =  XUartPs_LookupConfig(DeviceId);

    Status = XUartPs_CfgInitialize( &InstancePtrUART, Config, Config->BaseAddress);

	Status = XIic_Initialize(&instancePtrRTC, DeviceIdRTC);

	Status = XIic_SetAddress(&instancePtrRTC,AddressType, 0x51);

	// start I2C
	XIic_Start(&instancePtrRTC);
		// define I2C interrupt to GIC
		BaseType_t pass = xPortInstallInterruptHandler(XPAR_FABRIC_AXI_IIC_0_IIC2INTC_IRPT_INTR, XIic_InterruptHandler, &instancePtrRTC);
		if(pass != pdPASS){
			xil_printf("xPortInstallInterruptHandler fail\n\r");
		}
		XIic_SetRecvHandler(&instancePtrRTC, 0, (XIic_Handler) RecvHandler);
		XIic_SetSendHandler(&instancePtrRTC, 0, (XIic_Handler) SendHandler);
		vPortEnableInterrupt(XPAR_FABRIC_AXI_IIC_0_IIC2INTC_IRPT_INTR);
		XScuGic_SetPriorityTriggerType(&xInterruptController, XPAR_FABRIC_AXI_IIC_0_IIC2INTC_IRPT_INTR, 0xA0, 0x3);


    FreeRTOS_CLIRegisterCommand(&set_timeCommand);
    FreeRTOS_CLIRegisterCommand(&set_dateCommand);
    FreeRTOS_CLIRegisterCommand(&date_timeCommand);
    FreeRTOS_CLIRegisterCommand(&set_verbosityCommand);


    xTaskCreate(Tasklog, "Tasklog", configMINIMAL_STACK_SIZE*3, NULL, tskIDLE_PRIORITY + 1, &TasklogID);
    xTaskCreate(TaskRTC, "TaskRTC", configMINIMAL_STACK_SIZE*3, NULL, tskIDLE_PRIORITY + 2, &TaskRTCID);
    xTaskCreate(TaskCLI, "TaskCLI", configMINIMAL_STACK_SIZE*3, NULL, tskIDLE_PRIORITY + 1, &TaskCLIID);
    xTaskCreate(TaskMes, "TaskMes", configMINIMAL_STACK_SIZE*3, NULL, tskIDLE_PRIORITY + 1, &TaskMesID);

	xSemaphoreGive(MutexTASKRTC);
	xSemaphoreGive(Mutexlog);
	xSemaphoreGive(MutexCLI);
	xSemaphoreGive(MutexTaskMes);

	vTaskDelay(portMAX_DELAY);
}

void TaskRTC(void *pvParameters) {
	xSemaphoreTake(MutexTASKRTC, portMAX_DELAY);

		const TickType_t xDelay = pdMS_TO_TICKS(10);
		int ByteCount = 9 ;
		u8 buffersend = 0;
		int check = 0 ;
		LogVerbosity verbositystart = LOG_WARNING ;

		int sec , min , hour , day , week , month , year ;
			while (1) {
				vTaskDelay(xDelay);
				if(check==0){
					logg(verbositystart, "start");
				}
				check = 1;
			    if (xSemaphoreTake(MutexRTC, portMAX_DELAY) == pdTRUE) {

					XIic_MasterSend(&instancePtrRTC, &buffersend, 1);
					while(checksend == 0 || XIic_IsIicBusy(&instancePtrRTC) );
					checksend = 0;
					XIic_MasterRecv(&instancePtrRTC, BufferPtr,  ByteCount);
					while(checkrecv == 0 || XIic_IsIicBusy(&instancePtrRTC));
					checkrecv = 0;
					xSemaphoreGive(MutexRTC);

			    }
				sec = (BufferPtr[2]&0x0F) + ((BufferPtr[2]>>4)&0x07)*10      ;
				min = (BufferPtr[3]&0x0F) + ((BufferPtr[3]>>4)&0x07)*10;
				hour = (BufferPtr[4]&0x0F) + ((BufferPtr[4]>>4)&0x03)*10;
				day = (BufferPtr[5]&0x0F) + ((BufferPtr[5]>>4)&0x03)*10;
				week = (BufferPtr[6]&0x07) ;
				month = (BufferPtr[7]&0x0F) + ((BufferPtr[7]>>4)&0x01)*10;
				year = (BufferPtr[8]&0x0F) + (BufferPtr[8]>>4)*10;

			    if (xSemaphoreTake(MutexTIME, portMAX_DELAY) == pdTRUE) {

					Time.sec = sec;
					Time.Min = min;
					Time.hour = hour;
					Time.day = day;
					Time.week = week;
					Time.month = month;
					Time.year = year;

					xSemaphoreGive(MutexTIME);
			    }


			}



}
void TaskCLI(void *pvParameters) {
	xSemaphoreTake(MutexCLI, portMAX_DELAY);
	char ch;
	u32 NumBytes = 1 ;
	int count =0 ;
	LogVerbosity verbosityCLI = LOG_WARNING ;

		while (1) {
			Status = XUartPs_Recv(&InstancePtrUART, (u8*)&ch, NumBytes);
			if (Status != 0) {

				Status = XUartPs_Send(&InstancePtrUART, (u8*)&ch, NumBytes);
				BufferPtrUART[count] = ch;
				count++;
				if ((BufferPtrUART[count - 1] == '\n')
						|| (BufferPtrUART[count - 1] == '\r')) {
					xil_printf("\r\n");
					BufferPtrUART[count - 1] = '\0';
					count = 0;
					FreeRTOS_CLIProcessCommand(BufferPtrUART, BufferPtrCLI,100);
					xil_printf("%s\n\r",BufferPtrCLI);
					logg(verbosityCLI, "CLI");

					}
					taskYIELD();
				}

		}
}

void Tasklog(void *pvParameters) {
	xSemaphoreTake(Mutexlog, portMAX_DELAY);
	struct_Data receivedValue ;
	char buff[100];
	while(1){
	 xQueueReceive(xQueue, &receivedValue, portMAX_DELAY);

		if (receivedValue.verbosity >= system_verbosity_level) {
		    if (xSemaphoreTake(MutexTIME, portMAX_DELAY) == pdTRUE) {

		    	sprintf(buff,"Date : %d-%d-%d \n\r Time : %d:%d:%d\n\r ",Time.day,Time.month,Time.year,Time.hour,Time.Min,Time.sec);
				xSemaphoreGive(MutexTIME);
		    	}
			xil_printf ("%s\n\r",buff);
			xil_printf ("%s\n\r",receivedValue.message);


			}
	}

}
void TaskMes(void *pvParameters) {
	xSemaphoreTake(MutexTaskMes, portMAX_DELAY);

    const TickType_t xDelay = pdMS_TO_TICKS(30000); // 30 seconds delay
	LogVerbosity verbosityM = LOG_WARNING ;

    while(1){
        vTaskDelay(xDelay);
		logg(verbosityM, "TaskM");

    }

}


void logg(LogVerbosity verbosity, const char * message){

	struct_Data data ;
	strcpy(data.message, message);
	data.verbosity = verbosity ;
	Status = xQueueSendToBack(xQueue,(void*) &data, 0);
						if (Status != pdPASS) {
							xil_printf("Queue is FULL.\r\n");
						}

}
void logisr(LogVerbosity verbosity, const char * message, BaseType_t *pxHigherPriorityTaskWoken ){
	struct_Data data;

	strcpy(data.message, message);
	data.verbosity = verbosity;
	Status = xQueueSendFromISR(xQueue, (void* ) &data, pxHigherPriorityTaskWoken);
	if (Status != pdPASS) {
		xil_printf("Queue is FULL.\r\n");
	}
}

void SendHandler (){

	checksend = 1 ;

}
void RecvHandler (){
	checkrecv = 1 ;
}

BaseType_t set_time(char * writeBuffer, size_t bufferLen, const char *pcCommandString) {
	BaseType_t parameterStringLength;
	int h, m, s;
	u8  BufferPtrsend[4] ;

	const char * parameter = FreeRTOS_CLIGetParameter(  pcCommandString, 1, &parameterStringLength);

	sscanf(parameter, "%d:%d:%d", &h, &m, &s);
	if (h<0 || h>23){
		return 0;
	}
	if (m<0 || m>59){
		return 0;
	}
	if (s<0 || s>59){
		return 0;
	}
	BufferPtrsend[0] = 0X02 ;

	BufferPtrsend[1] = ((s/10) << 4)|(s%10);
	BufferPtrsend[2] = ((m/10) << 4)|(m%10);
	BufferPtrsend[3] = ((h/10) << 4)|(h%10);

	if (xSemaphoreTake(MutexRTC, portMAX_DELAY) == pdTRUE) {
		checksend = 0;
		XIic_MasterSend(&instancePtrRTC, BufferPtrsend, 4);
		while (checksend == 0 || XIic_IsIicBusy(&instancePtrRTC))
			;
		checksend = 0;
		num = 1;
		xSemaphoreGive(MutexRTC);
	}


	writeBuffer[0] = '\0';
	return pdFALSE;
}

BaseType_t set_date(char * writeBuffer, size_t bufferLen, const char *pcCommandString) {
	BaseType_t parameterStringLength;
	int d, m, y;
	u8  BufferPtrsend[5] ;

	const char * parameter = FreeRTOS_CLIGetParameter(  pcCommandString, 1, &parameterStringLength);

	sscanf(parameter, "%d-%d-%d", &d, &m, &y);
	if (d<0 || d>31){
		return 0;
	}
	if (m<0 || m>12){
		return 0;
	}
	if (y<0 || y>99){
		return 0;
	}
	BufferPtrsend[0] = 0X05 ;

	BufferPtrsend[1] = ((d/10) << 4)|(d%10);
	BufferPtrsend[2] = 0x00;
	BufferPtrsend[3] = ((m/10) << 4)|(m%10);
	BufferPtrsend[4] = ((y/10) << 4)|(y%10);

    if (xSemaphoreTake(MutexRTC, portMAX_DELAY) == pdTRUE) {

		XIic_MasterSend(&instancePtrRTC, BufferPtrsend , 5);
		while(checksend == 0 );
		checksend = 0;
		xSemaphoreGive(MutexRTC);
    }

	writeBuffer[0] = '\0';
	return pdFALSE;
}

BaseType_t date_time(char * writeBuffer, size_t bufferLen, const char *pcCommandString) {

	writeBuffer[0] = '\0';
	return pdFALSE;
}

BaseType_t set_verbosity(char * writeBuffer, size_t bufferLen, const char *pcCommandString) {
	BaseType_t parameterStringLength;
	int num ;
    if (xSemaphoreTake(levelMutex, portMAX_DELAY) == pdTRUE) {

    	const char * parameter = FreeRTOS_CLIGetParameter(  pcCommandString, 1, &parameterStringLength);
    	num = atoi(parameter);
    	if (xSemaphoreTake(Mutexverbosity, portMAX_DELAY) == pdTRUE) {
    	system_verbosity_level = num ;
		xSemaphoreGive(Mutexverbosity);
    	}

    	sprintf(writeBuffer," the system_verbosity_level = %d \n\r" ,system_verbosity_level );
        xSemaphoreGive(levelMutex);
    }
	writeBuffer[0] = '\0';
	return pdFALSE;
}

void initbutton() {

	// Instead of XScuPsu_Connect
	xPortInstallInterruptHandler(XPAR_FABRIC_GPIO_0_VEC_ID,
			(XInterruptHandler) vButtonISR, (void *) &Gpio);

	// Enable the interrupt for GPIO channel
	XGpio_InterruptEnable(&Gpio, 1);
	// Enable all interrupts in the GPIO
	XGpio_InterruptGlobalEnable(&Gpio);
	vPortEnableInterrupt(XPAR_FABRIC_GPIO_0_VEC_ID);
	XScuGic_SetPriorityTriggerType(&xInterruptController,
			XPAR_FABRIC_GPIO_0_VEC_ID, 0xA0, 0x3);
}
void vButtonISR(void *CallBackRef) {
 	u32 val = XGpio_DiscreteRead(&Gpio, 1);
 	if ((MASK_BUTTON & val) == 0 ) {
		BaseType_t xHigherPriorityTaskWoken = pdFALSE;
		LogVerbosity verbosity = LOG_INFO ;
		//xil_printf("yaaaaa");
		logisr( verbosity, "button", &xHigherPriorityTaskWoken );
 	}
	XGpio_InterruptClear(&Gpio, 1);

}
