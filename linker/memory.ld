/* NUCLEO-L552ZE-Q memory map (STM32L552ZE, TrustZone disabled), from the STM32CubeMX-generated
 * STM32L552ZETXQ_FLASH.ld: the image in flash bank 1, bank 2 kept out as NVM region 0 (dual-bank
 * mode, the option bytes' default). The one board-specific fragment: see default.ld for how the fragments fit together.
 * An application's own linker script may override the sizes below by assigning them again after
 * INCLUDE memory.ld. */

ENTRY(Reset_Handler)

MEMORY
{
  RAM   (xrw) : ORIGIN = 0x20000000, LENGTH = 192K
  RAM2  (xrw) : ORIGIN = 0x20030000, LENGTH = 64K
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 256K  /* bank 1: the image */
  NVM0  (rw)  : ORIGIN = 0x08040000, LENGTH = 256K  /* bank 2: NVM region 0, never loaded (src/bsp_nvm.c) */
}

/* Highest address of the user mode stack */
_estack = ORIGIN(RAM) + LENGTH(RAM);

_Min_Heap_Size = 0x2000;   /* required amount of heap */
_Min_Stack_Size = 0x8000;  /* required amount of stack (CubeMX's 1 KiB is tight for printf) */
