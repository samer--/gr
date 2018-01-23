
#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/NSFileManager.h>
#import <AppKit/AppKit.h>

#include <pthread.h>

#include "gks.h"
#include "gkscore.h"
#include "gksquartz.h"

#ifdef _WIN32

#include <windows.h>
#define DLLEXPORT __declspec(dllexport)

#ifdef __cplusplus
extern "C"
{
#endif

#else

#ifdef __cplusplus
#define DLLEXPORT extern "C"
#else
#define DLLEXPORT
#endif

#endif

DLLEXPORT void gks_quartzplugin(
  int fctid, int dx, int dy, int dimx, int *i_arr,
  int len_f_arr_1, double *f_arr_1, int len_f_arr_2, double *f_arr_2,
  int len_c_arr, char *c_arr, void **ptr);

#ifdef _WIN32
#ifdef __cplusplus
}
#endif
#endif

static
gks_state_list_t *gkss;

static
id plugin;

static
NSLock *mutex;

static
int num_windows = 0;

static
NSTask *task = NULL;


static int update(ws_state_list *wss)
{
  [wss->displayList initWithBytesNoCopy: wss->dl.buffer length: wss->dl.nbytes freeWhenDone: NO];
  @try {
    if (wss->pending_resize) {
      [plugin GKSQuartzResizeDraw: wss->win displayList: wss->displayList : wss->resize_width : wss->resize_height];
      wss->pending_resize = 0;
    } else
      [plugin GKSQuartzDraw: wss->win displayList: wss->displayList];
    return 0;
  } @catch (NSException *e) {
    return 1;
  }
}

@interface wss_wrapper:NSObject
{
  ws_state_list *wss;
}

@property ws_state_list *wss;
@end

@implementation wss_wrapper
@synthesize wss;
@end

@interface gks_quartz_thread : NSObject
+ (void) update: (id) param;
@end

@implementation gks_quartz_thread
+ (void) update: (id) param
{
  int didDie = 0;
  wss_wrapper *wrapper = (wss_wrapper *)param;
  ws_state_list *wss = [wrapper wss];
  [wrapper release];

  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
  while (!didDie && wss != NULL)
    {
      [mutex lock];
      if (wss->update_countdown > 0) {
        wss->update_countdown--;
        if (wss->update_countdown == 0) 
          didDie = update(wss);
      }
      @try
        {
          if ([plugin GKSQuartzIsAlive: wss->win] == 0)
            {
              /* This process should die when the user closes the last window */
              if (!wss->closed_by_api) {
                bool all_dead = YES;
                int win;
                for (win = 0; all_dead && win < MAX_WINDOWS; win++) {
                  all_dead = [plugin GKSQuartzIsAlive: win] == 0;
                }
#ifdef SIGUSR1
                if (all_dead) {
                  pthread_kill(wss->master_thread, SIGUSR1);
                }
#endif
              }
              didDie = 1;
            }
        }
      @catch (NSException *e)
        {
#ifdef SIGUSR1
          printf("q> killing master thread due to exception\n");
          pthread_kill(wss->master_thread, SIGUSR1);
#endif
          didDie = 1;
        }

      if (didDie) wss->thread_alive = NO;
      [mutex unlock];

      usleep(250000);
    }
  [pool drain];
}
@end

static
BOOL gks_terminal(void)
{
  NSURL *url;
  OSStatus status;
  BOOL isDir;

  NSFileManager *fm = [[NSFileManager alloc] init];
  NSString *grdir = [[[NSProcessInfo processInfo]
                      environment]objectForKey:@"GRDIR"];
  if ( grdir == NULL )
    grdir = [NSString stringWithUTF8String:GRDIR];

  NSString *path = [NSString stringWithFormat:@"%@/Applications/GKSTerm.app",
                    grdir];
  if ( ! ([fm fileExistsAtPath:path isDirectory:&isDir] && isDir) )
    path = [[NSString stringWithFormat:@"%@/GKSTerm.app",
             grdir] stringByStandardizingPath];
  if ( [fm fileExistsAtPath:path isDirectory:&isDir] && isDir )
  {
     url = [NSURL fileURLWithPath: path];
     status = LSOpenCFURLRef((CFURLRef) url, NULL);
     if (status != noErr)
     {
       task = [[NSTask alloc] init];
       task.launchPath = [NSString stringWithFormat:@"%@/Contents/MacOS/GKSTerm",
                          path];
       [task launch];
       if (task.isRunning)
         status = noErr;
     }
     return (status == noErr);
  }
  NSLog(@"Could not locate GKSTerm.app.");
  return false;
}

