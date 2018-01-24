
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include <time.h>

#if !defined(VMS) && !defined(_WIN32)
#include <unistd.h>
#endif

#include "gks.h"
#include "gkscore.h"
#include "marker.h"


#ifdef HAVE_ZLIB
#include <zlib.h>
#else
typedef unsigned char Byte;
typedef unsigned long uLong;
#endif

#define MAX_FONT 31
#define HATCH_STYLE 108
#define PATTERNS 120

#define MEMORY_INCREMENT 32768

#define MAX_OBJECTS 2500
#define MAX_PAGES 250
#define MAX_IMAGES 2500

#define NO_OF_BUFS 10

#define DrawBorder 0

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#define POINTS_PER_M (72/0.0254)

#define WC_to_NDC(xw, yw, tnr, xn, yn) \
  xn = a[tnr] * (xw) + b[tnr]; \
  yn = c[tnr] * (yw) + d[tnr]

#define WC_to_NDC_rel(xw, yw, tnr, xn, yn) \
  xn = a[tnr] * (xw); \
  yn = c[tnr] * (yw)

#define NDC_to_DC(xn, yn, xd, yd) \
  xd = p->a * (xn) + p->b; \
  yd = p->c * (yn) + p->d

#define DC_to_NDC(xd, yd, xn, yn) \
  xn = ((xd) - p->b) / p->a; \
  yn = ((yd) - p->d) / p->c

#define CharXform(phi, xrel, yrel, x, y) \
  x = cos(phi) * (xrel) - sin(phi) * (yrel); \
  y = sin(phi) * (xrel) + cos(phi) * (yrel);

#define PUSH_CLIP if (gkss->clip) { pdf_save(p); set_clip(gkss->viewport[gkss->cntnr]); }
#define POP_CLIP  if (gkss->clip) pdf_restore(p) 
#define CLIP_RECT(gkss) gkss->viewport[gkss->clip == GKS_K_CLIP ? gkss->cntnr : 0] // FIXME could take p->window if not clipping
#define CLIP_CODE(cr, x, y) ( (x < cr[0] ? 1 : x > cr[1] ? 2 : 0) \
                            | (y < cr[2] ? 4 : y > cr[3] ? 8 : 0))
#define COULD_BE_VISIBLE(c1, c2) (!(c1 && c2 && (c1 & c2)))

#define nint(a) ((int)(a + 0.5))

#define pdf_obj(p, id) \
  p->byte_offset[id] = p->stream->length; \
  pdf_printf(p->stream, "%ld 0 obj\n", id);

#define pdf_endobj(p)           pdf_printf(p->stream, "endobj\n")
#define pdf_dict(p)             pdf_printf(p->stream, "<<\n")
#define pdf_enddict(p)          pdf_printf(p->stream, ">>\n")
#define pdf_stream(p)           pdf_printf(p->stream, "stream\n")
#define pdf_endstream(p)        pdf_printf(p->stream, "endstream\n")

#define pdf_save(p)             pdf_printf(p->content, "q\n")
#define pdf_restore(p)          pdf_printf(p->content, "Q\n")
#define pdf_clip(p)             pdf_printf(p->content, "W n\n")
#define pdf_moveto(p, x, y)     pdf_printf(p->content, "%.2f %.2f m\n", x, y)
#define pdf_lineto(p, x, y)     pdf_printf(p->content, "%.2f %.2f l\n", x, y)
#define pdf_closepath(p)        pdf_printf(p->content, "h\n")
#define pdf_stroke(p)           pdf_printf(p->content, "S\n")
#define pdf_eofill(p)           pdf_printf(p->content, "f*\n")
#define pdf_point(p, x, y)      pdf_printf(p->content, "%.2f %.2f ", x, y)
#define pdf_curveto(p)          pdf_printf(p->content, "c\n")
#define pdf_setdash(p, dash)    pdf_printf(p->content, "%s 0 d\n", dash)

#define pdf_setlinewidth(p, width) \
  pdf_printf(p->content, "%s w\n", pdf_double(width))

#define pdf_text(p, xorg, yorg, text) \
  pdf_printf(p->content, "BT\n/F%d %.2f Tf\n%.2f %.2f Td\n(%s) Tj\nET\n", \
  p->font, p->pt, xorg, yorg, text)

#define pdf_setrgbcolor(p, red, green, blue) \
  pdf_printf(p->content, "%s %s %s RG\n", \
    pdf_double(red), pdf_double(green), pdf_double(blue))

#define pdf_setfillcolor(p, red, green, blue) \
  pdf_printf(p->content, "%s %s %s rg\n", \
    pdf_double(red), pdf_double(green), pdf_double(blue))

#define pdf_setalpha(p, alpha) \
  pdf_printf(p->content, "/GS%d gs\n", alpha)

#define PDF ws_state_list

typedef struct PDF_stream_t
  {
    Byte *buffer;
    uLong size, length;
  }
PDF_stream;

typedef struct PDF_image_t
  {
    long object;
    int width, height;
    int *data;
  }
PDF_image;

typedef struct PDF_page_t
  {
    long object, contents, fonts[MAX_FONT];
    double width, height;
    PDF_stream *stream;
    int first_image, last_image;
  }
PDF_page;

typedef struct ws_state_list_t
  {
    int fd;
    double window[4], viewport[4];
    double width, height;
    double a, b, c, d;
    double red[MAX_COLOR], green[MAX_COLOR], blue[MAX_COLOR];
    int color, fillcolor, alpha, ltype, font;
    double lwidth, angle, size, pt;
    PDF_stream *stream;
    long object_number;
    long info, root, outlines, pages;
    long *byte_offset;
    int max_objects;
    PDF_page **page;
    int current_page, max_pages;
    PDF_stream *content;
    int compress;
    int have_alpha[256];
    int pattern;
    int have_pattern[PATTERNS];
    int pattern_id[PATTERNS][2];
    PDF_image **image;
    int images, max_images;
    int clip;
  }
ws_state_list;

static
ws_state_list *p;

static
gks_state_list_t *gkss;

static
double a[MAX_TNR], b[MAX_TNR], c[MAX_TNR], d[MAX_TNR];

static
const char *fonts[MAX_FONT] =
  {
    "Times-Roman", "Times-Italic", "Times-Bold", "Times-BoldItalic",
    "Helvetica", "Helvetica-Oblique", "Helvetica-Bold", "Helvetica-BoldOblique",
    "Courier", "Courier-Oblique", "Courier-Bold", "Courier-BoldOblique",
    "Symbol",
    "Bookman-Light", "Bookman-LightItalic", "Bookman-Demi",
    "Bookman-DemiItalic",
    "NewCenturySchlbk-Roman", "NewCenturySchlbk-Italic",
    "NewCenturySchlbk-Bold", "NewCenturySchlbk-BoldItalic",
    "AvantGarde-Book", "AvantGarde-BookOblique", "AvantGarde-Demi",
    "AvantGarde-DemiOblique",
    "Palatino-Roman", "Palatino-Italic", "Palatino-Bold", "Palatino-BoldItalic",
    "ZapfChancery-MediumItalic", "ZapfDingbats"
  };

