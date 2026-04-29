#include "utils.h"
#include _CORTEX_CORE_HEADER

#define IRQ(__x)                    \
    void __x(void)                  \
    {                               \
        volatile noinit_t *n = noinit(); \
        n->irq_trap_count++;        \
        n->irq_trap_last_ipsr = __get_IPSR(); \
        while (1)                   \
            ;                       \
    }

//IRQ(Reset_Handler);
IRQ(NMI_Handler);
//IRQ(HardFault_Handler);
IRQ(MemManage_Handler);
IRQ(BusFault_Handler);
IRQ(UsageFault_Handler);
IRQ(SVC_Handler);
IRQ(DebugMon_Handler);
IRQ(PendSV_Handler);
//IRQ(SysTick_Handler);
IRQ(WWDG_IRQHandler);
IRQ(TAMP_STAMP_IRQHandler);
IRQ(RTC_WKUP_IRQHandler);
IRQ(FLASH_IRQHandler);
IRQ(RCC_IRQHandler);
//IRQ(EXTI0_IRQHandler);
//IRQ(EXTI1_IRQHandler);
//IRQ(EXTI2_TSC_IRQHandler);
IRQ(EXTI3_IRQHandler);
IRQ(EXTI4_IRQHandler);
IRQ(DMA1_Channel1_IRQHandler);
IRQ(DMA1_Channel2_IRQHandler);
IRQ(DMA1_Channel3_IRQHandler);
IRQ(DMA1_Channel4_IRQHandler);
IRQ(DMA1_Channel5_IRQHandler);
//IRQ(DMA1_Channel6_IRQHandler);
IRQ(DMA1_Channel7_IRQHandler);
//IRQ(ADC1_2_IRQHandler);
IRQ(CAN_TX_IRQHandler);
IRQ(CAN_RX0_IRQHandler);
IRQ(CAN_RX1_IRQHandler);
IRQ(CAN_SCE_IRQHandler);
//IRQ(EXTI9_5_IRQHandler);
IRQ(TIM1_BRK_TIM15_IRQHandler);
IRQ(TIM1_UP_TIM16_IRQHandler);
IRQ(TIM1_TRG_COM_TIM17_IRQHandler);
IRQ(TIM1_CC_IRQHandler);
//IRQ(TIM2_IRQHandler);
//IRQ(TIM3_IRQHandler);
IRQ(TIM4_IRQHandler);
IRQ(I2C1_EV_IRQHandler);
IRQ(I2C1_ER_IRQHandler);
IRQ(I2C2_EV_IRQHandler);
IRQ(I2C2_ER_IRQHandler);
IRQ(SPI1_IRQHandler);
IRQ(SPI2_IRQHandler);
//IRQ(USART1_IRQHandler);
//IRQ(USART2_IRQHandler);
//IRQ(USART3_IRQHandler);
IRQ(EXTI15_10_IRQHandler);
IRQ(RTC_Alarm_IRQHandler);
IRQ(TIM8_BRK_IRQHandler);
IRQ(TIM8_UP_IRQHandler);
IRQ(TIM8_TRG_COM_IRQHandler);
IRQ(TIM8_CC_IRQHandler);
IRQ(ADC3_IRQHandler);
IRQ(FMC_IRQHandler);
IRQ(SPI3_IRQHandler);
IRQ(UART4_IRQHandler);
IRQ(UART5_IRQHandler);
IRQ(TIM6_DAC_IRQHandler);
IRQ(TIM7_IRQHandler);
IRQ(DMA2_Channel1_IRQHandler);
IRQ(DMA2_Channel2_IRQHandler);
IRQ(DMA2_Channel3_IRQHandler);
IRQ(DMA2_Channel4_IRQHandler);
IRQ(DMA2_Channel5_IRQHandler);
IRQ(ADC4_IRQHandler);
IRQ(COMP1_2_3_IRQHandler);
IRQ(COMP4_5_6_IRQHandler);
IRQ(COMP7_IRQHandler);
IRQ(I2C3_EV_IRQHandler);
IRQ(I2C3_ER_IRQHandler);
IRQ(TIM20_BRK_IRQHandler);
IRQ(TIM20_UP_IRQHandler);
IRQ(TIM20_TRG_COM_IRQHandler);
IRQ(TIM20_CC_IRQHandler);
IRQ(FPU_IRQHandler);
IRQ(SPI4_IRQHandler);
