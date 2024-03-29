
#include "FreeRTOS.h"
#include "task.h"
#include "FreeRTOSConfig.h"
List_t pxReadyTasksLists[ configMAX_PRIORITIES ];
TCB_t volatile *pxCurrentTCB = NULL;

static volatile UBaseType_t uxCurrentNumberOfTasks 	= ( UBaseType_t ) 0U;
static volatile TickType_t xTickCount 				= ( TickType_t ) 0U;
static volatile UBaseType_t uxTopReadyPriority 		= tskIDLE_PRIORITY;

static List_t xDelayedTaskList1;
static List_t xDelayedTaskList2;
static List_t* volatile pxDelayedTaskList;
static List_t* volatile pxOverflowDelayedTaskList;

static volatile TickType_t xNextTaskUnblockTime		= ( TickType_t ) 0U;//下次唤醒时间
static volatile BaseType_t xNumOfOverflows 			= ( BaseType_t ) 0;//溢出时间计数器的任务数

extern TCB_t Task1TCB;
extern TCB_t Task2TCB;

static void prvResetNextTaskUnblockTime( void );


/* 将任务添加到就绪列表 */                                    
#define prvAddTaskToReadyList( pxTCB )																   \
	taskRECORD_READY_PRIORITY( ( pxTCB )->uxPriority );												   \
	vListInsertEnd( &( pxReadyTasksLists[ ( pxTCB )->uxPriority ] ),									\
	&( ( pxTCB )->xStateListItem ) ); 																	\


/* 查找最高优先级的就绪任务：通用方法 */                                    
#if ( configUSE_PORT_OPTIMISED_TASK_SELECTION == 0 )
	/* uxTopReadyPriority 存的是就绪任务的最高优先级 */
	#define taskRECORD_READY_PRIORITY( uxPriority )														\
	{																									\
		if( ( uxPriority ) > uxTopReadyPriority )														\
		{																								\
			uxTopReadyPriority = ( uxPriority );														\
		}																								\
	} /* taskRECORD_READY_PRIORITY */

	/*-----------------------------------------------------------*/

	#define taskSELECT_HIGHEST_PRIORITY_TASK()																\
	{																										\
		UBaseType_t uxTopPriority = uxTopReadyPriority;														\
																											\
			/* 寻找包含就绪任务的最高优先级的队列 */                                         				                \
			while( listLIST_IS_EMPTY( &( pxReadyTasksLists[ uxTopPriority ] ) ) )							\
			{																								\
				--uxTopPriority;																			\
			}																								\
																											\
			/* 获取优先级最高的就绪任务的TCB，然后更新到pxCurrentTCB */							            				\
			listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB, &( pxReadyTasksLists[ uxTopPriority ] ) );			\
			/* 更新uxTopReadyPriority */                                                                 	   \
			uxTopReadyPriority = uxTopPriority;																\
	} /* taskSELECT_HIGHEST_PRIORITY_TASK */

	/*-----------------------------------------------------------*/

	/* 这两个宏定义只有在选择优化方法时才用，这里定义为空 */
	#define taskRESET_READY_PRIORITY( uxPriority )
	#define portRESET_READY_PRIORITY( uxPriority, uxTopReadyPriority )
    
/* 查找最高优先级的就绪任务：根据处理器架构优化后的方法 */
#else /* configUSE_PORT_OPTIMISED_TASK_SELECTION */

	#define taskRECORD_READY_PRIORITY( uxPriority )	portRECORD_READY_PRIORITY( uxPriority, uxTopReadyPriority )

	/*-----------------------------------------------------------*/

	#define taskSELECT_HIGHEST_PRIORITY_TASK()														    \
	{																								    \
	UBaseType_t uxTopPriority;																		    \
																									    \
		/* 寻找最高优先级 */								                            \
		portGET_HIGHEST_PRIORITY( uxTopPriority, uxTopReadyPriority );								    \
		/* 获取优先级最高的就绪任务的TCB，然后更新到pxCurrentTCB */                                       \
		listGET_OWNER_OF_NEXT_ENTRY( pxCurrentTCB, &( pxReadyTasksLists[ uxTopPriority ] ) );		    \
	} /* taskSELECT_HIGHEST_PRIORITY_TASK() */

	/*-----------------------------------------------------------*/
