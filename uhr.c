#include "ch32fun.h"
#include <stdio.h>
#include <stdint.h>

// pc3-pc6 are for the charlyplexed leds

// the look up table for what pin shoud be high for a given led
// the first 6 bits are reversed
const uint8_t ledHighLookup[] = {4, 4, 4, 3, 3, 3, 5, 5};

// the look up table for what pin shoud be low for a given led
const uint8_t ledLowLookup[] = {6, 5, 3, 6, 5, 4, 3, 4};

// will turn on led l and turn off every other led
void turnOnLed(uint8_t l);
// will turn off all leds
void turnOffLEDs(void);

// display the byte n on the led's with every bit lighting up it's corrosponding led. this function has to be called in a fast loop to work properly 
void displayNum(uint8_t n);
uint8_t currentLED = 0;

#define SDA 1
#define SCL 2

#define TOUCH 0

#define SCL_LOW GPIOC->BSHR = 1<<(SCL+16)
#define SCL_HIGH GPIOC->BSHR = 1<<(SCL)

#define SDA_LOW GPIOC->BSHR = 1<<(SDA+16)
#define SDA_HIGH GPIOC->BSHR = 1<<(SDA)

#define I2C_ADDR 0b11011110

void i2cStart(void);
void i2cStop(void);

_Bool i2cSend(uint8_t data);
uint8_t i2cRecv(_Bool ack);

void i2cWriteReg(uint8_t reg, uint8_t data);
uint8_t i2cReadReg(uint8_t reg);

uint8_t bcdToBin(uint8_t bcdNum);
uint8_t binToBCD(uint8_t num);

