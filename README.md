This is a fork of https://github.com/jheinen/gr

It contains fixes and modifications addressing several issues:

Configuration
- Enables Xft for anti-aliased font rendering in X11 by default.
- On Mac OS, uses Macports for dependencies.
- Uses pkg-config to find X11 installation

Bug fixes
-  Potential buffer overflows relating to the use of fixed size
   string buffers of length MAXPATHLEN (including all calls to
   `gks_filepath`) have been completely eliminated
   by using asprintf to allocate memory on demand.
-  PDF resolution has been corrected to 72 dpi exactly
-  Handling of window resize requests in GKSTerm.app has been fixed so
   as not to request window resize during painting, and now takes
   effect at the right time, cooperating with UPDATE requests to
   minimise flickering.
-  Polyine renderers in Quartz and PDF drivers now correctly join the 
   first and last segments when given a closed polygon, consistent with
   behaviour of X11 and SVG drivers. This is important because the polyline
   renderer is the only way to draw a closed polygon with an arbitrary line
   width. In particular, drawrect relies on this, and without it, rectangles
   drawn with thick lines were incorrectly rendered.
-  Clipping in the PDF driver now works correctly with all graphics
   primitives. In particular, the polyline renderer has been rewritten
   to produce correctly output even when thick lines are clipped at the 
   viewport edge, while minimising the PDF file size when many line segments
   are outside the clip region.
-  Uses of named clip regions in SVG driver has been fixed in the case when
   the clip region cache is full.

Enhancements
-  X11, Quartz, SVG and PDF backends all updated to handle fractional font sizes.
-  X11, Quartz, SVG and PDF backends all interpret line width in physical points,
   ie units of 1/72 inch. Line widths (and marker sizes) no longer scale with
   viewport (width + height).
-  Marker rendering in X11, Quartz, SVG and PDF backends has been modified
   consistently to interpret the marker size as an approximate RADIUS
   in POINTS. The marker size is no longer quantised to an integer.
   Also, 'hollow' marker shapes are no longer filled with white,
   but left truly hollow. Also, the stroke width in marker rendering is 
   made proportional to the marker size so that the resulting marker is
   effectively a 'glyph', like a character in a font, with the same shape
   at any size.
-  Quartz and X11 drivers now keep track of both 'requested size' as specified
   in setwsviewport (in metres) and actual size as resulting from negotiation
   with window manager. This results in a 'zoom factor', which is applied
   to line widths and marker sizes, so that (a) line widths and marker sizes
   appear at their correct physical size in points if the window is allowed
   to assume its requested size, or the whole graphic is scaled uniformly to
   reach the actual window size.
-  X11 viewport set up improved to centre properly and treat pixel grid
   consistently, with integer coordinates corresponded to pixel CENTRES.
-  Maximium PDF size increased to approx 1m square


Optimisation and simplification
-  In GKS, operations are targetted at the relevant workstations at
   a higher level, making WS activity checks at lower levels redundant.
-  State data structure of PDF driver simplified to remove redundant information.

Remaining issues
-  Window resize event processing in X11 driver still not quite right.
-  Other backends remain to be updated to match behaviour of X11, Quartz, SVG and PDF.

GR - a universal framework for visualization applications
=========================================================

[![MIT license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE.md)
[![GitHub tag](https://img.shields.io/github/tag/jheinen/gr.svg)](https://github.com/jheinen/gr/releases)
[![PyPI version](https://img.shields.io/pypi/v/gr.svg)](https://pypi.python.org/pypi/gr)

*GR* is a universal framework for cross-platform visualization applications.
It offers developers a compact, portable and consistent graphics library for
their programs. Applications range from publication quality 2D graphs to the
representation of complex 3D scenes.

*GR* is essentially based on an implementation of a Graphical Kernel System (GKS)
and OpenGL. As a self-contained system it can quickly and easily be integrated
into existing applications (i.e. using the `ctypes` mechanism in Python or `ccall`
in Julia).

The *GR* framework can be used in imperative programming systems or integrated
into modern object-oriented systems, in particular those based on GUI toolkits.
*GR* is characterized by its high interoperability and can be used with modern
web technologies. The *GR* framework is especially suitable for real-time
or signal processing environments.

*GR* was developed by the Scientific IT-Systems group at the Peter Grünberg
Institute at Forschunsgzentrum Jülich. The main development has been done
by Josef Heinen who currently maintains the software, but there are other
developers who currently make valuable contributions. Special thanks to
Florian Rhiem (*GR3*] and Christian Felder (qtgr, setup.py).

Starting with release 0.6 *GR* can be used as a backend
for [Matplotlib](http://matplotlib.org) and significantly improve
the performance of existing Matplotlib or PyPlot applications written
in Python or Julia, respectively.
In [this](http://gr-framework.org/tutorials/matplotlib.html) tutorial
section you can find some examples.

Beginning with version 0.10.0 *GR* supports inline graphics which shows
up in IPython's Qt Console or interactive computing environments for *Python*
and *Julia*, such as [IPython](http://ipython.org) and
[Jupyter](https://jupyter.org). An interesting example can be found
[here](http://pgi-jcns.fz-juelich.de/pub/doc/700K_460.html).

For further information please refer to the [GR home page](http://gr-framework.org).
