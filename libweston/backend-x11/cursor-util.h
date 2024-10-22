/*
*   In order to avoid introducing the libxcursor and libxrender libraries,
*   we will separately introduce some APIs and implementations of the Xrender.h
*   and Xcursor. h header files.
*
*/

#ifndef _CURSOR_UTIL_H_
#define _CURSOR_UTIL_H_

#include <X11/Xlibint.h>
#include <X11/extensions/renderproto.h>
#include <limits.h>
#include <stdio.h>

#define XCURSOR_IMAGE_MAX_SIZE	    0x7fff	/* 32767x32767 max cursor size */
#define XCURSOR_IMAGE_VERSION	    1

typedef int XcursorBool;
#define XcursorTrue 1
#define XcursorFalse 0

#define RenderHasExtension(i) ((i) && ((i)->codes))
#define RenderCheckExtension(dpy,i,val) \
  if (!RenderHasExtension(i)) { return val; }
#define RenderSimpleCheckExtension(dpy,i) \
  if (!RenderHasExtension(i)) { return; }

typedef uint32_t	XcursorUInt;

typedef XcursorUInt	XcursorDim;
typedef XcursorUInt	XcursorPixel;

typedef struct _XcursorImage {
    XcursorUInt	    version;	/* version of the image data */
    XcursorDim	    size;	/* nominal size for matching */
    XcursorDim	    width;	/* actual width */
    XcursorDim	    height;	/* actual height */
    XcursorDim	    xhot;	/* hot spot x (must be inside image) */
    XcursorDim	    yhot;	/* hot spot y (must be inside image) */
    XcursorUInt	    delay;	/* animation delay to next frame (ms) */
    XcursorPixel    *pixels;	/* pointer to pixels */
} XcursorImage;


typedef struct XcursorCoreCursor {
    XImage  *src_image;
    XImage  *msk_image;
    XColor  on_color;
    XColor  off_color;
} XcursorCoreCursor;

#define ScaledPixels(c,n) ((c)/(XcursorPixel)(n))

typedef struct {
    /** Red component binary displacement. */
    short red;
    /** Red component bit mask. */
    short redMask;
    /** Green component binary displacement. */
    short green;
    /** Green component bit mask. */
    short greenMask;
    /** Blue component binary displacement. */
    short blue;
    /** Blue component bit mask. */
    short blueMask;
    /** Alpha component binary displacement. */
    short alpha;
    /** Alpha component bit mask. */
    short alphaMask;
} XRenderDirectFormat;


typedef struct {
    /** XID of this structure server instance. */
    PictFormat id;
    /** Color management type. */
    int type;
    /** Pixel bit depth. */
    int depth;
    /** Color component description. */
    XRenderDirectFormat direct;
    /** XID of the map of indexed colors on the server. */
    Colormap colormap;
} XRenderPictFormat;

#define PictStandardARGB32  0
#define PictStandardNUM 5

/*< XRenderPictFormat template field masks.
 * @{
 */
/** Include ID field. @hideinitializer */
#define PictFormatID (1 << 0)
/** Include Type field. @hideinitializer */
#define PictFormatType (1 << 1)
/** Include Depth field. @hideinitializer */
#define PictFormatDepth (1 << 2)

/*<--- XRenderPictFormat->direct fields. */
/** Include Direct->Red field. @hideinitializer */
#define PictFormatRed (1 << 3)
/** Include Direct->RedMask field. @hideinitializer */
#define PictFormatRedMask (1 << 4)
/** Include Direct->Green field. @hideinitializer */
#define PictFormatGreen (1 << 5)
/** Include Direct->GreenMask field. @hideinitializer */
#define PictFormatGreenMask (1 << 6)
/** Include Direct->Blue field. @hideinitializer */
#define PictFormatBlue (1 << 7)
/** Include Direct->BlueMask field. @hideinitializer */
#define PictFormatBlueMask (1 << 8)
/** Include Direct->Alpha field. @hideinitializer */
#define PictFormatAlpha (1 << 9)
/** Include Direct->AlphaMask field. @hideinitializer */
#define PictFormatAlphaMask (1 << 10)

