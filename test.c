
#include <arch/zxn.h>
#include <input.h>
#include <z80.h>
#include <intrinsic.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>     // outp(), inp()
#include <string.h>
#include <intrinsic.h>

#include "lib/zxn/zxnext_layer2.h"
#include "lib/zxn/zxnext_sprite.h"

#pragma output CRT_ORG_CODE = 0x6164
#pragma output REGISTER_SP = 0xC000
#pragma output CLIB_MALLOC_HEAP_SIZE = 0
#pragma output CLIB_STDIO_HEAP_SIZE = 0
#pragma output CLIB_FOPEN_MAX = -1
#pragma printf = "%c %s"

// ---------- CTC ports on the Next (channels 0..3) ----------
#define CTC_CH0 0x183B
#define CTC_CH1 0x193B
#define CTC_CH2 0x1A3B
#define CTC_CH3 0x1B3B

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
uint32_t g_ticks = 0;

/*******************************************************************************
 * Functions
 ******************************************************************************/

// static void enable_ctc_ports(void) {
//   // Many cores have CTC IO enabled by default; on some configs it's gated by a NextReg bit.
//   // If your core has that gate, it's typically in the “peripheral enables” register (write-then-OR bit).
//   // We read, OR the bit, and write back. If your core ignores this, it’s harmless.
//   const unsigned char REG_PERIPH_EN = 0x08;      // common location for misc HW enables
//   unsigned char v = ZXN_READ_REG(REG_PERIPH_EN);
//   v |= (1u << 3);                                 // bit often used to enable CTC IO ports
//   ZXN_WRITE_REG(REG_PERIPH_EN, v);
//   (void)v;
//   // (If you find this unnecessary on your core, you can remove this whole function.)
// }

// Make a safe C ISR and install it on the vector we’ll program into the CTC
IM2_DEFINE_ISR(timer_isr)
{
    ++g_ticks;
    // simple visual: toggle border
    __asm
        ld a,(0x5C8D)
        inc a
        and 7
        out (0xFE),a
    __endasm;
}

// ---------- CTC helpers ----------
/*
   Z80 CTC control word (datasheet):
   D7: Interrupt enable (1=enable)
   D6: Mode select      (0=timer, 1=counter)
   D5: Prescaler (timer mode) (0=÷16, 1=÷256)
   D4: Trigger edge select (timer: 0=falling, 1=rising)
   D3: Timer starts on next T state after write (timer mode)
   D2: Time-constant follows (1=next write is TC)
   D1: Software reset (1=reset this channel)
   D0: Must be 1 (this is a control word)
   (See datasheet page “Programming / Operation mode select”.) :contentReference[oaicite:1]{index=1}
*/
static void ctc_write(unsigned short port, unsigned char value) 
{
    z80_outp(port, value);
}

// Program the device-wide IM2 vector base (write vector byte to CH0).
static void ctc_program_vector_base(unsigned char vector_base_even) {
  // The CTC’s vector is written to channel 0's port; D0 of the vector is always 0,
  // and channels use base+(channel*2) as their identifiers. :contentReference[oaicite:2]{index=2}
  ctc_write(CTC_CH0, vector_base_even & 0xFE);
}

// Program CTC channel 0 as a timer that interrupts at `freq_hz`
// clocked from the CPU clock with prescaler ÷256.
static void ctc_ch0_timer_start(unsigned long cpu_hz, unsigned int freq_hz) {
  unsigned char control = (unsigned char)
  (
      (1<<7) |       // enable interrupts
      (0<<6) |       // timer mode
      (1<<5) |       // prescaler ÷256 (use 0 for ÷16)
      (1<<4) |       // start on rising edge
      (1<<3) |       // start on next T-state
      (1<<2) |       // time-constant follows
      (1<<1) |       // software reset
      (1<<0)        // this is a control word
  );

  // TC = cpu_hz / (prescaler * freq)
  uint16_t tc = (cpu_hz / 256UL) / (unsigned long)freq_hz;
  if (tc == 0) tc = 1;                 // 0 means 256 on CTC
  if (tc > 255) tc = 255;

  ctc_write(CTC_CH0, control);
  ctc_write(CTC_CH0, (unsigned char)tc);
}

static void init_hardware(void)
{
    // Put Z80 in 28 MHz turbo mode.
    ZXN_NEXTREG(REG_TURBO_MODE, 0x03);

    // Disable RAM memory contention.
    ZXN_NEXTREGA(REG_PERIPHERAL_3, ZXN_READ_REG(REG_PERIPHERAL_3) | RP3_DISABLE_CONTENTION);

    layer2_set_main_screen_ram_bank(8);
    layer2_set_shadow_screen_ram_bank(11);
}

