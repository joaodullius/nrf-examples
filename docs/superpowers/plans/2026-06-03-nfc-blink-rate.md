# nfc_blink_rate Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Criar o sample `nfc_blink_rate` no nRF54L15-DK que acorda via NFC ou botão, atualiza taxa de piscar do LED via NFC T4T writable, e persiste o valor em flash (ZMS).

**Architecture:** O dispositivo detecta o motivo do boot via RESETREAS e entra em Active Mode (LED piscando + BTN1 para sleep) ou NFC Mode (tag T4T writable exposta; celular escreve "LED <ms>"). NFC é sempre iniciado antes do system off para habilitar wake-on-field. Ambos os modos configuram GPIO sense do BTN1 antes de sys_poweroff().

**Tech Stack:** NCS v3.2.2, nRF54L15-DK (`nrf54l15dk/nrf54l15/cpuapp`), NFC T4T (`nfc_t4t_nrfxlib`), ZMS (flash), DK library, Zephyr work queue + delayed work.

---

## File Structure

```
nfc_blink_rate/
├── CMakeLists.txt
├── prj.conf
└── src/
    └── main.c
```

Sem overlay: `storage_partition` já está definido no DTS padrão do nRF54L15 DK (RRAM, erase-block-size = 4096).

---

## Task 1: Project Scaffold

**Files:**
- Create: `nfc_blink_rate/CMakeLists.txt`
- Create: `nfc_blink_rate/prj.conf`
- Create: `nfc_blink_rate/src/main.c` (stub)

- [ ] **Step 1: Criar CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nfc_blink_rate)

target_sources(app PRIVATE src/main.c)
```

- [ ] **Step 2: Criar prj.conf**

```
CONFIG_NCS_SAMPLES_DEFAULTS=y

CONFIG_NFC_T4T_NRFXLIB=y
CONFIG_NFC_NDEF=y
CONFIG_NFC_NDEF_MSG=y
CONFIG_NFC_NDEF_RECORD=y
CONFIG_NFC_NDEF_TEXT_RECORD=y

CONFIG_FLASH=y
CONFIG_FLASH_PAGE_LAYOUT=y
CONFIG_MPU_ALLOW_FLASH_WRITE=y
CONFIG_ZMS=y

CONFIG_POWEROFF=y

CONFIG_DK_LIBRARY=y
```

- [ ] **Step 3: Criar src/main.c stub mínimo para build passar**

```c
#include <zephyr/kernel.h>

int main(void)
{
    printk("nfc_blink_rate starting\n");
    return 0;
}
```

- [ ] **Step 4: Build**

```powershell
nrfutil sdk-manager toolchain launch --ncs-version=v3.2.2 --chdir C:\ncs\v3.2.2 -- `
    west build -p -b nrf54l15dk/nrf54l15/cpuapp `
    -d C:\work\nrf-examples\nfc_blink_rate\build `
    C:\work\nrf-examples\nfc_blink_rate `
    > C:\work\nrf-examples\nfc_blink_rate\build\build.log 2>&1
```

Verificar: `tail -n 20 C:\work\nrf-examples\nfc_blink_rate\build\build.log`

Expected: `Build successful` sem erros.

- [ ] **Step 5: Commit**

```bash
git add nfc_blink_rate/
git commit -m "feat: add nfc_blink_rate project scaffold"
```

---

## Task 2: Storage (ZMS)

**Files:**
- Modify: `nfc_blink_rate/src/main.c`

- [ ] **Step 1: Adicionar includes, defines e funções de storage em main.c**

Substituir o stub por (main() minimalista que compila — será expandido em Task 3):

```c
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/poweroff.h>

#include <hal/nrf_reset.h>
#include <hal/nrf_gpio.h>

#include <nfc_t4t_lib.h>
#include <nfc/ndef/msg.h>
#include <nfc/ndef/text_rec.h>
#include <nfc/t4t/ndef_file.h>

#include <dk_buttons_and_leds.h>

#include <zephyr/fs/zms.h>
#include <zephyr/storage/flash_map.h>

#define BLINK_RATE_DEFAULT_MS  500U
#define BLINK_RATE_MIN_MS      50U
#define BLINK_RATE_MAX_MS      10000U
#define SYSTEM_OFF_DELAY_S     3
#define NDEF_FILE_SIZE         256
#define ZMS_BLINK_RATE_ID      1

