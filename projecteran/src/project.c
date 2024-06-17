#include "xparameters.h"
#include "xgpio.h"
#include "platform.h"
#include "xil_printf.h"
#include "XScuGic.h"
#include "xttcps.h"
#include "XGpio.h"
#include <stdio.h>
#include <string.h>
#include "xuartps.h"
#include "xtmrctr.h"


void LED_on();
void LED_on_partial_brightness();
void LED_blinking();
void LED_off();
void Initialize_Timer_PWM();
int TTC_Initialization();
void TickHandler_LED_blinking();


XGpio Gpio;
XScuGic IntcInstance;
/* The Instance of the GPIO Driver */

XTmrCtr TimerCounterInst ;
#define LED1 (0x01 << 20)
#define LED2 (0x01 << 21)
#define LED_CHANNEL 1
#define MASK_BUTTON (0x01 << 17)
#define DeviceId 0
int Counter = 0 ;
char  BufferPtr[100] ;
XGpio Gpio;
XScuGic IntcInstance;
XScuGic intController;
u32  LED ;
#define DeviceID 0
#define TIMER_CNTR_0 0
int cuont_interruptReceived = 0;
int interruptReceived = 0;
int Status ;
int counter_led = 0;
char * bright = "bright" ;
char * blink = "blink" ;

XInterval Interval = 100000000;

XTtcPs InstancePtr_TTC;
XUartPs InstancePtrUART;
u32 PwmPeriod = 1000000 ;
u32 PwmHighTime = 0 ;
u32 intduty = 50 ;
u32 intdutynew = 50 ;





void GpioHandler(void *callbackRef);

int main() {
	int check = 0;
	init_platform();

	//Initialize GPIO
	Status = XGpio_Initialize(&Gpio, XPAR_GPIO_0_DEVICE_ID);
	XGpio_SetDataDirection(&Gpio, LED_CHANNEL, ~LED1);
	// Initialize and enable Interrupt controller
	//Status = XScuGic_Initialize(&IntcInstance, 0);
	XScuGic_Config *intConfig = XScuGic_LookupConfig(0);
	if (intConfig == NULL) {
		return XST_FAILURE;
	}
	Status = XScuGic_CfgInitialize(&IntcInstance, intConfig,
			intConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}
	Status = XScuGic_Connect(&IntcInstance, XPAR_FABRIC_GPIO_0_VEC_ID,
			(Xil_InterruptHandler) GpioHandler, (void *) &Gpio);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE; // Indicates a failure to connect the interrupt handler
	}
	XScuGic_SetPriorityTriggerType(&IntcInstance, XPAR_FABRIC_GPIO_0_VEC_ID, 0xA0, 3);
	XScuGic_Enable(&IntcInstance, XPAR_FABRIC_GPIO_0_VEC_ID);
	// Enable the GPIO interrupt in the GIC
	XGpio_InterruptEnable(&Gpio, 1);
	// Enable the interrupt for GPIO channel
	XGpio_InterruptGlobalEnable(&Gpio);
	// Enable all interrupts in the GPIO

	TTC_Initialization();

	Xil_ExceptionInit();

	Xil_ExceptionRegisterHandler(
	XIL_EXCEPTION_ID_IRQ_INT,             // the IRQ exception
			(Xil_ExceptionHandler) XScuGic_InterruptHandler, // a general interrupt handler
			&IntcInstance                      // an interrupt handler argument
			// a pointer to the controller
			);

	// At some later point when everything will be ready the IRQ exceptions should be allowed:
	Xil_ExceptionEnableMask(XIL_EXCEPTION_IRQ);

	Initialize_Timer_PWM() ;




    XUartPs_Config  *Config =  XUartPs_LookupConfig(DeviceId);

    Status = XUartPs_CfgInitialize( &InstancePtrUART, Config, Config->BaseAddress);


    int readerCounter =0 ;
	u32 NumBytes = 1 ;
	int count =0 ;
    char command[100];



	while (1) {
		if (interruptReceived == 1) {
			LED_on();
			xil_printf("1\n\r");
			interruptReceived = 0 ;
		}
		if (interruptReceived == 2) {
			check = 1 ;
			LED_on_partial_brightness();
			xil_printf("2\n\r");

			interruptReceived = 0 ;

		}
		if (interruptReceived == 3) {
			 check = 0 ;
			LED_blinking();
			xil_printf("3\n\r");

			interruptReceived = 0 ;

		}
		if (interruptReceived == 4) {
			XTtcPs_Stop(&InstancePtr_TTC);
			LED_off();
			xil_printf("4\n\r");

			interruptReceived = 0 ;

		}
	    int num ;
		char ch;

				Status = XUartPs_Recv(&InstancePtrUART, &ch, NumBytes);
				if (Status != 0) {
					Status = XUartPs_Send(&InstancePtrUART, &ch, NumBytes);
					BufferPtr[count] = ch;
					count++;
					if ((BufferPtr[count-1] == '\n') || (BufferPtr[count-1] == '\r')) {
						BufferPtr[count - 1] = '\0';
						count = 0 ;
						readerCounter = sscanf(BufferPtr, "%s %d", command, &num);
						if(readerCounter < 2){
										printf("need more information for the command\r\n");
										continue;
									}

						if (strcmp(command,bright) == 0) {
							 intdutynew = num ;
							 PwmHighTime = (PwmPeriod/100)*intdutynew;
							 if(check == 1){
							 XTmrCtr_PwmDisable(&TimerCounterInst);
							 Status = XTmrCtr_PwmConfigure( &TimerCounterInst,  PwmPeriod,  PwmHighTime);
							 XTmrCtr_PwmEnable(&TimerCounterInst);
							 }
								}
						if (strcmp(command,blink) == 0) {
							Interval=num*100000;
							XTtcPs_SetInterval(&InstancePtr_TTC, Interval);
							XTtcPs_ResetCounterValue(&InstancePtr_TTC);




						 	 }
						}

					}
	}
	return 0;
}

