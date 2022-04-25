#include <ioCC2530.h> 
#include <stdio.h>
#ifndef ULTRASOUND_H 
#define ULTRASOUND_H 
 
#define uchar unsigned char 
#define uint unsigned int 
 
#define TRIG P1_3    //P1_2    
#define ECHO P0_7    //P0_1 
 
extern uchar RG; 
extern  uchar H1; 
extern  uchar L1; 
extern  uchar H2; 
extern  uchar L2; 
extern  uchar H3; 
extern  uchar L3; 
extern  uint data; 
extern  float distance; 
extern  uchar LoadRegBuf[4]; 
 
 void Delay_1us(uint microSecs); 
 void Delay_10us(uint n); 
 void Delay_1s(uint n); 
 void SysClkSet32M(); 
 void Init_UltrasoundRanging(); 
 void UltrasoundRanging(uchar *ulLoadBufPtr); 
  __interrupt void P0_ISR(void); 
#endif
 
//×××××××××××Ultrasound.c****************************
#include <ioCC2530.h> 
uchar RG; 
uchar H1; 
uchar L1; 
uchar H2; 
uchar L2; 
uchar H3; 
uchar L3; 
uint  data; 
float distance; 
 
uchar LoadRegBuf[4];//全局数据，用以存储定时计数器的值。 
 
void Delay_1us(uint microSecs) 
{  while(microSecs--) 
  {    /* 32 NOPs == 1 usecs 因为延时还有计算的缘故，用了31个nop*/ 
    asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop"); 
    asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop"); 
    asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop"); 
    asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop"); 
    asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop"); 
    asm("nop"); asm("nop"); asm("nop"); asm("nop"); asm("nop"); 
    asm("nop"); 
  } 
} 
 
void Delay_10us(uint n) 
{ /* 320NOPs == 10usecs 因为延时还有计算的缘故，用了310个nop*/ 
    uint tt,yy; 
    for(tt = 0;tt<n;tt++); 
    for(yy = 310;yy>0;yy--); 
    {asm("NOP");} 
} 
 
void Delay_1s(uint n) 
{       uint ulloop=1000; 
        uint tt; 
    for(tt =n ;tt>0;tt--); 
    for( ulloop=1000;ulloop>0;ulloop--) 
    { 
          Delay_10us(100); 
        } 
 
} 
 
void SysClkSet32M() 
 { 
    CLKCONCMD &= ~0x40;         //设置系统时钟源为32MHZ晶振 
    while(CLKCONSTA & 0x40);     //等待晶振稳定 
    CLKCONCMD &= ~0x47;        //设置系统主时钟频率为32MHZ 
                               //此时的CLKCONSTA为0x88。即普通时钟和定时器时钟都是32M。      
  } 
 
 void Init_UltrasoundRanging() 
 {  
    P1DIR = 0x08;     //0为输入1为输出  00001000  设置TRIG P1_3为输出模式 
    TRIG=0;           //将TRIG 设置为低电平 
    
    P0INP &= ~0x80;   //有上拉、下拉 有初始化的左右 
    P0IEN |= 0x80;    //P0_7 中断使能 
    PICTL |= 0x01;    //设置P0_7引脚，下降沿触发中断   
    IEN1 |= 0x20;      // P0IE = 1; 
    P0IFG = 0;  
 
} 
 
 void UltrasoundRanging(uchar *ulLoadBufPtr) 
 {   
     SysClkSet32M(); 
     Init_UltrasoundRanging(); 
     EA = 0; 
     TRIG =1; 
      
     Delay_1us(10);     //需要延时10us以上的高电平 
     TRIG =0; 
 
     T1CNTL=0; 
     T1CNTH=0; 
      
     while(!ECHO);  
     T1CTL = 0x09;      //通道0,中断有效,32分频;自动重装模式(0x0000->0xffff); 
     L1=T1CNTL;  
     H1=T1CNTH;   
     *ulLoadBufPtr++=T1CNTL; 
     *ulLoadBufPtr++=T1CNTH; 
      EA = 1;  
      Delay_10us(60000);     
      Delay_10us(60000);  
  
 } 
 
#pragma vector = P0INT_VECTOR 
 __interrupt void P0_ISR(void) 
 { 
         EA=0;  
         T1CTL = 0x00;  
         LoadRegBuf[2]=T1CNTL;  
         LoadRegBuf[3]=T1CNTH;  
         L2=T1CNTL;  
         H2=T1CNTH;  
          
        if(P0IFG&0x080)          //外部ECHO反馈信号 
        {    
         P0IFG = 0;        
        } 
        T1CTL = 0x09;  
         T1CNTL=0; 
         T1CNTH=0; 
        P0IF = 0;             //清中断标志 
        EA=1; 
 } 