/* DK_LED1 = LED0 on board, DK_LED2 = LED1, DK_LED3 = LED2 */
#define LED_BLINK          DK_LED1
#define LED_NFC_FIELD      DK_LED2
#define LED_NFC_CONFIRM    DK_LED3

#define NFC_CONFIRM_MS     100
#define NFC_CONFIRM_STEPS  6   /* 3 blinks × on+off */

static uint8_t ndef_msg_buf[NDEF_FILE_SIZE];
static uint32_t blink_rate_ms = BLINK_RATE_DEFAULT_MS;
static bool nfc_mode;

static struct k_work_delayable system_off_work;
static struct k_work_delayable led_blink_work;
static struct k_work_delayable confirm_work;
static struct k_work save_work;
static atomic_t pending_rate = ATOMIC_INIT(0);

/* ZMS */
#define FLASH_DEVICE   FIXED_PARTITION_DEVICE(storage_partition)
#define SECTOR_SIZE    (DT_PROP(DT_CHOSEN(zephyr_flash), erase_block_size))
#define SECTOR_COUNT   2
#define STORAGE_OFFSET FIXED_PARTITION_OFFSET(storage_partition)

static struct zms_fs fs = {
    .sector_size  = SECTOR_SIZE,
    .sector_count = SECTOR_COUNT,
    .offset       = STORAGE_OFFSET,
};

static int storage_init(void)
{
    fs.flash_device = FLASH_DEVICE;
    if (!fs.flash_device) {
        return -ENODEV;
    }
    return zms_mount(&fs);
}

static uint32_t load_blink_rate(void)
{
    uint32_t rate;
    ssize_t rc = zms_read(&fs, ZMS_BLINK_RATE_ID, &rate, sizeof(rate));

    if (rc == sizeof(rate) &&
        rate >= BLINK_RATE_MIN_MS &&
        rate <= BLINK_RATE_MAX_MS) {
        return rate;
    }
    return BLINK_RATE_DEFAULT_MS;
}

static void do_save_rate(struct k_work *work)
{
    uint32_t rate = (uint32_t)atomic_get(&pending_rate);

    if (rate == 0) {
        return;
    }
    atomic_set(&pending_rate, 0);
    blink_rate_ms = rate;
    zms_write(&fs, ZMS_BLINK_RATE_ID, &rate, sizeof(rate));
    printk("Saved blink rate: %u ms\n", rate);
    k_work_reschedule(&confirm_work, K_NO_WAIT);
}

int main(void)
{
    int err;

    err = dk_leds_init();
    if (err) {
        printk("Cannot init LEDs (err %d)\n", err);
        return err;
    }

    err = storage_init();
    if (err) {
        printk("Cannot init storage (err %d)\n", err);
        return err;
    }
    blink_rate_ms = load_blink_rate();
    printk("Blink rate loaded: %u ms\n", blink_rate_ms);

    return 0;
}

- [ ] **Step 2: Build para verificar que compila**

```powershell
nrfutil sdk-manager toolchain launch --ncs-version=v3.2.2 --chdir C:\ncs\v3.2.2 -- `
    west build -p -b nrf54l15dk/nrf54l15/cpuapp `
    -d C:\work\nrf-examples\nfc_blink_rate\build `
    C:\work\nrf-examples\nfc_blink_rate `
    > C:\work\nrf-examples\nfc_blink_rate\build\build.log 2>&1
```

Expected: build successful.

- [ ] **Step 3: Commit**

```bash
git add nfc_blink_rate/src/main.c
git commit -m "feat(nfc_blink_rate): add ZMS storage for blink rate"
```

---

## Task 3: NFC T4T Init + NDEF Text Builder

**Files:**
- Modify: `nfc_blink_rate/src/main.c`

Adicionar antes de `main()`, depois das definições de storage:

- [ ] **Step 1: Adicionar funções NFC (build_ndef_text + parse_blink_rate)**

