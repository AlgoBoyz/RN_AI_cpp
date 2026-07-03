/**
 * STM32F411CEU6 startup for the HAL/USB-Device-Library build of mouse_minimal.
 *
 * Differences from ../startup_stm32f411xe.c (the TinyUSB shared startup):
 *   - Reset_Handler calls SystemInit() before main (HAL/CMSIS contract)
 *   - OTG_FS_IRQHandler is weak so stm32f4xx_it.c's HAL_PCD_IRQHandler wrapper wins
 *   - SystemCoreClock lives in system_stm32f4xx.c (do NOT define it here)
 *
 * Linker script (STM32F411CEUx_FLASH.ld) symbols used:
 *   _stack_end, _data_start, _data_end, _data_load, _bss_start, _bss_end
 */

#include <stdint.h>

extern uint8_t _stack_end;
extern uint8_t _data_start, _data_end, _data_load;
extern uint8_t _bss_start, _bss_end;

extern void SystemInit(void);
extern int  main(void);

void __attribute__((weak)) Default_Handler(void) { while (1) { __asm__ volatile ("nop"); } }

__attribute__((naked, noreturn))
void Reset_Handler(void) {
    __asm__ volatile (
        "ldr  r0, =_data_load        \n"
        "ldr  r1, =_data_start       \n"
        "ldr  r2, =_data_end         \n"
        "cmp  r1, r2                 \n"
        "beq  2f                     \n"
        "1:  ldr  r3, [r0], #4       \n"
        "    str  r3, [r1], #4       \n"
        "    cmp  r1, r2             \n"
        "    bcc  1b                 \n"
        "2:  ldr  r0, =_bss_start    \n"
        "ldr  r1, =_bss_end          \n"
        "mov  r2, #0                 \n"
        "cmp  r0, r1                 \n"
        "beq  4f                     \n"
        "3:  str  r2, [r0], #4       \n"
        "    cmp  r0, r1             \n"
        "    bcc  3b                 \n"
        "4:  bl   SystemInit         \n"
        "    bl   main               \n"
        "    b    .                  \n"
    );
}

/* Core + peripheral vectors — all weak so HAL/it.c can override. */
void __attribute__((weak)) NMI_Handler(void)              { Default_Handler(); }
void __attribute__((weak)) HardFault_Handler(void)        { while (1) { } }
void __attribute__((weak)) MemManage_Handler(void)        { while (1) { } }
void __attribute__((weak)) BusFault_Handler(void)         { while (1) { } }
void __attribute__((weak)) UsageFault_Handler(void)       { while (1) { } }
void __attribute__((weak)) SVC_Handler(void)              { Default_Handler(); }
void __attribute__((weak)) DebugMon_Handler(void)         { Default_Handler(); }
void __attribute__((weak)) PendSV_Handler(void)           { Default_Handler(); }
void __attribute__((weak)) SysTick_Handler(void)          { Default_Handler(); }

