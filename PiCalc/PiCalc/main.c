/*
 * PiCalc.c
 *
 * Created: 20.03.2018 18:32:07
 * Author : chaos
 */ 

//#include <avr/io.h>
#include "avr_compiler.h"
#include "pmic_driver.h"
#include "TC_driver.h"
#include "clksys_driver.h"
#include "sleepConfig.h"
#include "port_driver.h"
#include <stdio.h>
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "event_groups.h"
#include "stack_macros.h"
#include "math.h"

#include "mem_check.h"

#include "init.h"
#include "utils.h"
#include "errorHandler.h"
#include "NHD0420Driver.h"
#include "ButtonHandler.h"

#define BUTTON1SHORTPRESSEDMASK     0x01
#define BUTTON2SHORTPRESSEDMASK     0x02
#define BUTTON3SHORTPRESSEDMASK     0x04
#define BUTTON4SHORTPRESSEDMASK     0x08
#define BUTTON1LONGPRESSEDMASK     0x10
#define BUTTON2LONGPRESSEDMASK     0x20
#define BUTTON3LONGPRESSEDMASK     0x40
#define BUTTON4LONGPRESSEDMASK     0x80

#define STOPALGO    0x01
#define TASKSTOPPED	0x02
#define TASKRESUME  0x04
#define TASKDONE    0x08
#define STARTALGO    0x10
#define STOPALGOBUTTON 0x20
#define RESETALGO    0x40
#define CHANGEALGO    0x80



extern void vApplicationIdleHook( void );

void vTaskxSomayajiAlgo(void *pvParameters);
void vTaskxLeibnizAlgo(void *pvParameters);
void vButtonTask(void *pvParameters);
void vInitHeartbeatCounter(void);
void vUserInterface(void);
void vHeartbeat(void);


TaskHandle_t xLeibnizAlgo;
TaskHandle_t xButtonTaskHandle;
TaskHandle_t xHeartbeatTaskHandle;
TaskHandle_t xUserInterfaceHandle;
TaskHandle_t xTaskxSomayajiAlgo;

EventGroupHandle_t xGlobalEventGroup;

uint8_t ucHeartbeatTimerCounter;
uint8_t ucClockSeconds = 0;
uint8_t ucClockMinutes = 0;



volatile float GlPi = 0;
volatile uint32_t AnzahlDurchlauf = 0;
volatile uint32_t ucMsCounter = 0;





void vApplicationIdleHook( void )
{	
	
}

int main(void)
{
    vInitClock();
    vInitDisplay();
    vInitHeartbeatCounter();
	xGlobalEventGroup = xEventGroupCreate();
	xTaskCreate(vTaskxSomayajiAlgo, (const char *) "TaskxSomayajiAlgo", configMINIMAL_STACK_SIZE+100, NULL, 1, &xTaskxSomayajiAlgo);
	xTaskCreate(vTaskxLeibnizAlgo, (const char *) "LeibnizAlgo", configMINIMAL_STACK_SIZE+100, NULL, 1, &xLeibnizAlgo);
	xTaskCreate(vButtonTask, (const char *) "ButtonTask", configMINIMAL_STACK_SIZE, NULL, 2, &xButtonTaskHandle);
	xTaskCreate(vHeartbeat, (const char *) "Heartbeat", configMINIMAL_STACK_SIZE, NULL, 3, &xHeartbeatTaskHandle);
	xTaskCreate(vUserInterface, (const char *) "UserInterface", configMINIMAL_STACK_SIZE+100, NULL, 2, &xUserInterfaceHandle);

	vTaskSuspend(xHeartbeatTaskHandle);
	vTaskStartScheduler();
	return 0;
}




void vButtonTask(void *pvParameters) {
	initButtons();
	uint32_t EventBits = 0;	
	vTaskDelay(3000);
	for(;;) {
		updateButtons();
		EventBits = xEventGroupGetBits(xGlobalEventGroup);
		
		if(getButtonPress(BUTTON1) == SHORT_PRESSED) {
			xEventGroupSetBits(xGlobalEventGroup,TASKSTOPPED);
			xEventGroupSetBits(xGlobalEventGroup,STARTALGO);
		}
		if(getButtonPress(BUTTON2) == SHORT_PRESSED) {
			xEventGroupSetBits(xGlobalEventGroup,STOPALGOBUTTON);
		}
		if(getButtonPress(BUTTON3) == SHORT_PRESSED) {
			xEventGroupSetBits(xGlobalEventGroup,RESETALGO);
			//xEventGroupSetBits(xGlobalEventGroup,STARTALGO);
			GlPi = 0.000;
			AnzahlDurchlauf=0;
			ucMsCounter=0;
		}
		if(getButtonPress(BUTTON4) == SHORT_PRESSED) {
			xEventGroupSetBits(xGlobalEventGroup,CHANGEALGO);
		}
		if(getButtonPress(BUTTON1) == LONG_PRESSED) {
			xEventGroupSetBits(xGlobalEventGroup,STARTALGO);
			xEventGroupSetBits(xGlobalEventGroup,TASKSTOPPED);
		}
		if(getButtonPress(BUTTON2) == LONG_PRESSED) {
			xEventGroupSetBits(xGlobalEventGroup,STOPALGO);
			xEventGroupSetBits(xGlobalEventGroup,STARTALGO);
		}
		if(getButtonPress(BUTTON3) == LONG_PRESSED) {
			xEventGroupSetBits(xGlobalEventGroup,RESETALGO);
			//xEventGroupSetBits(xGlobalEventGroup,STARTALGO);
			GlPi = 0.000;
		}
		if(getButtonPress(BUTTON4) == LONG_PRESSED) {
			xEventGroupSetBits(xGlobalEventGroup,CHANGEALGO);
		}
		EventBits = xEventGroupGetBits(xGlobalEventGroup);
		vTaskDelay((1000/BUTTON_UPDATE_FREQUENCY_HZ)/portTICK_RATE_MS);
	}
}

