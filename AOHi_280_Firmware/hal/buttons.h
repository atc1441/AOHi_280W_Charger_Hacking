/* buttons.h - debounced button FSM (clean-room from stock button_fsm).
 * Two buttons: MENU (id 0) and WLAN (id 1), both active-low on GPIO port 0. */
#ifndef HAL_BUTTONS_H
#define HAL_BUTTONS_H
#include <stdint.h>

enum { BTN_MENU = 0, BTN_WLAN = 1, BTN_COUNT = 2 };

typedef void (*btn_cb_t)(void);

typedef struct {
    btn_cb_t on_click;
    btn_cb_t on_longpress;
    btn_cb_t on_doubleclick;
} btn_handlers_t;

/* Configure the button GPIO pins as inputs (stock sub_B048). */
void buttons_init(void);

int  button_is_pressed(uint8_t btn_id);

/* Advance the FSM for one button; call periodically (e.g. each main loop).
 * Returns the debounced raw pressed state. */
int  button_poll(uint8_t btn_id, const btn_handlers_t *h);

#endif /* HAL_BUTTONS_H */