#if 1
	#define taskRESET_READY_PRIORITY( uxPriority )														\
	{																									\
		if( listCURRENT_LIST_LENGTH( &( pxReadyTasksLists[ ( uxPriority ) ] ) ) == ( UBaseType_t ) 0 )	\
		{																								\
			portRESET_READY_PRIORITY( ( uxPriority ), ( uxTopReadyPriority ) );							\
		}																								\
	}
#else
    #define taskRESET_READY_PRIORITY( uxPriority )											            \
    {																							        \
            portRESET_READY_PRIORITY( ( uxPriority ), ( uxTopReadyPriority ) );					        \
    }
#endif
    
#endif /* configUSE_PORT_OPTIMISED_TASK_SELECTION */
                                   
#define taskSWITCH_DELAYED_LISTS()\
{\
	/* 交换延时链表和延时溢出链表 */\
	List_t *pxtemp;\
	pxtemp = pxDelayedTaskList;\
	pxDelayedTaskList = pxOverflowDelayedTaskList;\
	pxOverflowDelayedTaskList = pxtemp;\
	xNumOfOverflows++;\
	prvResetNextTaskUnblockTime();\
}

static void prvAddNewTaskToReadyList( TCB_t *pxNewTCB )
{
	/* 进入临界区 */
	taskENTER_CRITICAL();
	uxCurrentNumberOfTasks++;
	/* 如果当前任务为空，那么需要判断是否需要初始化任务链表，并且还需要指定当前TCB */
	if( pxCurrentTCB == NULL )
	{
		pxCurrentTCB = pxNewTCB;

		if( uxCurrentNumberOfTasks == (UBaseType_t)1 )
		{
			prvInitialiseTaskLists();
		}
	}
	/* 不为空的话，需要判断一下优先级，然后根据优先级判定的结果设置当前TCB为最高优先级的TCB */
	else
	{
		if( pxCurrentTCB->uxPriority <= pxNewTCB->uxPriority )
		{
			pxCurrentTCB = pxNewTCB;
		}
	}

	prvAddTaskToReadyList( pxNewTCB ); 
	
	taskEXIT_CRITICAL();
}


static void prvAddCurrentTaskToDelayedList(TickType_t xTickToWait)
{
	TickType_t xTimeToWake;

	/* 获取系统时基的值 */
	const TickType_t xConstTickCount = xTickCount;
	/* 将任务从就绪链表中移除 */
	if(uxListRemove((ListItem_t *)&(pxCurrentTCB->xStateListItem)) == (UBaseType_t)0)
	{
		/* 清除优先级位图中的对应位 */
		portRESET_READY_PRIORITY(pxCurrentTCB->uxPriority, 	uxTopReadyPriority);
	}
	/* 任务延时结束后，系统的xTickToWait的值 */
	xTimeToWake = xTickToWait + xConstTickCount;
	/* 将任务延时的值设置为节点的排序值 */
	listSET_LIST_ITEM_VALUE(&(pxCurrentTCB->xStateListItem),xTimeToWake);
	/* 如果延时溢出，就插入到溢出链表 */
	if(xConstTickCount > xTimeToWake)
	{
		vListInsert(pxOverflowDelayedTaskList, (ListItem_t *)&(pxCurrentTCB->xStateListItem));
	}
	else
	{
		/* 否则就插入到阻塞链表 */
		vListInsert(pxDelayedTaskList,(ListItem_t *)&(pxCurrentTCB->xStateListItem));
		/* 更新下一个任务解锁时刻xNextTaskUnblockTime的值 */
		if(xTimeToWake < xNextTaskUnblockTime)
		{
			xNextTaskUnblockTime = xTimeToWake;
		}
	}
		
}


