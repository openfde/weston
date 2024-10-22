#include "cursor-util.h"


static int XRenderCloseDisplay (Display *dpy, XExtCodes *codes);

XcursorImage *
XcursorImageCreate (int width, int height)
{
    XcursorImage    *image;

    if (width < 0 || height < 0)
       return NULL;
    if (width > XCURSOR_IMAGE_MAX_SIZE || height > XCURSOR_IMAGE_MAX_SIZE)
       return NULL;

    image = malloc (sizeof (XcursorImage) +
		    (size_t) (width * height) * sizeof (XcursorPixel));
    if (!image)
	return NULL;
    image->version = XCURSOR_IMAGE_VERSION;
    image->pixels = (XcursorPixel *) (image + 1);
    image->size = (XcursorDim) (width > height ? width : height);
    image->width = (XcursorDim) width;
    image->height = (XcursorDim) height;
    image->delay = 0;
    return image;
}

void
XcursorImageDestroy (XcursorImage *image)
{
    free (image);
}

static int
nativeByteOrder (void)
{
    int	x = 1;

    return (*((char *) &x) == 1) ? LSBFirst : MSBFirst;
}

/*
 * XRenderExtFindDisplay - look for a display in this extension; keeps a
 * cache of the most-recently used for efficiency. (Replaces
 * XextFindDisplay.)
 */
static XRenderExtDisplayInfo *
XRenderExtFindDisplay (XRenderExtInfo *extinfo,
                       Display        *dpy)
{
    XRenderExtDisplayInfo *dpyinfo;

    /*
     * see if this was the most recently accessed display
     */
    if ((dpyinfo = extinfo->cur) && dpyinfo->display == dpy)
        return dpyinfo;

    /*
     * look for display in list
     */
    _XLockMutex(_Xglobal_lock);
    for (dpyinfo = extinfo->head; dpyinfo; dpyinfo = dpyinfo->next) {
        if (dpyinfo->display == dpy) {
            extinfo->cur = dpyinfo;     /* cache most recently used */
            _XUnlockMutex(_Xglobal_lock);
            return dpyinfo;
        }
    }
    _XUnlockMutex(_Xglobal_lock);

    return NULL;
}

/*
 * XRenderExtRemoveDisplay - remove the indicated display from the
 * extension object. (Replaces XextRemoveDisplay.)
 */
static int
XRenderExtRemoveDisplay (XRenderExtInfo *extinfo, Display *dpy)
{
    XRenderExtDisplayInfo *dpyinfo, *prev;

    /*
     * locate this display and its back link so that it can be removed
     */
    _XLockMutex(_Xglobal_lock);
    prev = NULL;
    for (dpyinfo = extinfo->head; dpyinfo; dpyinfo = dpyinfo->next) {
	if (dpyinfo->display == dpy) break;
	prev = dpyinfo;
    }
    if (!dpyinfo) {
	_XUnlockMutex(_Xglobal_lock);
	return 0;		/* hmm, actually an error */
    }

    /*
     * remove the display from the list; handles going to zero
     */
    if (prev)
	prev->next = dpyinfo->next;
    else
	extinfo->head = dpyinfo->next;

    extinfo->ndisplays--;
    if (dpyinfo == extinfo->cur) extinfo->cur = NULL;  /* flush cache */
    _XUnlockMutex(_Xglobal_lock);

    Xfree (dpyinfo);
    return 1;
}

static DepthCheckPtr	depthChecks;

static int
XRenderDepthCheckErrorHandler (Display *dpy, XErrorEvent *evt)
{
    if (evt->request_code == X_CreatePixmap && evt->error_code == BadValue)
    {
	DepthCheckPtr	d;

	_XLockMutex(_Xglobal_lock);
	for (d = depthChecks; d; d = d->next)
	{
	    if (d->dpy == dpy)
	    {
		if ((long) (evt->serial - d->serial) >= 0)
		    d->missing |= DEPTH_MASK(evt->resourceid);
		break;
	    }
        }
	_XUnlockMutex (_Xglobal_lock);
    }
    return 0;
}

