/*
 * FreeRTOS V202107.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * http://www.FreeRTOS.org
 * http://aws.amazon.com/freertos
 *
 * 1 tab == 4 spaces!
 */


#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H

/*-----------------------------------------------------------
 * Application specific definitions.
 *
 * These definitions should be adjusted for your particular hardware and
 * application requirements.
 *
 * THESE PARAMETERS ARE DESCRIBED WITHIN THE 'CONFIGURATION' SECTION OF THE
 * FreeRTOS API DOCUMENTATION AVAILABLE ON THE FreeRTOS.org WEB SITE.
 *
 * See http://www.freertos.org/a00110.html
 *----------------------------------------------------------*/

#include <stdint.h>
#include "build_config.h"
#include "cstone/platform.h"

#if defined PLATFORM_EMBEDDED && !defined __SYSTEM_STM32F4XX_H && !defined __SYSTEM_STM32F10X_H
extern uint32_t SystemCoreClock;
#endif

// Section for heap defined in cstone rtos.c
#ifdef BOARD_STM32F429I_DISC1
#  define FREERTOS_HEAP_SECTION   ".ccmram"
#else
#  define FREERTOS_HEAP_SECTION   ".noinit"
#endif

#ifndef KB
#  define KB  * 1024
#endif

#define configUSE_PREEMPTION            1
#define configUSE_PORT_OPTIMISED_TASK_SELECTION   0
#define configUSE_TICKLESS_IDLE         0
#define configUSE_IDLE_HOOK             1
#define configUSE_TICK_HOOK             0
#ifdef PLATFORM_EMBEDDED
#  define configCPU_CLOCK_HZ              ( SystemCoreClock )
#endif
#define PERF_CLOCK_HZ                   10000ul  // Generated timer clock (10kHz / 100us)
#define configTICK_RATE_HZ              ( ( TickType_t ) 1000 )
#define configMAX_PRIORITIES            ( 5 )
#define configMINIMAL_STACK_SIZE        ( (unsigned short) 400 / sizeof(StackType_t) )
#if defined BOARD_STM32F429I_DISC1 || defined PLATFORM_HOSTED
#  define configTOTAL_HEAP_SIZE           ( ( size_t ) ( 64 KB ) )
#elif defined BOARD_STM32F401_BLACK_PILL
#  define configTOTAL_HEAP_SIZE           ( ( size_t ) ( 20 KB ) )
#else
#  define configTOTAL_HEAP_SIZE           ( ( size_t ) ( 7 KB ) )
#endif
#define configMAX_TASK_NAME_LEN         ( 10 )
#define configUSE_TRACE_FACILITY        1
#define configUSE_16_BIT_TICKS          0
#define configIDLE_SHOULD_YIELD         1
#define configUSE_TASK_NOTIFICATIONS    1
#define configTASK_NOTIFICATION_ARRAY_ENTRIES     1
#define configUSE_MUTEXES               1
#define configQUEUE_REGISTRY_SIZE       6
#define configCHECK_FOR_STACK_OVERFLOW  2
#define configUSE_RECURSIVE_MUTEXES     1
#define configUSE_MALLOC_FAILED_HOOK    1
#define configUSE_APPLICATION_TASK_TAG  0
#define configUSE_COUNTING_SEMAPHORES   1
#define configGENERATE_RUN_TIME_STATS   1
#define configUSE_QUEUE_SETS            0
#define configUSE_TIME_SLICING          1
#define configUSE_NEWLIB_REENTRANT      0
#define configNUM_THREAD_LOCAL_STORAGE_POINTERS   1
#define configSUPPORT_STATIC_ALLOCATION           1   // Needed for tinyusb
#define configSUPPORT_DYNAMIC_ALLOCATION          1
#define configAPPLICATION_ALLOCATED_HEAP          1   // Needed to relocate in .ccmram section
#define configSTACK_ALLOCATION_FROM_SEPARATE_HEAP 0
#define configENABLE_BACKWARD_COMPATIBILITY       1 // FIXME: Needed for FR+pthreads
#define configRECORD_STACK_HIGH_ADDRESS           0