static
int flags[MAX_FONT] =
  {
         042,     0142, 01000042, 01000142,
         040,     0140, 01000040, 01000140,
         042,     0142, 01000042, 01000142,
          06,
         042,     0142, 01000042, 01000142,
         042,     0142, 01000042, 01000142,
         040,     0140, 01000040, 01000140,
         042,     0142, 01000042, 01000142,
        0142,       04
  };

static
int stems[MAX_FONT] =
  {
      80,  80, 160, 160,
      80,  80, 160, 160,
      80,  80, 160, 160,
      80,
      80,  80, 160, 160,
      80,  80, 160, 160,
      80,  80, 160, 160,
      80,  80, 160, 160,
      80,  80
  };

static
const char *bboxes[MAX_FONT] =
  {
    "-168 -218 1000 898",
    "-169 -217 1010 883",
    "-168 -218 1000 935",
    "-200 -218 996 921",
    "-166 -225 1000 931",
    "-170 -225 1116 931",
    "-170 -228 1003 962",
    "-174 -228 1114 962",
    "-28 -250 628 805",
    "-28 -250 742 805",
    "-113 -250 749 801",
    "-56 -250 868 801",
    "-180 -293 1090 1010",
    "-188 -251 1266 908 ",
    "-228 -250 1269 883 ",
    "-194 -250 1346 934 ",
    "-231 -250 1333 941 ",
    "-195 -250 1000 965",
    "-166 -250 994 958",
    "-165 -250 1000 988",
    "-205 -250 1147 991",
    "-113 -222 1148 955",
    "-113 -222 1279 955",
    "-123 -251 1222 1021",
    "-123 -251 1256 1021",
    "-166 -283 1021 927",
    "-170 -276 1010 918",
    "-152 -266 1000 924",
    "-170 -271 1073 926",
    "-181 -314 1065 831",
    "-1 -143 981 820"
  };

static
double angles[MAX_FONT] =
  {
        0, -15.5,     0,   -15,
        0,   -12,     0,   -12,
        0,   -12,     0,   -12,
        0,
        0,   -10,     0,   -10,
        0,   -16,     0,   -16,
        0, -10.5,     0, -10.5,
        0,   -10,     0,   -10,
      -14,     0
  };

static
double capheights[MAX_FONT] =
  {
    0.662, 0.653, 0.676, 0.669,
    0.718, 0.718, 0.718, 0.718,
    0.562, 0.562, 0.562, 0.562,
    0.667,
    0.681, 0.681, 0.681, 0.681,
    0.722, 0.722, 0.722, 0.722,
    0.740, 0.740, 0.740, 0.740,
    0.692, 0.692, 0.681, 0.681,
    0.587, 0.692
  };

static
int ascenders[MAX_FONT] =
  {
     683,  683,  676,  699,
     718,  718,  718,  718,
     629,  629,  626,  626,
       0,
     717,  717,  725,  732,
     737,  737,  737,  737,
     740,  740,  740,  740,
     726,  733,  720,  726,
     714,    0
  };

static
int map[32] =
  {
    22,  9,  5, 14, 18, 26, 13,  1,
    24, 11,  7, 16, 20, 28, 13,  3,
    23, 10,  6, 15, 19, 27, 13,  2,
    25, 12,  8, 17, 21, 29, 13,  4
  };

static
int rmap[29] =
  {
     8, 24, 16, 32,
     3, 19, 11, 27,
     2, 18, 10, 26,
    23,
     4, 20, 12, 28,
     5, 21, 13, 29,
     1, 17,  9, 25,
     6, 22, 14, 30
  };

static
char bitmap[PATTERNS][17];

static
int predef_font[] = { 1, 1, 1, -2, -3, -4 };

static
int predef_prec[] = { 0, 1, 2, 2, 2, 2 };

static
int predef_ints[] = { 0, 1, 3, 3, 3 };

static
int predef_styli[] = { 1, 1, 1, 2, 3 };

static
double xfac[4] = { 0, 0, -0.5, -1 };

static
double yfac[6] = { 0, -1.2, -1, -0.5, 0, 0.2 };

static
void fill_routine(int n, double *px, double *py, int tnr);

static char buf_array[NO_OF_BUFS][20];
static int current_buf = 0;

static
const char *pdf_double(double f)
{
  char *buf = buf_array[(current_buf++) % NO_OF_BUFS];

  if (fabs(f) < 0.00001)
    return "0";

  sprintf(buf, "%.4g", f);
  if (strchr(buf, 'e'))
    {
      if (fabs(f) < 1)
        sprintf(buf, "%1.5f", f);
      else if (fabs(f) < 1000)
        sprintf(buf, "%1.2f", f);
      else
        sprintf(buf, "%1.0f", f);
    }

  return buf;
}

static
char *pdf_calloc(size_t count, size_t size)
{
  char *p;

  p = gks_malloc(count * size);
  if (p == NULL)
    exit(-1);

  return p;
}

static
char *pdf_realloc(void *ptr, size_t size)
{
  char *p;

  p = gks_realloc(ptr, size);
  if (p == NULL)
    exit(-1);

  return p;
}

static
void pdf_memcpy(PDF_stream *p, char *s, size_t n)
{
  if (p->length + n >= p->size)
    {
      while (p->length + n >= p->size)
        p->size += MEMORY_INCREMENT;
      p->buffer = (Byte *) pdf_realloc(p->buffer, p->size);
    }

  memmove(p->buffer + p->length, s, n);
  p->length += n;
}

static
void pdf_printf(PDF_stream *p, const char *fmt,...)
{
  va_list ap;
  char s[BUFSIZ];

  va_start(ap, fmt);
  vsprintf(s, fmt, ap);
  va_end(ap);

  pdf_memcpy(p, s, strlen(s));
}

static
PDF_stream *pdf_alloc_stream(void)
{
  PDF_stream *p;

  p = (PDF_stream *) pdf_calloc(1, sizeof(PDF_stream));
  p->buffer = NULL;
  p->size = p->length = 0;

  return p;
}

static
long pdf_alloc_id(PDF *p)
{
  if (p->object_number >= p->max_objects)
    {
      p->max_objects += MAX_OBJECTS;
      p->byte_offset = (long *) pdf_realloc(p->byte_offset,
                                            p->max_objects * sizeof(long));
    }
  return ++(p->object_number);
}

static
PDF_image *pdf_image(PDF *p, int width, int height)
{
  PDF_image *image;

  if (p->images + 1 >= MAX_IMAGES)
    {
      p->max_images += MAX_IMAGES;
      p->image = (PDF_image **) pdf_realloc(
        p->image, p->max_images * sizeof(PDF_image *));
    }

  image = (PDF_image *) pdf_calloc(1, sizeof(PDF_image));

  image->object = pdf_alloc_id(p);
  image->width = width;
  image->height = height;
  image->data = (int *) pdf_calloc(width * height, sizeof(int));

  return image;
}