static void prvInitialiseNewTask(TaskFunction_t pxTaskCode,
										  const char * const pcName,
										  const uint32_t ulStackDepth,
										  void * const pvParameters,
										  UBaseType_t uxPriority,
										  TaskHandle_t * const pxCreatedTask, 
										  TCB_t *pxNewTCB )
{
	StackType_t *pxTopOfStack;
	UBaseType_t x;

	/* 获取栈顶地址 */
	pxTopOfStack = pxNewTCB->pxStack + (ulStackDepth - (uint32_t)1);
	
	/* 栈顶地址向高地址做 8 字节对齐,32位机只需要四字节对齐即可，但是需要考虑兼容浮点运算的64位操作 */
 	pxTopOfStack = (StackType_t*)(((uint32_t)pxTopOfStack)&(~(uint32_t)0x0007));

	/* 将任务的名字存储在 TCB 中 */
	for( x = 0; x < (UBaseType_t)configMAX_TASK_NAME_LEN && pcName[x] == 0; x++ )
	//条件判定可以么？没问题！但是pcName[x] == '\0'则不可以，复习0与'\0'的区别
	{
		pxNewTCB->pcTaskName[x] = pcName[x];
	}

	/* 任务名字的长度不能超过 configMAX_TASK_NAME_LEN */
	pxNewTCB->pcTaskName[configMAX_TASK_NAME_LEN - 1] = '\0';

	/* 初始化 TCB 中的 xStateListItem 节点 */
	vListInitialiseItem(&(pxNewTCB->xStateListItem));

 	/* 设置 xStateListItem 节点的拥有者 */
	listSET_LIST_ITEM_OWNER(&(pxNewTCB->xStateListItem),pxNewTCB);
	
	/* 初始化任务栈 */
	pxNewTCB->pxTopOfStack = (StackType_t*)pxPortInitialiseStack( pxTopOfStack, pxTaskCode, pvParameters ); 

	if(uxPriority >= (UBaseType_t)configMAX_PRIORITIES)
	{
		uxPriority = (UBaseType_t)configMAX_PRIORITIES - 1U;
	}
	pxNewTCB->uxPriority = uxPriority;
	
	/* 让任务句柄指向任务控制块 */
	*pxCreatedTask = (TaskHandle_t)pxNewTCB;//对pxNewTCB取值如何？pxCreatedTask为const类型的指针，不能修改地址
	
}


/* 外部需要调用的任务创建函数 */
#if( configSUPPORT_STATIC_ALLOCATION == 1 )
 TaskHandle_t xTaskCreateStatic( TaskFunction_t pxTaskCode,			 //任务入口，也是函数名称
								 const char * const pcName,
								 const uint32_t ulStackDepth,
								 void * const pvParameters,
								 UBaseType_t uxPriority,
								 StackType_t * const puxStackptr, //任务块起始地址指针
								 TCB_t * const pxTaskCtrlptr )		 //任务块控制指针

{
	TCB_t *pxNewTCB;	//在这里生成TCB结构体
	TaskHandle_t xReturn;//TaskHandle_t 是 void* 类型

	if( puxStackptr != NULL && pxTaskCtrlptr != NULL )//防止非法传参
	{
		pxNewTCB = pxTaskCtrlptr;
		pxNewTCB->pxStack = puxStackptr;

		/* 创建新任务 */
		prvInitialiseNewTask(pxTaskCode,
							pcName,
							ulStackDepth,
							pvParameters,
							uxPriority,
							&xReturn,
							pxNewTCB);
		prvAddNewTaskToReadyList( pxNewTCB );
	}
	else
	{
		xReturn = NULL;
	}

	/* 返回任务句柄 */
	return xReturn; 
}
#endif /* configSUPPORT_STATIC_ALLOCATION */

/* 初始化任务列表 */
void prvInitialiseTaskLists( void )
{

 	UBaseType_t uxPriority;
	for( uxPriority = (UBaseType_t)0U; 
		uxPriority < (UBaseType_t)configMAX_PRIORITIES; 
		uxPriority++ )
	{
		/* 就绪列表的初始化 */
		vListInitialise(&(pxReadyTasksLists[uxPriority]));
	}
	vListInitialise(&xDelayedTaskList1);
	vListInitialise(&xDelayedTaskList2);

	pxDelayedTaskList = &xDelayedTaskList1;
	pxOverflowDelayedTaskList = &xDelayedTaskList2;
}

extern TCB_t IdleTaskTCB;
extern TaskHandle_t xIdleTaskHandle;

void vApplicationGetIdleTaskMemory( TCB_t **ppxIdleTaskTCBBuffer,
												StackType_t **ppxIdleTaskStackBuffer,
												uint32_t *pulIdleTaskStackSize );
portCHAR flagIdle = 0;
extern portCHAR flag1;
extern portCHAR flag2;