#ifdef PLATFORM_EMBEDDED
#  ifndef RTOS_H
extern void perf_timer_init(void);
extern uint32_t perf_timer_count(void);
#    define PERF_TIMER_DECL  // Avoid duplicate declaration with rtos.h
#  endif

#  define portCONFIGURE_TIMER_FOR_RUN_TIME_STATS()  perf_timer_init()
#  define portGET_RUN_TIME_COUNTER_VALUE()          perf_timer_count()
#endif // PLATFORM_EMBEDDED

/* Co-routine definitions. */
#define configUSE_CO_ROUTINES           0
#define configMAX_CO_ROUTINE_PRIORITIES ( 2 )

/* Software timer definitions. */
#define configUSE_TIMERS                1
#define configUSE_DAEMON_TASK_STARTUP_HOOK 1
#define configTIMER_TASK_PRIORITY       ( 2 )
#define configTIMER_QUEUE_LENGTH        4
#define configTIMER_TASK_STACK_DEPTH    ( configMINIMAL_STACK_SIZE * 1 )

/* Set the following definitions to 1 to include the API function, or zero
to exclude the API function. */
#define INCLUDE_vTaskPrioritySet        1
#define INCLUDE_uxTaskPriorityGet       1
#define INCLUDE_vTaskDelete             1
#define INCLUDE_vTaskCleanUpResources   1
#define INCLUDE_vTaskSuspend            1
#define INCLUDE_vTaskDelayUntil         1
#define INCLUDE_vTaskDelay              1

#define INCLUDE_xTaskGetIdleTaskHandle  1

#ifdef PLATFORM_EMBEDDED

/* Cortex-M specific definitions. */
#ifdef __NVIC_PRIO_BITS
    /* __BVIC_PRIO_BITS will be specified when CMSIS is being used. */
    #define configPRIO_BITS             __NVIC_PRIO_BITS
#else
    #define configPRIO_BITS             4        /* 15 priority levels */
#endif

/* The lowest interrupt priority that can be used in a call to a "set priority"
function. */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         0xf

/* The highest interrupt priority that can be used by any interrupt service
routine that makes calls to interrupt safe FreeRTOS API functions.  DO NOT CALL
INTERRUPT SAFE FREERTOS API FUNCTIONS FROM ANY INTERRUPT THAT HAS A HIGHER
PRIORITY THAN THIS! (higher priorities are lower numeric values. */
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY    5

/* Interrupt priorities used by the kernel port layer itself.  These are generic
to all Cortex-M ports, and do not rely on any particular library functions. */
#define configKERNEL_INTERRUPT_PRIORITY         ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
/* !!!! configMAX_SYSCALL_INTERRUPT_PRIORITY must not be set to zero !!!!
See http://www.FreeRTOS.org/RTOS-Cortex-M3-M4.html. */
#define configMAX_SYSCALL_INTERRUPT_PRIORITY     ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* Normal assert() semantics without relying on the provision of an assert.h
header file. */
//#define configASSERT( x )   if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ); }

// https://mcuoneclipse.com/2021/01/23/assert-__file__-path-and-other-cool-gnu-gcc-tricks-to-be-aware-of/
#ifndef __FILE_NAME__ // Clang and GCC 12 extension for short basenames
#  define __FILE_NAME__  (__builtin_strrchr("/" __FILE__, '/') + 1)
#endif
extern void assert_failed(uint8_t *file, uint32_t line);
#define configASSERT( x )   if( ( x ) == 0 ) { assert_failed((uint8_t *)__FILE_NAME__, __LINE__); }

/* Definitions that map the FreeRTOS port interrupt handlers to their CMSIS
standard names. */
#define vPortSVCHandler     SVC_Handler
#define xPortPendSVHandler  PendSV_Handler
// We want our own SysTick handler so that HAL_Delay() can be used in init code
//#define xPortSysTickHandler SysTick_Handler

#endif // PLATFORM_EMBEDDED

#endif /* FREERTOS_CONFIG_H */

