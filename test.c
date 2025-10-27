
/*******************************************************************************
 * CTC test program for ZX Spectrum Next'
 * 2025, by Hannu Viitala
 * Uses MIT license. See LICENSE file for details.
 ******************************************************************************/

 #include <z80.h>
#include <arch/zxn.h>
#include <input.h>
#include <intrinsic.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lib/zxn/zxnext_layer2.h"
#include "lib/zxn/zxnext_sprite.h"

#pragma output CRT_ORG_CODE = 0x6164
#pragma output REGISTER_SP = 0xC000
#pragma output CLIB_MALLOC_HEAP_SIZE = 0
#pragma output CLIB_STDIO_HEAP_SIZE = 0
#pragma output CLIB_FOPEN_MAX = -1
#pragma printf = "%c %s"

__sfr __banked __at 0x183b IO_CTC_0;
__sfr __banked __at 0x193b IO_CTC_1;
__sfr __banked __at 0x1a3b IO_CTC_2;
__sfr __banked __at 0x1b3b IO_CTC_3;

/*
 * Define IDE_FRIENDLY in your C IDE to disable Z88DK C extensions and avoid
 * parser errors/warnings in the IDE. Do NOT define IDE_FRIENDLY when compiling
 * the code with Z88DK.
 */
#ifdef IDE_FRIENDLY
#define __z88dk_fastcall
#define __preserves_regs(...)
#endif

#define printAt(col, row, str) printf("\x16%c%c%s", (col), (row), (str))

/*******************************************************************************
 * Function Prototypes
 ******************************************************************************/

static void init_hardware(void);

static void init_isr(void);

static void test(void);


/*******************************************************************************
 * Variables
 ******************************************************************************/

static uint8_t test_number = 0;

/*******************************************************************************
 * Functions
 ******************************************************************************/

static void init_hardware(void)
{
    // Put Z80 in 28 MHz turbo mode.
    ZXN_NEXTREG(REG_TURBO_MODE, 0x03);

    // Disable RAM memory contention.
    ZXN_NEXTREGA(REG_PERIPHERAL_3, ZXN_READ_REG(REG_PERIPHERAL_3) | RP3_DISABLE_CONTENTION);

    layer2_set_main_screen_ram_bank(8);
    layer2_set_shadow_screen_ram_bank(11);
}

static void init_isr_and_ctc(void)
{
    // Set up IM2 interrupt service routine:
    // Put Z80 in IM2 mode with a 257-byte interrupt vector table located
    // at 0x6000 (before CRT_ORG_CODE) filled with 0x61 bytes. Install an
    // empty interrupt service routine at the interrupt service routine
    // entry at address 0x6161.

    intrinsic_di();
    im2_init((void *) 0x6000);
    memset((void *) 0x6000, 0x61, 257);
    z80_bpoke(0x6161, 0xFB);
    z80_bpoke(0x6162, 0xED);
    z80_bpoke(0x6163, 0x4D);
    
    // *** Z80 CTC port control word bits:
    // bit 7  interrupt
    // bit 6  counter (0: timer)
    // bit 5  256 (0: 16) (only in timer mode)
    // bit 4  rising edge (0:falling)
    // bit 3  pulse starts timer (0: auto)
    // bit 2  time constant follows
    // bit 1  reset
    // bit 0  control
    
    #define CTC_CHANNEL_0_PORT_183B 0x183B
    #define CTC_CHANNEL_1_PORT_193B 0x193B
    #define CTC_CHANNEL_2_PORT_1A3B 0x1A3B
    #define CTC_CHANNEL_3_PORT_1B3B 0x1B3B

    // Init CTC channels 1, 2, and 3 as counters. The counter means that it is counting events from the previous channel.
    // The event happens when the previous counter rolls over to the setup value (0 or 128)
    // Note: using 128 as the initial for bigges end counter so that the value will be correct from the start.
    //       If 0 is used as the initial value, the ctc3 counter will be zero until ctc0 and ctc1 and ctc2 have rolled 
    //       over once. That can be over 10 seconds. 
    
    IO_CTC_1 = 0x47; //%01000111 => counter|time constant follows|reset|control
    IO_CTC_1 = 0; // 0..255
    IO_CTC_2 = 0x47;
    IO_CTC_2 = 0; // 0..255
    IO_CTC_3 = 0x47;
    IO_CTC_3 = 128;  // 1-128

    // Init CTC channel 0 as a timer. Timer means it is counting from the system clock. 
    // Timer means counting from the system clock (28 MHz), but only every 16th clock cycle (that is by design).
    IO_CTC_0 = 0x07; // %00010111 => time constant follows|reset|control
    IO_CTC_0 = 0; // 0..255

    intrinsic_ei();
}

