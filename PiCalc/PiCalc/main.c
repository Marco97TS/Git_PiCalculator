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

void vLedBlink(void *pvParameters);
void vTaskxEulerAlgo(void *pvParameters);
void vTaskxLeibnizAlgo(void *pvParameters);
void vButtonTask(void *pvParameters);
void vInitHeartbeatCounter(void);
void vUserInterface(void);
//void xSetTime(uint8_t ucDigitToSet, uint8_t ucButtonValue, uint8_t *ucSeconds, uint8_t *ucMinutes, uint8_t *ucHours);
void vHeartbeat(void);

TaskHandle_t xledTask;
TaskHandle_t xLeibnizAlgo;
TaskHandle_t xButtonTaskHandle;
TaskHandle_t xHeartbeatTaskHandle;
TaskHandle_t xUserInterfaceHandle;
TaskHandle_t xTaskxEulerAlgo;

EventGroupHandle_t xGlobalEventGroup;

uint8_t ucHeartbeatTimerCounter;
uint8_t ucClockSeconds = 0;
uint8_t ucClockMinutes = 0;



float GlPi = 0;
int AnzahlDurchlauf = 0;

typedef enum
{
	Leipniz,
	//SetClock,
	//SetAlarmClock
} eAlgoStates;
eAlgoStates eAlgoStateMachine = Leipniz;




void vApplicationIdleHook( void )
{	
	
}

int main(void)
{
    vInitClock();
    vInitDisplay();
    vInitHeartbeatCounter();
	xGlobalEventGroup = xEventGroupCreate();
	xTaskCreate(vTaskxEulerAlgo, (const char *) "TaskxEulerAlgo", configMINIMAL_STACK_SIZE+100, NULL, 1, &xTaskxEulerAlgo);
	xTaskCreate(vLedBlink, (const char *) "ledBlink", configMINIMAL_STACK_SIZE+10, NULL, 2, &xledTask);
	xTaskCreate(vTaskxLeibnizAlgo, (const char *) "LeibnizAlgo", configMINIMAL_STACK_SIZE+100, NULL, 1, &xLeibnizAlgo);
	xTaskCreate(vButtonTask, (const char *) "ButtonTask", configMINIMAL_STACK_SIZE, NULL, 2, &xButtonTaskHandle);
	xTaskCreate(vHeartbeat, (const char *) "Heartbeat", configMINIMAL_STACK_SIZE, NULL, 3, &xHeartbeatTaskHandle);
	xTaskCreate(vUserInterface, (const char *) "UserInterface", configMINIMAL_STACK_SIZE+100, NULL, 2, &xUserInterfaceHandle);

	
	vTaskStartScheduler();
	return 0;
}


void vLedBlink(void *pvParameters) {
	(void) pvParameters;
	PORTF.DIRSET = PIN0_bm; //LED1
	PORTF.OUT = 0x01;
	uint32_t ulBlinkStatus = 0;
	for(;;) {
		xTaskNotifyWait(0, 0xffffffff, &ulBlinkStatus, pdMS_TO_TICKS(5));
		if (ulBlinkStatus)
		{
			PORTF.OUTCLR = 0x01;
			vTaskDelay(100 / portTICK_RATE_MS);
			PORTF.OUTSET = 0x01;
			vTaskDelay(100 / portTICK_RATE_MS);
		}
	}
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
			xEventGroupSetBits(xGlobalEventGroup,STARTALGO);
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
			xEventGroupSetBits(xGlobalEventGroup,STOPALGOBUTTON);
		}
		if(getButtonPress(BUTTON3) == LONG_PRESSED) {
			xEventGroupSetBits(xGlobalEventGroup,RESETALGO);
			xEventGroupSetBits(xGlobalEventGroup,STARTALGO);
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
	TCC1.PER = 4999;                                    // TOP after 5000 counts -> Tp = 1/(500kHz) * 5000 = 10ms
	PMIC.CTRL |= 1 << PMIC_LOLVLEN_bp;                  // enable low-level interrupt
}
/*———————————————————–*/



/*———————————————————–*/
uint8_t ucMsCounter = 0;
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
           /* if (ucMsCounter >= 100)
            {
                ucMsCounter = 0;
                ++ucClockSeconds;
                if (ucClockSeconds >= 60)
                {
                    ucClockSeconds = 0;
                    ++ucClockMinutes;
                    if (ucClockMinutes >= 60)
                    {
                        ucClockMinutes = 0;
                    }
                }
            }
            */
            ucMsInterruptCounter = 0;
        }
        
        
        xInterruptOccurred = xTaskNotifyWait(0, 0xffffffff, &ucMsInterruptCounter, pdMS_TO_TICKS(50));
    }
}
/*———————————————————–*/

ISR(TCC1_OVF_vect)
{
    /* Occurs all 10ms */
    
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    
    xTaskNotifyFromISR(xHeartbeatTaskHandle, ucHeartbeatTimerCounter, eIncrement, &xHigherPriorityTaskWoken);
}
/*———————————————————–*/


