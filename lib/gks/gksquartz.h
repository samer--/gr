#ifndef GKSQUARTZ_H
#define GKSQUARTZ_H

#define MAX_WINDOWS 50

#define GKSTERM_FUNCTION_UNKNOWN 0
#define GKSTERM_FUNCTION_CREATE_WINDOW 1
#define GKSTERM_FUNCTION_DRAW 2
#define GKSTERM_FUNCTION_IS_ALIVE 3
#define GKSTERM_FUNCTION_CLOSE_WINDOW 4
#define GKSTERM_FUNCTION_IS_RUNNING 5
#define GKSTERM_FUNCTION_RESIZE_DRAW 6

typedef struct
  {
    int state;
    int win;
    gks_display_list_t dl;
    double width, height;
    double a, b, c, d;
    double window[4], viewport[4];
    CGColorRef rgb[MAX_COLOR];
    int family, capheight;
    double angle;
    CGRect rect[MAX_TNR];
    pthread_t master_thread;
    int update_countdown; // -1 means disabled, in deferred update mode
    int pending_resize;
    double resize_width, resize_height;
    bool thread_alive;
    bool closed_by_api;
 }
ws_state_list;

#endif