void __attribute__((weak)) WWDG_IRQHandler(void)                { Default_Handler(); }
void __attribute__((weak)) PVD_IRQHandler(void)                 { Default_Handler(); }
void __attribute__((weak)) TAMP_STAMP_IRQHandler(void)          { Default_Handler(); }
void __attribute__((weak)) RTC_WKUP_IRQHandler(void)            { Default_Handler(); }
void __attribute__((weak)) FLASH_IRQHandler(void)               { Default_Handler(); }
void __attribute__((weak)) RCC_IRQHandler(void)                 { Default_Handler(); }
void __attribute__((weak)) EXTI0_IRQHandler(void)               { Default_Handler(); }
void __attribute__((weak)) EXTI1_IRQHandler(void)               { Default_Handler(); }
void __attribute__((weak)) EXTI2_IRQHandler(void)               { Default_Handler(); }
void __attribute__((weak)) EXTI3_IRQHandler(void)               { Default_Handler(); }
void __attribute__((weak)) EXTI4_IRQHandler(void)               { Default_Handler(); }
void __attribute__((weak)) DMA1_Stream0_IRQHandler(void)        { Default_Handler(); }
void __attribute__((weak)) DMA1_Stream1_IRQHandler(void)        { Default_Handler(); }
void __attribute__((weak)) DMA1_Stream2_IRQHandler(void)        { Default_Handler(); }
void __attribute__((weak)) DMA1_Stream3_IRQHandler(void)        { Default_Handler(); }
void __attribute__((weak)) DMA1_Stream4_IRQHandler(void)        { Default_Handler(); }
void __attribute__((weak)) DMA1_Stream5_IRQHandler(void)        { Default_Handler(); }
void __attribute__((weak)) DMA1_Stream6_IRQHandler(void)        { Default_Handler(); }
void __attribute__((weak)) ADC_IRQHandler(void)                 { Default_Handler(); }
void __attribute__((weak)) EXTI9_5_IRQHandler(void)             { Default_Handler(); }
void __attribute__((weak)) TIM1_BRK_TIM9_IRQHandler(void)       { Default_Handler(); }
void __attribute__((weak)) TIM1_UP_TIM10_IRQHandler(void)       { Default_Handler(); }
void __attribute__((weak)) TIM1_TRG_COM_TIM11_IRQHandler(void)  { Default_Handler(); }
void __attribute__((weak)) TIM1_CC_IRQHandler(void)             { Default_Handler(); }
void __attribute__((weak)) TIM2_IRQHandler(void)                { Default_Handler(); }
void __attribute__((weak)) TIM3_IRQHandler(void)                { Default_Handler(); }
void __attribute__((weak)) TIM4_IRQHandler(void)                { Default_Handler(); }
void __attribute__((weak)) I2C1_EV_IRQHandler(void)             { Default_Handler(); }
void __attribute__((weak)) I2C1_ER_IRQHandler(void)             { Default_Handler(); }
void __attribute__((weak)) I2C2_EV_IRQHandler(void)             { Default_Handler(); }
void __attribute__((weak)) I2C2_ER_IRQHandler(void)             { Default_Handler(); }
void __attribute__((weak)) SPI1_IRQHandler(void)                { Default_Handler(); }
void __attribute__((weak)) SPI2_IRQHandler(void)                { Default_Handler(); }
void __attribute__((weak)) USART1_IRQHandler(void)              { Default_Handler(); }
void __attribute__((weak)) USART2_IRQHandler(void)              { Default_Handler(); }
void __attribute__((weak)) EXTI15_10_IRQHandler(void)           { Default_Handler(); }
void __attribute__((weak)) RTC_Alarm_IRQHandler(void)           { Default_Handler(); }
void __attribute__((weak)) OTG_FS_WKUP_IRQHandler(void)         { Default_Handler(); }
void __attribute__((weak)) DMA1_Stream7_IRQHandler(void)        { Default_Handler(); }
void __attribute__((weak)) SDIO_IRQHandler(void)                { Default_Handler(); }
void __attribute__((weak)) TIM5_IRQHandler(void)                { Default_Handler(); }
void __attribute__((weak)) SPI3_IRQHandler(void)                { Default_Handler(); }
void __attribute__((weak)) DMA2_Stream0_IRQHandler(void)        { Default_Handler(); }
void __attribute__((weak)) DMA2_Stream1_IRQHandler(void)        { Default_Handler(); }
void __attribute__((weak)) DMA2_Stream2_IRQHandler(void)        { Default_Handler(); }
void __attribute__((weak)) DMA2_Stream3_IRQHandler(void)        { Default_Handler(); }
void __attribute__((weak)) DMA2_Stream4_IRQHandler(void)        { Default_Handler(); }
void __attribute__((weak)) OTG_FS_IRQHandler(void)              { Default_Handler(); }
void __attribute__((weak)) DMA2_Stream5_IRQHandler(void)        { Default_Handler(); }
void __attribute__((weak)) DMA2_Stream6_IRQHandler(void)        { Default_Handler(); }
void __attribute__((weak)) DMA2_Stream7_IRQHandler(void)        { Default_Handler(); }
void __attribute__((weak)) USART6_IRQHandler(void)              { Default_Handler(); }
void __attribute__((weak)) I2C3_EV_IRQHandler(void)             { Default_Handler(); }
void __attribute__((weak)) I2C3_ER_IRQHandler(void)             { Default_Handler(); }
void __attribute__((weak)) FPU_IRQHandler(void)                 { Default_Handler(); }
void __attribute__((weak)) SPI4_IRQHandler(void)                { Default_Handler(); }
void __attribute__((weak)) SPI5_IRQHandler(void)                { Default_Handler(); }