void vUserInterface(void)
{
	BaseType_t xButtonTaskNotification = pdFALSE;	
	uint32_t ulInterfaceStatus = 0;	
	uint32_t EventBits = 0;	
	char PiString[11];
	bool LeibinzRunning = true;
	bool EulerRunning = false;
	
	
	vTaskSuspend(xTaskxEulerAlgo);
	
	while (1)
	{
		
		//xEventGroupClearBits(EventBit_4);
		//xEventGroupClearBits(xGlobalEventGroup,EventBit_1);
		
		
		EventBits = xEventGroupGetBits(xGlobalEventGroup);

		if ((EventBits & CHANGEALGO) && LeibinzRunning){
			vTaskSuspend(xLeibnizAlgo);
			vTaskResume(xTaskxEulerAlgo);
			EulerRunning = true;
			LeibinzRunning = false;
			xEventGroupClearBits(xGlobalEventGroup,CHANGEALGO);
			xEventGroupSetBits(xGlobalEventGroup, RESETALGO);
			//xEventGroupSetBits()
		}
		
		else if((EventBits & CHANGEALGO) && EulerRunning){
			vTaskSuspend(xTaskxEulerAlgo);
			vTaskResume(xLeibnizAlgo);
			EulerRunning = false;
			LeibinzRunning = true;
			xEventGroupClearBits(xGlobalEventGroup,CHANGEALGO);
			xEventGroupSetBits(xGlobalEventGroup, RESETALGO);
		}
		
		
		xEventGroupSetBits(xGlobalEventGroup,STOPALGO); // Anforderung um Task zu Beenden
		xEventGroupWaitBits(xGlobalEventGroup,TASKSTOPPED,pdFALSE,pdFALSE,1000000);
		
		EventBits = xEventGroupGetBits(xGlobalEventGroup);
		vDisplayClear();
		vDisplayWriteStringAtPos(0,0,"Pi Calculator");
		//sprintf(PiString, "%05f" ,GlPi);
		sprintf(PiString, "%02f" ,GlPi);
		vDisplayWriteStringAtPos(1,0,"PI:  %s", PiString); // + TaskName
		sprintf(PiString, "%d" ,AnzahlDurchlauf);
		vDisplayWriteStringAtPos(2,0,"Steps: %d", PiString); // Format
		printf(PiString, "%d" ,ucMsCounter);
		vDisplayWriteStringAtPos(3,0,"mSec: %d", PiString); //Format + korrekte Zeit
		 
		if ((GlPi > 3.141589) && (GlPi <= 3.141599)){  // Zu spät
			xEventGroupSetBits(xGlobalEventGroup,TASKDONE);
		}
		
		EventBits = xEventGroupGetBits(xGlobalEventGroup);
		
		if (!(EventBits & STOPALGOBUTTON) && (!(EventBits & TASKDONE ))){
			xEventGroupClearBits(xGlobalEventGroup,STOPALGO);
			xEventGroupClearBits(xGlobalEventGroup,STARTALGO);
			xEventGroupClearBits(xGlobalEventGroup,TASKSTOPPED);
			xEventGroupSetBits(xGlobalEventGroup,TASKRESUME); // Anforderung um Task zu fortsetzen
		}

		if ((EventBits & STARTALGO)){
			xEventGroupClearBits(xGlobalEventGroup,STOPALGOBUTTON);
			xEventGroupClearBits(xGlobalEventGroup,STARTALGO);
			xEventGroupSetBits(xGlobalEventGroup,TASKRESUME); // Anforderung um Task zu fortsetzen
		}
		vTaskDelay(200 / portTICK_RATE_MS);
		
	}
}


void vTaskxLeibnizAlgo(void *pvParameters) {
	float pi=1.0;
	uint32_t n=3;
	AnzahlDurchlauf =0;
	int  EventBits;
	while (1){	
		EventBits=xEventGroupGetBits(xGlobalEventGroup);
		if (EventBits & RESETALGO){
			pi=1.0;
			n=3;
			AnzahlDurchlauf =0;
			xEventGroupClearBits(xGlobalEventGroup,TASKDONE);
			xEventGroupClearBits(xGlobalEventGroup,RESETALGO);
		}
		
		if ((EventBits & STOPALGO) ){//|| (EventBits & STOPALGOBUTTON)){
			xEventGroupSetBits(xGlobalEventGroup,TASKSTOPPED);
			xEventGroupWaitBits(xGlobalEventGroup,TASKRESUME,pdTRUE,pdFALSE,100000);
		}
		else{
			pi=pi-(float)(1.0/n);
			n+=2;
			pi=pi+(float)(1.0/n);
			n+=2;
			GlPi = (float)(pi*4);
			AnzahlDurchlauf++;	
		}
	}
}



void vTaskxEulerAlgo(void *pvParameters) {

	float piSqr = 1;
	float pi=1;
	int n=2;
	AnzahlDurchlauf =0;
	int  EventBits;
	
	for(;;){
		EventBits=xEventGroupGetBits(xGlobalEventGroup);
		
		if (EventBits & RESETALGO){
			float piSqr = 1;
			int n=2;
			AnzahlDurchlauf =0;
			xEventGroupClearBits(xGlobalEventGroup,TASKDONE);
			xEventGroupClearBits(xGlobalEventGroup,RESETALGO);
		}
		
		if ((EventBits & STOPALGO) ){//|| (EventBits & STOPALGOBUTTON)){
			xEventGroupSetBits(xGlobalEventGroup,TASKSTOPPED);
			xEventGroupWaitBits(xGlobalEventGroup,TASKRESUME,pdTRUE,pdFALSE,100000);
		}
		else{
			piSqr=piSqr+(float)(1.0/pow(n,2));
			n++;
			piSqr=piSqr+(float)(1.0/pow(n,2));
			n++;
			piSqr=piSqr+(float)(1.0/pow(n,2));
			n++; 
			pi = sqrt(piSqr*6);
			GlPi = (float)(pi);
			AnzahlDurchlauf++;	
		}
	};
}