int main(void) {
    SystemInit();

    // set the clock to sysclk / 8 to save power
    RCC->CFGR0 &= ~(0xf<<4);
    RCC->CFGR0 |= 10<<4;

    // give us some time to program before messing around with standby stuff
    Delay_Ms(5000);

	// Enable GPIOs
	RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_GPIOD | RCC_APB2Periph_GPIOA;

    // set unused gpios to output to save power
	GPIOD->CFGLR = (GPIO_CNF_OUT_PP<<(4*7)) |
				   (GPIO_CNF_OUT_PP<<(4*6)) |
				   (GPIO_CNF_OUT_PP<<(4*5)) |
				   (GPIO_CNF_OUT_PP<<(4*4)) |
				   (GPIO_CNF_OUT_PP<<(4*3)) |
				   (GPIO_CNF_OUT_PP<<(4*2)) |
				   (GPIO_CNF_OUT_PP<<(4*1)) |
				   (GPIO_CNF_OUT_PP<<(4*0));
	GPIOD->BSHR = 0xff00;
    
    GPIOA->CFGLR = (GPIO_CNF_OUT_PP<<(4*2)) |
                   (GPIO_CNF_OUT_PP<<(4*1));
	GPIOA->BSHR = 0x600;

    // this is also unused
    GPIOC->CFGLR = (GPIO_CNF_OUT_PP<<(4*7));
	GPIOC->BSHR = 1<<(16+7);
    

    // set both of these to high because thats the default state
    GPIOC->BSHR = 1<<SDA;
    GPIOC->BSHR = 1<<SCL;

    // set the i2c pins to open drain output
    GPIOC->CFGLR &= ~(0xf<<(4*SDA));
    GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_OD)<<(4*SDA);

    GPIOC->CFGLR &= ~(0xf<<(4*SCL));
    GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_OD)<<(4*SCL);

    // pc0 is the touch pin
    GPIOC->CFGLR &= ~(0xf<<(4*TOUCH));
    GPIOC->CFGLR |= (GPIO_CNF_IN_PUPD)<<(4*TOUCH);

    // make it pull down
    GPIOC->BSHR = 1<<16;

	// AFIO is needed for EXTI
	RCC->APB2PCENR |= RCC_AFIOEN;

    // set port c 0 to interrupt
    AFIO->EXTICR = 0b10;
    
	// enable line0 interrupt event with rising edge
	EXTI->EVENR |= EXTI_Line0;
	EXTI->RTENR |= EXTI_Line0;

	// select standby on power-down
	PWR->CTLR |= PWR_CTLR_PDDS;

    // peripheral interrupt controller send to deep sleep
	PFIC->SCTLR |= (1 << 2);

    Delay_Ms(250);

    // write to seconds and enable the oscilator
    i2cWriteReg(0, 0x80);
    
    // write to minutes
    i2cWriteReg(1, 0x00);

    // write to hours and set it to 24 hour mode
    i2cWriteReg(2, 0x00);

    _Bool lastTouchState = 0;
    uint32_t pressedInARows = 0;

    // 0 is seconds, 1 is minutes and 2 is hours
    uint8_t mode = 0;

    _Bool setMode = 0;

    while(1) {
        // set the led pins to output low while sleeping
        for (int i = 3; i<=6; i++) {
            // do this allways
            GPIOC->CFGLR &= ~(0xf<<(4*i));
            GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP)<<(4*i);
            GPIOC->BSHR = 1<<(16+i);
        }

        // go to standby mode
		__WFE();
		// restore clock to full speed
		SystemInit();
        // set the clock to sysclk / 8 to save power
        RCC->CFGR0 &= ~(0xf<<4);
        RCC->CFGR0 |= 10<<4;

        // if where in set mode don't make it turn off
        for (int t = 0; t < 10000 || setMode; t++) {
            _Bool touchState = GPIOC->INDR & 1;

            // if the last touch state was 1 and the current one is 1 two increment the counter
            if (lastTouchState && touchState)
                pressedInARows++;
            else if (!touchState) {
                // handle the button press
                if (pressedInARows > 2000) {
                    // if where not in set mode go into set mode. if where not in set mode go into set mode
                    if (!setMode) {
                        setMode = 1;
                        // also turn of the rtc oscilator
                        i2cWriteReg(0, i2cReadReg(0) & 0x7f);
                    } else {
                        setMode = 0;
                        //  turn the rtc oscilator back on again
                        i2cWriteReg(0, i2cReadReg(0) | 0x80);
                    }

                    // also reset the timer
                    t = 0;
                } else if (pressedInARows > 500) {  
                    if (!setMode)
                        // increment mode but don't allow it to go over 2
                        mode = mode+1 > 2 ? 0 : mode+1;
                    else {
                        if (mode == 0 || mode == 1) {   // it can also be incremented with a longer press
                            uint8_t num = bcdToBin(i2cReadReg(mode) & 0x7f);
                            num = num+1 > 59 ? 0 : num+1;
                            i2cWriteReg(mode, binToBCD(num));
                        } else {
                            uint8_t num = bcdToBin(i2cReadReg(mode) & 0x3f);
                            num = num+1 > 23 ? 0 : num+1;
                            i2cWriteReg(mode, binToBCD(num));
                        }
                    }

                    // also reset the timer
                    t = 0;
                } else if (pressedInARows > 50 && setMode) {   // if wher in set mode the currently selecet thing can be incremented with a short press
                    if (mode == 0 || mode == 1) {
                        uint8_t num = bcdToBin(i2cReadReg(mode) & 0x7f);
                        num = num+1 > 59 ? 0 : num+1;
                        i2cWriteReg(mode, binToBCD(num));
                    } else {
                        uint8_t num = bcdToBin(i2cReadReg(mode) & 0x3f);
                        num = num+1 > 23 ? 0 : num+1;
                        i2cWriteReg(mode, binToBCD(num));
                    }
                }

                pressedInARows = 0;
            }

            lastTouchState = touchState;

            // handle button 

            uint8_t displayedNumber = bcdToBin(i2cReadReg(mode) & 0x7f);
            displayedNumber |= mode << 6;

            // make the blue led flash while where in set mode
            if (setMode && (t & 0xff) > 0x7f)
                displayedNumber |= 0xC0;

            displayNum(displayedNumber);
        }
    }
}

void turnOnLed(uint8_t l) {
    // set the highpin high and the low pin low. do this first so we dont get any ghosting
    GPIOC->BSHR = 1<<ledHighLookup[l];
    GPIOC->BSHR = 1<<(16+ledLowLookup[l]);

    // set all the pins that are not used to floating. Set the high and low pin to pp
    for (int i = 3; i<=6; i++) {
        // do this allways
        GPIOC->CFGLR &= ~(0xf<<(4*i));

        if (ledHighLookup[l] == i || ledLowLookup[l] == i) {
            GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_PP)<<(4*i);
        } else {
            GPIOC->CFGLR |= (GPIO_CNF_IN_FLOATING)<<(4*i);
        }
    }
}

