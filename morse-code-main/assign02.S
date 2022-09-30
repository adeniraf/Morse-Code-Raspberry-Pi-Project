#include "hardware/regs/addressmap.h"
#include "hardware/regs/io_bank0.h"
#include "hardware/regs/timer.h"
#include "hardware/regs/m0plus.h"

.syntax unified                         @ Specify unified assembly syntax
.cpu    cortex-m0plus                   @ Specify CPU type is Cortex M0+
.thumb                                  @ Specify thumb assembly for RP2040
.global main_asm                        @ Provide program starting address to the linker
.align 4                                @ Specify code alignment

.equ    TIMEOUT_TIME,       9000000     @ Time interval to reset application with watchdog timer (9s)
.equ    END_ENTRY_TIME,     2000000     @ Time interval to indicate that player wants to submit answer (2s)
.equ    BLANK_ENTRY_TIME,   1000000     @ Time interval to indicate that player wants to enter a blank space (1s)
.equ    DASH_ENTRY_TIME,    1000000     @ Time interval to indicate that player wants to enter a dash (1s)

.equ    GPIO_BTN_MSK_F, 0x800000        @ Bit-23 for falling-edge event on GP21
.equ    GPIO_BTN_MSK_R, 0x400000        @ Bit-22 for rising-edge event on GP21
.equ    GPIO_BTN,     21                @ Specify pin for the input button
.equ    GPIO_DIR_IN,   0                @ Specify input direction for a GPIO pin
.equ    GPIO_DIR_OUT,  1                @ Specify output direction for a GPIO pin

.equ    GPIO_ISR_OFFSET, 0x74           @ GPIO is int #13 (vector table entry 29)
.equ    ALRM_ISR_OFFSET, 0x40           @ ALARM0 is int #0 (vector table entry 16)

// r5 - stores time between button press and button release
// r6 - stores time between button presses
// r7 - turns alarm isr on or off

