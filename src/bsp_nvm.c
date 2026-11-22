/* NVM region 0: flash bank 2, which the image never occupies (linker/memory.ld).
 *
 * The erase and program sequences are lean-ftl's target/stm32/stm32l5-accessors.c (same author,
 * Apache-2.0), bounded to the region so that the application cannot erase its own code. They go
 * straight to the flash controller (RM0438 chapter 6), interrupts masked for the whole operation:
 *  - erase: page by page (2 KiB in dual-bank mode, DBANK=1, the option bytes' default), the bank
 *    chosen from the address, SWAP_BANK taken into account;
 *  - write: double-words (8 bytes, the program unit and the ECC word), from any source alignment,
 *    then read back;
 *  - the instruction cache is off during each erase and program: it treats them as errors;
 *  - the error flags are cleared before each operation and checked after it, so that a refused
 *    operation is reported once, to the caller, and poisons nothing (RM0438 6.3.6 to 6.3.8).
 * Status: BSP_NVM_OK; 1, 2 outside the region; 3 misaligned address; 4 size not a multiple of the
 * program unit; 5 programmed without error but read back different; BSP_NVM_REFUSED the flash
 * refused or aborted the operation; 7 the flash is in single-bank mode, where bank 2 does not
 * exist. */
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "main.h"
#include "bsp_api.h"
#include "bsp_nvm.h"

/* Bank 2 of the STM32L552ZE's 512 KiB in dual-bank mode, as constants: the CMSIS FLASH_SIZE reads
 * the flash size from the chip at run time, which a constant table cannot use. linker/memory.ld
 * agrees. */
#define BANK_SIZE  0x40000u
#define PAGE_SIZE  0x800u
#define NVM0_BASE  (0x08000000u + BANK_SIZE)
#define NVM0_SIZE  BANK_SIZE
#define NVM0_WRITE_SIZE 8u /* a double-word */

#define FLASH_KEY1 0x45670123U
#define FLASH_KEY2 0xCDEF89ABU

#define NVM_SR_ERRORS (FLASH_NSSR_NSOPERR | FLASH_NSSR_NSPROGERR | FLASH_NSSR_NSWRPERR | FLASH_NSSR_NSPGAERR | \
                       FLASH_NSSR_NSSIZERR | FLASH_NSSR_NSPGSERR | FLASH_NSSR_OPTWERR)

static inline void clear_flags(void) { WRITE_REG(FLASH_NS->NSSR, NVM_SR_ERRORS | FLASH_NSSR_NSEOP); }

static inline uint32_t errors(void) { return READ_BIT(FLASH_NS->NSSR, NVM_SR_ERRORS); }

static inline bool in_region(uintptr_t addr, uintptr_t size) {
    return addr >= NVM0_BASE && size <= NVM0_SIZE && addr - NVM0_BASE <= NVM0_SIZE - size;
}

static bool dual_bank(void) { return READ_BIT(FLASH->OPTR, FLASH_OPTR_DBANK) != 0u; }

static bool bank_swapped(void) { return READ_BIT(FLASH->OPTR, FLASH_OPTR_SWAP_BANK) != 0u; }

static bool icache_off(void) {
    if (!READ_BIT(ICACHE->CR, ICACHE_CR_EN)) return false;
    WRITE_REG(ICACHE->FCR, ICACHE_FCR_CBSYENDF);
    CLEAR_BIT(ICACHE->CR, ICACHE_CR_EN);
    while (READ_BIT(ICACHE->CR, ICACHE_CR_EN)) {
    }
    return true;
}

static void icache_back(bool was_on) {
    if (was_on) SET_BIT(ICACHE->CR, ICACHE_CR_EN);
}

static void unlock(void) {
    WRITE_REG(FLASH->NSKEYR, FLASH_KEY1);
    WRITE_REG(FLASH->NSKEYR, FLASH_KEY2);
}

static void lock(void) { SET_BIT(FLASH->NSCR, FLASH_NSCR_NSLOCK); }