static
void pdf_page(void)
{
  PDF_page *page;
  int font;

  if (p->current_page + 1 >= MAX_PAGES)
    {
      p->max_pages += MAX_PAGES;
      p->page = (PDF_page **) pdf_realloc(p->page,
                                          p->max_pages * sizeof(PDF_page *));
    }

  page = (PDF_page *) pdf_calloc(1, sizeof(PDF_page));

  page->object = pdf_alloc_id(p);
  page->contents = pdf_alloc_id(p);
  page->width = p->width;
  page->height = p->height;
  page->stream = pdf_alloc_stream();

  p->page[p->current_page++] = page;
  p->content = page->stream;

  for (font = 0; font < MAX_FONT; font++)
    page->fonts[font] = 0;

  page->first_image = page->last_image = p->images;
}

static
void pdf_close(PDF *p)
{
  time_t timer;
  struct tm ltime;
  long start_xref;
  int count, object, font, pattern;
  int image, width, height, length, *rgba, alpha;
  Byte red, green, blue, data[3];
  int mask_id, filter_id, i;
  stroke_data_t s;

  pdf_printf(p->stream, "%%PDF-1.%d\n", p->compress ? 2 : 0);
  pdf_printf(p->stream, "%%\344\343\317\322\n");  /* %����\n */

  time(&timer);
  ltime = *localtime(&timer);

  pdf_obj(p, p->info);
  pdf_dict(p);
  pdf_printf(p->stream, "/Creator (GKS)\n");
  pdf_printf(p->stream, "/CreationDate (D:%04d%02d%02d%02d%02d%02d)\n",
             ltime.tm_year + 1900, ltime.tm_mon + 1, ltime.tm_mday,
             ltime.tm_hour, ltime.tm_min, ltime.tm_sec);
  pdf_printf(p->stream, "/Producer (%s)\n", "GKS 5 PDF driver");
  pdf_enddict(p);
  pdf_endobj(p);

  pdf_obj(p, p->root);
  pdf_dict(p);
  pdf_printf(p->stream, "/Type /Catalog\n");
  pdf_printf(p->stream, "/Pages %ld 0 R\n", p->pages);
  pdf_printf(p->stream, "/Outlines %ld 0 R\n", p->outlines);
  pdf_enddict(p);
  pdf_endobj(p);

  pdf_obj(p, p->outlines);
  pdf_dict(p);
  pdf_printf(p->stream, "/Type /Outlines\n");
  pdf_printf(p->stream, "/Count 0\n");
  pdf_enddict(p);
  pdf_endobj(p);

  pdf_obj(p, p->pages);
  pdf_dict(p);
  pdf_printf(p->stream, "/Type /Pages\n");
  pdf_printf(p->stream, "/Count %d\n", p->current_page);
  pdf_printf(p->stream, "/Kids [");

  for (count = 0; count < p->current_page; count++)
    {
      pdf_printf(p->stream, "%ld 0 R", p->page[count]->object);
      if (count < p->current_page - 1)
        pdf_printf(p->stream, count % 6 ? (char *) " " : (char *) "\n");
    }

  pdf_printf(p->stream, "]\n");
  pdf_enddict(p);
  pdf_endobj(p);

  filter_id = pdf_alloc_id(p);
  pdf_obj(p, filter_id);
  pdf_dict(p);
  pdf_printf(p->stream, "/Length 12\n/Filter/ASCIIHexDecode\n");
  pdf_enddict(p);
  pdf_stream(p);
  pdf_printf(p->stream, "000000ffffff\n");
  pdf_endstream(p);
  for (pattern = 0; pattern < PATTERNS; pattern++)
    {
      if (p->have_pattern[pattern])
        {
          pdf_obj(p, p->pattern_id[pattern][0]);
          pdf_dict(p);
          pdf_printf(p->stream, "/Subtype/Image\n");
          pdf_printf(p->stream, "/Width 8\n");
          pdf_printf(p->stream, "/Height 8\n");
          pdf_printf(p->stream, "/BitsPerComponent 1\n");
          pdf_printf(p->stream, "/ColorSpace [/Indexed/DeviceRGB 1 %d 0 R]\n",
                     filter_id);
          pdf_printf(p->stream, "/Filter/ASCIIHexDecode\n");
          pdf_printf(p->stream, "/Length 16\n");
          pdf_enddict(p);
          pdf_stream(p);
          pdf_printf(p->stream, bitmap[pattern]);
          pdf_printf(p->stream, "\n");
          pdf_endstream(p);
          pdf_endobj(p);

          pdf_obj(p, p->pattern_id[pattern][1]);
          pdf_dict(p);
          pdf_printf(p->stream, "/PatternType 1\n");
          pdf_printf(p->stream, "/PaintType 1\n");
          pdf_printf(p->stream, "/TilingType 1\n");
          pdf_printf(p->stream, "/BBox[0 0 8 8]\n");
          pdf_printf(p->stream, "/XStep 8\n");
          pdf_printf(p->stream, "/YStep 8\n");
          pdf_printf(p->stream, "/Resources ");
          pdf_printf(p->stream, "<<");
          pdf_printf(p->stream,
                  "/ProcSet[/PDF/ImageB/ImageC/ImageI/Text]\n/XObject<<\n");
          pdf_printf(p->stream, "/I%03d %d 0 R\n",
                     pattern, p->pattern_id[pattern][0]);
          pdf_printf(p->stream, ">>>>\n");
          pdf_printf(p->stream, "/Length 44\n");
          pdf_enddict(p);
          pdf_stream(p);
          pdf_printf(p->stream,
                     "q\n1 0 0 1 0 8 cm\n8 0 0 -8 0 0 cm\n/I%03d Do\nQ\n\n",
                     pattern);
          pdf_endstream(p);
          pdf_endobj(p);
        }
    }

  for (count = 0; count < p->current_page; count++)
    {
      PDF_page *page = p->page[count];

      pdf_obj(p, page->object);
      pdf_dict(p);
      pdf_printf(p->stream, "/Type /Page\n");
      pdf_printf(p->stream, "/Parent %ld 0 R\n", p->pages);
      pdf_printf(p->stream, "/Resources << /Font <<");
      for (font = 0; font < MAX_FONT; font++)
        {
          if (page->fonts[font])
            pdf_printf(p->stream, " /F%d %ld 0 R", font, page->fonts[font]);
        }
      pdf_printf(p->stream, " >>\n");

      pdf_printf(p->stream, "/ExtGState <<\n");
      for (alpha = 0; alpha < 256; alpha++)
        {
          if (p->have_alpha[alpha])
            pdf_printf(p->stream, "/GS%d << /CA %g /ca %g >>\n",
                       alpha, alpha / 255.0, alpha / 255.0);
        }
      pdf_printf(p->stream, ">>\n");

      pdf_printf(p->stream, "/Pattern <<\n");
      for (pattern = 0; pattern < PATTERNS; pattern++)
        {
          if (p->have_pattern[pattern])
            {
              pdf_printf(p->stream, "/P%d %d 0 R\n",
                         pattern, p->pattern_id[pattern][1]);
            }
        }
      pdf_printf(p->stream, ">>\n");

      pdf_printf(p->stream, "/XObject <<\n");
      for (image = page->first_image; image < page->last_image; image++)
          pdf_printf(p->stream, "/Im%d %d 0 R\n",
                     image + 1, p->image[image]->object);
      pdf_printf(p->stream, ">>\n>>\n");

      pdf_printf(p->stream, "/MediaBox [0 0 %g %g]\n",
                 page->height, page->width);
      pdf_printf(p->stream, "/Contents %ld 0 R\n", page->contents);
      pdf_enddict(p);
      pdf_endobj(p);

      p->content = page->stream;
      pdf_obj(p, page->contents);
      pdf_dict(p);

#ifdef HAVE_ZLIB
      if (p->compress)
        {
          Byte *buffer;
          uLong length;
          int err;

          length = p->content->length + 1024;
          buffer = (Byte *) pdf_calloc((int) length, 1);
          if ((err = compress(buffer, &length, p->content->buffer,
                              p->content->length)) != Z_OK)
            {
              gks_perror("compression failed (err=%d)", err);
              exit(-1);
            }
          free(p->content->buffer);

          p->content->buffer = buffer;
          p->content->size = p->content->length = length;
          pdf_printf(p->stream, "/Length %ld\n", p->content->length);
          pdf_printf(p->stream, "/Filter [/FlateDecode]\n");
          buffer[p->content->length++] = '\n';
        }
      else
        {
          pdf_printf(p->stream, "/Length %ld\n", p->content->length);
        }
#else
      pdf_printf(p->stream, "/Length %ld\n", p->content->length);
#endif

      pdf_enddict(p);
      pdf_stream(p);
      pdf_memcpy(p->stream, (char *) p->content->buffer, p->content->length);
      pdf_endstream(p);
      pdf_endobj(p);

      for (font = 0; font < MAX_FONT; font++)
        {
          if (page->fonts[font])
            {
              pdf_obj(p, page->fonts[font]);
              pdf_dict(p);
              pdf_printf(p->stream, "/Type /Font\n");
              pdf_printf(p->stream, "/Subtype /Type1\n");
              pdf_printf(p->stream, "/Name /F%d\n", font);
              pdf_printf(p->stream, "/BaseFont /%s\n", fonts[font]);
              pdf_printf(p->stream, "/FirstChar 0\n");
              pdf_printf(p->stream, "/LastChar 255\n");
              pdf_printf(p->stream, "/Widths [");
              for (i = 0; i < 256; i++)
                {
                  gks_lookup_afm(rmap[font], i, &s);
                  pdf_printf(p->stream, "%d ", s.right - s.left);
                }
              pdf_printf(p->stream, "]\n");
              pdf_printf(p->stream, "/FontDescriptor %d 0 R\n",
                         page->fonts[font] + 1);
              if (font != 12)
                pdf_printf(p->stream, "/Encoding /WinAnsiEncoding\n");
              pdf_enddict(p);
              pdf_endobj(p);

              pdf_obj(p, page->fonts[font] + 1);
              pdf_dict(p);
              pdf_printf(p->stream, "/Type /FontDescriptor\n");
              pdf_printf(p->stream, "/FontName /%s\n", fonts[font]);
              pdf_printf(p->stream, "/Flags %d\n", flags[font]);
              pdf_printf(p->stream, "/FontBBox [%s]\n", bboxes[font]);
              pdf_printf(p->stream, "/StemV %d\n", stems[font]);
              pdf_printf(p->stream, "/CapHeight %d\n", s.cap);
              pdf_printf(p->stream, "/Ascent %d\n", ascenders[font]);
              pdf_printf(p->stream, "/Descent %d\n", s.bottom);
              pdf_printf(p->stream, "/ItalicAngle %.1f\n", angles[font]);
              pdf_enddict(p);
              pdf_endobj(p);
           }
        }
      free(p->content->buffer);
    }

  for (image = 0; image < p->images; image++)
    {
      width = p->image[image]->width;
      height = p->image[image]->height;
      length = width * height;
      rgba = p->image[image]->data;

      mask_id = pdf_alloc_id(p);
      pdf_obj(p, mask_id);
      pdf_dict(p);
      pdf_printf(p->stream, "/Type /XObject\n");
      pdf_printf(p->stream, "/Subtype /Image\n");
      pdf_printf(p->stream, "/BitsPerComponent 8\n");
      pdf_printf(p->stream, "/ColorSpace /DeviceGray\n");
      pdf_printf(p->stream, "/Height %d\n",height);
      pdf_printf(p->stream, "/Width %d\n", width);
      pdf_printf(p->stream, "/Length %d\n", length);
      pdf_enddict(p);

      pdf_stream(p);
      for (i = 0; i < length; i++)
        {
          alpha = (*rgba & 0xff000000) >> 24;
          rgba++;
          pdf_memcpy(p->stream, (char *) &alpha, 1);
        }
      pdf_printf(p->stream, "\n");
      pdf_endstream(p);
      pdf_endobj(p);

      rgba = p->image[image]->data;

      pdf_obj(p, p->image[image]->object);
      pdf_dict(p);
      pdf_printf(p->stream, "/Type /XObject\n");
      pdf_printf(p->stream, "/Subtype /Image\n");
      pdf_printf(p->stream, "/BitsPerComponent 8\n");
      pdf_printf(p->stream, "/ColorSpace /DeviceRGB\n");
      pdf_printf(p->stream, "/Height %d\n", height);
      pdf_printf(p->stream, "/Width %d\n", width);
      pdf_printf(p->stream, "/SMask %d 0 R\n", mask_id);
      pdf_printf(p->stream, "/Length %d\n", length * 3);
      pdf_enddict(p);

      pdf_stream(p);
      for (i = 0; i < length; i++)
        {
          red = (*rgba & 0xff);
          green = (*rgba & 0xff00) >> 8;
          blue = (*rgba & 0xff0000) >> 16;
          rgba++;
          data[0] = (Byte) red;
          data[1] = (Byte) green;
          data[2] = (Byte) blue;
          pdf_memcpy(p->stream, (char *) data, 3);
        }
      pdf_printf(p->stream, "\n");
      pdf_endstream(p);
      pdf_endobj(p);
    }

  start_xref = p->stream->length;
  pdf_printf(p->stream, "xref\n");
  pdf_printf(p->stream, "0 %ld\n", p->object_number + 1);
  pdf_printf(p->stream, "0000000000 65535 f \n");
  for (object = 1; object <= p->object_number; object++)
    pdf_printf(p->stream, "%010ld 00000 n \n", p->byte_offset[object]);

  pdf_printf(p->stream, "trailer\n");
  pdf_dict(p);
  pdf_printf(p->stream, "/Size %ld\n", p->object_number + 1);
  pdf_printf(p->stream, "/Root %ld 0 R\n", p->root);
  pdf_printf(p->stream, "/Info %ld 0 R\n", p->info);
  pdf_enddict(p);
  pdf_printf(p->stream, "startxref\n");
  pdf_printf(p->stream, "%ld\n", start_xref);

  pdf_printf(p->stream, "%%%%EOF\n");

  gks_write_file(p->fd, p->stream->buffer, p->stream->length);

  free(p->stream->buffer);

  free(p->image);
  free(p->page);
  free(p->byte_offset);
}