static Bool
XRenderHasDepths (Display *dpy)
{
    int	s;

    for (s = 0; s < ScreenCount (dpy); s++)
    {
	CARD32		    depths = 0;
	CARD32		    missing;
	Screen		    *scr = ScreenOfDisplay (dpy, s);
	int		    d;

	for (d = 0; d < scr->ndepths; d++)
	    depths |= DEPTH_MASK(scr->depths[d].depth);
	missing = ~depths & REQUIRED_DEPTHS;
	if (missing)
	{
	    DepthCheckRec   dc, **dp;
	    XErrorHandler   previousHandler;

	    /*
	     * Ok, this is ugly.  It should be sufficient at this
	     * point to just return False, but Xinerama is broken at
	     * this point and only advertises depths which have an
	     * associated visual.  Of course, the other depths still
	     * work, but the only way to find out is to try them.
	     */
	    dc.dpy = dpy;
	    dc.missing = 0;
	    dc.serial = XNextRequest (dpy);
	    _XLockMutex(_Xglobal_lock);
	    dc.next = depthChecks;
	    depthChecks = &dc;
	    _XUnlockMutex (_Xglobal_lock);
	    /*
	     * I suspect this is not really thread safe, but Xlib doesn't
	     * provide a lot of options here
	     */
	    previousHandler = XSetErrorHandler (XRenderDepthCheckErrorHandler);
	    /*
	     * Try each missing depth and see if pixmap creation succeeds
	     */
	    for (d = 1; d <= 32; d++)
		/* don't check depth 1 == Xcursor recurses... */
		if ((missing & DEPTH_MASK(d)) && d != 1)
		{
		    Pixmap  p;
		    p = XCreatePixmap (dpy, RootWindow (dpy, s), 1, 1, (unsigned) d);
		    XFreePixmap (dpy, p);
		}
	    XSync (dpy, False);
	    XSetErrorHandler (previousHandler);
	    /*
	     * Unhook from the list of depth check records
	     */
	    _XLockMutex(_Xglobal_lock);
	    for (dp = &depthChecks; *dp; dp = &(*dp)->next)
	    {
		if (*dp == &dc)
		{
		    *dp = dc.next;
		    break;
		}
	    }
	    _XUnlockMutex (_Xglobal_lock);
	    if (dc.missing)
		return False;
	}
    }
    return True;
}

/*
 * XRenderExtAddDisplay - add a display to this extension. (Replaces
 * XextAddDisplay)
 */
static XRenderExtDisplayInfo *
XRenderExtAddDisplay (XRenderExtInfo *extinfo,
                      Display        *dpy,
                      char           *ext_name)
{
    XRenderExtDisplayInfo *dpyinfo;

    dpyinfo = Xmalloc (sizeof (XRenderExtDisplayInfo));
    if (!dpyinfo) return NULL;
    dpyinfo->display = dpy;
    dpyinfo->info = NULL;

    if (XRenderHasDepths (dpy))
	dpyinfo->codes = XInitExtension (dpy, ext_name);
    else
	dpyinfo->codes = NULL;

    /*
     * if the server has the extension, then we can initialize the
     * appropriate function vectors
     */
    if (dpyinfo->codes) {
        XESetCloseDisplay (dpy, dpyinfo->codes->extension,
                           XRenderCloseDisplay);
    } else {
	/* The server doesn't have this extension.
	 * Use a private Xlib-internal extension to hang the close_display
	 * hook on so that the "cache" (extinfo->cur) is properly cleaned.
	 * (XBUG 7955)
	 */
	XExtCodes *codes = XAddExtension(dpy);
	if (!codes) {
	    XFree(dpyinfo);
	    return NULL;
	}
        XESetCloseDisplay (dpy, codes->extension, XRenderCloseDisplay);
    }

    /*
     * now, chain it onto the list
     */
    _XLockMutex(_Xglobal_lock);
    dpyinfo->next = extinfo->head;
    extinfo->head = dpyinfo;
    extinfo->cur = dpyinfo;
    extinfo->ndisplays++;
    _XUnlockMutex(_Xglobal_lock);
    return dpyinfo;
}

char XRenderExtensionName[] = RENDER_NAME;