void prvIdleTask( void *p_arg )
{

/* 永远不要在空闲任务里加入阻塞或者死循环！！！！！！！！！！！！！！！
	否则当其他任务优先级为0时，空闲任务会霸占整个时间片*/
//	for( ;; )
//	{
////		flagIdle = 1;
////		flag1 = 0;
////		flag2 = 0;
//	}

	/* 正常运行时，这里执行IdleTask的代码，否则会转到prvTaskExitError执行 */
	for(;;);//由于已经支持优先级且Task1与Task2的优先级均不为0，因此可以加循环
}

void vTaskStartScheduler( void )
{
/*=======================创建空闲任务 start=======================*/
	 TCB_t *pxIdleTaskTCBBuffer = NULL;
	 /* 用于指向空闲任务控制块 */
	 StackType_t *pxIdleTaskStackBuffer = NULL; /* 用于空闲任务栈起始地址 */
	 uint32_t ulIdleTaskStackSize;
	 /* 获取空闲任务的内存:任务栈和任务 TCB */ 
	 vApplicationGetIdleTaskMemory(  &pxIdleTaskTCBBuffer,
									 &pxIdleTaskStackBuffer,
									 &ulIdleTaskStackSize );
	 
	 /* 创建空闲任务 */ 
	 xIdleTaskHandle =
	 xTaskCreateStatic( (TaskFunction_t)prvIdleTask,
						 /* 任务入口 */
						 (char *)"IDLE",
						 /* 任务名称,字符串形式 */
						 (uint32_t)ulIdleTaskStackSize , /* 任务栈大小,单位为字 */
						 (void *) NULL,
						 (UBaseType_t) tskIDLE_PRIORITY,
						 /* 任务形参 */
						 (StackType_t *)pxIdleTaskStackBuffer, /* 任务栈起始地址 */
						 (TCB_t *)pxIdleTaskTCBBuffer ); /* 任务控制块 */

	/* 将任务添加到就绪列表，具体已经在xTaskCreateStatic内实现了，因此注释掉 */
	//vListInsertEnd( &( pxReadyTasksLists[0] ),&( ((TCB_t *)pxIdleTaskTCBBuffer)->xStateListItem ) );

/*==========================创建空闲任务 end=====================*/
	/* 手动指定第一个运行的任务，注释原因同上 */
	//pxCurrentTCB = &Task1TCB;

	xNextTaskUnblockTime = portMAX_DELAY;
	xTickCount = ( TickType_t ) 0U;

	/* 启动调度器 */
	if ( xPortStartScheduler() != pdFALSE )
	{
	}

}




void vTaskDelay( const TickType_t xTicksToDelay )
{
//	TCB_t *pxTCB = NULL;
//	pxTCB = (TCB_t *)pxCurrentTCB;
//	pxTCB->xTicksToDelay = xTicksToDelay;
//	/* 延时不等于0，也就是任务被延时时，将优先位图表对应的位清零 */
//	if( xTicksToDelay != 0 )
//	{
//		/* 从就绪链表移除时还需要将pxTCB->xStateListItem移到阻塞链表 */
//		//uxListRemove(&(pxTCB->xStateListItem));
//		taskRESET_READY_PRIORITY(pxTCB->uxPriority);
//	}

	prvAddCurrentTaskToDelayedList(xTicksToDelay);

	/* 为什么非得任务切换？因为pxCurrentTCB已经被设置延时了，
	假如延时不为0的话整个系统都会被阻塞，若pxCurrentTCB不设置延时，这也会破坏优先级执行 */
	taskYIELD();
}

#if 0

/* 切换上下文 */
void vTaskSwitchContext( void )
{    
    if( pxCurrentTCB == &Task1TCB )
    {
        pxCurrentTCB = &Task2TCB;
    }
    else
    {
        pxCurrentTCB = &Task1TCB;
    }
}

#else


void vTaskSwitchContext( void )
{
//	/*
//	TCB_t  *pxtempTCB = NULL;
//	int i = 0;
//	if( pxCurrentTCB.xTicksToDelay == 0 )
//	{
//		for( i = configMAX_PRIORITIES-1; i >= 0; i-- )
//		{
//			//怎么从链表中取出TCB?
//			((TCB_t*)(pxReadyTasksLists[i]->xListEnd->pxNext->pvOwner))->xTicksToDelay != 0;
//			//下面是正确做法，为什么？
//			pxtempTCB = ((TCB_t*)(pxReadyTasksLists[i]->xListEnd->pxNext->pvOwner));
//			pxtempTCB->xTicksToDelay != 0;
//		}
//		
//	}*/ 
	
	taskSELECT_HIGHEST_PRIORITY_TASK();
}