void vInitHeartbeatCounter(void)
{
	/* Initializes the Heartbeat timer counter */
	TCC1.CTRLA |= (0b0101) << TC1_CLKSEL_gp;            // CLKdiv = 64 -> fTimer = 500kHz
	TCC1.INTCTRLA |= 1 << TC1_OVFINTLVL_gp;             // Overflow interrupt enable at low-level priority
	TCC1.PER = 499;                                    // TOP after 5000 counts -> Tp = 1/(500kHz) * 5000 = 10ms
	PMIC.CTRL |= 1 << PMIC_LOLVLEN_bp;                  // enable low-level interrupt
}
/*———————————————————–*/



/*———————————————————–*/

void vHeartbeat(void)
{
    
    uint8_t ucMsInterruptCounter = 0;
    BaseType_t xInterruptOccurred = pdFALSE;
    
    while (1)
    {
        if (xInterruptOccurred == pdTRUE)               // process time counting only when interrupt occurred
       {
            xInterruptOccurred = pdFALSE;
            ucMsCounter += ucMsInterruptCounter;
            ucMsInterruptCounter = 0;
        }        
        xInterruptOccurred = xTaskNotifyWait(0, 0xffffffff, &ucMsInterruptCounter, pdMS_TO_TICKS(50));
    }
}

ISR(TCC1_OVF_vect)
{
    /* Occurs all 10ms */
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTaskNotifyFromISR(xHeartbeatTaskHandle, ucHeartbeatTimerCounter, eIncrement, &xHigherPriorityTaskWoken);
}


void vUserInterface(void)
{
	BaseType_t xButtonTaskNotification = pdFALSE;	
	uint32_t ulInterfaceStatus = 0;	
	uint32_t EventBits = 0;	
	TickType_t xLastWakeTime =0;
	char PiString[11];
	bool LeibinzRunning = true;
	bool SomayajiRunning = false;
	vTaskSuspend(xTaskxSomayajiAlgo);
	xEventGroupSetBits(xGlobalEventGroup,STOPALGOBUTTON);
	while (1)
	{	
		EventBits = xEventGroupGetBits(xGlobalEventGroup);
		xEventGroupSetBits(xGlobalEventGroup,STOPALGO); // Anforderung um Task zu Beenden
		xEventGroupWaitBits(xGlobalEventGroup,TASKSTOPPED,pdFALSE,pdFALSE,1000000);
		//Display Output
		EventBits = xEventGroupGetBits(xGlobalEventGroup);
		vDisplayClear();
		vDisplayWriteStringAtPos(0,0,"Pi Calculator");
		if (LeibinzRunning) { // Displaying which task is running
			vDisplayWriteStringAtPos(0,18,"LB");
		}
		else{
			vDisplayWriteStringAtPos(0,17,"KNS");
		}
		sprintf(PiString, "%05f",GlPi);
		vDisplayWriteStringAtPos(1,0,"PI:  %s", PiString);
		sprintf(PiString, "%lu" ,AnzahlDurchlauf);
		vDisplayWriteStringAtPos(2,0,"Steps: %s",PiString); 
		sprintf(PiString, "%lu" ,ucMsCounter);
		vDisplayWriteStringAtPos(3,0,"mSec:  %s" ,PiString);
		 	
		EventBits = xEventGroupGetBits(xGlobalEventGroup);
		if (!(EventBits & STOPALGOBUTTON) && (!(EventBits & TASKDONE ))){
			xEventGroupClearBits(xGlobalEventGroup,STOPALGO);
			xEventGroupClearBits(xGlobalEventGroup,STARTALGO);
			xEventGroupClearBits(xGlobalEventGroup,TASKSTOPPED);
			xEventGroupSetBits(xGlobalEventGroup,TASKRESUME); // Anforderung um Task zu fortsetzen	
			}
		else{
			vTaskSuspend(xHeartbeatTaskHandle);
		}

		if ((EventBits & STARTALGO)){
			vTaskResume(xHeartbeatTaskHandle);
			xEventGroupClearBits(xGlobalEventGroup,STOPALGOBUTTON);
			xEventGroupClearBits(xGlobalEventGroup,TASKDONE);
			xEventGroupClearBits(xGlobalEventGroup,STOPALGO);
			xEventGroupClearBits(xGlobalEventGroup,STARTALGO);
			xEventGroupSetBits(xGlobalEventGroup,TASKRESUME); // Anforderung um Task zu fortsetzen
		}
		
		EventBits = xEventGroupGetBits(xGlobalEventGroup);
		if (EventBits & CHANGEALGO){
			xEventGroupClearBits(xGlobalEventGroup,CHANGEALGO);
			xEventGroupSetBits(xGlobalEventGroup, RESETALGO);
			xEventGroupSetBits(xGlobalEventGroup, TASKSTOPPED);
			xEventGroupSetBits(xGlobalEventGroup,STOPALGOBUTTON);
			vTaskSuspend(xHeartbeatTaskHandle);
			if (LeibinzRunning){
				vTaskSuspend(xLeibnizAlgo);
				vTaskResume(xTaskxSomayajiAlgo);
				SomayajiRunning = true;
				LeibinzRunning = false;
			}
			else{
				vTaskSuspend(xTaskxSomayajiAlgo);
				vTaskResume(xLeibnizAlgo);
				SomayajiRunning = false;
				LeibinzRunning = true;
			}
		}	
		vTaskDelayUntil(xLastWakeTime,500 / portTICK_RATE_MS);
		
	}
}


