#include "delay.h"
#include "sys.h"
#include "usart.h"
#include "includes.h"
#include "wifiap.h"
#include "usart3.h"
#include "ap.h"
#include "malloc.h"
#include "sram.h"
#include "usmart.h"
#include "sdio_sdcard.h"
#include "ff.h"  
#include "exfuns.h" 

#include "acquisiton.h"
#include "IPM.h"
#include "power.h"
#include "cs5532.h"

#include "includes.h"

void system_init(void);

//创建开始任务
//任务优先级
#define START_TASK_PRIO		2
//任务堆栈大小	
#define START_STK_SIZE 		512
//任务控制块
OS_TCB StartTaskTCB;
//任务堆栈	
CPU_STK START_TASK_STK[START_STK_SIZE];
//任务函数
void start_task(void *p_arg);

//Usart3信号量处理任务创建
#define Usart3_Deal_TASK_PRIO		5
#define Usart3_Deal_STK_SIZE 		128
OS_TCB Usart3DealTaskTCB;
CPU_STK Usart3_Deal_TASK_STK[Usart3_Deal_STK_SIZE];
void Usart3_Deal_Task(void *p_arg);



OS_TMR 	usart3_deal_tmr;
//OS_TMR 	usart_deal_tmr;
void Usart3_Deal_Callback(void *p_tmr, void *p_arg);
//void Usart_Deal_Callback(void *p_tmr, void *p_arg);




//main函数	  					
int main(void)
{ 	
	OS_ERR err;
	CPU_SR_ALLOC();
	
	system_init();		//系统初始化 
	
	OSInit(&err);
	
 	OS_CRITICAL_ENTER();//进入临界区
	////创建开始任务
	OSTaskCreate((OS_TCB 	* )&StartTaskTCB,		//任务控制块
							 (CPU_CHAR	* )"start task", 		//任务名字
                 (OS_TASK_PTR )start_task, 			//任务函数
                 (void		* )0,					//传递给任务函数的参数
                 (OS_PRIO	  )START_TASK_PRIO,     //任务优先级
                 (CPU_STK   * )&START_TASK_STK[0],	//任务堆栈基地址
                 (CPU_STK_SIZE)START_STK_SIZE/10,	//任务堆栈深度限位
                 (CPU_STK_SIZE)START_STK_SIZE,		//任务堆栈大小
                 (OS_MSG_QTY  )0,					//任务内部消息队列能够接收的最大消息数目,为0时禁止接收消息
                 (OS_TICK	  )0,					//当使能时间片轮转时的时间片长度，为0时为默认长度，
                 (void   	* )0,					//用户补充的存储区
                 (OS_OPT      )OS_OPT_TASK_STK_CHK|OS_OPT_TASK_STK_CLR, //任务选项
                 (OS_ERR 	* )&err);				//存放该函数错误时的返回值
	OS_CRITICAL_EXIT();	//退出临界区					 
								 
	OSStart(&err);	  						    
}

void system_init(void)
{
	//u8 res = 0;
	
	delay_init();  //时钟初始化
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);//中断分组配置
	uart_init(115200);								//初始化串口波特率为115200
	usart3_init(115200);							//初始化串口2波特率为115200
//	atk_8266_init();								//AP初始化
//	usmart_dev.init(72);		//初始化USMART
	
	FSMC_SRAM_Init();			//初始化外部SRAM  
	my_mem_init(SRAMIN);		//初始化内部内存池
	my_mem_init(SRAMEX);		//初始化外部内存池
	
	IPM_Init();
	Acquisition_Init();
	Power_Init();
	CS5532_Init();
		
	usmart_dev.init(SystemCoreClock/1000000);	//初始化USMART	
	while(SD_Init())
	{
		printf("检测SD卡中...\r\n");
	}
	printf("SD卡已准备 \r\n");	
	exfuns_init();							//为fatfs相关变量申请内存	
  f_mount(fs[0],"0:",1); 					//挂载SD卡 
			
}

//开始任务函数
void start_task(void *p_arg)
{
	OS_ERR err;
	CPU_SR_ALLOC();
	p_arg = p_arg;
	
	CPU_Init();
	
#if OS_CFG_STAT_TASK_EN > 0u
   OSStatTaskCPUUsageInit(&err);  	//统计任务                
#endif
	
#ifdef CPU_CFG_INT_DIS_MEAS_EN		//如果使能了测量中断关闭时间
    CPU_IntDisMeasMaxCurReset();	
#endif
	
#if	OS_CFG_SCHED_ROUND_ROBIN_EN  //当使用时间片轮转的时候
	 //使能时间片轮转调度功能,时间片长度为1个系统时钟节拍，既1*5=5ms
	OSSchedRoundRobinCfg(DEF_ENABLED,1,&err);  
#endif		
	
	OS_CRITICAL_ENTER();	//进入临界区	
		
	//创建Usart3定时器
	OSTmrCreate((OS_TMR		*)&usart3_deal_tmr,		//Usart3定时器
                (CPU_CHAR	*)"usart3 deal tmr",		//定时器名字
                (OS_TICK	 )0,			
                (OS_TICK	 )10,          //10*10=100ms
                (OS_OPT		 )OS_OPT_TMR_PERIODIC, //周期模式
                (OS_TMR_CALLBACK_PTR)Usart3_Deal_Callback,//Usart3定时器回调函数
                (void	    *)0,			//参数为0
                (OS_ERR	    *)&err);		//返回的错误码
//	//创建Usart定时器
//	OSTmrCreate((OS_TMR		*)&usart_deal_tmr,		//Usart定时器
//                (CPU_CHAR	*)"usart deal tmr",		//定时器名字
//                (OS_TICK	 )0,			
//                (OS_TICK	 )10,          //10*10=100ms
//                (OS_OPT		 )OS_OPT_TMR_PERIODIC, //周期模式
//                (OS_TMR_CALLBACK_PTR)Usart_Deal_Callback,//Usart定时器回调函数
//                (void	    *)0,			//参数为0
//                (OS_ERR	    *)&err);		//返回的错误码
								
	OSTmrStart(&usart3_deal_tmr,&err);
//	OSTmrStart(&usart_deal_tmr,&err);
	
	//创建Usart3信号量处理任务
	OSTaskCreate((OS_TCB 	* )&Usart3DealTaskTCB,		
				 (CPU_CHAR	* )"Usart3 Deal task", 		
                 (OS_TASK_PTR )Usart3_Deal_Task, 			
                 (void		* )0,					
                 (OS_PRIO	  )Usart3_Deal_TASK_PRIO,     	
                 (CPU_STK   * )&Usart3_Deal_TASK_STK[0],	
                 (CPU_STK_SIZE)Usart3_Deal_STK_SIZE/10,	
                 (CPU_STK_SIZE)Usart3_Deal_STK_SIZE,		
                 (OS_MSG_QTY  )0,					
                 (OS_TICK	  )0,					
                 (void   	* )0,				
                 (OS_OPT      )OS_OPT_TASK_STK_CHK|OS_OPT_TASK_STK_CLR, 
                 (OS_ERR 	* )&err);				
								 							 
	OS_TaskSuspend((OS_TCB*)&StartTaskTCB,&err);	//挂起开始任务			 
	OS_CRITICAL_EXIT();								//退出临界区
	OSTaskDel((OS_TCB*)0,&err);	//删除start_task任务自身
}