XRenderExtDisplayInfo *
XRenderFindDisplay (Display *dpy)
{
    XRenderExtDisplayInfo *dpyinfo;

    dpyinfo = XRenderExtFindDisplay (&XRenderExtensionInfo, dpy);
    if (!dpyinfo)
	dpyinfo = XRenderExtAddDisplay (&XRenderExtensionInfo, dpy,
                                        XRenderExtensionName);
    return dpyinfo;
}


static void
XRenderFreeXRenderInfo (XRenderInfo *xri)
{
    Xfree(xri->format);
    Xfree(xri->screen);
    Xfree(xri->depth);
    Xfree(xri->visual);
    Xfree(xri);
}

static int
XRenderCloseDisplay (Display *dpy, XExtCodes *codes _X_UNUSED)
{
    XRenderExtDisplayInfo *info = XRenderFindDisplay (dpy);
    if (info && info->info) XRenderFreeXRenderInfo (info->info);

    return XRenderExtRemoveDisplay (&XRenderExtensionInfo, dpy);
}

static Bool
_XRenderVersionHandler (Display	    *dpy,
			xReply	    *rep,
			char	    *buf,
			int	    len,
			XPointer    data)
{
    xRenderQueryVersionReply	replbuf;
    xRenderQueryVersionReply	*repl;
    _XrenderVersionState	*state = (_XrenderVersionState *) data;

    if (dpy->last_request_read != state->version_seq)
	return False;
    if (rep->generic.type == X_Error)
    {
	state->error = True;
	return False;
    }
    repl = (xRenderQueryVersionReply *)
	_XGetAsyncReply(dpy, (char *)&replbuf, rep, buf, len,
		     (SIZEOF(xRenderQueryVersionReply) - SIZEOF(xReply)) >> 2,
			True);
    state->major_version = (int) repl->majorVersion;
    state->minor_version = (int) repl->minorVersion;
    return True;
}

static XRenderPictFormat *
_XRenderFindFormat (XRenderInfo *xri, PictFormat format)
{
    int	nf;

    for (nf = 0; nf < xri->nformat; nf++)
	if (xri->format[nf].id == format)
	    return &xri->format[nf];
    return NULL;
}

static Visual *
_XRenderFindVisual (Display *dpy, VisualID vid)
{
    return _XVIDtoVisual (dpy, vid);
}

