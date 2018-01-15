
#define MAX_WINDOWS 50

typedef struct
  {
    int state;
    int win;
    gks_display_list_t dl;
    double width, height;
    double ppmm_x, ppmm_y; // screen units per millimetre, both axes
    double mwidth, mheight; // screen size in metres
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
    NSData *displayList;
 }
ws_state_list;

@protocol gks_protocol
- (int) GKSQuartzCreateWindow;
- (void) GKSQuartzResize: (int) win : (double) width : (double) height;
- (void) GKSQuartzDraw: (int) win displayList: (id) displayList needsDisplay: (BOOL) needsDisplay;
- (int) GKSQuartzIsAlive: (int) win;
- (void) GKSQuartzCloseWindow: (int) win;
@end
