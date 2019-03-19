/*
 * Copyright 2013, 2017, Jernej Kovacic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software. If you wish to use our Amazon
 * FreeRTOS name, please do so in a fair use way that does not cause confusion.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */


/**
 * @file
 * A simple demo application.
 *
 * @author Jernej Kovacic
 */


#include <stddef.h>

#include <FreeRTOS.h>
#include <queue.h>
#include <task.h>

#include "app_config.h"
#include "print.h"
#include "receive.h"


/*
 * This diagnostic pragma will suppress the -Wmain warning,
 * raised when main() does not return an int
 * (which is perfectly OK in bare metal programming!).
 *
 * More details about the GCC diagnostic pragmas:
 * https://gcc.gnu.org/onlinedocs/gcc/Diagnostic-Pragmas.html
 */
#pragma GCC diagnostic ignored "-Wmain"


/* Struct with settings for each task */
typedef struct _paramStruct
{
  portCHAR* text;                  /* text to be printed by the task */
  UBaseType_t  delay;              /* delay in milliseconds */
  QueueHandle_t taskQueue;
} paramStruct;

/* Default parameters if no parameter struct is available */
static const portCHAR defaultText[] = "<NO TEXT>\r\n";
static const UBaseType_t defaultDelay = 1000;

/* Parameters for two tasks */
#define TASK_LIST_SIZE 3
static paramStruct tParam[TASK_LIST_SIZE] =
{
    (paramStruct) { .text="Task0", .delay=2000 },
    (paramStruct) { .text="Task1", .delay=3000 },
    (paramStruct) { .text="Task2", .delay=3000 }
};

QueueHandle_t taskSwitcherQueue;
QueueHandle_t activeTaskQueue;

void vTaskSwitcher( void *pvParameters ) {
  (void)pvParameters;
  taskSwitcherQueue = xQueueCreate(PRINT_QUEUE_SIZE, sizeof(portCHAR) * 2);
  if ( 0 == taskSwitcherQueue ) {
    vPrintMsg("create task switch queue failed");
  }

  portCHAR message[2];
  vPrintMsg("task switcher started\r\n");

  while(1) {
    xQueueReceive(taskSwitcherQueue, (void*) message, portMAX_DELAY);

    switch(message[0]) {
    case MSG_TASK_SWITCH:
      if (message[1] < TASK_LIST_SIZE) {
        vPrintMsg("task switch: ");
        vPrintChar(message[1] + 0x30);
        int task_num = message[1];
        activeTaskQueue = tParam[task_num].taskQueue;
        vPrintMsg("\r\n");
      }
      break;

    case MSG_SHOW_TASK_LIST:
      {
        int i = 0;
        for(; i < TASK_LIST_SIZE; i++) {
          vPrintChar(i + 0x30);
          vPrintMsg(" - ");
          vPrintMsg(tParam[i].text);
          vPrintMsg("\r\n");
        }
        vPrintMsg("\r\n");
        activeTaskQueue = taskSwitcherQueue;
      }
      break;
    default:
      vPrintMsg("task switcher: unknown msg\n");
      break;
    }
  }

}

/* Task function - may be instantiated in multiple tasks */
void vTaskFunction( void *pvParameters )
{
    const portCHAR* taskName;
    //UBaseType_t  delay;
    paramStruct* params = (paramStruct*) pvParameters;
    portCHAR message[2];

    taskName = ( NULL==params || NULL==params->text ? defaultText : params->text );
    //delay = ( NULL==params ? defaultDelay : params->delay);

    volatile portCHAR running = 1;

    activeTaskQueue = xQueueCreate(PRINT_QUEUE_SIZE, sizeof(portCHAR) * 2);
    params->taskQueue = activeTaskQueue;

    if ( 0 == activeTaskQueue ) {
      vPrintMsg("create task queue failed");
      running = 0;
    }

    while( running )
    {
        /* Print out the name of this task. */


        xQueueReceive(activeTaskQueue, (void*) message, portMAX_DELAY);
        switch(message[0]) {
        case MSG_QUIT:
          vPrintMsg(taskName);
          vPrintMsg(" :quit task\r\n");
          break;

        default:
          vPrintMsg(taskName);
          vPrintMsg(" :unknown message\r\n");
          break;
        }

        //vTaskDelay( delay / portTICK_RATE_MS );
    }

    /*
     * If the task implementation ever manages to break out of the
     * infinite loop above, it must be deleted before reaching the
     * end of the function!
     */
    vTaskDelete(NULL);
}


