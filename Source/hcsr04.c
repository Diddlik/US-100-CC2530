#include <ioCC2530.h> 
#include <stdio.h>
#include "hcsr_04.h"
#define ULTRASOUND_H 
#define uchar unsigned char 
#define uint unsigned int 
extern float getDistance(void); 
float getDistance(void) 
{ 
  UltrasoundRanging(LoadRegBuf); 
  Delay_1s(1); 
  data=256*H2+L2-L1-256*H1;  
  distance=(float)data*340/10000; 
  Delay_1s(5);
  return distance;
}