Status
XRenderQueryFormats (Display *dpy)
{
    XRenderExtDisplayInfo	*info = XRenderFindDisplay (dpy);
    _XAsyncHandler		async;
    _XrenderVersionState	async_state;
    xRenderQueryVersionReq	*vreq;
    xRenderQueryPictFormatsReply rep;
    xRenderQueryPictFormatsReq  *req;
    XRenderInfo			*xri;
    XRenderPictFormat		*format;
    XRenderScreen		*screen;
    XRenderDepth		*depth;
    XRenderVisual		*visual;
    xPictFormInfo		*xFormat;
    xPictScreen			*xScreen;
    xPictDepth			*xPDepth;
    xPictVisual			*xVisual;
    CARD32			*xSubpixel;
    void			*xData;
    int				ns, nd;
    unsigned			nf;
    unsigned long		rlength;
    unsigned long		nbytes;

    RenderCheckExtension (dpy, info, 0);
    LockDisplay (dpy);
    if (info->info)
    {
	UnlockDisplay (dpy);
	return 1;
    }
    GetReq (RenderQueryVersion, vreq);
    vreq->reqType = (CARD8) info->codes->major_opcode;
    vreq->renderReqType = X_RenderQueryVersion;
    vreq->majorVersion = RENDER_MAJOR;
    vreq->minorVersion = RENDER_MINOR;

    async_state.version_seq = dpy->request;
    async_state.error = False;
    async.next = dpy->async_handlers;
    async.handler = _XRenderVersionHandler;
    async.data = (XPointer) &async_state;
    dpy->async_handlers = &async;

    GetReq (RenderQueryPictFormats, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderQueryPictFormats;

    if (!_XReply (dpy, (xReply *) &rep, 0, xFalse))
    {
	DeqAsyncHandler (dpy, &async);
	UnlockDisplay (dpy);
	SyncHandle ();
	return 0;
    }
    DeqAsyncHandler (dpy, &async);
    if (async_state.error)
    {
	UnlockDisplay(dpy);
	SyncHandle();
	return 0;
    }
    /*
     * Check for the lack of sub-pixel data
     */
    if (async_state.major_version == 0 && async_state.minor_version < 6)
	rep.numSubpixel = 0;

    if ((rep.numFormats < ((INT_MAX / 4) / sizeof (XRenderPictFormat))) &&
	(rep.numScreens < ((INT_MAX / 4) / sizeof (XRenderScreen))) &&
	(rep.numDepths  < ((INT_MAX / 4) / sizeof (XRenderDepth))) &&
	(rep.numVisuals < ((INT_MAX / 4) / sizeof (XRenderVisual))) &&
	(rep.numSubpixel < ((INT_MAX / 4) / 4)) &&
	(rep.length < (INT_MAX >> 2)) ) {
        /* Zero-initialize so that pointers are NULL if there is a failure. */
	xri = Xcalloc (1, sizeof (XRenderInfo));
	rlength = ((rep.numFormats * sizeof (xPictFormInfo)) +
		   (rep.numScreens * sizeof (xPictScreen)) +
		   (rep.numDepths * sizeof (xPictDepth)) +
		   (rep.numVisuals * sizeof (xPictVisual)) +
		   (rep.numSubpixel * 4));
	xData = Xmalloc (rlength);
	nbytes = (unsigned long) rep.length << 2;
    } else {
	xri = NULL;
	xData = NULL;
	rlength = nbytes = 0;
    }

    if (!xri || !xData || nbytes < rlength)
    {
	if (xri) Xfree (xri);
	if (xData) Xfree (xData);
	_XEatDataWords (dpy, rep.length);
	UnlockDisplay (dpy);
	SyncHandle ();
	return 0;
    }
    xri->major_version = async_state.major_version;
    xri->minor_version = async_state.minor_version;
    xri->format = Xcalloc(rep.numFormats, sizeof(XRenderPictFormat));
    xri->nformat = (int) rep.numFormats;
    xri->screen = Xcalloc(rep.numScreens, sizeof(XRenderScreen));
    xri->nscreen = (int) rep.numScreens;
    xri->depth = Xcalloc(rep.numDepths, sizeof(XRenderDepth));
    xri->ndepth = (int) rep.numDepths;
    xri->visual = Xcalloc(rep.numVisuals, sizeof(XRenderVisual));
    xri->nvisual = (int) rep.numVisuals;
    _XRead (dpy, (char *) xData, (long) rlength);
    format = xri->format;
    xFormat = (xPictFormInfo *) xData;
    for (nf = 0; nf < rep.numFormats; nf++)
    {
	format->id = xFormat->id;
	format->type = xFormat->type;
	format->depth = xFormat->depth;
	format->direct.red       = (short) xFormat->direct.red;
	format->direct.redMask   = (short) xFormat->direct.redMask;
	format->direct.green     = (short) xFormat->direct.green;
	format->direct.greenMask = (short) xFormat->direct.greenMask;
	format->direct.blue      = (short) xFormat->direct.blue;
	format->direct.blueMask  = (short) xFormat->direct.blueMask;
	format->direct.alpha     = (short) xFormat->direct.alpha;
	format->direct.alphaMask = (short) xFormat->direct.alphaMask;
	format->colormap = xFormat->colormap;
	format++;
	xFormat++;
    }
    xScreen = (xPictScreen *) xFormat;
    screen = xri->screen;
    depth = xri->depth;
    visual = xri->visual;
    for (ns = 0; ns < xri->nscreen; ns++)
    {
	screen->depths = depth;
	screen->ndepths = (int) xScreen->nDepth;
	screen->fallback = _XRenderFindFormat (xri, xScreen->fallback);
	screen->subpixel = SubPixelUnknown;
	xPDepth = (xPictDepth *) (xScreen + 1);
	if ((unsigned int)screen->ndepths > rep.numDepths) {
	    XRenderFreeXRenderInfo(xri);
	    Xfree (xData);
	    _XEatDataWords (dpy, rep.length);
	    UnlockDisplay (dpy);
	    SyncHandle ();
	    return 0;
	}
	rep.numDepths -= (CARD32) screen->ndepths;
	for (nd = 0; nd < screen->ndepths; nd++)
	{
	    int nv;

	    depth->depth = xPDepth->depth;
	    depth->nvisuals = xPDepth->nPictVisuals;
	    depth->visuals = visual;
	    xVisual = (xPictVisual *) (xPDepth + 1);
	    if ((unsigned int)depth->nvisuals > rep.numVisuals) {
		XRenderFreeXRenderInfo (xri);
		Xfree (xData);
		_XEatDataWords (dpy, rep.length);
		UnlockDisplay (dpy);
		SyncHandle ();
		return 0;
	    }
	    rep.numVisuals -= (CARD32) depth->nvisuals;
	    for (nv = 0; nv < depth->nvisuals; nv++)
	    {
		visual->visual = _XRenderFindVisual (dpy, xVisual->visual);
		visual->format = _XRenderFindFormat (xri, xVisual->format);
		visual++;
		xVisual++;
	    }
	    depth++;
	    xPDepth = (xPictDepth *) xVisual;
	}
	screen++;
	xScreen = (xPictScreen *) xPDepth;
    }
    xSubpixel = (CARD32 *) xScreen;
    screen = xri->screen;
    for (ns = 0; (unsigned int)ns < rep.numSubpixel; ns++)
    {
	screen->subpixel = (int) *xSubpixel;
	xSubpixel++;
	screen++;
    }
    info->info = xri;
    /*
     * Skip any extra data
     */
    if (nbytes > rlength)
	_XEatData (dpy, (unsigned long) (nbytes - rlength));

    UnlockDisplay (dpy);
    SyncHandle ();
    Xfree (xData);
    return 1;
}

XRenderPictFormat *
XRenderFindFormat (Display		*dpy,
		   unsigned long	mask,
		   _Xconst XRenderPictFormat	*template,
		   int			count)
{
    XRenderExtDisplayInfo *info = XRenderFindDisplay (dpy);
    int		    nf;
    XRenderInfo     *xri;

    RenderCheckExtension (dpy, info, NULL);
    if (!XRenderQueryFormats (dpy))
	return NULL;
    xri = info->info;
    for (nf = 0; nf < xri->nformat; nf++)
    {
	if (mask & PictFormatID)
	    if (template->id != xri->format[nf].id)
		continue;
	if (mask & PictFormatType)
	if (template->type != xri->format[nf].type)
		continue;
	if (mask & PictFormatDepth)
	    if (template->depth != xri->format[nf].depth)
		continue;
	if (mask & PictFormatRed)
	    if (template->direct.red != xri->format[nf].direct.red)
		continue;
	if (mask & PictFormatRedMask)
	    if (template->direct.redMask != xri->format[nf].direct.redMask)
		continue;
	if (mask & PictFormatGreen)
	    if (template->direct.green != xri->format[nf].direct.green)
		continue;
	if (mask & PictFormatGreenMask)
	    if (template->direct.greenMask != xri->format[nf].direct.greenMask)
		continue;
	if (mask & PictFormatBlue)
	    if (template->direct.blue != xri->format[nf].direct.blue)
		continue;
	if (mask & PictFormatBlueMask)
	    if (template->direct.blueMask != xri->format[nf].direct.blueMask)
		continue;
	if (mask & PictFormatAlpha)
	    if (template->direct.alpha != xri->format[nf].direct.alpha)
		continue;
	if (mask & PictFormatAlphaMask)
	    if (template->direct.alphaMask != xri->format[nf].direct.alphaMask)
		continue;
	if (mask & PictFormatColormap)
	    if (template->colormap != xri->format[nf].colormap)
		continue;
	if (count-- == 0)
	    return &xri->format[nf];
    }
    return NULL;
}

XRenderPictFormat *
XRenderFindStandardFormat (Display  *dpy,
			   int	    format)
{
    static struct {
	XRenderPictFormat   templ;
	unsigned long	    mask;
    } standardFormats[PictStandardNUM] = {
	/* PictStandardARGB32 */
	{
	    {
		0,			    /* id */
		PictTypeDirect,		    /* type */
		32,			    /* depth */
		{			    /* direct */
		    16,			    /* direct.red */
		    0xff,		    /* direct.redMask */
		    8,			    /* direct.green */
		    0xff,		    /* direct.greenMask */
		    0,			    /* direct.blue */
		    0xff,		    /* direct.blueMask */
		    24,			    /* direct.alpha */
		    0xff,		    /* direct.alphaMask */
		},
		0,			    /* colormap */
	    },
	    PictFormatType |
	    PictFormatDepth |
	    PictFormatRed |
	    PictFormatRedMask |
	    PictFormatGreen |
	    PictFormatGreenMask |
	    PictFormatBlue |
	    PictFormatBlueMask |
	    PictFormatAlpha |
	    PictFormatAlphaMask,
	},
	/* PictStandardRGB24 */
	{
	    {
		0,			    /* id */
		PictTypeDirect,		    /* type */
		24,			    /* depth */
		{			    /* direct */
		    16,			    /* direct.red */
		    0xff,		    /* direct.redMask */
		    8,			    /* direct.green */
		    0xff,		    /* direct.greenMask */
		    0,			    /* direct.blue */
		    0xff,		    /* direct.blueMask */
		    0,			    /* direct.alpha */
		    0x00,		    /* direct.alphaMask */
		},
		0,			    /* colormap */
	    },
	    PictFormatType |
	    PictFormatDepth |
	    PictFormatRed |
	    PictFormatRedMask |
	    PictFormatGreen |
	    PictFormatGreenMask |
	    PictFormatBlue |
	    PictFormatBlueMask |
	    PictFormatAlphaMask,
	},
	/* PictStandardA8 */
	{
	    {
		0,			    /* id */
		PictTypeDirect,		    /* type */
		8,			    /* depth */
		{			    /* direct */
		    0,			    /* direct.red */
		    0x00,		    /* direct.redMask */
		    0,			    /* direct.green */
		    0x00,		    /* direct.greenMask */
		    0,			    /* direct.blue */
		    0x00,		    /* direct.blueMask */
		    0,			    /* direct.alpha */
		    0xff,		    /* direct.alphaMask */
		},
		0,			    /* colormap */
	    },
	    PictFormatType |
	    PictFormatDepth |
	    PictFormatRedMask |
	    PictFormatGreenMask |
	    PictFormatBlueMask |
	    PictFormatAlpha |
	    PictFormatAlphaMask,
	},
	/* PictStandardA4 */
	{
	    {
		0,			    /* id */
		PictTypeDirect,		    /* type */
		4,			    /* depth */
		{			    /* direct */
		    0,			    /* direct.red */
		    0x00,		    /* direct.redMask */
		    0,			    /* direct.green */
		    0x00,		    /* direct.greenMask */
		    0,			    /* direct.blue */
		    0x00,		    /* direct.blueMask */
		    0,			    /* direct.alpha */
		    0x0f,		    /* direct.alphaMask */
		},
		0,			    /* colormap */
	    },
	    PictFormatType |
	    PictFormatDepth |
	    PictFormatRedMask |
	    PictFormatGreenMask |
	    PictFormatBlueMask |
	    PictFormatAlpha |
	    PictFormatAlphaMask,
	},
	/* PictStandardA1 */
	{
	    {
		0,			    /* id */
		PictTypeDirect,		    /* type */
		1,			    /* depth */
		{			    /* direct */
		    0,			    /* direct.red */
		    0x00,		    /* direct.redMask */
		    0,			    /* direct.green */
		    0x00,		    /* direct.greenMask */
		    0,			    /* direct.blue */
		    0x00,		    /* direct.blueMask */
		    0,			    /* direct.alpha */
		    0x01,		    /* direct.alphaMask */
		},
		0,			    /* colormap */
	    },
	    PictFormatType |
	    PictFormatDepth |
	    PictFormatRedMask |
	    PictFormatGreenMask |
	    PictFormatBlueMask |
	    PictFormatAlpha |
	    PictFormatAlphaMask,
	},
    };
    if (0 <= format && format < PictStandardNUM)
	return XRenderFindFormat (dpy,
				  standardFormats[format].mask,
				  &standardFormats[format].templ,
				  0);
    return NULL;
}

Cursor
XRenderCreateCursor (Display	    *dpy,
		     Picture	    source,
		     unsigned int   x,
		     unsigned int   y)
{
    XRenderExtDisplayInfo	*info = XRenderFindDisplay (dpy);
    Cursor			cid;
    xRenderCreateCursorReq	*req;

    RenderCheckExtension (dpy, info, 0);
    LockDisplay(dpy);
    GetReq(RenderCreateCursor, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderCreateCursor;
    req->cid = (CARD32) (cid = XAllocID (dpy));
    req->src = (CARD32) source;
    req->x = (CARD16) x;
    req->y = (CARD16) y;

    UnlockDisplay(dpy);
    SyncHandle();
    return cid;
}

static void
_XRenderProcessPictureAttributes (Display		    *dpy,
				  xRenderChangePictureReq   *req,
				  unsigned long		    valuemask,
				  _Xconst XRenderPictureAttributes  *attributes)
{
    unsigned long values[32];
    register unsigned long *value = values;
    unsigned int nvalues;

    if (valuemask & CPRepeat)
	*value++ = (unsigned long) attributes->repeat;
    if (valuemask & CPAlphaMap)
	*value++ = attributes->alpha_map;
    if (valuemask & CPAlphaXOrigin)
	*value++ = (unsigned long) attributes->alpha_x_origin;
    if (valuemask & CPAlphaYOrigin)
	*value++ = (unsigned long) attributes->alpha_y_origin;
    if (valuemask & CPClipXOrigin)
	*value++ = (unsigned long) attributes->clip_x_origin;
    if (valuemask & CPClipYOrigin)
	*value++ = (unsigned long) attributes->clip_y_origin;
    if (valuemask & CPClipMask)
	*value++ = attributes->clip_mask;
    if (valuemask & CPGraphicsExposure)
	*value++ = (unsigned long) attributes->graphics_exposures;
    if (valuemask & CPSubwindowMode)
	*value++ = (unsigned long) attributes->subwindow_mode;
    if (valuemask & CPPolyEdge)
	*value++ = (unsigned long) attributes->poly_edge;
    if (valuemask & CPPolyMode)
	*value++ = (unsigned long) attributes->poly_mode;
    if (valuemask & CPDither)
	*value++ = attributes->dither;
    if (valuemask & CPComponentAlpha)
	*value++ = (unsigned long) attributes->component_alpha;

    req->length += (nvalues = (unsigned) (value - values));

    nvalues <<= 2;			    /* watch out for macros... */
    Data32 (dpy, (long *) values, (long)nvalues);
}

Picture
XRenderCreatePicture (Display			*dpy,
		      Drawable			drawable,
		      _Xconst XRenderPictFormat		*format,
		      unsigned long		valuemask,
		      _Xconst XRenderPictureAttributes	*attributes)
{
    XRenderExtDisplayInfo   *info = XRenderFindDisplay (dpy);
    Picture		    pid;
    xRenderCreatePictureReq *req;

    RenderCheckExtension (dpy, info, 0);
    LockDisplay(dpy);
    GetReq(RenderCreatePicture, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderCreatePicture;
    req->pid = (CARD32) (pid = XAllocID(dpy));
    req->drawable = (CARD32) drawable;
    req->format = (CARD32) format->id;
    if ((req->mask = (CARD32) valuemask))
	_XRenderProcessPictureAttributes (dpy,
					  (xRenderChangePictureReq *) req,
					  valuemask,
					  attributes);
    UnlockDisplay(dpy);
    SyncHandle();
    return pid;
}

void
XRenderFreePicture (Display                   *dpy,
		    Picture                   picture)
{
    XRenderExtDisplayInfo   *info = XRenderFindDisplay (dpy);
    xRenderFreePictureReq   *req;

    RenderSimpleCheckExtension (dpy, info);
    LockDisplay(dpy);
    GetReq(RenderFreePicture, req);
    req->reqType = (CARD8) info->codes->major_opcode;
    req->renderReqType = X_RenderFreePicture;
    req->picture = (CARD32) picture;
    UnlockDisplay(dpy);
    SyncHandle();
}

Bool XRenderQueryExtension (Display *dpy)
{
    XRenderExtDisplayInfo *info = XRenderFindDisplay (dpy);

    if (RenderHasExtension(info)) {
	return True;
    } else {
	return False;
    }
}

Status XRenderQueryVersion (Display *dpy,
			    int	    *major_versionp,
			    int	    *minor_versionp)
{
    XRenderExtDisplayInfo *info = XRenderFindDisplay (dpy);
    XRenderInfo	    *xri;

    if (!RenderHasExtension (info))
	return 0;

    if (!XRenderQueryFormats (dpy))
	return 0;

    xri = info->info;
    *major_versionp = xri->major_version;
    *minor_versionp = xri->minor_version;
    return 1;
}


XcursorBool
XcursorSupportsARGB (Display *dpy)
{
    int major, minor;
    XcursorBool has_render_cursor = XcursorFalse;
    if (XRenderQueryExtension (dpy) &&
	XRenderQueryVersion (dpy, &major, &minor))
    {
	if (major > 0 || minor >= 5)
	{
	    has_render_cursor = XcursorTrue;
	}
    }
    return has_render_cursor;
}


Cursor
XcursorImageLoadCursor (Display *dpy, const XcursorImage *image)
{
    Cursor  cursor;

    if(XcursorSupportsARGB (dpy))
    {
	XImage		    ximage;
	int		    screen = DefaultScreen (dpy);
	Pixmap		    pixmap;
	Picture		    picture;
	GC		    gc;
	XRenderPictFormat   *format;

	ximage.width = (int) image->width;
	ximage.height = (int) image->height;
	ximage.xoffset = 0;
	ximage.format = ZPixmap;
	ximage.data = (char *) image->pixels;
	ximage.byte_order = nativeByteOrder ();
	ximage.bitmap_unit = 32;
	ximage.bitmap_bit_order = ximage.byte_order;
	ximage.bitmap_pad = 32;
	ximage.depth = 32;
	ximage.bits_per_pixel = 32;
	ximage.bytes_per_line = (int) (image->width * 4);
	ximage.red_mask = 0xff0000;
	ximage.green_mask = 0x00ff00;
	ximage.blue_mask = 0x0000ff;
	ximage.obdata = NULL;
	if (!XInitImage (&ximage))
	    return None;
	pixmap = XCreatePixmap (dpy, RootWindow (dpy, screen),
				image->width, image->height, 32);
	gc = XCreateGC (dpy, pixmap, 0, NULL);
	XPutImage (dpy, pixmap, gc, &ximage,
		   0, 0, 0, 0, image->width, image->height);
	XFreeGC (dpy, gc);
	format = XRenderFindStandardFormat (dpy, PictStandardARGB32);
	picture = XRenderCreatePicture (dpy, pixmap, format, 0, NULL);
	XFreePixmap (dpy, pixmap);
	cursor = XRenderCreateCursor (dpy, picture,
				      image->xhot, image->yhot);
	XRenderFreePicture (dpy, picture);
    }

    return cursor;
}

uint32_t convertPixelA8B8G8R8ToA8R8G8B8(uint32_t pixel) {
    uint8_t a = (pixel >> 24) & 0xFF;
    uint8_t b = (pixel >> 16) & 0xFF;
    uint8_t g = (pixel >> 8) & 0xFF;
    uint8_t r = pixel & 0xFF;

    return (uint32_t)a << 24 | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

void convertABGRToARGB(uint32_t *data, size_t num_pixels) {
    for (size_t i = 0; i < num_pixels; i++) {
        data[i] = convertPixelA8B8G8R8ToA8R8G8B8(data[i]);
    }
}


Cursor create_cursor(Display *display, uint8_t *data, int width, int height, int stride, int hot_x, int hot_y) {
    // Create an XImage for the cursor
    XcursorImage *cursor_image = XcursorImageCreate(width, height);
    if (!cursor_image) {
        fprintf(stderr, "Failed to create XcursorImage\n");
        return None;
    }

    // Set the cursor's hotspot to the center
    cursor_image->xhot = hot_x;
    cursor_image->yhot = hot_y;

    convertABGRToARGB((uint32_t *)data, width*height);

    for (int y = 0; y < height; y++) {
        uint8_t *source_row = data + y * stride;
        uint32_t *target_row = cursor_image->pixels + y * width;
        memcpy(target_row, source_row, width * sizeof(uint32_t));
    }

    // Create a cursor from the image
    Cursor cursor = XcursorImageLoadCursor(display, cursor_image);
    XcursorImageDestroy(cursor_image);
    return cursor;
}

