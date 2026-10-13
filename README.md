# lean-bsp-nucleo-l552

The [lean-bsp](https://github.com/sebastien-riou/lean-bsp) BSP for the ST NUCLEO-L552ZE-Q board
(STM32L552ZE, Cortex-M33 at 110 MHz, TrustZone disabled). Any lean-bsp application builds and runs
on it:

````
lbsp build-load-run --bsp nucleo-l552 --until 'hello-world done' --timeout 60
````

## What it needs

- The arm-none-eabi toolchain. `CROSS_COMPILE` selects it, e.g.
  `CROSS_COMPILE=/opt/toolchains/arm-none-eabi-gcc/xpack-arm-none-eabi-gcc-14.2.1-1.1/bin/arm-none-eabi-`;
  without it, `arm-none-eabi-gcc` is taken from `PATH`.
- For `load` and `run`, [lean-hwbridge](https://github.com/sebastien-riou/lean-hwbridge) serving the
  board: `hwbridge` on `PATH`, or the command in `HWBRIDGE` (e.g. `HWBRIDGE="python3 -m hwbridge"`).
  The board is its usbman nickname, `HWBRIDGE_BOARD`, by default `nucleo-l552`. Give it
  `swap_bank_reset = true` in the bridge's config, so a firmware that swapped the flash banks (as
  lean-ftl's tests do) cannot leave the next one booting from the wrong bank. Without a bridge,
  `load` prints the STM32CubeProgrammer command to flash the board by hand.

## How the contract maps to the board

| Primitive | On NUCLEO-L552ZE-Q |
|---|---|
| UART 0 (and `printf`, `stdin`) | USART3 (PD8/PD9), the ST-LINK virtual COM port with solder bridges SB124/SB126 on and SB127/SB129 off, 115200 8N1; see below. Reception is polled: bytes arriving while nobody reads can overrun. |
| LEDs | 0 green (LD1), 1 blue (LD2), 2 red (LD3) |
| Button | 0 is the blue user button |
| Cycle counter, CPU frequency | DWT `CYCCNT`, at `SystemCoreClock` |
| Delay | busy loop on the cycle counter: works with interrupts masked |
| Cache clear | the instruction cache invalidated (the STM32L5 has no data cache) |
| Heap usage | growth of the heap end since the last reset, tracked in `_sbrk` (`Core/Src/sysmem.c`) |
| Reset | `NVIC_SystemReset()` |
| `app_tick` | from `SysTick_Handler`, every millisecond |
| `app_on_fault` | from the HardFault, MemManage, BusFault and UsageFault handlers (`pc` is 0) |
| `app_on_exit` | default does nothing: `main()` then parks the CPU |
| Flash/NVM | not yet |

## Layout

The board support is an STM32CubeMX project (`nucleo-l552.ioc`), taken from
[lean-ftl-stm32l5](https://github.com/sebastien-riou/lean-ftl-stm32l5) and kept regeneration-safe:
lean-bsp code is only in `USER CODE` sections of the generated files (the glue in `main()`,
`app_tick` and `app_on_fault` in `stm32l5xx_it.c`) and in files CubeMX does not generate. The
project itself is lean-ftl-stm32l5's, renamed: `bsp_init()` sets up UART 0 (below) and makes the
user button pin an input (the project gives it to the wakeup function). The project still
initialises the ADC, RTC, UCPD, USB and LPUART1 peripherals as lean-ftl-stm32l5 did; nothing uses
them.

### Which UART reaches the ST-LINK

A NUCLEO-L552ZE-Q as it ships routes LPUART1 (PG7/PG8) to the ST-LINK virtual COM port
(UM2581, table 10: SB127/SB129 on). The board this BSP is run on has its solder bridges set the other
way, routing USART3 (PD8/PD9) there (table 11: SB124/SB126 on, SB127/SB129 off), and UART 0 follows
that board. On a board as it ships, either move those solder bridges or change `uart0_init()` in
`src/bsp_nucleo_l552.c` to LPUART1.

- `Core/`, `Drivers/`: the vendor-generated bring-up (HAL and LL drivers, CMSIS; device headers for
  this part only).
- `src/bsp_nucleo_l552.c`: the primitives, `bsp_init()` and the default application hooks.
- `include/bsp_api.h`: the contract header, identical to lean-bsp's canonical copy (`lbsp check` verifies it).
- `linker/`: `memory.ld` (this board's memory map, with the whole 512 KiB of flash), and the Cortex-M
  `sections_head/data/tail.ld` and `default.ld`, identical to the other STM32 lean-bsp BSPs'. An
  application needing its own sections copies `default.ld`.
- `bsp.toml`, `bsp_cli.py`: what lean-bsp reads and runs.

To open it in STM32CubeIDE, regenerate an IDE project from `nucleo-l552.ioc` with STM32CubeMX.