@ Entry point to the ASM portion of the program
main_asm:
    @ Initialise button and install button ISRs. Also initialise registers r5 and r6
    bl      init_btns
    bl      install_gpio_isr
    bl      opening_sequence                    @ Call opening sequence
    ldr     r2, =TIMER_BASE                     
    ldr     r5, [r2, #TIMER_TIMELR_OFFSET]      @ Store current time into r5
    ldr     r6, [r2, #TIMER_TIMELR_OFFSET]      @ Store current time into r6
enter_level: 
    @ Loop to handle prompting of the user to choose a level
    bl      choose_level                        @ Call choose_level subroutine in C              
    cmp     r0, #1                              @ Check if user is finished choosing a level
    bne     enter_level                         @ If not finished, loop again
    
    @ Reset r5 and r6 to curret time
    ldr     r5, [r2, #TIMER_TIMELR_OFFSET]
    ldr     r6, [r2, #TIMER_TIMELR_OFFSET]

    @ Install alarm ISR and initialise watchdog timer
    bl      install_alrm_isr                    @ Installs the timer alarm ISR
    movs    r7, #1                              @ Set r7 to 1 to turn on the alarm ISR in set_alarm
    bl	    init_watchdog_timer
main_loop:
    bl      set_alarm                           @ Set next alarm interrupt
    bl      reset_watchdog_timer                
    wfi                                         @ Wait here until any interrupt fires
    b       main_loop                           @ Always loop

@ Initialises the GP21 Button
init_btns:
    push    {lr}                                @ Push return address (in link register) onto stack

    @ Initialise + set directions
    movs    r0, #GPIO_BTN                       @ Write value of GPIO 21 pin on the PI PICO board into r0
    bl      asm_gpio_init                       @ Call subroutine to initialise this GPIO pin 
    movs    r0, #GPIO_BTN                       @ Write value of GPIO 21 pin on the PI PICO board into r0
    movs    r1, #GPIO_DIR_IN                    @ To set GPIO pin as an input pin 
    bl      asm_gpio_set_dir                    @ Call subroutine to set this GPIO pin to an input pin as specified by r1
    
    @ Initialise falling + rising edge interrupts for GP21
    movs    r0, #GPIO_BTN
    bl      asm_gpio_set_irq_fall               @ Call subroutine to initialise falling edge interrupts
    movs    r0, #GPIO_BTN
    bl      asm_gpio_set_irq_rise               @ Call subroutine to initialise rising edge interrupts
    
    pop {pc}                                    @ Exit subroutine

@ Install the Alarm ISR
install_alrm_isr:
    ldr     r2, =(PPB_BASE + M0PLUS_VTOR_OFFSET)    @ Get the address of the RAM vector table register
    ldr     r1, [r2]
    movs    r2, #ALRM_ISR_OFFSET                    @ Add ALARM0 ISR offset 
    add     r2, r1
    ldr     r0, =alrm_isr
    str     r0, [r2]                                @ Store address of alrm_isr handler to the correct offset for ALARM0 in the vector table

    movs    r0, #1
    ldr     r2, =PPB_BASE
    ldr     r1, =M0PLUS_NVIC_ICPR_OFFSET
    add     r1, r2
    str     r0, [r1]                                @ Disable the ALARM0 IRQ by writing to (PPB_BASE + M0PLUS_NVIC_ICPR_OFFSET)
    ldr     r1, =M0PLUS_NVIC_ISER_OFFSET
    add     r1, r2
    str     r0, [r1]                                @ Enable the ALARM0 IRQ by writing to (PPB_BASE + M0PLUS_NVIC_ISER_OFFSET)

    bx      lr                                      @ Exit subroutine

@ Enables/Disables the alarm and sets time it should fire off 
set_alarm:
    @ Enable/Disable alarm timer interrupts with the (TIMER_BASE + TIMER_INTE_OFFSET) register
    ldr     r2, =TIMER_BASE
    movs    r0, r7                              @ Enables alarm if r7 = 1, disables if r7 = 0
    str     r0, [r2, #TIMER_INTE_OFFSET]

    @ Updates time the alarm must next fire off
    ldr     r1, [r2, #TIMER_TIMELR_OFFSET]      @ Retrieve current timer count from (TIMER_BASE + TIMER_TIMELR_OFFSET) register
    ldr     r0, =END_ENTRY_TIME                
    add     r1, r0                              @ Add 2s to the current timer count
    str     r1, [r2, #TIMER_ALARM0_OFFSET]      @ Push updated time value to (TIMER_BASE + TIMER_ALARM0_OFFSET) register
    
    bx      lr                                  @ Exit subroutine

@ Install the Button ISR
install_gpio_isr:
    ldr     r2, =PPB_BASE
    ldr     r1, =M0PLUS_VTOR_OFFSET
    add     r2, r1
    ldr     r1, [r2]                            @ Get address of the RAM vector table using the (PPB_BASE + M0PLUS_VTOR_OFFSET) register
    movs    r2, #GPIO_ISR_OFFSET
    add     r2, r1
    ldr     r0, =gpio_isr
    str     r0, [r2]                            @ Store address of gpio_isr handler to the correct offset for GPIO in the vector table

    ldr     r0, =#0x2000
    ldr     r2, =PPB_BASE
    ldr     r1, =M0PLUS_NVIC_ICPR_OFFSET        @ Disable the GPIO IRQ by writing to (PPB_BASE + M0PLUS_NVIC_ICPR_OFFSET) register
    add     r1, r2
    str     r0, [r1]
    ldr     r1, =M0PLUS_NVIC_ISER_OFFSET        @ Enable the GPIO IRQ by writing to (PPB_BASE + M0PLUS_NVIC_ISER_OFFSET) register
    add     r1, r2
    str     r0, [r1]

    bx  lr                                      @ Exit subroutine

@ Service the pending interrupt from the ALARM0 TIMER - Submit answer player has put in
.thumb_func
alrm_isr:
    push    {lr}                                @ Push return address (in link register) onto stack

    movs    r7, #0
    bl      set_alarm                           @ Disable the alarm ISR

    @ Disable pending interrupt from TIMER 
    ldr     r2, =(TIMER_BASE + TIMER_INTR_OFFSET)
    movs    r1, #1
    str     r1, [r2]
    
    bl      check_answer                        @ Check if player's answer is correct
    bl      check_if_game_complete              @ Check if the game is over/completed
    cmp     r0, #1                              
    beq     game_complete_loop                  @ If game is complete, branch to game_complete_loop


    pop     {pc}                                @ Exit ISR

@ Service the pending interrupt from the button 
.thumb_func
gpio_isr:
    push {lr}                                                   @ Store the link register to the stack 
    
    bl      check_if_game_complete                              @ Check if game is over/complete
    cmp     r0, #1
    beq     finish                                              @ If game is over/complete, don't do anything

    ldr     r2, =(IO_BANK0_BASE + IO_BANK0_PROC0_INTS2_OFFSET)
    ldr     r1, [r2]                                            @ Read interrupt status event from (IO_BANK0_BASE + IO_BANK0_PROC0_INTS2_OFFSET) register

    @ Button GP 21 Pushed
    ldr     r0, =GPIO_BTN_MSK_R                                 @ Load the mask for the GPIO 21 rising edge interrupt
    cmp     r1, r0                                              @ Check if button has just been pressed
    beq     button_pressed                                      

    @ Button GP 21 Released
    ldr     r0, =GPIO_BTN_MSK_F                                 @ Load the mask for the GPIO 21 falling edge interrupt
    cmp     r1, r0                                              @ Check if button has just been released
    beq     button_released

finish:
    @ Disable the pending interrupt from GPIO by writing to (IO_BANK0_BASE + IO_BANK0_INTR2_OFFSET)
    ldr     r2, =(IO_BANK0_BASE + IO_BANK0_INTR2_OFFSET)
    str     r4, [r2]                                            
    
    @ Disable any pending interrupts from the Alarm
    ldr     r2, =(TIMER_BASE + TIMER_INTR_OFFSET)               
    movs    r1, #1
    str     r1, [r2]
    bl      set_alarm                                           @ Turn alarm ISR on/off

    pop     {pc}                                                @ Exit ISR

@ Handles button press event
button_pressed:
    movs    r7, #0
    bl      set_alarm                                           @ Turn off alarm so it does not fire accidentally during subroutine

    ldr     r4, =GPIO_BTN_MSK_R                                 @ Load the mask for the GPIO 21 rising edge interrupt
    
    ldr     r2, =TIMER_BASE
    ldr     r5, [r2, #TIMER_TIMELR_OFFSET]                      @ Reset r5 (time between button press and release) to current time

    ldr     r0, [r2, #TIMER_TIMELR_OFFSET]                      @ Get the current timer count from (TIMER_BASE + TIMER_TIMELR_OFFSET) register
    movs    r1, #0
    add     r1, r6
    bl      calculate_duration                                  @ Calculate time between last button release and now
    ldr     r1, =BLANK_ENTRY_TIME                               @ Check if player wants to input a blank space
    cmp     r0, r1
    ble     finish                                              @ If time interval < 1s, do not enter a space

    @ Time interval > 1s, enter a space character
    movs    r0, #3                                          
    bl      add_input                                           @ Add a blank space to player_input
    b       finish

@ Handles button release event
button_released:
    movs    r7, #1                                               @ To re-enable alarm ISR later

    ldr     r4, =GPIO_BTN_MSK_F                                  @ Load the mask for the GPIO 21 falling edge interrupt
    ldr     r2, =TIMER_BASE
    ldr     r0, [r2, #TIMER_TIMELR_OFFSET]                       @ Get the current timer count from (TIMER_BASE + TIMER_TIMELR_OFFSET) register
    movs    r1, #0
    add     r1, r5                                               @ Get captured time at last button press
    bl      calculate_duration                                   @ Calculate time between last button press and now, when it has been released
    ldr     r1, =DASH_ENTRY_TIME                                 @ Check if user wants to enter a dash
    cmp     r0, r1
    ble     input_dot                                            @ Button pressed for <= 1s, player wants to enter a dot

    @ Button pressed for > 1s, player wants to enter a dash
    movs    r0, #1
    bl      add_input                                            @ Add a dash to player_input
    ldr     r2, =TIMER_BASE
    ldr     r6, [r2, #TIMER_TIMELR_OFFSET]                       @ Reset r6 (time between button presses) to current time
    b       finish

@ Handles inputting a dot to player input
input_dot:
    movs    r0, #2
    bl      add_input                                           @ Add a dot to player_input
    ldr     r2, =TIMER_BASE
    ldr     r6, [r2, #TIMER_TIMELR_OFFSET]                      @ Reset r6 (time between button presses) to current time
    b       finish

@ Called when game is over/complete
game_complete_loop:
    movs    r7, #0
    bl      set_alarm                                           @ Disable the alarm ISR
    b       game_complete_loop                                  @ Loop forever until watchdog timer resets application


@ Set data alignment
.data
    .align 4
