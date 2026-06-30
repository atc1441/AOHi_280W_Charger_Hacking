/* buttons.c - button FSM (clean-room from stock button_fsm / button_is_pressed). */
#include "buttons.h"
#include "gpio.h"
#include "tick.h"

#define DEBOUNCE_MS        10u     /* 0xA   */
#define LONGPRESS_MS       3000u   /* 0xBB8 */
#define DOUBLECLICK_GAP_MS 181u    /* 0xB5  */

enum { S_IDLE=0, S_DEBOUNCE=1, S_PRESSED=2, S_RELEASE=3, S_WAIT2=4, S_LONGHELD=5 };

typedef struct {
    uint8_t  state;
    uint32_t t0;
    uint32_t dur;
    uint8_t  clicks;
    uint8_t  prev;
    uint8_t  lp_fired;
} btn_state_t;

static btn_state_t s_btn[BTN_COUNT];

void buttons_init(void)
{
    /* stock sub_B048: configure port0 pins 0x08 (WLAN/PA3) and 0x8000 (MENU/PA15)
     * as INPUTS. gpio_config maps dir==2 -> input; cfg={0} left dir=0 = output, so
     * the buttons were driven as outputs (live A/B: POERA bits 3,15 set vs stock
     * cleared) and unreadable. dir=2 = input. */
    gpio_cfg_t cfg = {0};
    cfg.dir  = 2;        /* input */
    cfg.w3   = 0x40;     /* stock PCR bit6 = PUU (pull-up) */
    cfg.pull = 1;        /* enable pull-up: buttons are active-low; without it the
                          * input floats -> spurious presses -> rogue navigation/
                          * blanking. Stock encodes this in w3=0x40 (PUU). */
    gpio_config(0, 0x0008, &cfg);
    gpio_config(0, 0x8000, &cfg);
}

int button_is_pressed(uint8_t btn_id)
{
    /* active-low: MENU = port0 0x8000, WLAN = port0 0x8.
     * (A debug-inject that read 0x1FFFE050/51 was REMOVED: that RAM is
     * UNINITIALIZED on a cold boot -> random non-zero -> phantom button held
     * -> long-press -> standby -> display/backlight off. Worked after a warm
     * stock->custom reflash because the RAM was stable; broke on power-cycle.) */
    if (btn_id) return gpio_read(0, 0x0008) == 0;
    else        return gpio_read(0, 0x8000) == 0;
}

int button_poll(uint8_t btn_id, const btn_handlers_t *h)
{
    if (btn_id >= BTN_COUNT) return 0;
    btn_state_t *b = &s_btn[btn_id];
    int pressed = button_is_pressed(btn_id);
    uint32_t now = get_tick_ms();

    switch (b->state) {
    case S_IDLE:
        if (pressed && !b->prev) {
            b->state = S_DEBOUNCE; b->t0 = now; b->lp_fired = 0;
        }
        break;

    case S_DEBOUNCE:
        if (now - b->t0 >= DEBOUNCE_MS) {
            if (pressed) { b->state = S_PRESSED; b->clicks++; }
            else         { b->state = S_IDLE; }
        }
        break;

    case S_PRESSED:
        if (pressed || !b->prev) {
            if (!b->lp_fired && now - b->t0 >= LONGPRESS_MS) {
                b->state = S_LONGHELD; b->lp_fired = 1;
                if (h && h->on_longpress) h->on_longpress();
            }
        } else {
            b->state = S_RELEASE; b->dur = now - b->t0; b->t0 = now;
        }
        break;

    case S_RELEASE:
        if (now - b->t0 >= DEBOUNCE_MS) {
            if (pressed) {
                b->state = S_PRESSED;
            } else if (b->clicks == 1 && (b->dur >> 4) <= 0x7C) {
                b->state = S_WAIT2; b->t0 = now;
            } else if (b->clicks < 2 || (b->dur >> 4) > 0x7C) {
                b->state = S_IDLE; b->clicks = 0;
            } else {
                if (h && h->on_doubleclick) h->on_doubleclick();
                b->state = S_IDLE; b->clicks = 0;
            }
        }
        break;

    case S_WAIT2:
        if (!pressed || b->prev) {
            if (now - b->t0 >= DOUBLECLICK_GAP_MS) {
                if (b->clicks == 1 && h && h->on_click) h->on_click();
                b->state = S_IDLE; b->clicks = 0;
            }
        } else {
            b->state = S_DEBOUNCE; b->t0 = now;
        }
        break;

    case S_LONGHELD:
        if (!pressed && b->prev) {
            b->state = S_RELEASE; b->dur = now - b->t0; b->t0 = now;
        }
        break;

    default: break;
    }

    b->prev = (uint8_t)(pressed & 1);
    return pressed & 1;
}