void GpioHandler(void *callbackRef) {

	XGpio *gpioPtr = (XGpio *) callbackRef; // Convert generic pointer to a pointer to the specific object
	// Implement the interrupt logic
	u32 val = XGpio_DiscreteRead(&Gpio, 1);
	if ((MASK_BUTTON & val) == 0) {
		cuont_interruptReceived++ ;
		if (cuont_interruptReceived == 5){
			cuont_interruptReceived=1;
		}
		interruptReceived = cuont_interruptReceived ;
	}
	XGpio_InterruptClear(gpioPtr, val); // Clear the interrupt
}

void LED_on(){
	intduty=99;
	PwmHighTime = (PwmPeriod / 100) * intduty;
	XTmrCtr_PwmDisable(&TimerCounterInst);
	Status = XTmrCtr_PwmConfigure(&TimerCounterInst, PwmPeriod, PwmHighTime);
	XTmrCtr_PwmEnable(&TimerCounterInst);

}

void LED_on_partial_brightness(){
	intduty=intdutynew;
	PwmHighTime = (PwmPeriod / 100) * intduty;
	XTmrCtr_PwmDisable(&TimerCounterInst);
	 Status = XTmrCtr_PwmConfigure(&TimerCounterInst, PwmPeriod, PwmHighTime);
	XTmrCtr_PwmEnable(&TimerCounterInst);

}
void LED_blinking(){

	XTtcPs_Start(&InstancePtr_TTC);

}
void LED_off(){
	intduty=1;
	PwmHighTime = (PwmPeriod / 100) * intduty;
	XTmrCtr_PwmDisable(&TimerCounterInst);
	 Status = XTmrCtr_PwmConfigure(&TimerCounterInst, PwmPeriod, PwmHighTime);
	XTmrCtr_PwmEnable(&TimerCounterInst);
}

void Initialize_Timer_PWM()
{
	PwmHighTime=(PwmPeriod/100)*intduty;
	 Status = XTmrCtr_Initialize(&TimerCounterInst, DeviceId);
		      if (Status != 0) {
		       printf(" XTmrCtr_Initialize fail\n");
		      }
		      XTmrCtr_PwmDisable(&TimerCounterInst);
		      Status = XTmrCtr_PwmConfigure( &TimerCounterInst,  PwmPeriod,  PwmHighTime);
		      XTmrCtr_PwmEnable(&TimerCounterInst);
		      if (Status != 0) {
		    	  printf("XTmrCtr_PwmEnable fail\n");
		      }
		      return;
}


int TTC_Initialization() {
	u8 Prescaler;
	//initialize the TTC
	XTtcPs_Config * ttc_config = XTtcPs_LookupConfig(DeviceId);
	int Status = XTtcPs_CfgInitialize(&InstancePtr_TTC, ttc_config,ttc_config->BaseAddress);
	if (Status != 0) {
		return 1;
	}


	// Connect the TTC device to the InterruptHandler
	Status = XScuGic_Connect(&IntcInstance, XPS_TTC0_0_INT_ID,(Xil_InterruptHandler) XTtcPs_InterruptHandler, &InstancePtr_TTC);
 	XTtcPs_SetStatusHandler(&InstancePtr_TTC, &Gpio, TickHandler_LED_blinking);
 	Status = XTtcPs_SetOptions(&InstancePtr_TTC, XTTCPS_OPTION_INTERVAL_MODE);
	if (Status != 0) {
		return 1;
	}


	// Calculate and set the timer interval and prescaler
	XTtcPs_SetInterval(&InstancePtr_TTC, Interval);
	XTtcPs_SetPrescaler(&InstancePtr_TTC, 0);


	//Enable the TTC and the GIC
	XTtcPs_EnableInterrupts(&InstancePtr_TTC, XTTCPS_IXR_INTERVAL_MASK);
	XScuGic_Enable(&IntcInstance, XPS_TTC0_0_INT_ID);



	return 0;
}

void TickHandler_LED_blinking() {
		counter_led++;
		if (counter_led % 2 == 0) {
			if (intduty  == 1){
			LED_on();
			}else {
			LED_off();
			}
		}



}