```c
static int build_ndef_text(uint32_t rate)
{
    static const uint8_t en_code[] = {'e', 'n'};
    static char text[32];
    int text_len = snprintf(text, sizeof(text), "LED %u", rate);

    NFC_NDEF_TEXT_RECORD_DESC_DEF(text_rec, UTF_8,
                                   en_code, sizeof(en_code),
                                   (const uint8_t *)text, text_len);
    NFC_NDEF_MSG_DEF(nfc_text_msg, 1);

    int err = nfc_ndef_msg_record_add(&NFC_NDEF_MSG(nfc_text_msg),
                                       &NFC_NDEF_TEXT_RECORD_DESC(text_rec));
    if (err) {
        return err;
    }

    uint32_t ndef_size = nfc_t4t_ndef_file_msg_size_get(sizeof(ndef_msg_buf));

    err = nfc_ndef_msg_encode(&NFC_NDEF_MSG(nfc_text_msg),
                               nfc_t4t_ndef_file_msg_get(ndef_msg_buf),
                               &ndef_size);
    if (err) {
        return err;
    }

    return nfc_t4t_ndef_file_encode(ndef_msg_buf, &ndef_size);
}

static uint32_t parse_blink_rate(void)
{
    const char prefix[] = "LED ";
    size_t prefix_len = strlen(prefix);

    for (size_t i = 0; i + prefix_len < sizeof(ndef_msg_buf); i++) {
        if (memcmp(&ndef_msg_buf[i], prefix, prefix_len) == 0) {
            long val = strtol((const char *)&ndef_msg_buf[i + prefix_len],
                              NULL, 10);
            if (val >= BLINK_RATE_MIN_MS && val <= BLINK_RATE_MAX_MS) {
                return (uint32_t)val;
            }
        }
    }
    return 0;
}
```

- [ ] **Step 2: Adicionar nfc_callback stub (sem lógica ainda)**

```c
static void nfc_callback(void *context, nfc_t4t_event_t event,
                          const uint8_t *data, size_t data_length,
                          uint32_t flags)
{
    ARG_UNUSED(context);
    ARG_UNUSED(data);
    ARG_UNUSED(flags);
    ARG_UNUSED(data_length);
    ARG_UNUSED(event);
}
```

- [ ] **Step 3: Adicionar stubs de work handlers e button_handler; substituir main() com versão completa**

Adicionar ANTES de main():

```c
static void do_system_off(struct k_work *work) { sys_poweroff(); }
static void led_blink_handler(struct k_work *work) {}
static void confirm_blink_handler(struct k_work *work) {}
static void button_handler(uint32_t button_state, uint32_t has_changed)
{
    ARG_UNUSED(button_state);
    ARG_UNUSED(has_changed);
}
```

Substituir main() completamente:

```c
int main(void)
{
    int err;

    k_work_init_delayable(&system_off_work, do_system_off);
    k_work_init_delayable(&led_blink_work, led_blink_handler);
    k_work_init_delayable(&confirm_work, confirm_blink_handler);
    k_work_init(&save_work, do_save_rate);

    err = dk_leds_init();
    if (err) {
        printk("Cannot init LEDs (err %d)\n", err);
        return err;
    }

    err = storage_init();
    if (err) {
        printk("Cannot init storage (err %d)\n", err);
        return err;
    }
    blink_rate_ms = load_blink_rate();
    printk("Blink rate loaded: %u ms\n", blink_rate_ms);

    return 0;
}
```

- [ ] **Step 4: Adicionar NFC init em main() — substituir o return 0 final**

Dentro de `main()`, substituir o `return 0` final por:

```c
    /* Detect wake reason */
    uint32_t reas = nrf_reset_resetreas_get(NRF_RESET);
    nrf_reset_resetreas_clear(NRF_RESET, reas);
    nfc_mode = (reas & NRF_RESET_RESETREAS_NFC_MASK) != 0;
    printk("Wake reason: 0x%08X -> %s mode\n",
           reas, nfc_mode ? "NFC" : "Active");

    err = build_ndef_text(blink_rate_ms);
    if (err) {
        printk("Cannot build NDEF (err %d)\n", err);
        return err;
    }

    err = nfc_t4t_setup(nfc_callback, NULL);
    if (err < 0) {
        printk("Cannot setup NFC T4T (err %d)\n", err);
        return err;
    }

    err = nfc_t4t_ndef_rwpayload_set(ndef_msg_buf, sizeof(ndef_msg_buf));
    if (err < 0) {
        printk("Cannot set NFC payload (err %d)\n", err);
        return err;
    }

    err = nfc_t4t_emulation_start();
    if (err < 0) {
        printk("Cannot start NFC emulation (err %d)\n", err);
        return err;
    }

    printk("NFC started. Tag content: LED %u\n", blink_rate_ms);
```

