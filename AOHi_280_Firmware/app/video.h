/* video.h - internal rickroll video player (page 21, centred on the display). */
#ifndef APP_VIDEO_H
#define APP_VIDEO_H

#define VIDEO_PAGE 21

void video_start(void);   /* clear screen + begin playback (call from ui_set_page case 21) */
void video_stop(void);    /* stop playback (call on leaving the page)                       */
void video_tick(void);    /* per-tick: draw the next frame if due (call from the main loop) */

#endif /* APP_VIDEO_H */
