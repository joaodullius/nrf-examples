# Design: nfc_blink_rate — NFC + System OFF para nRF54L15-DK

**Data:** 2026-06-03  
**NCS:** 3.0.0  
**Target:** `nrf54l15dk/nrf54l15/cpuapp`

---

## Objetivo

Demonstrar o uso combinado de NFC T4T writable, system off e persistência em flash no nRF54L15-DK.

O celular escreve a taxa de piscar do LED via NFC. O valor é salvo em flash. O dispositivo alterna entre modo ativo (LED piscando) e system off, acordando por NFC ou pelo botão.

---

## Máquina de Estados

```
Power-on / GPIO wakeup (BTN1)     NFC field wakeup
            ↓                           ↓
      ┌─────────────┐           ┌───────────────┐
      │ ACTIVE MODE │           │   NFC MODE    │
      │ LED0 pisca  │           │ LED1 acende   │
      │ na taxa     │           │ T4T writable  │
      │ configurada │           │ exposto       │
      └──────┬──────┘           └──────┬────────┘
             │                         │
        BTN1 press             Celular escreve "LED <ms>"
             │                  → LED2 pisca (confirmação)
             ↓                  → salva em Settings/ZMS
        sys_poweroff() ◄────── campo NFC remove + delay 3s
```

**Detecção via RESETREAS** (`nrf_reset_resetreas_get`):
- `NRF_RESET_RESETREAS_NFC_MASK` → NFC Mode
- qualquer outro (GPIO, power-on) → Active Mode

---

## Componentes

### NFC
- Biblioteca: `nfc_t4t_nrfxlib` (Type 4 Tag, read-write NDEF)
- Modo: `nfc_t4t_ndef_rwpayload_set()` com buffer estático
- NDEF inicial: text record com o valor atual, ex: `"LED 500"`
- Callback `NFC_T4T_EVENT_NDEF_UPDATED` → parse do novo valor
- Callback `NFC_T4T_EVENT_FIELD_ON/OFF` → controle do LED1 e do delayed work de system off

### Protocolo de Texto
- Formato esperado no NDEF text record: `"LED <valor_ms>"`
- Exemplos válidos: `"LED 100"`, `"LED 2000"`
- Range aceito: 50ms a 10000ms
- Valores fora do range: ignorados, mantém o valor anterior

### Storage
- Subsistema: Zephyr Settings com backend ZMS
- Chave: `"blink/rate_ms"` (uint32_t)
- Default: 500ms (quando não há valor salvo)
- Escrita após NDEF_UPDATED; leitura no boot

### LEDs
| LED | Função |
|-----|--------|
| LED0 | Pisca na taxa configurada (Active Mode) |
| LED1 | Acende quando campo NFC está presente (NFC Mode) |
| LED2 | Pisca 3x (100ms on/off) ao confirmar atualização via NFC |

### Botão
- BTN1 (`sw0`): em Active Mode, pressionar vai para system off
- No system off: configurado como GPIO sense wakeup (wakeup source)

### System OFF
- `sys_poweroff()` após BTN1 em Active Mode
- `sys_poweroff()` após remoção do campo NFC + delay de 3s (NFC Mode)
- Ambas as fontes de wakeup configuradas antes de entrar em system off:
  - NFC: automático via NFCT peripheral
  - BTN1: `nrf_gpio_cfg_sense_set()` com `NRF_GPIO_PIN_SENSE_LOW`

---

## Estrutura de Arquivos

```
nfc_blink_rate/
├── CMakeLists.txt
├── prj.conf
├── src/
│   └── main.c
└── boards/
    └── nrf54l15dk_nrf54l15_cpuapp.overlay
```

### prj.conf (principais configs)
```
CONFIG_NFC_T4T_NRFXLIB=y
CONFIG_NFC_NDEF=y
CONFIG_NFC_NDEF_MSG=y
CONFIG_NFC_NDEF_RECORD=y
CONFIG_NFC_NDEF_TEXT_RECORD=y
CONFIG_POWEROFF=y
CONFIG_DK_LIBRARY=y
CONFIG_SETTINGS=y
CONFIG_SETTINGS_ZMS=y
CONFIG_ZMS=y
CONFIG_FLASH=y
CONFIG_FLASH_MAP=y
CONFIG_MPU_ALLOW_FLASH_WRITE=y
```

### overlay (storage_partition para ZMS)
O nRF54L15-DK já define `storage_partition` no DTS padrão — verificar se precisa de overlay ou não.

---

## Referências de Samples NCS

- `nrf/samples/nfc/system_off` — NFC wakeup + system off
- `nrf/samples/nfc/writable_ndef_msg` — T4T writable NDEF + flash storage
- `nrf/samples/nfc/record_text` — encoding de text record NDEF
- Repo local: `system_off/src/main.c` — padrão de wake reason + sys_poweroff

---

## Restrições e Observações

- O HFXO deve estar rodando antes de o NFCT entrar em ACTIVATED state (gerenciado pela lib)
- RESETREAS deve ser limpo no boot (`nrf_reset_resetreas_clear`) antes de entrar em system off
- Se o campo NFC já estiver presente quando o dispositivo entrar em system off, o NFCT vai acordar imediatamente — isso é comportamento esperado do hardware
- O `storage_partition` do nRF54L15 usa flash interna com page size de 4KB