char text[20];
static void test(void)
{
    // *** Read CTC counters
    intrinsic_di(); // Disable interrrupts.
    uint8_t ctc0 = 0;
    while(true)
    {
        // Read the ctc0 counter.
        ctc0 = IO_CTC_0;
        
        // If ctc0 is 5 or smaller it can affect to the other chained counters if it reaches 0 before all ctc conters are read.
        // That would give an incorrect result. To avoid that we wait until ctc0 rolls over to 255.
        // Note that as it waits max 5 ticks. That is (28Mhz/16)*5 = 5.80 us, which is very fast (90 t-states).
        if(ctc0 > 5 )
            break;
    } 
    uint8_t ctc1 = IO_CTC_1;
    uint8_t ctc2 = IO_CTC_2;
    uint8_t ctc3 = IO_CTC_3;
    intrinsic_ei(); // Enable interrupts.

    // *** Print counters
    
    // By default, the layer 2 screen is over the ULA screen.
    // Draw a filled rectangle with the transparency colour in the middle
    // of the layer 2 screen so the underlying ULA screen shows through.
    
    // Clear previous text
    layer2_fill_rect(1*8, 5*8, 230, 16, 0xE3, NULL);
        
    // *** Draw separate counters.
    layer2_draw_text(5, 1, "Counters:", 0x88, NULL);
    itoa(ctc3, text, 10);
    layer2_draw_text(5, 1+10, text, 0x88, NULL);
    itoa(ctc2, text, 10);
    layer2_draw_text(5, 1+14, text, 0x88, NULL);
    itoa(ctc1, text, 10);
    layer2_draw_text(5, 1+18, text, 0x88, NULL);
    itoa(ctc0, text, 10);
    layer2_draw_text(5, 1+22, text, 0x88, NULL);
 
    // *** Calc and draw the milliseconds value.
    // ctc0 counts the 28 MHz / 16 clock ticks.
    uint32_t total_ctc0_ticks = (uint32_t)((((uint32_t)(128-ctc3)) << 24) | (((uint32_t)(255-ctc2)) << 16) | (((uint32_t)(255-ctc1)) << 8) | ((uint32_t)(255-ctc0)));
    uint32_t milliseconds = total_ctc0_ticks / (28000000 / 16 / 1000);
    ltoa(milliseconds, text, 10);
    layer2_draw_text(6, 1, text, 0x88, NULL);
    layer2_draw_text(6, 1+10, "ms", 0x88, NULL);
}

int main(void)
{
    init_hardware();
    init_isr_and_ctc();

    zx_border(INK_WHITE);
    zx_cls(INK_BLACK | PAPER_WHITE);

    //printAt(5,  7, "Press any key to start");
    //in_wait_key();
    
    zx_border(INK_YELLOW);
    zx_cls(INK_BLACK | PAPER_WHITE);
    layer2_configure(true, false, false, 0);
    
    zx_cls(INK_BLACK | PAPER_WHITE);    
    layer2_clear_screen(0x37, NULL);

    uint32_t framecount = 0;
    while (true)
    {
        while (ZXN_READ_REG(REG_ACTIVE_VIDEO_LINE_L) != 200);  // Loop until low byte is 200
        test(); 
        
        //
        ltoa(framecount, text, 10);
        layer2_draw_text(6, 1+24, text, 0x88, NULL);

        framecount++;
    }
}
