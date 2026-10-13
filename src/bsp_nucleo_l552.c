/* NUCLEO-L552ZE-Q: the lean-bsp contract on top of the STM32L5 HAL.
 *
 * The vendor-generated main() (Core/Src/main.c) brings the board up and initialises the LED pins
 * and the instruction cache; its USER CODE 2 section then calls bsp_init() and the application.
 * Heap tracking lives in Core/Src/sysmem.c, next to _sbrk.
 *
 * UART 0 is USART3 (PD8/PD9), set up here rather than in the CubeMX project: on the board this BSP
 * runs on, solder bridges SB124/SB126 route USART3 to the ST-LINK virtual COM port. A board as it
 * ships routes LPUART1 (PG7/PG8) there instead (UM2581, table 10), which the generated code still
 * initialises, unused. */
#include "main.h"
#include "bsp_api.h"
#include "bsp_board.h"

static UART_HandleTypeDef uart0;  /* USART3 */

static const bsp_capabilities_t capabilities = {
    .board_name = "nucleo-l552",
    .n_uarts = 1,
    .n_leds = 3,
    .n_buttons = 1,
    .has_cycle_counter = true,
    .n_nvm_regions = 0,
    .nvm_regions = NULL,
};

const bsp_capabilities_t *bsp_get_capabilities(void) { return &capabilities; }

/* ---- the application hooks' defaults ---- */

__attribute__((weak)) void app_init(void) {}

__attribute__((weak)) void app_tick(uint32_t tick_ms) { (void)tick_ms; }

__attribute__((weak)) void app_on_fault(uint32_t reason, uintptr_t pc) {
    (void)reason;
    (void)pc;
}

__attribute__((weak)) void app_on_exit(int code) { (void)code; }  /* main() then parks the CPU */

static void uart0_init(void) {
    __HAL_RCC_USART3_CLK_ENABLE();  /* clocked by PCLK1, the reset default */
    __HAL_RCC_GPIOD_CLK_ENABLE();
    GPIO_InitTypeDef pins = {.Pin = GPIO_PIN_8 | GPIO_PIN_9, .Mode = GPIO_MODE_AF_PP, .Pull = GPIO_NOPULL,
                             .Speed = GPIO_SPEED_FREQ_LOW, .Alternate = GPIO_AF7_USART3};
    HAL_GPIO_Init(GPIOD, &pins);
    uart0.Instance = USART3;
    uart0.Init.BaudRate = 115200;
    uart0.Init.WordLength = UART_WORDLENGTH_8B;
    uart0.Init.StopBits = UART_STOPBITS_1;
    uart0.Init.Parity = UART_PARITY_NONE;
    uart0.Init.Mode = UART_MODE_TX_RX;
    uart0.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    uart0.Init.OverSampling = UART_OVERSAMPLING_16;
    uart0.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    uart0.Init.ClockPrescaler = UART_PRESCALER_DIV1;
    uart0.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    if (HAL_UART_Init(&uart0) != HAL_OK) Error_Handler();  /* the FIFO stays off, as CubeMX leaves it */
}

void bsp_init(void) {
    uart0_init();
    /* The project gives PC13 to the wakeup function, so the generated code leaves it an analog
     * pin, which always reads 0: make it the input it is on this board. */
    GPIO_InitTypeDef button = {.Pin = USER_BUTTON_Pin, .Mode = GPIO_MODE_INPUT, .Pull = GPIO_NOPULL};
    HAL_GPIO_Init(USER_BUTTON_GPIO_Port, &button);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;  /* the cycle counter */
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    bsp_heap_usage_reset();
}

/* ---- UART 0: USART3, the ST-LINK virtual COM port on this board; stdin and stdout too ---- */

void bsp_uart_tx(unsigned port, const void *buf, size_t len) {
    if (port != 0) return;
    const uint8_t *p = buf;
    while (len) {
        uint16_t n = len > 0xFFFFu ? 0xFFFFu : (uint16_t)len;
        HAL_UART_Transmit(&uart0, (uint8_t *)p, n, HAL_MAX_DELAY);  /* bounded by the baud rate */
        p += n;
        len -= n;
    }
}

/* Polled: what arrives while nobody calls this can overrun the one-byte data register (the FIFO
 * is off); an interrupt-fed ring buffer is the obvious next step once an application needs
 * continuous input. */
size_t bsp_uart_rx(unsigned port, void *buf, size_t len) {
    if (port != 0) return 0;
    if (__HAL_UART_GET_FLAG(&uart0, UART_FLAG_ORE)) __HAL_UART_CLEAR_FLAG(&uart0, UART_CLEAR_OREF);
    uint8_t *p = buf;
    size_t n = 0;
    while (n < len && __HAL_UART_GET_FLAG(&uart0, UART_FLAG_RXNE)) p[n++] = (uint8_t)(uart0.Instance->RDR & 0xFFu);
    return n;
}

/* newlib's stdout and stdin, overriding the weak ones of Core/Src/syscalls.c */
int _write(int file, char *ptr, int len) {
    (void)file;
    if (len > 0) bsp_uart_tx(0, ptr, (size_t)len);
    return len;
}

int _read(int file, char *ptr, int len) {
    (void)file;
    if (len <= 0) return 0;
    size_t n;
    while ((n = bsp_uart_rx(0, ptr, (size_t)len)) == 0) {
    }
    return (int)n;
}

/* ---- LEDs (LD1 green, LD2 blue, LD3 red) and the user button ---- */

static GPIO_TypeDef *const led_ports[] = {LED_GREEN_GPIO_Port, LED_BLUE_GPIO_Port, LED_RED_GPIO_Port};
static const uint16_t led_pins[] = {LED_GREEN_Pin, LED_BLUE_Pin, LED_RED_Pin};

void bsp_led_set(unsigned id, bool on) {
    if (id >= sizeof led_pins / sizeof led_pins[0]) return;
    HAL_GPIO_WritePin(led_ports[id], led_pins[id], on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool bsp_button_get(unsigned id) {
    return id == 0 && HAL_GPIO_ReadPin(USER_BUTTON_GPIO_Port, USER_BUTTON_Pin) == GPIO_PIN_SET;
}

/* ---- time: the DWT cycle counter, at the core clock ---- */

uint32_t bsp_cycle_count(void) { return DWT->CYCCNT; }

uint32_t bsp_cpu_freq_hz(void) { return SystemCoreClock; }

/* On the cycle counter rather than HAL_Delay: works with interrupts masked, and is bounded. */
void bsp_delay_ms(uint32_t ms) {
    const uint32_t per_ms = SystemCoreClock / 1000u;
    while (ms--) {
        const uint32_t start = DWT->CYCCNT;
        while (DWT->CYCCNT - start < per_ms) {
        }
    }
}

void bsp_reset(void) { NVIC_SystemReset(); }

/* The instruction cache in front of the flash; the STM32L5 has no data cache. */
void bsp_clear_caches(void) { HAL_ICACHE_Invalidate(); }
