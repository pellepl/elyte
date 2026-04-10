#ifndef _BOARD_H_
#define _BOARD_H_

///////////
// ELYTE //
///////////

#include "board_common.h"
#include "stm32f334x8.h"

#define PORTA(x) (0 + (x))
#define PORTB(x) (16 + (x))
#define PORTC(x) (32 + (x))
#define PORTD(x) (48 + (x))
#define PORTF(x) (64 + (x))

#define PININPORT(x) ((x) & 0xf)
#define PORT(x) ((GPIO_TypeDef *[]){GPIOA, GPIOB, GPIOC, GPIOD, GPIOF})[((x) >> 4)]
#define PORTCHAR(x) ((char[]){"ABCDF"})[((x) >> 4)]

#define BOARD_PIN_MAX PORTF(16)

#define BOARD_BUTTON_COUNT (2)
#define BOARD_BUTTON_GPIO_PIN ((const uint16_t[BOARD_BUTTON_COUNT]){PIN_BUTTON})
#define BOARD_BUTTON_GPIO_ACTIVE ((const uint8_t[BOARD_BUTTON_COUNT]){0})

#define BOARD_LED_COUNT (2)
#define BOARD_LED_GPIO_PIN ((const uint16_t[BOARD_LED_COUNT]){PIN_LED_G, PIN_LED_R})
#define BOARD_LED_GPIO_ACTIVE ((const uint8_t[BOARD_LED_COUNT]){0, 0})

// used like this
// static const board_uart_pindef_t uart_pindefs[BOARD_UART_COUNT] = BOARD_UART_GPIO_PINS;
#define BOARD_UART_COUNT (1)
#define BOARD_UART_GPIO_PINS                                                                                                           \
    {                                                                                                                                  \
        (board_uart_pindef_t){.rx_pin = PIN_USART_RX, .tx_pin = PIN_USART_TX, .cts_pin = BOARD_PIN_UNDEF, .rts_pin = BOARD_PIN_UNDEF}, \
    }

#define PIN_USART_TX PORTA(9)
#define PIN_USART_RX PORTA(10)

#define PIN_LED_R PORTC(10)
#define PIN_LED_G PORTC(11)

#define PIN_BUTTON PORTC(8)

#define PIN_ROT_A PORTA(0)          // TIM2_CH1
#define PIN_ROT_B PORTA(1)          // TIM2_CH2
#define PIN_ROT_PUSH PORTA(2)

#define PIN_DAC PORTA(4)            // DAC1_OUT1
#define PIN_CURR_L PORTC(5)         // OPAMP2_VINM
#define PIN_CURR_H PORTB(0)         // OPAMP2_VINP
#define PIN_VSENSE_P PORTB(14)      // ADC2_IN14
#define PIN_VSENSE_N PORTB(15)      // ADC2_IN15

#define PIN_TEMP_SW PORTC(9)
#define PIN_I2C_SDA PORTB(7)        // I2C1_SDA
#define PIN_I2C_SCL PORTB(6)        // I2C1_SCL

#define PIN_PWM PORTA(8)            // TIM1_CH1
#define PIN_SWCLK PORTA(14)
#define PIN_SWDIO PORTA(13)
#define PIN_SWDIO_SHORT PORTB(12)

#endif // _BOARD_H_
