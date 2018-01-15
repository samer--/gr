#import <Cocoa/Cocoa.h>
#import <AppKit/AppKit.h>

#import "gkscore.h"
#import "gksquartz.h"

typedef struct {
  double fontsize;
  NSString *fontfamily;
} _FontInfo;

@interface GKSView : NSView 
{
  @private
    char *buffer;
    int size;
    int win_id;
    double window[4];
    double angle;
    double line_width_factor;
    double req_width, req_height; 
    double ppm_x, ppm_y; // pixels per metre
    NSSize screen_size;  // in metres
    int has_been_resized;
    IBOutlet NSBox *extendSavePanelView;
    IBOutlet NSPopUpButton *saveFormatPopUp;
    IBOutlet NSSlider *compressionSlider;
}
- (void) setDisplayList: (id) display_list : (BOOL) needsDisplay;
- (void) close;

- (void) setWinID: (int) winid;
- (int) getWinID;

- (void) resize_window: (double) vp_w : (double) vp_h;
- (void) set_clip_rect: (int) tnr;
- (void) set_zoom: (NSSize) size;

- (void) gks_set_shadow;

- (void) polyline: (int) n : (double *) px : (double *) py;
- (void) draw_marker: (double) xn : (double) yn : (int) mtype : (double) mscale : (int) mcolor : (CGContextRef) context;
- (void) polymarker: (int) n : (double *) px : (double *) py;
- (void) fillarea: (int) n : (double *) px : (double *) py;
- (void) cellarray:
  (double) xmin : (double) xmax : (double) ymin : (double) ymax :
  (int) dx : (int) dy : (int) dimx : (int *) colia : (int) true_color;
  
- (void) text: (double) px : (double) py : (char *) text;
- (_FontInfo) set_font: (int) font;
- (NSString *) stringForText: (const char*)text withFontFamilyID : (int)family;

- (IBAction) keep_on_display: (id) sender;
- (IBAction) rotate: (id) sender;
@end