/* In RAM, as in lean-ftl, whatever the bank the code runs from. */
static void __attribute__((section(".RamFunc"))) erase_page(uintptr_t page_addr) {
    const uintptr_t bank_addr = page_addr & ~((uintptr_t)BANK_SIZE - 1u);
    const uint32_t bank = (uint32_t)((bank_addr - 0x08000000u) / BANK_SIZE);
    const uint32_t page = (uint32_t)((page_addr - bank_addr) / PAGE_SIZE);
    if ((bank ^ (uint32_t)bank_swapped()) != 0u) SET_BIT(FLASH_NS->NSCR, FLASH_NSCR_NSBKER);
    else CLEAR_BIT(FLASH_NS->NSCR, FLASH_NSCR_NSBKER);
    MODIFY_REG(FLASH_NS->NSCR, FLASH_NSCR_NSPNB | FLASH_NSCR_NSPER, (page << FLASH_NSCR_NSPNB_Pos) | FLASH_NSCR_NSPER);
    const bool icache = icache_off();
    SET_BIT(FLASH_NS->NSCR, FLASH_NSCR_NSSTRT);
    while (READ_BIT(FLASH_NS->NSSR, FLASH_NSSR_NSBSY)) {
    }
    WRITE_REG(FLASH_NS->NSCR, 0);
    icache_back(icache);
}

/* `n` double-words; the source may be misaligned, or in the flash itself. */
static void __attribute__((section(".RamFunc"))) program(volatile uint32_t *dst, const uint8_t *src, uint32_t n) {
    const bool icache = icache_off();
    WRITE_REG(FLASH_NS->NSCR, FLASH_NSCR_NSPG);
    for (uint32_t i = 0; i < n; i++) {
        uint32_t pair[2];
        memcpy(pair, src, sizeof pair);
        src += sizeof pair;
        dst[0] = pair[0];
        dst[1] = pair[1];
        dst += 2;
        while (READ_BIT(FLASH_NS->NSSR, FLASH_NSSR_NSBSY)) {
        }
    }
    WRITE_REG(FLASH_NS->NSCR, 0);
    icache_back(icache);
}

static uint8_t nvm0_erase(void *base_address, unsigned n_pages) {
    if (n_pages == 0) return BSP_NVM_OK;
    const uintptr_t base = (uintptr_t)base_address;
    if ((uintptr_t)n_pages > NVM0_SIZE / PAGE_SIZE) return 2;
    if (!in_region(base, (uintptr_t)n_pages * PAGE_SIZE)) return base < NVM0_BASE ? 1 : 2;
    if (base % PAGE_SIZE) return 3;
    if (!dual_bank()) return 7;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    unlock();
    clear_flags();
    for (unsigned i = 0; i < n_pages; i++) erase_page(base + (uintptr_t)i * PAGE_SIZE);
    const uint32_t failed = errors();
    clear_flags();
    lock();
    __set_PRIMASK(primask);
    return failed ? BSP_NVM_REFUSED : BSP_NVM_OK;
}

static uint8_t nvm0_write(void *dst, const void *src, uintptr_t size) {
    if (size == 0) return BSP_NVM_OK;
    if (!in_region((uintptr_t)dst, size)) return (uintptr_t)dst < NVM0_BASE ? 1 : 2;
    if ((uintptr_t)dst % NVM0_WRITE_SIZE) return 3;
    if (size % NVM0_WRITE_SIZE) return 4;
    if (!dual_bank()) return 7;
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    unlock();
    clear_flags();
    program((volatile uint32_t *)dst, (const uint8_t *)src, (uint32_t)(size / NVM0_WRITE_SIZE));
    const uint32_t failed = errors();
    clear_flags();
    lock();
    const uint8_t status = failed ? BSP_NVM_REFUSED : (memcmp(dst, src, size) ? 5 : BSP_NVM_OK);
    __set_PRIMASK(primask);
    return status;
}

static uint8_t nvm0_read(void *dst, const void *src, uintptr_t size) {
    if (!in_region((uintptr_t)src, size)) return (uintptr_t)src < NVM0_BASE ? 1 : 2;
    memcpy(dst, src, size);
    return BSP_NVM_OK;
}

const bsp_nvm_region_t bsp_nvm_regions[BSP_N_NVM_REGIONS] = {
    {
        .name = "bank2",
        .base_address = (void *)NVM0_BASE,
        .size = NVM0_SIZE,
        .page_size = PAGE_SIZE,
        .write_size = NVM0_WRITE_SIZE,
        .zero_overwrite = true, /* an all-zero double-word may be programmed over a programmed one */
        .erase = nvm0_erase,
        .write = nvm0_write,
        .read = nvm0_read,
    },
};
