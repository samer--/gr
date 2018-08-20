
#import <Foundation/Foundation.h>
#import <ApplicationServices/ApplicationServices.h>
#import <Foundation/NSFileManager.h>
#import <AppKit/AppKit.h>

#include <pthread.h>
#include <zmq.h>

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

#define GKSTERM_DEFAULT_TIMEOUT 5000
/* Timeout for is running is short so that GKSTerm will be started quickly. */
#define GKSTERM_IS_RUNNING_TIMEOUT 50

static
gks_state_list_t *gkss;

static
NSLock *mutex;

static
int num_windows = 0;

static
NSTask *task = NULL;


@interface wss_wrapper:NSObject
{
  ws_state_list *wss;
}

@property ws_state_list *wss;
@end

@implementation wss_wrapper
@synthesize wss;
@end

static bool gksterm_is_running();

static void gksterm_communicate(const char *request, size_t request_len, int timeout, bool only_if_running, void (^reply_handler)(char*, size_t)) {
  int rc;
  void *context = zmq_ctx_new();
  void *socket = zmq_socket(context, ZMQ_REQ);
  CFAbsoluteTime startTime = CFAbsoluteTimeGetCurrent();
  // Disable waiting for unsent messages when closing a socket
  int linger = 0;
  zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(int));

  rc = zmq_connect(socket, "ipc:///tmp/GKSTerm.sock");
  assert(rc == 0);

  zmq_msg_t message;
  zmq_msg_init_size(&message, request_len);
  memcpy(zmq_msg_data(&message), (void *)request, request_len);
  do {
    if ((only_if_running && !gksterm_is_running()) || (CFAbsoluteTimeGetCurrent() - startTime) * 1000.0 > timeout) {
      zmq_msg_close(&message);
      zmq_close(socket);
      zmq_ctx_term(context);
      @throw  [NSException exceptionWithName:@"GKSTermHasDiedException"
                                      reason:@"The connection to GKSTerm has timed out."
                                    userInfo:nil];
    }
    if (rc == -1 && errno == EAGAIN) {
      usleep(10000);
    }
    rc = zmq_msg_send(&message, socket, ZMQ_DONTWAIT);
  } while (rc == -1 && (errno == EINTR || errno == EAGAIN));
  assert(rc == request_len);
  zmq_msg_close(&message);

  zmq_msg_init(&message);
  do {
    if ((only_if_running && !gksterm_is_running()) || (CFAbsoluteTimeGetCurrent() - startTime) * 1000.0 > timeout) {
      zmq_msg_close(&message);
      zmq_close(socket);
      zmq_ctx_term(context);
      @throw  [NSException exceptionWithName:@"GKSTermHasDiedException"
                                      reason:@"The connection to GKSTerm has timed out."
                                    userInfo:nil];
    }
    if (rc == -1 && errno == EAGAIN) {
      usleep(10000);
    }
    rc = zmq_msg_recv(&message, socket, ZMQ_DONTWAIT);
  } while (rc == -1 && (errno == EINTR || errno == EAGAIN));
  assert(rc >= 1);

  char *reply = (char *)zmq_msg_data(&message);
  assert(reply[0] == request[0]);

  reply_handler(reply+1, rc-1);

  zmq_msg_close(&message);
  zmq_close(socket);
  zmq_ctx_term(context);
}

static bool gksterm_is_running() {
  size_t request_len = 1;
  char request[1];
  request[0] = GKSTERM_FUNCTION_IS_RUNNING;

  @try {
    gksterm_communicate(request, request_len, GKSTERM_IS_RUNNING_TIMEOUT, NO, ^(char* reply, size_t reply_len) {
      assert(reply_len == 0);
    });
  } @catch (NSException *) {
    return false;
  }
  return true;
}

static bool gksterm_is_alive(int window) {
  size_t request_len = 1+sizeof(int);
  char request[1+sizeof(int)];
  request[0] = GKSTERM_FUNCTION_IS_ALIVE;
  *(int*)(request+1) = window;

  __block bool result = NO;
  gksterm_communicate(request, request_len, GKSTERM_DEFAULT_TIMEOUT, YES, ^(char* reply, size_t reply_len) {
    assert(reply_len == 1);
    result = (reply[0] == 1);
  });
  return result;
}