static
void pdf_text_ex(PDF *p, double xorg, double yorg, char *text)
{
  double rad, c, s;

  rad = p->angle * M_PI / 180;
  c = cos(rad);
  s = sin(rad);

  pdf_printf(p->content,
             "BT\n/F%d %.2f Tf\n%s %s %s %s %.2f %.2f Tm\n(%s) Tj\nET\n",
             p->font, p->pt, pdf_double(c), pdf_double(s), pdf_double(-s),
             pdf_double(c), xorg, yorg, text);
}

static
void set_norm_xform(int tnr, double *wn, double *vp)
{
  a[tnr] = (vp[1] - vp[0]) / (wn[1] - wn[0]);
  b[tnr] = vp[0] - wn[0] * a[tnr];
  c[tnr] = (vp[3] - vp[2]) / (wn[3] - wn[2]);
  d[tnr] = vp[2] - wn[2] * c[tnr];
}

static
void init_norm_xform(void)
{
  int tnr;

  for (tnr = 0; tnr < MAX_TNR; tnr++)
    set_norm_xform(tnr, gkss->window[tnr], gkss->viewport[tnr]);
}

static
void set_xform(void)
{
  p->a = p->width / (p->window[1] - p->window[0]);
  p->c = p->height / (p->window[3] - p->window[2]);
  p->b = (p->viewport[0] * POINTS_PER_M - p->window[0] * p->a);
  p->d = (p->viewport[2] * POINTS_PER_M - p->window[2] * p->c);
}

