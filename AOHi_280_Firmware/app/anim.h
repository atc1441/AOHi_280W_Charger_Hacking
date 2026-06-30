/* anim.h - flash-resident frame animations (the clock-screensaver "eyes").
 *
 * Stock loads 4 animations from external flash at boot (anim_load_all @0x11d1c ->
 * load_anim_page @0x11d68): pages 10/11/12/13 from flash 0x1000000/0x1300000/
 * 0x1600000/0x1900000. Each animation = a count byte followed by count 12-byte
 * frame records: [BE32 image_off][BE16 w][BE16 h][BE16 x][BE16 y].
 * The animator (stock 0x16dd0 -> sub_13894 -> sub_A6AC) blits frame[cur] each tick
 * and loops. Theme B of the clock screensaver (g_clock_alt != 0) plays this.
 *
 * The frame image data + the header tables live on the DEVICE's external flash;
 * we read them at runtime via extflash_read (so the correct flash offset is always
 * used - no dependency on any local dump). If a slot's count byte is 0/0xFF the
 * slot is simply empty and playback is a no-op. */
#ifndef APP_ANIM_H
#define APP_ANIM_H
#include <stdint.h>

extern uint8_t g_anim_playing;     /* 1 while a screensaver animation is on screen */

void anim_load_all(void);          /* boot: load the 4 flash animations (pages 10-13) */
void anim_start(uint8_t slot);     /* slot 0..3 -> pages 10..13; clears + begins */
void anim_stop(void);              /* stop playback (call on any page change / button) */
void anim_tick(void);              /* per-tick: blit the next frame if playing */

#endif /* APP_ANIM_H */