static int gksterm_create_window() {
  size_t request_len = 1;
  char request[1];
  request[0] = GKSTERM_FUNCTION_CREATE_WINDOW;

  __block int result = 0;
  gksterm_communicate(request, request_len, GKSTERM_DEFAULT_TIMEOUT, YES, ^(char* reply, size_t reply_len) {
    assert(reply_len == sizeof(int));
    result = *(int*)reply;
  });
  return result;
}

static void gksterm_close_window(int window) {
  size_t request_len = 1+sizeof(int);
  char request[1+sizeof(int)];
  request[0] = GKSTERM_FUNCTION_CLOSE_WINDOW;
  *(int*)(request+1) = window;

  gksterm_communicate(request, request_len, GKSTERM_DEFAULT_TIMEOUT, YES, ^(char* reply, size_t reply_len) {
    assert(reply_len == 0);
  });
}

static void gksterm_draw(int window, void*displaylist, size_t displaylist_len) {
  size_t request_len = 1 + sizeof(int) + sizeof(size_t) + displaylist_len;
  char *request = (char *)malloc(request_len), *p=request;
  assert(request != NULL);
  *p = GKSTERM_FUNCTION_DRAW; p++;
  *(int*)p = window;    p += sizeof(int);
  *(size_t*)p = displaylist_len; p += sizeof(size_t);
  memcpy((void*)p, displaylist, displaylist_len);

  @try {
    gksterm_communicate(request, request_len, GKSTERM_DEFAULT_TIMEOUT, YES, ^(char* reply, size_t reply_len) {
      assert(reply_len == 0);
    });
  } @finally {
    free(request);
  }
}

static void gksterm_resize_draw(int window, double width, double height, void*displaylist, size_t displaylist_len) {
  size_t request_len = 1 + sizeof(int) + 2*sizeof(double) + sizeof(size_t) + displaylist_len;
  char *request = (char *)malloc(request_len), *p = request;
  assert(request != NULL);
  *p = GKSTERM_FUNCTION_RESIZE_DRAW; p++;
  *(int*)p = window;    p += sizeof(int);
  *(double*)p = width;  p += sizeof(double);
  *(double*)p = height; p += sizeof(double);
  *(size_t*)p = displaylist_len; p += sizeof(size_t);
  memcpy((void*)p, displaylist, displaylist_len);

  @try {
    gksterm_communicate(request, request_len, GKSTERM_DEFAULT_TIMEOUT, YES, ^(char* reply, size_t reply_len) {
      assert(reply_len == 0);
    });
  } @finally {
    free(request);
  }
}

static int update(ws_state_list *wss)
{
  @try {
    if (wss->pending_resize) {
      gksterm_resize_draw(wss->win, wss->resize_width, wss->resize_height, wss->dl.buffer, wss->dl.nbytes);
      wss->pending_resize = 0;
    } else
      gksterm_draw(wss->win, wss->dl.buffer, wss->dl.nbytes);
    return 0;
  } @catch (NSException *e) {
    return 1;
  }
}

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
          if (!gksterm_is_alive(wss->win))
            {
              /* This process should die when the user closes the last window */
              if (!wss->closed_by_api) {
                bool all_dead = YES;
                int win;
                for (win = 0; all_dead && win < MAX_WINDOWS; win++) {
                  all_dead = !gksterm_is_alive(win);
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
      bool is_connected = gksterm_is_running() ? YES : NO;
      if (mutex == nil) {
        mutex = [[NSLock alloc] init];
      }
      wss->master_thread = pthread_self();

      if (!is_connected)
        {
          if (!gks_terminal())
            {
               NSLog(@"Launching GKSTerm failed.");
               exit(-1);
            }
          else
            {
              int counter;
              for (counter = 0; counter < 10 && !is_connected; counter++)
                {
                  [NSThread sleepUntilDate:[NSDate dateWithTimeIntervalSinceNow: 1.0]];
                  if (gksterm_is_running()) {
                    is_connected = YES;
                  }
                }
            }
        }

        wss->win = gksterm_create_window();
        num_windows++;

      if (is_connected)
        {
          wss_wrapper *wrapper = [wss_wrapper alloc];
          [wrapper init];
          wrapper.wss = wss;
          wss->thread_alive = YES;
          wss->closed_by_api = NO;
          [NSThread detachNewThreadSelector: @selector(update:) toTarget:[gks_quartz_thread class] withObject:wrapper];
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
          gksterm_close_window(wss->win);
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
      }
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