static
void seg_xform(double *x, double *y)
{
  double xx;

  xx = *x * gkss->mat[0][0] + *y * gkss->mat[0][1] + gkss->mat[2][0];
  *y = *x * gkss->mat[1][0] + *y * gkss->mat[1][1] + gkss->mat[2][1];
  *x = xx;
}

static
void seg_xform_rel(double *x, double *y)
{
  double xx;

  xx = *x * gkss->mat[0][0] + *y * gkss->mat[0][1];
  *y = *x * gkss->mat[1][0] + *y * gkss->mat[1][1];
  *x = xx;
}

static
void set_color_rep(int color, double red, double green, double blue)
{
  if (color >= 0 && color < MAX_COLOR)
    {
      p->red[color] = red;
      p->green[color] = green;
      p->blue[color] = blue;
      p->color = p->fillcolor = -1;
    }
}

static
void init_colors(void)
{
  int color;
  double red, green, blue;

  for (color = 0; color < MAX_COLOR; color++)
    {
      gks_inq_rgb(color, &red, &green, &blue);
      p->red[color] = red;
      p->green[color] = green;
      p->blue[color] = blue;
    }
  for (color = 0; color < 256; color++)
    p->have_alpha[color] = 0;
}

static
void create_patterns(void)
{
  int i, j, k;
  int pattern, parray[33];

  for (i = 0; i < PATTERNS; i++)
    {
      pattern = i;
      gks_inq_pattern_array(pattern, parray);
      for (j = 0, k = 1; j < 16; j += 2)
        {
          sprintf(bitmap[i] + j, "%02x", parray[k]);
          if (++k > *parray)
            k = 1;
        }
      bitmap[i][16] = '\0';
      p->have_pattern[i] = 0;
    }
}


static
void open_ws(int fd, int wstype)
{
  p = (ws_state_list *) pdf_calloc(1, sizeof(struct ws_state_list_t));

  p->compress = wstype == 102;
  p->window[0] = p->window[2] = 0.0;
  p->window[1] = p->window[3] = 1.0;
  p->viewport[0] = p->viewport[2] = 0;
  p->viewport[1] = p->viewport[3] = 0.16;
  p->width = p->height = 0.16 * POINTS_PER_M;
  p->color = p->fillcolor = -1;
  p->alpha = 0xff;
  p->ltype = -999; p->lwidth = -1.0;
  p->font = 1; p->size = 24; p->angle = 0;
  p->pt = p->size / capheights[0];
  p->clip  = 1;

  p->fd = fd;
  p->stream = pdf_alloc_stream();

  p->object_number = p->current_page = 0;
  p->max_objects = MAX_OBJECTS;
  p->byte_offset = (long *) pdf_calloc(p->max_objects, sizeof(long));

  p->info = pdf_alloc_id(p);
  p->root = pdf_alloc_id(p);
  p->outlines = pdf_alloc_id(p);
  p->pages = pdf_alloc_id(p);
  p->max_pages = MAX_PAGES;
  p->max_images = MAX_IMAGES;
  p->images = 0;
  p->content = 0;

  p->page = (PDF_page **) pdf_calloc(p->max_pages, sizeof(PDF_page *));
  p->image = (PDF_image **) pdf_calloc(p->max_images, sizeof(PDF_image *));

  set_xform();
  init_norm_xform();
  init_colors();
  create_patterns(); // UNINITIALISED: pattern, pattern_id
}

static
void set_clip(double *clrt)
{
  double x0, x1, y0, y1;

  NDC_to_DC(clrt[0], clrt[2], x0, y0);
  NDC_to_DC(clrt[1], clrt[3], x1, y1);

  pdf_moveto(p, x0, y0);
  pdf_lineto(p, x1, y0);
  pdf_lineto(p, x1, y1);
  pdf_lineto(p, x0, y1);
  pdf_closepath(p);
  pdf_clip(p);
}

static
void set_color(int color)
{
  if (p->color != color)
    {
      pdf_setrgbcolor(p, p->red[color], p->green[color], p->blue[color]);
      p->color = color;
    }
}

static
void set_fillcolor(int color)
{
  if (p->fillcolor != color)
    {
      pdf_setfillcolor(p, p->red[color], p->green[color], p->blue[color]);
      p->fillcolor = color;
    }
}

static
void set_transparency(int alpha)
{
  pdf_setalpha(p, alpha);
  p->alpha = alpha;
  p->have_alpha[alpha] = 1;
}

static
void close_ws(void)
{
  pdf_close(p);
  free(p);
}

static
void clear_ws(void)
{
  p->content = 0;
}

static
void line_routine(int n, double *px, double *py, int ltype, int tnr)
{
  int i;
  double x, y, xdev, ydev;

  for (i = 0; i < n; i++)
    {
      WC_to_NDC(px[i], py[i], tnr, x, y);
      seg_xform(&x, &y);
      NDC_to_DC(x, y, xdev, ydev);

      if (i == 0)
        pdf_moveto(p, xdev, ydev);
      else
        pdf_lineto(p, xdev, ydev);
    }

  if (ltype == DrawBorder) 
    pdf_closepath(p);
  pdf_stroke(p);
}

static
void set_linetype(int ltype, double lwidth)
{
  if (p->ltype != ltype || p->lwidth != lwidth)
    {
      char dash[80];
      gks_get_dash(ltype, lwidth, dash);
      pdf_setdash(p, dash);
      p->ltype = ltype;
    }
}

static
void set_linewidth(double lwidth)
{
  if (p->lwidth != lwidth)
    {
      pdf_setlinewidth(p, lwidth);
      p->lwidth = lwidth;
    }
}

