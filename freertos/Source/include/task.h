#ifndef TASK_H
#define TASK_H

#include "list.h"


#define portYIELD()											\
{															\
	/* 触发 PendSV,产生上下文切换 */									\
	portNVIC_INT_CTRL_REG = portNVIC_PENDSVSET_BIT; 		\
	__dsb( portSY_FULL_READ_WRITE );						\
	__isb( portSY_FULL_READ_WRITE );						\
}

#define taskYIELD()			portYIELD()


typedef void * TaskHandle_t;


#if( configSUPPORT_STATIC_ALLOCATION == 1 )
TaskHandle_t xTaskCreateStatic(	TaskFunction_t pxTaskCode,
					            const char * const pcName,
					            const uint32_t ulStackDepth,
					            void * const pvParameters,
					            StackType_t * const puxStackBuffer,
					            TCB_t * const pxTaskBuffer );
#endif /* configSUPPORT_STATIC_ALLOCATION */
                                
void prvInitialiseTaskLists( void );                                
void vTaskStartScheduler( void );
void vTaskSwitchContext( void );


                                
#endif /* TASK_H */
								