void vTaskxLeibnizAlgo(void *pvParameters) {
	float pi=1.0;
	float pitemp=1.0;
	uint32_t n=3;
	AnzahlDurchlauf =0;
	int i=0;
	int  EventBits;
	while (1){	
		EventBits=xEventGroupGetBits(xGlobalEventGroup);
		//Task Reset
		if (EventBits & RESETALGO){
			xEventGroupClearBits(xGlobalEventGroup,TASKDONE);
			pi=1.0;
			i=0;
			ucMsCounter=0;
			n=3;
			AnzahlDurchlauf =0;
			GlPi = 0.000;
			xEventGroupSetBits(xGlobalEventGroup, TASKSTOPPED);
			xEventGroupClearBits(xGlobalEventGroup,RESETALGO);	
		}
		//Task Stopped else do Calculations
		if ((EventBits & STOPALGO) ){
			xEventGroupSetBits(xGlobalEventGroup,TASKSTOPPED);
			xEventGroupWaitBits(xGlobalEventGroup,TASKRESUME,pdTRUE,pdFALSE,100000);
		}
		else{
			pi=pi-(float)(1.0/n);
			n+=2;
			pi=pi+(float)(1.0/n);
			n+=2;
			GlPi = pi*4;
			AnzahlDurchlauf++;	
		}

		if ((uint32_t)(GlPi*1000000.0)==3141599){	
			if (i==4800){ //empirisch ermittlet
				xEventGroupSetBits(xGlobalEventGroup,TASKDONE);
				xEventGroupSetBits(xGlobalEventGroup,STOPALGO);
				vTaskSuspend(xHeartbeatTaskHandle);	
			}
			else{
				i++;
			}
		}
	}
}



void vTaskxSomayajiAlgo(void *pvParameters) {

	float pi=3;
	int n=3;
	AnzahlDurchlauf =0;
	int  EventBits;
	
	for(;;){
		EventBits=xEventGroupGetBits(xGlobalEventGroup);
		
		if (EventBits & RESETALGO){
			xEventGroupClearBits(xGlobalEventGroup,TASKDONE);
			pi=3;
			n=3;
			AnzahlDurchlauf =0;
			ucMsCounter=0;
			GlPi = 0.00000;
			xEventGroupSetBits(xGlobalEventGroup, TASKSTOPPED);
			xEventGroupClearBits(xGlobalEventGroup,RESETALGO);
		}
		
		if ((EventBits & STOPALGO) ){
			xEventGroupSetBits(xGlobalEventGroup,TASKSTOPPED);
			xEventGroupWaitBits(xGlobalEventGroup,TASKRESUME,pdTRUE,pdFALSE,100000);
		}
		else{			
			pi=pi+(float)(4.0/(pow(n,3)-n));
			n=n+2;
			pi=pi-(float)(4.0/(pow(n,3)-n));
			n=n+2;
			GlPi=pi;
			AnzahlDurchlauf++;
			
			if ((uint32_t)(GlPi*100000)==(uint32_t)(M_PI*100000)){	//Hält an bei 3.14160
				xEventGroupSetBits(xGlobalEventGroup,TASKDONE);
				xEventGroupSetBits(xGlobalEventGroup,STOPALGO);
				vTaskSuspend(xHeartbeatTaskHandle);		
			}
		}
	};
}