static
void polyline(int n, double *px, double *py)
{
  int tnr         = gkss->cntnr;
  int ln_type     = gkss->asf[0] ? gkss->ltype : gkss->lindex;
  int ln_color    = gkss->asf[2] ? gkss->plcoli : 1;
  double ln_width = gkss->asf[1] ? gkss->lwidth : 1;
  double *ncr     = CLIP_RECT(gkss); // in NDC
  double margin   = 2*ln_width;
  double xx, yy;
  double dcr[4]; // device clip rectangle

  set_linetype(ln_type, ln_width);
  set_linewidth(ln_width);
  set_transparency(p->alpha);
  set_color(ln_color);

  NDC_to_DC(ncr[0], ncr[2], dcr[0], dcr[2]);
  NDC_to_DC(ncr[1], ncr[3], dcr[1], dcr[3]);

  dcr[0] -= margin; dcr[1] += margin;
  dcr[2] -= margin; dcr[3] += margin;

  PUSH_CLIP;
  if (px[0] == px[n-1] && py[0] == py[n-1] && (n--,1)) { // closed path, ignore last point
    double *xs = (double *)pdf_calloc(n, sizeof(double));
    double *ys = (double *)pdf_calloc(n, sizeof(double));
    char *clip_codes  = (char *)pdf_calloc(n, sizeof(char));
    char *draw_seg    = (char *)pdf_calloc(n, sizeof(char));
    int  i, j, num_visible_segs = 0;

    for (i=0; i<n; i++) { // device coords and clip codes for all points
      WC_to_NDC(px[i], py[i], tnr, xx, yy);
      seg_xform(&xx, &yy);
      NDC_to_DC(xx, yy, xs[i], ys[i]);
      clip_codes[i] = CLIP_CODE(dcr, xs[i], ys[i]);
    }

    for (i=n-1, j=0; j<n; i=j, j++) { // visibility of all segments i--j, cyclic closure
      draw_seg[j] = COULD_BE_VISIBLE(clip_codes[i], clip_codes[j]);
      if (draw_seg[j]) num_visible_segs++; // for segment ENDING on point j
    }

    if (2*num_visible_segs >= n) { // no fancy stuff - draw them all
      pdf_moveto(p, xs[0], ys[0]);
      for (i = 1; i < n; i++) pdf_lineto(p, xs[i], ys[i]);
      pdf_closepath(p);
    } else if (num_visible_segs > 0) { // reduce PDF size by skipping segments
      // The trick here is to join the first and last segments correctly (if
      // they are visible) without relying on pdf_closepath(), which will not
      // work as required when there are breaks in the path. We do this by rotating
      // the traversal of points to start and end at break in the path.
   
      int k, l; // counters for rotated traversal
      // find first visible segment following an invisible segment
      for (i=n-1, j=0; draw_seg[i] || !draw_seg[j]; i=j, j++);

      // begin path with segment i--j
      for (k=i, l=j; l < n; k=l, l++) {
	if (draw_seg[l]) {
	  if (!draw_seg[k]) pdf_moveto(p, xs[k], ys[k]);
	  pdf_lineto(p, xs[l], ys[l]);
	}
      }
      // continue cyclically until just before i--j
      for (l=0; l < j-1; k=l, l++) { // j-1 because we know last segment is invisible
	if (draw_seg[l]) {
	  if (!draw_seg[k]) pdf_moveto(p, xs[k], ys[k]);
	  pdf_lineto(p, xs[l], ys[l]);
	}
      }
    }

    free(xs); free(ys);
    free(clip_codes); 
    free(draw_seg); 
  } else {
    // open path: compute and draw everything incrementally
    double x1, y1, x2, y2;
    int i, pendown, c1, c2;

    WC_to_NDC(px[0], py[0], tnr, xx, yy);
    seg_xform(&xx, &yy);
    NDC_to_DC(xx, yy, x1, y1);
    c1 = CLIP_CODE(dcr, x1, y1);

    for (pendown=0, i = 1; i < n; i++) {
      WC_to_NDC(px[i], py[i], tnr, xx, yy);
      seg_xform(&xx, &yy);
      NDC_to_DC(xx, yy, x2, y2);
      c2 = CLIP_CODE(dcr, x2, y2);
      if (COULD_BE_VISIBLE(c1, c2)) {
	if (!pendown) pdf_moveto(p, x1, y1);
	pdf_lineto(p, x2, y2);
	pendown = 1;
      } else {
	printf("pdf> skipping segment in open polyline\n");
	pendown = 0;
      }
      x1 = x2; y1 = y2; c1 = c2;
    }
  }
  pdf_stroke(p);
  POP_CLIP;
}

static
void draw_marker(double xn, double yn, int mindex, double r, int mcolor)
{
  double scale = r/1000, x, y;
  int pc, op, curve, i;

  static double cx[4][3] = {
    { 0.5523, 1, 1 },
    { 1, 0.5523, 0 },
    { -0.5523, -1, -1 },
    { -1, -0.5523, 0 }
  };

  static double cy[4][3] = {
    { -1, -0.5523, 0 },
    { 0.5523, 1, 1 },
    { 1, 0.5523, 0 },
    { -0.5523, -1, -1 }
  };

  NDC_to_DC(xn, yn, x, y);

  pc = 0;
  do
  {
    op = marker[mindex][pc];
    switch (op)
    {
      case 1: /* point */
        pdf_moveto(p, x, y);
        pdf_lineto(p, x, y);
        pdf_stroke(p);
        break;

      case 2: /* line */
        for (i = 0; i < 2; i++)
        {
          double xr =  scale * marker[mindex][pc + 2 * i + 1];
          double yr = -scale * marker[mindex][pc + 2 * i + 2];
          seg_xform_rel(&xr, &yr);
          if (i == 0)
            pdf_moveto(p, x - xr, y - yr);
          else
            pdf_lineto(p, x - xr, y - yr);
        }
        pdf_stroke(p);
        pc += 4;
        break;

      case 3: /* polyline */
      case 4: /* filled polygon */
      case 5: /* hollow polygon */
        for (i = 0; i < marker[mindex][pc + 1]; i++)
        {
          double xr =  scale * marker[mindex][pc + 2 + 2 * i];
          double yr = -scale * marker[mindex][pc + 3 + 2 * i];
          seg_xform_rel(&xr, &yr);
          if (i == 0)
            pdf_moveto(p, x - xr, y - yr);
          else
            pdf_lineto(p, x - xr, y - yr);
        }
        if (op==4) pdf_eofill(p); else pdf_stroke(p);
        pc += 1 + 2 * marker[mindex][pc + 1];
        break;

      case 6: /* arc */
      case 7: /* filled arc */
      case 8: /* hollow arc */
	{
	  double xr =  0;
	  double yr =  -r;
	  seg_xform_rel(&xr, &yr);
	  pdf_moveto(p, x - xr, y - yr);
	  for (curve = 0; curve < 4; curve++)
	  {
	    for (i = 0; i < 3; i++)
	    {
	      double xr = r * cx[curve][i];
	      double yr = r * cy[curve][i];
	      seg_xform_rel(&xr, &yr);
	      pdf_point(p, x - xr, y - yr);
	    }
	    pdf_curveto(p);
	  }
	  if (op==7) pdf_eofill(p); else pdf_stroke(p);
	  break;
	}
    }
    pc++;
  }
  while (op != 0);
}