- [ ] **Step 5: Build e flash**

```powershell
nrfutil sdk-manager toolchain launch --ncs-version=v3.2.2 --chdir C:\ncs\v3.2.2 -- `
    west build -b nrf54l15dk/nrf54l15/cpuapp `
    -d C:\work\nrf-examples\nfc_blink_rate\build `
    C:\work\nrf-examples\nfc_blink_rate `
    > C:\work\nrf-examples\nfc_blink_rate\build\build.log 2>&1

nrfutil sdk-manager toolchain launch --ncs-version=v3.2.2 --chdir C:\ncs\v3.2.2 -- `
    west flash -d C:\work\nrf-examples\nfc_blink_rate\build
```

- [ ] **Step 6: Verificar com NFC Tools**

1. Abrir NFC Tools no Android
2. Aproximar o celular do DK
3. Expected: app mostra uma tag NFC com texto `"LED 500"`

- [ ] **Step 7: Commit**

```bash
git add nfc_blink_rate/src/main.c
git commit -m "feat(nfc_blink_rate): add NFC T4T init with text NDEF"
```

---

## Task 4: NFC Callback + NDEF Parser

**Files:**
- Modify: `nfc_blink_rate/src/main.c`

Substituir o `nfc_callback` stub pelo definitivo:

- [ ] **Step 1: Implementar nfc_callback completo**

```c
static void nfc_callback(void *context, nfc_t4t_event_t event,
                          const uint8_t *data, size_t data_length,
                          uint32_t flags)
{
    ARG_UNUSED(context);
    ARG_UNUSED(data);
    ARG_UNUSED(flags);

    switch (event) {
    case NFC_T4T_EVENT_FIELD_ON:
        if (nfc_mode) {
            k_work_cancel_delayable(&system_off_work);
            dk_set_led_on(LED_NFC_FIELD);
            printk("NFC field on\n");
        }
        break;

    case NFC_T4T_EVENT_FIELD_OFF:
        if (nfc_mode) {
            dk_set_led_off(LED_NFC_FIELD);
            k_work_reschedule(&system_off_work, K_SECONDS(SYSTEM_OFF_DELAY_S));
            printk("NFC field off, sleeping in %ds\n", SYSTEM_OFF_DELAY_S);
        }
        break;

    case NFC_T4T_EVENT_NDEF_UPDATED:
        if (nfc_mode && data_length > 0) {
            uint32_t rate = parse_blink_rate();

            if (rate > 0) {
                printk("NFC update: LED %u ms\n", rate);
                atomic_set(&pending_rate, rate);
                k_work_submit(&save_work);
            } else {
                printk("NFC update: invalid format (expected 'LED <ms>')\n");
            }
        }
        break;

    default:
        break;
    }
}
```

- [ ] **Step 2: Build, flash e testar**

```powershell
nrfutil sdk-manager toolchain launch --ncs-version=v3.2.2 --chdir C:\ncs\v3.2.2 -- `
    west build -b nrf54l15dk/nrf54l15/cpuapp `
    -d C:\work\nrf-examples\nfc_blink_rate\build `
    C:\work\nrf-examples\nfc_blink_rate `
    > C:\work\nrf-examples\nfc_blink_rate\build\build.log 2>&1

nrfutil sdk-manager toolchain launch --ncs-version=v3.2.2 --chdir C:\ncs\v3.2.2 -- `
    west flash -d C:\work\nrf-examples\nfc_blink_rate\build
```

Verificar no NFC Tools:
1. Aproximar celular → tag mostra `"LED 500"`, LED2 acende
2. Em NFC Tools → Write → Text record: escrever `"LED 200"`
3. Expected: log mostra `"NFC update: LED 200 ms"` e `"Saved blink rate: 200 ms"`
4. Reiniciar o DK → log deve mostrar `"Blink rate loaded: 200 ms"`

- [ ] **Step 3: Commit**

```bash
git add nfc_blink_rate/src/main.c
git commit -m "feat(nfc_blink_rate): implement NFC callback and NDEF parser"
```