/** Include Colormap field. @hideinitializer */
#define PictFormatColormap (1 << 11)



typedef struct {
    Visual		*visual;
    XRenderPictFormat	*format;
} XRenderVisual;

typedef struct {
    int			depth;
    int			nvisuals;
    XRenderVisual	*visuals;
} XRenderDepth;

typedef struct {
    XRenderDepth	*depths;
    int			ndepths;
    XRenderPictFormat	*fallback;
    int			subpixel;
} XRenderScreen;

typedef struct _XRenderInfo {
    int			major_version;
    int			minor_version;
    XRenderPictFormat	*format;
    int			nformat;
    XRenderScreen	*screen;
    int			nscreen;
    XRenderDepth	*depth;
    int			ndepth;
    XRenderVisual	*visual;
    int			nvisual;
    int			*subpixel;
    int			nsubpixel;
    char		**filter;
    int			nfilter;
    short    		*filter_alias;
    int			nfilter_alias;
} XRenderInfo;

/* replaces XRenderExtDisplayInfo */
typedef struct _XRenderExtDisplayInfo {
    struct _XRenderExtDisplayInfo *next;    /* keep a linked list */
    Display                       *display; /* which display this is */
    XExtCodes                     *codes;   /* the extension protocol codes */
    XRenderInfo                   *info;    /* extra data for the extension to use */
} XRenderExtDisplayInfo;

/* replaces XExtensionInfo */
typedef struct _XRenderExtInfo {
    XRenderExtDisplayInfo  *head;           /* start of the list */
    XRenderExtDisplayInfo  *cur;            /* most recently used */
    int                     ndisplays;      /* number of displays */
} XRenderExtInfo;

XRenderExtInfo XRenderExtensionInfo;

/*
 * If the server is missing support for any of the required depths on
 * any screen, tell the application that Render is not present.
 */

#define DEPTH_MASK(d)	(1U << ((d) - 1))

/*
 * Render requires support for depth 1, 4, 8, 24 and 32 pixmaps
 */

#define REQUIRED_DEPTHS	(DEPTH_MASK(1) | \
			 DEPTH_MASK(4) | \
			 DEPTH_MASK(8) | \
			 DEPTH_MASK(24) | \
			 DEPTH_MASK(32))

typedef struct _DepthCheckRec {
    struct _DepthCheckRec *next;
    Display *dpy;
    CARD32  missing;
    unsigned long serial;
} DepthCheckRec, *DepthCheckPtr;

typedef struct _renderVersionState {
    unsigned long   version_seq;
    Bool	    error;
    int		    major_version;
    int		    minor_version;

} _XrenderVersionState;

/**
 * Picture rendering attributes.
 */
typedef struct _XRenderPictureAttributes {
    /** How to repeat the picture. */
    int repeat;

    /** A replacement alpha-map. Must be a pixmap-containing Picture. */
    Picture alpha_map;
    /** Horizontal displacement of the replacement alpha-map. */
    int alpha_x_origin;
    /** Vertical displacement of the replacement alpha-map. */
    int alpha_y_origin;

    /** Horizontal displacement of the clip mask. */
    int clip_x_origin;
    /** Vertical displacement of the clip mask. */
    int clip_y_origin;
    /** A r/w restriction to the drawable. */
    Pixmap clip_mask;

    /** Whether to receive GraphicsExpose events. @note Ignored field. */
    Bool graphics_exposures;
    /** How to clip pixels on subwindow overlap. */
    int subwindow_mode;
    /** Alpha mask generation mode. */
    int poly_edge;
    /** Alpha value rasterization mode. */
    int poly_mode;
    /** Dithering mode. @note Ignored field. */
    Atom dither;
    /** Treat alpha channels independently. */
    Bool component_alpha;
} XRenderPictureAttributes;

Cursor
XcursorImageLoadCursor (Display *dpy, const XcursorImage *image);

XcursorImage *
XcursorImageCreate (int width, int height);

void
XcursorImageDestroy (XcursorImage *image);

Cursor create_cursor(Display *display, uint8_t *data, int width, int height, int stride, int hot_x, int hot_y);

#endif /* _CURSOR_UTIL_H_ */