static
void polymarker(int n, double *px, double *py)
{
  int    mk_type = gkss->asf[3] ? gkss->mtype : gkss->mindex;
  int    mk_color = gkss->asf[5] ? gkss->pmcoli : 1;
  double mk_size = gkss->asf[4] ? gkss->mszsc : 1;
  double *cr = CLIP_RECT(gkss), x, y, radius;
  int    mk_index = mk_type + marker_off, i;

  {
    double xr = mk_size, yr = 0;
    seg_xform_rel(&xr, &yr);
    radius = sqrt(xr * xr + yr * yr);
  }

  set_linetype(GKS_K_LINETYPE_SOLID, radius / 4);
  set_linewidth(radius / 4);
  set_transparency(p->alpha);
  set_color(mk_color);
  set_fillcolor(mk_color);

  PUSH_CLIP;
  for (i = 0; i < n; i++)
    {
      WC_to_NDC(px[i], py[i], gkss->cntnr, x, y);
      seg_xform(&x, &y);

      if (x >= cr[0] && x <= cr[1] && y >= cr[2] && y <= cr[3])
        draw_marker(x, y, mk_index, radius, mk_color);
    }
  POP_CLIP;
}

static
void set_font(int font)
{
  double ux, uy, scale;
  double width, height;
  PDF_page *page = p->page[p->current_page - 1];

  font = abs(font);
  if (font >= 101 && font <= 131)
    font -= 100;
  else if (font >= 1 && font <= 32)
    font = map[font - 1];
  else
    font = 9;

  font--;
  if (!page->fonts[font])
    {
      page->fonts[font] = pdf_alloc_id(p);
      pdf_alloc_id(p);
    }
  p->font = font;

  WC_to_NDC_rel(gkss->chup[0], gkss->chup[1], gkss->cntnr, ux, uy);
  seg_xform_rel(&ux, &uy);

  p->angle = -atan2(ux, uy) * 180 / M_PI;
  if (p->angle < 0)
    p->angle += 360;

  scale = sqrt(gkss->chup[0] * gkss->chup[0] + gkss->chup[1] * gkss->chup[1]);
  ux = gkss->chup[0] / scale * gkss->chh;
  uy = gkss->chup[1] / scale * gkss->chh;
  WC_to_NDC_rel(ux, uy, gkss->cntnr, ux, uy);

  width = 0;
  height = sqrt(ux * ux + uy * uy);
  seg_xform_rel(&width, &height);

  height = sqrt(width * width + height * height);
  p->size = height * fabs(p->c);
  p->pt = p->size / capheights[p->font];
}

static
void text_routine(double x, double y, int nchars, char *chars)
{
  int tx_font = gkss->asf[6] ? gkss->txfont : predef_font[gkss->tindex - 1];
  int tx_prec = gkss->asf[6] ? gkss->txprec : predef_prec[gkss->tindex - 1];
  double xorg, yorg;
  char s[BUFSIZ], *cp;
  int ch, i;

  NDC_to_DC(x, y, xorg, yorg);

  if (tx_prec == GKS_K_TEXT_PRECISION_STRING)
    {
      double phi = p->angle * M_PI / 180;
      double ax, ay,xrel, yrel, width=0;
      stroke_data_t buffer;

      for (i = 0; i < nchars; i++)
        {
          ch = chars[i];
          gks_lookup_afm(tx_font, ch, &buffer);
          width += buffer.right - buffer.left;
        }

      xrel = (width * p->size / buffer.size) * xfac[gkss->txal[0]];
      yrel = p->size * yfac[gkss->txal[1]];
      CharXform(phi, xrel, yrel, ax, ay);

      xorg += ax;
      yorg += ay;
    }

  cp = s;
  for (i = 0; i < nchars; i++)
    {
      ch = chars[i];
      if (ch == '(' || ch == ')' || ch == '\\')
        *cp++ = '\\';
      *cp++ = ch;
    }
  *cp = '\0';

  if (fabs(p->angle) > FEPS)
    pdf_text_ex(p, xorg, yorg, s);
  else
    pdf_text(p, xorg, yorg, s);
}

static
void text(double px, double py, int nchars, char *chars)
{
  int tx_font = gkss->asf[6] ? gkss->txfont : predef_font[gkss->tindex - 1];
  int tx_prec = gkss->asf[6] ? gkss->txprec : predef_prec[gkss->tindex - 1];
  int tx_color = gkss->asf[9] ? gkss->txcoli : 1;
  double x, y;

  if (nchars == 0) return;
  set_linetype(GKS_K_LINETYPE_SOLID, 1.0);
  set_linewidth(1.0);
  set_transparency(p->alpha);
  set_color(tx_color);
  set_fillcolor(tx_color);

  if (tx_prec != GKS_K_TEXT_PRECISION_STROKE)
    set_font(tx_font);

  PUSH_CLIP;
  if (tx_prec == GKS_K_TEXT_PRECISION_STRING)
    {
      WC_to_NDC(px, py, gkss->cntnr, x, y);
      seg_xform(&x, &y);

      text_routine(x, y, nchars, chars);
    }
  else
    {
      gks_emul_text(px, py, nchars, chars, line_routine, fill_routine);
    }
  POP_CLIP;
}

static
void fill_routine(int n, double *px, double *py, int tnr)
{
  int i;
  double x, y, xdev, ydev;

  gks_set_dev_xform(gkss, p->window, p->viewport);

  if (p->pattern)
    pdf_printf(p->content, "/Pattern cs/P%d scn\n", p->pattern);

  for (i = 0; i < n; i++)
    {
      WC_to_NDC(px[i], py[i], tnr, x, y);
      seg_xform(&x, &y);
      NDC_to_DC(x, y, xdev, ydev);

      if (i == 0)
        pdf_moveto(p, xdev, ydev);
      else
        pdf_lineto(p, xdev, ydev);
    }

  if (p->pattern)
    {
      pdf_printf(p->content, "f/Pattern cs/P0 scn\n");

      if (!p->have_pattern[p->pattern])
        {
          p->have_pattern[p->pattern] = 1;
          p->pattern_id[p->pattern][0] = pdf_alloc_id(p);
          p->pattern_id[p->pattern][1] = pdf_alloc_id(p);
        }
      if (!p->have_pattern[0])
        {
          p->have_pattern[0] = 1;
          p->pattern_id[0][0] = pdf_alloc_id(p);
          p->pattern_id[0][1] = pdf_alloc_id(p);
        }
    }
  else
    {
      pdf_eofill(p);
    }
}