---

## Task 5: LED Blink + Confirm Blink

**Files:**
- Modify: `nfc_blink_rate/src/main.c`

Substituir os stubs `led_blink_handler` e `confirm_blink_handler`:

- [ ] **Step 1: Implementar led_blink_handler**

```c
static void led_blink_handler(struct k_work *work)
{
    static bool led_on;

    led_on = !led_on;
    dk_set_led(LED_BLINK, led_on);
    k_work_reschedule(&led_blink_work, K_MSEC(blink_rate_ms));
}
```

- [ ] **Step 2: Implementar confirm_blink_handler**

```c
static int confirm_step;

static void confirm_blink_handler(struct k_work *work)
{
    confirm_step++;
    dk_set_led(LED_NFC_CONFIRM, confirm_step % 2);
    if (confirm_step < NFC_CONFIRM_STEPS) {
        k_work_reschedule(&confirm_work, K_MSEC(NFC_CONFIRM_MS));
    } else {
        dk_set_led_off(LED_NFC_CONFIRM);
        confirm_step = 0;
    }
}
```

- [ ] **Step 3: Ativar LED blink em Active mode no final de main()**

Após `nfc_t4t_emulation_start()`, adicionar:

```c
    if (nfc_mode) {
        printk("NFC mode: waiting for phone\n");
        k_work_reschedule(&system_off_work, K_SECONDS(SYSTEM_OFF_DELAY_S));
    } else {
        printk("Active mode: LED blink at %u ms\n", blink_rate_ms);
        err = dk_buttons_init(button_handler);
        if (err) {
            printk("Cannot init buttons (err %d)\n", err);
            return err;
        }
        k_work_reschedule(&led_blink_work, K_NO_WAIT);
    }
```

- [ ] **Step 4: Build, flash e testar Active mode**

Após flash, verificar:
- LED0 (LED_BLINK) pisca a 500ms (toggle a cada 500ms → período de 1s)
- Logs mostram `"Active mode: LED blink at 500 ms"`

- [ ] **Step 5: Commit**

```bash
git add nfc_blink_rate/src/main.c
git commit -m "feat(nfc_blink_rate): add LED blink and NFC confirm blink"
```

---

## Task 6: System OFF + Button Handler

**Files:**
- Modify: `nfc_blink_rate/src/main.c`

- [ ] **Step 1: Implementar do_system_off definitivo**

Substituir o stub:

```c
static void do_system_off(struct k_work *work)
{
    printk("Entering system off\n");
    k_work_cancel_delayable(&led_blink_work);
    k_work_cancel_delayable(&confirm_work);
    dk_set_leds(DK_NO_LEDS_MSK);

    /* Configure BTN1 (sw0) for GPIO wakeup from system off */
    nrf_gpio_cfg_input(NRF_DT_GPIOS_TO_PSEL(DT_ALIAS(sw0), gpios),
                       NRF_GPIO_PIN_PULLUP);
    nrf_gpio_cfg_sense_set(NRF_DT_GPIOS_TO_PSEL(DT_ALIAS(sw0), gpios),
                           NRF_GPIO_PIN_SENSE_LOW);

    sys_poweroff();
}
```

- [ ] **Step 2: Implementar button_handler definitivo**

Substituir o stub:

```c
static void button_handler(uint32_t button_state, uint32_t has_changed)
{
    if ((has_changed & DK_BTN1_MSK) && (button_state & DK_BTN1_MSK)) {
        printk("BTN1 pressed: entering system off\n");
        k_work_reschedule(&system_off_work, K_NO_WAIT);
    }
}
```

- [ ] **Step 3: Build e flash**

```powershell
nrfutil sdk-manager toolchain launch --ncs-version=v3.2.2 --chdir C:\ncs\v3.2.2 -- `
    west build -b nrf54l15dk/nrf54l15/cpuapp `
    -d C:\work\nrf-examples\nfc_blink_rate\build `
    C:\work\nrf-examples\nfc_blink_rate `
    > C:\work\nrf-examples\nfc_blink_rate\build\build.log 2>&1

nrfutil sdk-manager toolchain launch --ncs-version=v3.2.2 --chdir C:\ncs\v3.2.2 -- `
    west flash -d C:\work\nrf-examples\nfc_blink_rate\build