/*
 * A convenience function that is called when a FreeRTOS API call fails
 * and a program cannot continue. It prints a message (if provided) and
 * ends in an infinite loop.
 */
static void FreeRTOS_Error(const portCHAR* msg)
{
    if ( NULL != msg )
      {
        vPrintMsg("freeRTOS error\n");
        vDirectPrintMsg(msg);
    }

    for ( ; ; );
}

/* Startup function that creates and runs two FreeRTOS tasks */
void main(void)
{
    /* Init of print related tasks: */
    if ( pdFAIL == printInit(PRINT_UART_NR) )
    {
        FreeRTOS_Error("Initialization of print failed\r\n");
    }

    /*
     * I M P O R T A N T :
     * Make sure (in startup.s) that main is entered in Supervisor mode.
     * When vTaskStartScheduler launches the first task, it will switch
     * to System mode and enable interrupt exceptions.
     */
    vDirectPrintMsg("= = = T E S T   S T A R T E D = = =\r\n\r\n");

    /* Init of receiver related tasks: */
    if ( pdFAIL == recvInit(RECV_UART_NR) )
    {
        FreeRTOS_Error("Initialization of receiver failed\r\n");
    }

    /* Create a print gate keeper task: */
    if ( pdPASS != xTaskCreate(printGateKeeperTask, "gk", 128, NULL,
                               PRIOR_PRINT_GATEKEEPR, NULL) )
    {
        FreeRTOS_Error("Could not create a print gate keeper task\r\n");
    }

    if ( pdPASS != xTaskCreate(recvTask, "recv", 128, NULL, PRIOR_RECEIVER, NULL) )
    {
        FreeRTOS_Error("Could not create a receiver task\r\n");
    }

    /* And finally create two tasks: */
    if ( pdPASS != xTaskCreate(vTaskFunction, "task0", 128, (void*) &tParam[0],
                               PRIOR_PERIODIC, NULL) )
    {
        FreeRTOS_Error("Could not create task1\r\n");
    }

    if ( pdPASS != xTaskCreate(vTaskFunction, "task1", 128, (void*) &tParam[1],
                               PRIOR_PERIODIC, NULL) )
    {
        FreeRTOS_Error("Could not create task1\r\n");
    }

    if ( pdPASS != xTaskCreate(vTaskFunction, "task2", 128, (void*) &tParam[2],
                               PRIOR_PERIODIC, NULL) )
    {
        FreeRTOS_Error("Could not create task1\r\n");
    }

    if ( pdPASS != xTaskCreate(vTaskSwitcher, "taskSwitcher", 128, (void*) NULL,
                               PRIOR_PERIODIC, NULL) )
    {
        FreeRTOS_Error("Could not create task1\r\n");
    }
/*
    if ( pdPASS != xTaskCreate(vPeriodicTaskFunction, "task2", 128, (void*) &tParam[1],
                               PRIOR_FIX_FREQ_PERIODIC, NULL) )
    {
        FreeRTOS_Error("Could not create task2\r\n");
    }*/

    vDirectPrintMsg("A text may be entered using a keyboard.\r\n");
    vDirectPrintMsg("It will be displayed when 'Enter' is pressed.\r\n\r\n");

    /* Start the FreeRTOS scheduler */
    vTaskStartScheduler();

    /*
     * If all goes well, vTaskStartScheduler should never return.
     * If it does return, typically not enough heap memory is reserved.
     */

    FreeRTOS_Error("Could not start the scheduler!!!\r\n");

    /* just in case if an infinite loop is somehow omitted in FreeRTOS_Error */
    for ( ; ; );
}