void gks_quartzplugin(
  int fctid, int dx, int dy, int dimx, int *ia,
  int lr1, double *r1, int lr2, double *r2,
  int lc, char *chars, void **ptr)
{

  ws_state_list *wss = (ws_state_list *) *ptr;
  NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];

  switch (fctid)
    {
    case OPEN_WS:
      gkss = (gks_state_list_t *) *ptr;

      wss = (ws_state_list *) calloc(1, sizeof(ws_state_list));
      wss->pending_resize = 0;
      wss->update_countdown = -1; // assume deferred mode
      wss->displayList = [[NSData alloc] initWithBytesNoCopy: wss
                                    length: sizeof(ws_state_list)
                                    freeWhenDone: NO];
      if (plugin != nil) {
        /* verify that the plugin connection is still alive */
        @try {
          [plugin GKSQuartzIsAlive: 0];
        } @catch (NSException *) {
          [plugin release];
          plugin = nil;
        }
      }
      if (plugin == nil) {
        plugin = [NSConnection rootProxyForConnectionWithRegisteredName:
                  @"GKSQuartz" host: nil];
      }
      if (mutex == nil) {
        mutex = [[NSLock alloc] init];
      }
      wss->master_thread = pthread_self();

      if (plugin == nil)
        {
          if (!gks_terminal())
            {
               NSLog(@"Launching GKSTerm failed.");
               exit(-1);
            }
          else
            {
              int counter = 10;
              while (--counter && !plugin)
                {
                  [NSThread sleepUntilDate:[NSDate dateWithTimeIntervalSinceNow: 1.0]];
                  plugin = [NSConnection rootProxyForConnectionWithRegisteredName:
                            @"GKSQuartz" host: nil];
                }
            }
        }

        wss->win = [plugin GKSQuartzCreateWindow];
        num_windows++;

      if (plugin)
        {
          wss_wrapper *wrapper = [wss_wrapper alloc];
          [wrapper init];
          wrapper.wss = wss;
          wss->thread_alive = YES;
          wss->closed_by_api = NO;
          [NSThread detachNewThreadSelector: @selector(update:) toTarget:[gks_quartz_thread class] withObject:wrapper];
          [plugin setProtocolForProxy: @protocol(gks_protocol)];
        }

      *ptr = wss;

      CGSize size = CGDisplayScreenSize(CGMainDisplayID());
      r1[0] = 0.001 * size.width;
      r2[0] = 0.001 * size.height;
      ia[0] = (int) NSMaxX([[[NSScreen screens] objectAtIndex:0] frame]);
      ia[1] = (int) NSMaxY([[[NSScreen screens] objectAtIndex:0] frame]);
      break;

    case CLOSE_WS:

      [mutex lock];
      wss->closed_by_api = YES;
      [mutex unlock];
      @try
        {
          [plugin GKSQuartzCloseWindow: wss->win];
          num_windows--;
        }
      @catch (NSException *e)
        {
          ;
        }

      [mutex lock];
      while (wss->thread_alive) {
        [mutex unlock];
        usleep(100000);
        [mutex lock];
      }
      [mutex unlock];

      if (num_windows == 0) {
        [mutex release];
        mutex = nil;
        [plugin release];
        plugin = nil;
      }
      [wss->displayList release];
      free(wss);
      wss = NULL;

      if (task != NULL)
      {
        [task terminate];
        task = NULL;
      }
      break;

   case SET_WS_VIEWPORT:
      wss->pending_resize = 1;
      wss->resize_width = r1[1] - r1[0];
      wss->resize_height = r2[1] - r2[0];
      wss = NULL;
      break;

   case CLEAR_WS:
      [mutex lock];
      wss->update_countdown = (ia[1] == GKS_K_CLEAR_CONDITIONALLY) ? -1 : 8;
      [mutex unlock];
      break;

   case UPDATE_WS:
      if (ia[1] == GKS_K_PERFORM_FLAG) {
        [mutex lock];
        wss->update_countdown = -1;
        update(wss);
        [mutex unlock];
      }
      wss = NULL;
      break;

    case POLYLINE:
    case POLYMARKER:
    case TEXT:
    case FILLAREA:
    case CELLARRAY:
    case GDP:
    case DRAW_IMAGE:
      if (wss->update_countdown >= 0) {
        [mutex lock];
        wss->update_countdown = 4;
        [mutex unlock];
      }
      break;
    }

  if (wss != NULL)
    gks_dl_write_item(&wss->dl,
      fctid, dx, dy, dimx, ia, lr1, r1, lr2, r2, lc, chars, gkss);

  [pool drain];
}
// vim: ts=2 sw=2 expandtab
