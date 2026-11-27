#!/usr/bin/env python3
"""lean-bsp entry point of the NUCLEO-L552ZE-Q BSP, made of lean-bsp's board helpers (leanbsp.backends).

build compiles the vendor-generated board support (Core/, Drivers/), src/ and the application with
the arm-none-eabi toolchain named by CROSS_COMPILE (default: arm-none-eabi- from PATH), with the
CPU flags of bsp.toml, and links with linker/default.ld (or the application's own script). load
and run go through lean-hwbridge; the board is the usbman nickname in HWBRIDGE_BOARD, default
nucleo-l552.
"""

import json
import sys
from pathlib import Path

try:
    from leanbsp.backends import board, gcc
except ImportError:
    print(json.dumps({'status': 'error', 'message': 'this BSP needs a lean-bsp with leanbsp.backends: update lean-bsp'}))
    sys.exit(1)

ROOT = Path(__file__).resolve().parent

SOURCES = [
    *sorted((ROOT / 'Core' / 'Src').glob('*.c')),
    *sorted((ROOT / 'Drivers' / 'STM32L5xx_HAL_Driver' / 'Src').glob('*.c')),  # only what CubeMX copied: all used
    *sorted((ROOT / 'src').glob('*.c')),
    ROOT / 'Core' / 'Startup' / 'startup_stm32l552zetxq.s',
]
INCLUDE_DIRS = [
    ROOT / 'include',
    ROOT / 'Core' / 'Inc',
    ROOT / 'Drivers' / 'STM32L5xx_HAL_Driver' / 'Inc',
    ROOT / 'Drivers' / 'STM32L5xx_HAL_Driver' / 'Inc' / 'Legacy',
    ROOT / 'Drivers' / 'CMSIS' / 'Device' / 'ST' / 'STM32L5xx' / 'Include',
    ROOT / 'Drivers' / 'CMSIS' / 'Include',
]
DEFINES = ['USE_HAL_DRIVER', 'USE_FULL_LL_DRIVER', 'STM32L552xx']


def build(app: dict, target, bdir: Path) -> dict:
    return gcc.build(app, target, bdir, root=ROOT, sources=SOURCES, include_dirs=INCLUDE_DIRS, defines=DEFINES,
                     linker_script=ROOT / 'linker' / 'default.ld', link_flags=[f'-L{ROOT / "linker"}', *gcc.HEAP_WRAP_FLAGS])


BOARD = board.Board(
    root=ROOT,
    title='NUCLEO-L552ZE-Q',
    flash_by_hand='STM32_Programmer_CLI --connect port=swd mode=UR -ob SWAP_BANK=0 --erase all --download {hex} -rst',
    run_by_hand='Press the black RESET button and read the ST-LINK virtual COM port',
)

if __name__ == '__main__':
    sys.exit(board.main(BOARD, build))
