#ifndef _FUNCONFIG_H
#define _FUNCONFIG_H

// Place configuration items here, you can see a full list in ch32fun/ch32fun.h
// To reconfigure to a different processor, update TARGET_MCU in the  Makefile


#define FUNCONF_USE_PLL 0               // Don't Use built-in 2x PLL 
#define FUNCONF_USE_HSI 1               // Use HSI Internal Oscillator
#define FUNCONF_SYSTEM_CORE_CLOCK 3000000 // this is the effective clock speed 24/8

#endif

