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


extern void vApplicationIdleHook( void );

void vLedBlink(void *pvParameters);
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

uint8_t ucHeartbeatTimerCounter;
uint8_t ucClockSeconds = 0;
uint8_t ucClockMinutes = 0;



float GlPi = 0;

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
	
	xTaskCreate(vLedBlink, (const char *) "ledBlink", configMINIMAL_STACK_SIZE+10, NULL, 2, &xledTask);
	xTaskCreate(vTaskxLeibnizAlgo, (const char *) "LeibnizAlgo", configMINIMAL_STACK_SIZE+100, NULL, 1, &xLeibnizAlgo);
	xTaskCreate(vButtonTask, (const char *) "ButtonTask", configMINIMAL_STACK_SIZE, NULL, 2, &xButtonTaskHandle);
	xTaskCreate(vHeartbeat, (const char *) "Heartbeat", configMINIMAL_STACK_SIZE, NULL, 3, &xHeartbeatTaskHandle);
	xTaskCreate(vUserInterface, (const char *) "UserInterface", configMINIMAL_STACK_SIZE, NULL, 2, &xUserInterfaceHandle);

	
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
	vTaskDelay(3000);
	for(;;) {
		updateButtons();
		
		if(getButtonPress(BUTTON1) == SHORT_PRESSED) {
			xTaskNotify(xUserInterfaceHandle, BUTTON1SHORTPRESSEDMASK, eSetBits);
		}
		if(getButtonPress(BUTTON2) == SHORT_PRESSED) {
			xTaskNotify(xUserInterfaceHandle, BUTTON2SHORTPRESSEDMASK, eSetBits);
		}
		if(getButtonPress(BUTTON3) == SHORT_PRESSED) {
			xTaskNotify(xUserInterfaceHandle, BUTTON3SHORTPRESSEDMASK, eSetBits);
		}
		if(getButtonPress(BUTTON4) == SHORT_PRESSED) {
			xTaskNotify(xUserInterfaceHandle, BUTTON4SHORTPRESSEDMASK, eSetBits);
		}
		if(getButtonPress(BUTTON1) == LONG_PRESSED) {
			xTaskNotify(xUserInterfaceHandle, BUTTON1LONGPRESSEDMASK, eSetBits);
		}
		if(getButtonPress(BUTTON2) == LONG_PRESSED) {
			xTaskNotify(xUserInterfaceHandle, BUTTON2LONGPRESSEDMASK, eSetBits);
		}
		if(getButtonPress(BUTTON3) == LONG_PRESSED) {
			xTaskNotify(xUserInterfaceHandle, BUTTON3LONGPRESSEDMASK, eSetBits);
		}
		if(getButtonPress(BUTTON4) == LONG_PRESSED) {
			xTaskNotify(xUserInterfaceHandle, BUTTON4LONGPRESSEDMASK, eSetBits);
		}
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


void vUserInterface(void)
{
	BaseType_t xButtonTaskNotification = pdFALSE;	
	uint32_t ulInterfaceStatus = 0;	
	while (1)
	{
		xTaskNotifyWait(0, 0xffffffff, &ulInterfaceStatus, pdMS_TO_TICKS(5));
		char PiString[11];
		
		//Statemachine Vorbereitung für zusätzliche Algo
		/*
		switch (eAlgoStateMachine)
		{
			case Leipniz:{
			
				break;
			}
		}
		*/
		
		vDisplayClear();
		vDisplayWriteStringAtPos(0,0,"Pi Calculator");
		//sprintf(PiString, "%05f" ,GlPi);
		sprintf(PiString, "%02f" ,GlPi);
		vDisplayWriteStringAtPos(1,0,"  PI:  %s", PiString);

		vTaskDelay(200 / portTICK_RATE_MS);
	}
}
/*———————————————————–*/

void vHeartbeat(void)
{
    uint8_t ucMsCounter = 0;
    uint8_t ucMsInterruptCounter = 0;
    BaseType_t xInterruptOccurred = pdFALSE;
    
    while (1)
    {
        
        if (xInterruptOccurred == pdTRUE)               // process time counting only when interrupt occurred
        {
            xInterruptOccurred = pdFALSE;
            
            ucMsCounter += ucMsInterruptCounter;
            if (ucMsCounter >= 100)
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




void vTaskxLeibnizAlgo(void *pvParameters) {
	float pi=1.0;
	float pifull;
	int n=3;
	
	while (1){
		pi=pi-(1/n);
		n+=2;
		pi=pi+(1/n);
		GlPi = pi*4;
		xTaskNotify(xUserInterfaceHandle, 0x01, pdTRUE);
		vTaskDelay(200 / portTICK_RATE_MS);
	}
}