static
void fillarea(int n, double *px, double *py)
{
  int fl_inter, fl_style, fl_color;

  fl_inter = gkss->asf[10] ? gkss->ints : predef_ints[gkss->findex - 1];
  fl_style = gkss->asf[11] ? gkss->styli : predef_styli[gkss->findex - 1];
  fl_color = gkss->asf[12] ? gkss->facoli : 1;

  p->pattern = 0;
  if (fl_inter == GKS_K_INTSTYLE_HOLLOW)
    {
      set_linetype(GKS_K_LINETYPE_SOLID, 1.0);
      set_linewidth(1.0);
      set_transparency(p->alpha);
      set_color(fl_color);

      PUSH_CLIP;
      line_routine(n, px, py, DrawBorder, gkss->cntnr);
      POP_CLIP;
    }
  else if (fl_inter == GKS_K_INTSTYLE_SOLID)
    {
      set_transparency(p->alpha);
      set_fillcolor(fl_color);
      PUSH_CLIP;
      fill_routine(n, px, py, gkss->cntnr);
      POP_CLIP;
    }
  else if (fl_inter == GKS_K_INTSTYLE_PATTERN ||
           fl_inter == GKS_K_INTSTYLE_HATCH)
    {
      set_transparency(p->alpha);
      set_fillcolor(fl_color);

      if (fl_inter == GKS_K_INTSTYLE_HATCH)
        fl_style += HATCH_STYLE;
      if (fl_style >= PATTERNS)
        fl_style = 1;
      p->pattern = fl_style;

      PUSH_CLIP;
      fill_routine(n, px, py, gkss->cntnr);
      POP_CLIP;
    }
}

static
void cellarray(double xmin, double xmax, double ymin, double ymax,
               int dx, int dy, int dimx, int *colia, int true_color)
{
  double x1, y1, x2, y2;
  int x, y, width, height;
  double rx1, rx2, ry1, ry2;
  int i, j, ix, iy, color;
  PDF_image *image;
  int swapx, swapy, count, chars_per_line;
  unsigned char data[3];
  int have_alpha;

  WC_to_NDC(xmin, ymax, gkss->cntnr, x1, y1);
  seg_xform(&x1, &y1);
  NDC_to_DC(x1, y1, rx1, ry1);

  WC_to_NDC(xmax, ymin, gkss->cntnr, x2, y2);
  seg_xform(&x2, &y2);
  NDC_to_DC(x2, y2, rx2, ry2);

  width = (int) (fabs(rx2 - rx1));
  height = (int) (fabs(ry2 - ry1));
  if (width == 0 || height == 0) return;
  x = (int) min(rx1, rx2);
  y = (int) min(ry1, ry2);

  swapx = rx1 > rx2;
  swapy = ry1 > ry2;

  set_transparency(p->alpha);

  PUSH_CLIP;
  pdf_save(p);
  pdf_printf(p->content, "%d 0 0 %d %d %d cm\n", width, height, x, y);

  have_alpha = 0;
  if (true_color)
    {
      for (j = 0; j < dy; j++)
        for (i = 0; i < dx; i++)
          if ((colia[j * dimx + i] & 0xff000000) != 0xff000000)
            {
              have_alpha = 1;
              break;
            }
    }

  if (true_color && have_alpha)
    {
      image = pdf_image(p, dx, dy);
      p->image[p->images++] = image;

      for (j = 0; j < dy; j++)
        {
          iy = swapy ? dy - 1 - j : j;
          for (i = 0; i < dx; i++)
            {
              ix = swapx ? dx - 1 - i : i;
              image->data[j * dx + i] = colia[iy * dimx + ix];
            }
        }

      pdf_printf(p->content, "/Im%d Do\n", p->images);
      p->page[p->current_page - 1]->last_image = p->images;
    }
  else
    {
      pdf_printf(p->content, "BI\n");
      pdf_printf(p->content, "/W %d\n", dx);
      pdf_printf(p->content, "/H %d\n", dy);
      pdf_printf(p->content, "/BPC %d\n", 8);
      pdf_printf(p->content, "/CS /RGB\n");
      pdf_printf(p->content, "/F /AHx\n");
      pdf_printf(p->content, "ID ");

      chars_per_line = 0;
      for (j = 0; j < dy; j++)
        {
          iy = swapy ? dy - 1 - j : j;
          for (i = 0; i < dx; i++)
            {
              ix = swapx ? dx - 1 - i : i;
              color = colia[iy * dimx + ix];
              if (!true_color)
                {
                  color = FIX_COLORIND(color);
                  data[0] = (Byte) (p->red[color]   * 255);
                  data[1] = (Byte) (p->green[color] * 255);
                  data[2] = (Byte) (p->blue[color]  * 255);
                }
              else
                {
                  data[0] = (Byte) ( color & 0xff           );
                  data[1] = (Byte) ((color & 0xff00)   >>  8);
                  data[2] = (Byte) ((color & 0xff0000) >> 16);
                }
              for (count = 0; count < 3; count++)
                {
                  pdf_printf(p->content, "%02x", data[count]);
                  if ((chars_per_line += 2) >= 64)
                    {
                      pdf_printf(p->content, "\n");
                      chars_per_line = 0;
                    }
                }
            }
        }

      pdf_printf(p->content, ">EI\n");
    }

  pdf_restore(p);
  POP_CLIP;
}

#define BP if (!p->content) { pdf_page(); set_clip(p->window); }

#ifndef EMSCRIPTEN
void gks_drv_pdf(
#else
void gks_drv_js(
#endif
  int fctid, int dx, int dy, int dimx, int *ia,
  int lr1, double *r1, int lr2, double *r2, int lc, char *chars, void **ptr)
{
  p = (ws_state_list *) * ptr;

  switch (fctid)
    {
    case 2:
/* open workstation */
      gkss = (gks_state_list_t *) * ptr;
      open_ws(ia[1], ia[2]);
      *ptr = p;
      break;

    case 4: case 5: case 8: break; // activate, deactivate, update == no-op
    case 3: close_ws(); break;     // close workstation
    case 6: clear_ws(); break;     // clear workstation
    case 12: BP; polyline(ia[0], r1, r2);   break;
    case 13: BP; polymarker(ia[0], r1, r2); break;
    case 14: BP; text(r1[0], r2[0], strlen(chars), chars); break;
    case 15: BP; fillarea(ia[0], r1, r2);   break;
    case 16:
    case DRAW_IMAGE:
      BP; 
      cellarray(r1[0], r1[1], r2[0], r2[1], dx, dy, dimx, ia, fctid == DRAW_IMAGE);
      break;

    case 48:
/* set color representation */
      set_color_rep(ia[1], r1[0], r1[1], r1[2]);
      break;

    case 49:
/* set window */
      set_norm_xform(ia[0], gkss->window[ia[0]], gkss->viewport[ia[0]]);
      break;

    case 50:
/* set viewport */
      set_norm_xform(ia[0], gkss->window[ia[0]], gkss->viewport[ia[0]]);
      break;

    case 54:
/* set workstation window */
      p->window[0] = r1[0];
      p->window[1] = r1[1];
      p->window[2] = r2[0];
      p->window[3] = r2[1];

      set_xform();
      if (p->content) {
	printf("pdf> WARNING: changing ws window on non-empty page\n");
	set_clip(p->window);
      }
      break;

    case 55:
/* set workstation viewport */
      p->viewport[0] = r1[0];
      p->viewport[1] = r1[1];
      p->viewport[2] = r2[0];
      p->viewport[3] = r2[1];
      p->width  = (r1[1] - r1[0]) * POINTS_PER_M;
      p->height = (r2[1] - r2[0]) * POINTS_PER_M;

      set_xform();
      if (p->content) {
	printf("pdf> WARNING: changing ws viewport on non-empty page\n");
	set_clip(p->window);
      }
      break;

    case 203:
/* set transparency */
      p->alpha = (int) (r1[0] * 255.0);
      break;
    }
}