__attribute__((section(".vectors"), used, aligned(4)))
const uint32_t __Vectors[] = {
    [0]  = (uint32_t)&_stack_end,
    [1]  = (uint32_t)Reset_Handler,
    [2]  = (uint32_t)NMI_Handler,
    [3]  = (uint32_t)HardFault_Handler,
    [4]  = (uint32_t)MemManage_Handler,
    [5]  = (uint32_t)BusFault_Handler,
    [6]  = (uint32_t)UsageFault_Handler,
    [11] = (uint32_t)SVC_Handler,
    [12] = (uint32_t)DebugMon_Handler,
    [14] = (uint32_t)PendSV_Handler,
    [15] = (uint32_t)SysTick_Handler,

    [16 +  0] = (uint32_t)WWDG_IRQHandler,
    [16 +  1] = (uint32_t)PVD_IRQHandler,
    [16 +  2] = (uint32_t)TAMP_STAMP_IRQHandler,
    [16 +  3] = (uint32_t)RTC_WKUP_IRQHandler,
    [16 +  4] = (uint32_t)FLASH_IRQHandler,
    [16 +  5] = (uint32_t)RCC_IRQHandler,
    [16 +  6] = (uint32_t)EXTI0_IRQHandler,
    [16 +  7] = (uint32_t)EXTI1_IRQHandler,
    [16 +  8] = (uint32_t)EXTI2_IRQHandler,
    [16 +  9] = (uint32_t)EXTI3_IRQHandler,
    [16 + 10] = (uint32_t)EXTI4_IRQHandler,
    [16 + 11] = (uint32_t)DMA1_Stream0_IRQHandler,
    [16 + 12] = (uint32_t)DMA1_Stream1_IRQHandler,
    [16 + 13] = (uint32_t)DMA1_Stream2_IRQHandler,
    [16 + 14] = (uint32_t)DMA1_Stream3_IRQHandler,
    [16 + 15] = (uint32_t)DMA1_Stream4_IRQHandler,
    [16 + 16] = (uint32_t)DMA1_Stream5_IRQHandler,
    [16 + 17] = (uint32_t)DMA1_Stream6_IRQHandler,
    [16 + 18] = (uint32_t)ADC_IRQHandler,
    [16 + 23] = (uint32_t)EXTI9_5_IRQHandler,
    [16 + 24] = (uint32_t)TIM1_BRK_TIM9_IRQHandler,
    [16 + 25] = (uint32_t)TIM1_UP_TIM10_IRQHandler,
    [16 + 26] = (uint32_t)TIM1_TRG_COM_TIM11_IRQHandler,
    [16 + 27] = (uint32_t)TIM1_CC_IRQHandler,
    [16 + 28] = (uint32_t)TIM2_IRQHandler,
    [16 + 29] = (uint32_t)TIM3_IRQHandler,
    [16 + 30] = (uint32_t)TIM4_IRQHandler,
    [16 + 31] = (uint32_t)I2C1_EV_IRQHandler,
    [16 + 32] = (uint32_t)I2C1_ER_IRQHandler,
    [16 + 33] = (uint32_t)I2C2_EV_IRQHandler,
    [16 + 34] = (uint32_t)I2C2_ER_IRQHandler,
    [16 + 35] = (uint32_t)SPI1_IRQHandler,
    [16 + 36] = (uint32_t)SPI2_IRQHandler,
    [16 + 37] = (uint32_t)USART1_IRQHandler,
    [16 + 38] = (uint32_t)USART2_IRQHandler,
    [16 + 40] = (uint32_t)EXTI15_10_IRQHandler,
    [16 + 41] = (uint32_t)RTC_Alarm_IRQHandler,
    [16 + 42] = (uint32_t)OTG_FS_WKUP_IRQHandler,
    [16 + 47] = (uint32_t)DMA1_Stream7_IRQHandler,
    [16 + 49] = (uint32_t)SDIO_IRQHandler,
    [16 + 50] = (uint32_t)TIM5_IRQHandler,
    [16 + 51] = (uint32_t)SPI3_IRQHandler,
    [16 + 56] = (uint32_t)DMA2_Stream0_IRQHandler,
    [16 + 57] = (uint32_t)DMA2_Stream1_IRQHandler,
    [16 + 58] = (uint32_t)DMA2_Stream2_IRQHandler,
    [16 + 59] = (uint32_t)DMA2_Stream3_IRQHandler,
    [16 + 60] = (uint32_t)DMA2_Stream4_IRQHandler,
    [16 + 67] = (uint32_t)OTG_FS_IRQHandler,
    [16 + 68] = (uint32_t)DMA2_Stream5_IRQHandler,
    [16 + 69] = (uint32_t)DMA2_Stream6_IRQHandler,
    [16 + 70] = (uint32_t)DMA2_Stream7_IRQHandler,
    [16 + 71] = (uint32_t)USART6_IRQHandler,
    [16 + 72] = (uint32_t)I2C3_EV_IRQHandler,
    [16 + 73] = (uint32_t)I2C3_ER_IRQHandler,
    [16 + 81] = (uint32_t)FPU_IRQHandler,
    [16 + 84] = (uint32_t)SPI4_IRQHandler,
    [16 + 85] = (uint32_t)SPI5_IRQHandler,
};