void Usart3_Deal_Callback(void *p_tmr, void *p_arg)
{
	if(USART3_RX_STA&0X8000)		//接收到一次数据了
	{
		u16 rlen=0;
		
		rlen=USART3_RX_STA&0X7FFF;	//得到本次接收到的数据长度
		USART3_RX_BUF[rlen]=0;		//添加结束符 
		USART3_RX_STA=0;
		
		printf("%s",USART3_RX_BUF);
		
		if(USART3_RX_BUF[11]=='A' & USART3_RX_BUF[12]=='P')
		{
			printf("AP指令\r\n");
		}
		else
		{
			printf("未知指令\r\n");
			printf("%c %c",USART3_RX_BUF[11],USART3_RX_BUF[12]);
		}
	}	
}

//void Usart_Deal_Callback(void *p_tmr, void *p_arg)
//{
//	OS_ERR err;
//	printf("AP指令\r\n");
//	OSTimeDlyHMSM(0,0,5,0,OS_OPT_TIME_HMSM_STRICT,&err);
//	
////	if(USART_RX_STA&0X8000)		//接收到一次数据了
////	{		
////		printf("%s\r\n",USART_RX_BUF);
////		USART_RX_STA=0;
////		
////		if(USART_RX_BUF[0]=='A' & USART_RX_BUF[1]=='P')
////		{
////			printf("AP指令\r\n");
////		}
////		else
////		{
////			printf("未知指令\r\n");
////			printf("%c %c",USART_RX_BUF[11],USART_RX_BUF[12]);
////		}
////	}	
//}

//IPM测试
//void Usart3_Deal_Task(void *p_arg)
//{
//	OS_ERR err;
////	CPU_SR_ALLOC();
//	
//	IPM_POWER_EN= 0;
//	
//	while(1)
//	{
//		IPM_Start_AB();
//		printf("SUP：%lu；SVP：%lu；SUN：%lu；SUN：%lu。\r\n",IPM_SUP,IPM_SVP,IPM_SUN,IPM_SVN);		
//		OSTimeDlyHMSM(0,0,2,0,OS_OPT_TIME_HMSM_STRICT,&err);
//		IPM_Start_BA();
//		printf("SUP：%lu；SVP：%lu；SUN：%lu；SUN：%lu。\r\n",IPM_SUP,IPM_SVP,IPM_SUN,IPM_SVN);		
//		OSTimeDlyHMSM(0,0,2,0,OS_OPT_TIME_HMSM_STRICT,&err);
//		IPM_Stop_AB();	
//		printf("SUP：%lu；SVP：%lu；SUN：%lu；SUN：%lu。\r\n",IPM_SUP,IPM_SVP,IPM_SUN,IPM_SVN);	
//		OSTimeDlyHMSM(0,0,2,0,OS_OPT_TIME_HMSM_STRICT,&err);
//		
//	}
//}

//AD测试
void Usart3_Deal_Task(void *p_arg)
{
	OS_ERR err;
//	CPU_SR_ALLOC();
	
	AD_POWER_EN= 0;
	
	while(1)
	{
		IPM_Start_AB();
		printf("SUP：%lu；SVP：%lu；SUN：%lu；SUN：%lu。\r\n",IPM_SUP,IPM_SVP,IPM_SUN,IPM_SVN);		
		OSTimeDlyHMSM(0,0,2,0,OS_OPT_TIME_HMSM_STRICT,&err);
		IPM_Start_BA();
		printf("SUP：%lu；SVP：%lu；SUN：%lu；SUN：%lu。\r\n",IPM_SUP,IPM_SVP,IPM_SUN,IPM_SVN);		
		OSTimeDlyHMSM(0,0,2,0,OS_OPT_TIME_HMSM_STRICT,&err);
		IPM_Stop_AB();	
		printf("SUP：%lu；SVP：%lu；SUN：%lu；SUN：%lu。\r\n",IPM_SUP,IPM_SVP,IPM_SUN,IPM_SVN);	
		OSTimeDlyHMSM(0,0,2,0,OS_OPT_TIME_HMSM_STRICT,&err);
		
	}
}