static void init_isr(void)
{
    // Set up IM2 interrupt service routine:
    // Put Z80 in IM2 mode with a 257-byte interrupt vector table located
    // at 0x6000 (before CRT_ORG_CODE) filled with 0x61 bytes. Install an
    // empty interrupt service routine at the interrupt service routine
    // entry at address 0x6161.

    intrinsic_di();
    
    im2_init((void *) 0x6000);
    memset((void *) 0x6000, 0x61, 257);
    //z80_bpoke(0x6161, 0xFB);
    //z80_bpoke(0x6162, 0xED);
    //z80_bpoke(0x6163, 0x4D);
    z80_bpoke(0x6161, 0xc3);                // z80 JP instruction
    z80_wpoke(0x6162, (unsigned int)timer_isr);   // to the isr routine
    
    
    // // Choose a vector base (even). We’ll use 0x40.
    // // On the CTC, channel n then asserts vector = base + 2*n. (datasheet) :contentReference[oaicite:4]{index=4}
    // const unsigned char VECTOR_BASE = 0x40;
    // ctc_program_vector_base(VECTOR_BASE);

    // // Install our ISR on channel 0’s vector (base + 0)
    // im2_InstallISR(VECTOR_BASE, timer_isr);         // z88dk IM2 API :contentReference[oaicite:5]{index=5}

    // --- Start CTC0 as a ~1 kHz timer ---
    // NOTE: The CTC is clocked from the CPU clock. If your Next is at 3.5MHz, use 3_500_000.
    // If 14MHz turbo, use 14_000_000, etc. Adjust freq_hz as you wish.
    const unsigned long CPU_HZ = 28000000UL;        // change if you’re at 3.5/7/28 MHz
    ctc_ch0_timer_start(CPU_HZ, 1000);
    
    intrinsic_ei();
}

// // Init interrupt.
// static void init_isr(void)
// {
//     // Set up IM2 interrupt service routine:
//     // Put Z80 in IM2 mode with a 257-byte interrupt vector table located
//     // at 0x8000 (before CRT_ORG_CODE) filled with 0x81 bytes. Install an
//     // empty interrupt service routine at the interrupt service routine
//     // entry at address 0x8181 .

//     intrinsic_di();
//     im2_init((void *) 0x8000);
//     memset((void *) 0x8000, 0x81, 257);

//     z80_bpoke(0x8181, 0xc3);                // z80 JP instruction
//     z80_wpoke(0x8182, (unsigned int)isr);   // to the isr routine

//     intrinsic_ei();
// }



static void test(void)
{
    // By default, the layer 2 screen is over the ULA screen.
    // Draw a filled rectangle with the transparency colour in the middle
    // of the layer 2 screen so the underlying ULA screen shows through.

    zx_cls(INK_BLACK | PAPER_WHITE);
    printAt(11, 12, "ULA SCREEN");

    layer2_clear_screen(0x37, NULL);
    layer2_draw_text(5, 9, "LAYER 2 SCREEN", 0x88, NULL);
    layer2_fill_rect(64, 80, 128, 40, 0xE3, NULL);

    in_wait_key();
    // Reset to default settings.
    zx_cls(INK_BLACK | PAPER_WHITE);
}

char text[128];
int main(void)
{
    init_hardware();
    init_isr();

    // zx_border(INK_WHITE);
    // zx_cls(INK_BLACK | PAPER_WHITE);

    // printAt(5,  7, "Press any key to start");
    // in_wait_key();
    
    zx_border(INK_YELLOW);
    zx_cls(INK_BLACK | PAPER_WHITE);
    layer2_configure(true, false, false, 0);
    
    uint32_t last = 0;
    while (1) 
    {
        if (g_ticks - last >= 50) 
        {        // ~20 Hz message
            
            // Clear previous text
            layer2_fill_rect(9*8, 5*8, 100, 8, 0xE3, NULL);
            
            last = g_ticks;
            // Put your app logic here; keep it light.
            ltoa(g_ticks, text, 10);
            strcat(text, " ms");
            layer2_draw_text(5, 9, text, 0x88, NULL);
        }
    }

    // while (true)
    // {
    //     if (in_inkey())
    //     {
    //         in_wait_nokey();
    //         test();
    //     }
    // }
}