```

- [ ] **Step 4: Testar BTN1 → system off → BTN1 → Active mode**

1. LED0 piscando (Active mode)
2. Pressionar BTN1 → log `"Entering system off"` → LEDs apagam
3. Pressionar BTN1 novamente → DK reinicia
4. Log deve mostrar: `"Wake reason: 0x... -> Active mode"`, LED0 piscando

- [ ] **Step 5: Commit**

```bash
git add nfc_blink_rate/src/main.c
git commit -m "feat(nfc_blink_rate): add system off and button handler"
```

---

## Task 7: Teste de Integração NFC Wakeup

> Esta task é de teste — nenhum código novo, apenas verificação do fluxo completo.

- [ ] **Step 1: Testar fluxo completo**

Com o código da Task 6 flashado:

**Fluxo A — Atualizar taxa via NFC:**
1. LED0 pisca (Active mode, 500ms padrão)
2. Pressionar BTN1 → system off
3. Aproximar celular do DK → DK acorda
4. Log: `"Wake reason: 0x... NFC_MASK -> NFC mode"`; LED2 acende
5. NFC Tools → Write → Text: `"LED 200"`
6. Log: `"NFC update: LED 200 ms"` + `"Saved blink rate: 200 ms"`
7. LED3 pisca 3x (confirmação)
8. Afastar celular → LED2 apaga → log `"sleeping in 3s"`
9. Após 3s → system off

**Fluxo B — Acordar pelo botão com nova taxa:**
1. (Após Fluxo A, em system off)
2. Pressionar BTN1 → DK acorda
3. Log: `"Wake reason: 0x... -> Active mode"` + `"Blink rate loaded: 200 ms"`
4. LED0 pisca mais rápido (200ms)

**Fluxo C — Testar valor inválido:**
1. NFC Tools → escrever `"HELLO"` (sem prefixo "LED ")
2. Log: `"NFC update: invalid format (expected 'LED <ms>')"`
3. Taxa não é alterada

**Fluxo D — Testar valor fora do range:**
1. NFC Tools → escrever `"LED 30"` (< 50ms mínimo)
2. Log: `"NFC update: invalid format..."` (parse retorna 0)
3. Taxa não é alterada

- [ ] **Step 2: Se necessário, ajustar bugs encontrados no teste**

Verificar com UART:

```powershell
# Ler UART por 10s após reset
nrfutil sdk-manager toolchain launch --ncs-version=v3.2.2 -- `
    python nordicsemi_uart_monitor.py read --port COM<N> --baud 115200 --duration 10
```

(Consultar `nrfutil device list` para identificar a porta correta)

- [ ] **Step 3: Commit final**

```bash
git add nfc_blink_rate/
git commit -m "feat(nfc_blink_rate): complete NFC blink rate sample for nRF54L15-DK"
```

---

## Referências

| Arquivo | Propósito |
|---------|-----------|
| `system_off/src/main.c` (repo local) | Padrão de RESETREAS + sys_poweroff() |
| `C:\ncs\v3.2.2\nrf\samples\nfc\writable_ndef_msg\src\main.c` | NFC T4T writable pattern |
| `C:\ncs\v3.2.2\nrf\samples\nfc\writable_ndef_msg\src\ndef_file_m.c` | ZMS init/read/write pattern |
| `C:\ncs\v3.2.2\nrf\samples\nfc\system_off\src\main.c` | NFC + system off (NCS oficial) |

## Notas Importantes

- **NCS v3.2.2**: versão instalada mais próxima do v3.0.0 especificado. O sample deve funcionar em ambas.
- **Porta UART no nRF54L15**: logs saem na segunda porta VCOM (uart20 = porta VCOM1 por padrão). Se não ver logs, trocar para a outra COM port.
- **RESETREAS**: deve ser limpo logo após leitura (`nrf_reset_resetreas_clear`). Se não limpar, o dispositivo pode acordar imediatamente do system off.
- **NFC sempre iniciado**: o NFC T4T é iniciado em ambos os modos para que NFCT entre em SENSE mode antes do sys_poweroff(), habilitando wake-on-field independente do modo de sleep.
- **NFC em Active mode**: a `nfc_callback` ignora eventos (`if (nfc_mode)`) quando em Active mode, evitando que um campo NFC acidental dispare o system off.