#endif

/* 在xPortSysTickHandler中被调用，也就是SysTick_Handler 此函数负责时基自增，任务计时自减 */
BaseType_t xTaskIncrementTick( void )
{
	TCB_t *pxTCB = NULL;
// 	BaseType_t i = 0;
	TickType_t xItemValue;
	BaseType_t xSwitchRequired = pdFALSE;
	
	/* 更新系统时基计数器 xTickCount,xTickCount 是一个在 port.c 中定义的全局变量 */
 	const TickType_t xConstTickCount = xTickCount + 1;//常量赋值？是的，const可以被在定义时被变量赋值
 	xTickCount = xConstTickCount;

//	/* 扫描就绪列表中所有任务的 xTicksToDelay,如果不为 0,则减 1 */
//	for( i = 0; i < configMAX_PRIORITIES; i++ )
//	{
//		pxTCB = ((TCB_t*)((pxReadyTasksLists[i].xListEnd.pxNext)->pvOwner));
//		
//		//也可以用这种方法
//		//pxTCB = (TCB_t * ) listGET_OWNER_OF_HEAD_ENTRY( ( &pxReadyTasksLists[i] ) );
//		if( pxTCB->xTicksToDelay > 0 )
//		{
//			pxTCB->xTicksToDelay --;
//			/* 延时为0时，使用taskRECORD_READY_PRIORITY判断是否需要就绪任务 */
//			if( pxTCB->xTicksToDelay == 0 )
//			{
//				taskRECORD_READY_PRIORITY( pxTCB->uxPriority );
//			}
//		}
//	}

	/* 时间超过uint32位的最大值既溢出时，切换到延时链表 */
	if(xConstTickCount == (TickType_t)0U)//没用的函数
	{
		taskSWITCH_DELAYED_LISTS();
	}
	/* 延时任务到期 */
	if(xConstTickCount >= xNextTaskUnblockTime)
	{
		for(;;)
		{
			/* 判断延时链表是否为空，为空则将下次延时任务解锁的时间设置为最大值，防止睡死
			不为空则重新设置任务延时，将任务转移至就绪链表，因为无法预知延时链表
			中还有几个任务，因此需要for(;;)*/
			if(listLIST_IS_EMPTY(pxDelayedTaskList))
			{
				xNextTaskUnblockTime = portMAX_DELAY;
				break;
			}
			else
			{
				pxTCB = (TCB_t*)listGET_OWNER_OF_HEAD_ENTRY(pxDelayedTaskList);
				xItemValue = listGET_ITEM_VALUE_OF_HEAD_ENTRY(pxDelayedTaskList);//自己改动了

				/* 如果系统内部已经没有被唤醒的延时任务时，将下次唤醒时间更新并跳出循环 */
				if(xConstTickCount < xItemValue)
				{
					xNextTaskUnblockTime = xItemValue;
					break;
				}

				uxListRemove(&(pxTCB->xStateListItem));
				prvAddTaskToReadyList(pxTCB);
				//prvAddNewTaskToReadyList(pxTCB);
//不能使用这个函数，因为这里面将pxTCB变为了当前任务，而上一步已经将pxTCB从就绪链表中移除了

				#if(configUSE_PREEMPTION == 1)
				if(pxTCB->uxPriority >= pxCurrentTCB->uxPriority)
				{
					xSwitchRequired = pdTRUE;
				}
				#endif	
			}
		}
	}

	#if(configUSE_PREEMPTION == 1 && configUSE_TIME_SLICING == 1)
	if(listCURRENT_LIST_LENGTH(&pxReadyTasksLists[pxCurrentTCB->uxPriority]) > (UBaseType_t)1)
	{
		xSwitchRequired = pdTRUE;
	}
	#endif
	
	//taskYIELD();
	return xSwitchRequired;
}

static void prvResetNextTaskUnblockTime( void )
{
	TCB_t* pxTCB;
	if(listLIST_IS_EMPTY(pxDelayedTaskList))
	{
		xNextTaskUnblockTime = portMAX_DELAY;
	}
	else
	{
		pxTCB = (TCB_t*)listGET_OWNER_OF_HEAD_ENTRY(pxDelayedTaskList);
		xNextTaskUnblockTime = listGET_LIST_ITEM_VALUE(&(pxTCB->xStateListItem));
	}
}