void turnOffLEDs(void) {
    for (int i = 3; i<=6; i++) {
        GPIOC->CFGLR &= ~(0xf<<(4*i));
        GPIOC->CFGLR |= (GPIO_CNF_IN_FLOATING)<<(4*i);
    }
}

void displayNum(uint8_t n) {
    if (currentLED > 7)
        currentLED = 0;

    if ((n >> currentLED) & 1) {
        turnOnLed(currentLED);
    } else {
        turnOffLEDs();
    }
    currentLED++;
}

void i2cStart(void) {
    SCL_LOW;
    SDA_HIGH;
    Delay_Us(10);

    SCL_HIGH;
    Delay_Us(10);
    SDA_LOW;
    Delay_Us(10);
}

void i2cStop(void) {
    SCL_LOW;
    SDA_LOW;
    Delay_Us(10);

    SCL_HIGH;
    Delay_Us(10);
    SDA_HIGH;
    Delay_Us(10);
}

_Bool i2cSend(uint8_t data) {
    // I2C_Start;

    for (int i = 7; i >= 0; i--) {
        Delay_Us(10);
        SCL_LOW;

        Delay_Us(10);

        // set the bit
        if ((data >> i) & 1 == 1)
            SDA_HIGH;
        else
            SDA_LOW;

        // wait for a bit
        Delay_Us(10);

        // then toggle the clock
        SCL_HIGH;
        Delay_Us(10);

        // printf("Tok!\n");
    }

    SCL_LOW;
    Delay_Us(10);

    // then set SDA to input for reading the acknoledge
    GPIOC->CFGLR &= ~(0xf<<(4*SDA));
    GPIOC->CFGLR |= (GPIO_CNF_IN_FLOATING)<<(4*SDA);
    SDA_HIGH;

    SCL_HIGH;
    Delay_Us(10);

    uint8_t ack = GPIOC->INDR;

    // make sda open drain again
    GPIOC->CFGLR &= ~(0xf<<(4*SDA));
    GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_OD)<<(4*SDA);

    return (ack>>SDA) & 1;
}

uint8_t i2cRecv(_Bool ack) {
    // make sda an input
    GPIOC->CFGLR &= ~(0xf<<(4*SDA));
    GPIOC->CFGLR |= (GPIO_CNF_IN_FLOATING)<<(4*SDA);

    uint8_t data = 0;

    for (int i = 7; i >= 0; i--) {
        Delay_Us(10);
        SCL_LOW;

        // wait for a bit
        Delay_Us(10);

        // then toggle the clock
        SCL_HIGH;
        Delay_Us(10);

        // read in the data
        data <<= 1;
        data |= (GPIOC->INDR>>SDA) & 1;
    }


    // make sda open drain again
    GPIOC->CFGLR &= ~(0xf<<(4*SDA));
    GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_OD)<<(4*SDA);

    SCL_LOW;
    // and send the acknolegde
    if (ack)
        SDA_LOW;
    else
        SDA_HIGH;

    Delay_Us(10);
    

    SCL_HIGH;
    Delay_Us(10);

    return data;
}

void i2cWriteReg(uint8_t reg, uint8_t data) {
    i2cStart();
    // address
    i2cSend(I2C_ADDR);

    // register address
    i2cSend(reg);

    // data to write
    i2cSend(data);
    i2cStop();
}

uint8_t i2cReadReg(uint8_t reg) {
    // to read first write the address
    i2cStart();
    i2cSend(I2C_ADDR);

    i2cSend(reg);

    // resend the start signal
    i2cStart();

    // this time read
    i2cSend(I2C_ADDR | 1);

    // recv the data. don't ack so it knows its the last byte we want to read
    uint8_t data = i2cRecv(0);
    i2cStop();

    return data;
}

uint8_t bcdToBin(uint8_t bcdNum) {
    return (bcdNum&0xf) + ((bcdNum>>4) * 10);
}

uint8_t binToBCD(uint8_t num) {
    return ((num/10)<<4) | num%10;
}