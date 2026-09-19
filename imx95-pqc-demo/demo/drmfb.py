"""Minimal DRM dumb-buffer text renderer.

Torizon's kernel has no fbdev emulation (CONFIG_DRM_FBDEV_EMULATION is off),
so /dev/fb0 never exists and nothing written to a VT can reach the display.
Containers are expected to own the screen through KMS. This module is the
smallest possible version of that: open /dev/dri/card0, pick the connected
connector's preferred mode, create one dumb buffer, set the CRTC once, and
draw text into the mapped pixels with an embedded 8x8 font.

Pure ctypes - no libdrm, no PIL, no new packages on the device. Must run as
root and be the only DRM master (no weston container running).

ioctl numbers are computed from the struct sizes, the same way the kernel's
_IOWR macro does, so a wrong struct definition fails loudly at open time
rather than corrupting memory.
"""

import ctypes
import fcntl
import mmap
import os

from font8x8 import FONT

DRM_IOCTL_BASE = ord('d')


def _IOWR(nr, struct_type):
    return (3 << 30) | (ctypes.sizeof(struct_type) << 16) | \
           (DRM_IOCTL_BASE << 8) | nr


class DrmModeRes(ctypes.Structure):
    _fields_ = [
        ("fb_id_ptr", ctypes.c_uint64),
        ("crtc_id_ptr", ctypes.c_uint64),
        ("connector_id_ptr", ctypes.c_uint64),
        ("encoder_id_ptr", ctypes.c_uint64),
        ("count_fbs", ctypes.c_uint32),
        ("count_crtcs", ctypes.c_uint32),
        ("count_connectors", ctypes.c_uint32),
        ("count_encoders", ctypes.c_uint32),
        ("min_width", ctypes.c_uint32),
        ("max_width", ctypes.c_uint32),
        ("min_height", ctypes.c_uint32),
        ("max_height", ctypes.c_uint32),
    ]


class DrmModeModeinfo(ctypes.Structure):
    _fields_ = [
        ("clock", ctypes.c_uint32),
        ("hdisplay", ctypes.c_uint16), ("hsync_start", ctypes.c_uint16),
        ("hsync_end", ctypes.c_uint16), ("htotal", ctypes.c_uint16),
        ("hskew", ctypes.c_uint16),
        ("vdisplay", ctypes.c_uint16), ("vsync_start", ctypes.c_uint16),
        ("vsync_end", ctypes.c_uint16), ("vtotal", ctypes.c_uint16),
        ("vscan", ctypes.c_uint16),
        ("vrefresh", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("type", ctypes.c_uint32),
        ("name", ctypes.c_char * 32),
    ]


class DrmModeGetConnector(ctypes.Structure):
    _fields_ = [
        ("encoders_ptr", ctypes.c_uint64),
        ("modes_ptr", ctypes.c_uint64),
        ("props_ptr", ctypes.c_uint64),
        ("prop_values_ptr", ctypes.c_uint64),
        ("count_modes", ctypes.c_uint32),
        ("count_props", ctypes.c_uint32),
        ("count_encoders", ctypes.c_uint32),
        ("encoder_id", ctypes.c_uint32),
        ("connector_id", ctypes.c_uint32),
        ("connector_type", ctypes.c_uint32),
        ("connector_type_id", ctypes.c_uint32),
        ("connection", ctypes.c_uint32),
        ("mm_width", ctypes.c_uint32),
        ("mm_height", ctypes.c_uint32),
        ("subpixel", ctypes.c_uint32),
        ("pad", ctypes.c_uint32),
    ]


class DrmModeGetEncoder(ctypes.Structure):
    _fields_ = [
        ("encoder_id", ctypes.c_uint32),
        ("encoder_type", ctypes.c_uint32),
        ("crtc_id", ctypes.c_uint32),
        ("possible_crtcs", ctypes.c_uint32),
        ("possible_clones", ctypes.c_uint32),
    ]


class DrmModeCreateDumb(ctypes.Structure):
    _fields_ = [
        ("height", ctypes.c_uint32),
        ("width", ctypes.c_uint32),
        ("bpp", ctypes.c_uint32),
        ("flags", ctypes.c_uint32),
        ("handle", ctypes.c_uint32),
        ("pitch", ctypes.c_uint32),
        ("size", ctypes.c_uint64),
    ]


class DrmModeFbCmd(ctypes.Structure):
    _fields_ = [
        ("fb_id", ctypes.c_uint32),
        ("width", ctypes.c_uint32),
        ("height", ctypes.c_uint32),
        ("pitch", ctypes.c_uint32),
        ("bpp", ctypes.c_uint32),
        ("depth", ctypes.c_uint32),
        ("handle", ctypes.c_uint32),
    ]


class DrmModeMapDumb(ctypes.Structure):
    _fields_ = [
        ("handle", ctypes.c_uint32),
        ("pad", ctypes.c_uint32),
        ("offset", ctypes.c_uint64),
    ]


class DrmModeDestroyDumb(ctypes.Structure):
    _fields_ = [
        ("handle", ctypes.c_uint32),
    ]


class DrmModeCrtc(ctypes.Structure):
    _fields_ = [
        ("set_connectors_ptr", ctypes.c_uint64),
        ("count_connectors", ctypes.c_uint32),
        ("crtc_id", ctypes.c_uint32),
        ("fb_id", ctypes.c_uint32),
        ("x", ctypes.c_uint32),
        ("y", ctypes.c_uint32),
        ("gamma_size", ctypes.c_uint32),
        ("mode_valid", ctypes.c_uint32),
        ("mode", DrmModeModeinfo),
    ]


GETRESOURCES = _IOWR(0xA0, DrmModeRes)
SETCRTC      = _IOWR(0xA2, DrmModeCrtc)
GETENCODER   = _IOWR(0xA6, DrmModeGetEncoder)
GETCONNECTOR = _IOWR(0xA7, DrmModeGetConnector)
ADDFB        = _IOWR(0xAE, DrmModeFbCmd)
CREATE_DUMB  = _IOWR(0xB2, DrmModeCreateDumb)
MAP_DUMB     = _IOWR(0xB3, DrmModeMapDumb)
DESTROY_DUMB = _IOWR(0xB4, DrmModeDestroyDumb)
RMFB         = _IOWR(0xAF, ctypes.c_uint32)

DRM_MODE_CONNECTED = 1

# CEA-861 modelines to fall back on when a connected connector reports zero
# modes - the LT8912B DSI-to-HDMI bridge on the Toradex DSI adapter often
# fails the EDID read, leaving the connector "connected" but modeless.
# (clock kHz, hd, hss, hse, ht, vd, vss, vse, vt, refresh, flags PHSYNC|PVSYNC)
FALLBACK_MODES = (
    ("1920x1080", 148500, 1920, 2008, 2052, 2200, 1080, 1084, 1089, 1125, 60),
    ("1280x720",   74250, 1280, 1390, 1430, 1650,  720,  725,  730,  750, 60),
)


def _make_mode(name, clock, hd, hss, hse, ht, vd, vss, vse, vt, hz):
    m = DrmModeModeinfo()
    m.clock = clock
    m.hdisplay, m.hsync_start, m.hsync_end, m.htotal = hd, hss, hse, ht
    m.vdisplay, m.vsync_start, m.vsync_end, m.vtotal = vd, vss, vse, vt
    m.vrefresh = hz
    m.flags = 0x5  # PHSYNC | PVSYNC
    m.type = 1 << 3  # DRM_MODE_TYPE_PREFERRED
    m.name = name.encode()
    return m

# The few SGR colour codes twopane.py emits, as XRGB8888.
COLORS = {
    "0":    0x00CCCCCC,   # reset -> light grey
    "1;36": 0x0000E5E5,   # bold cyan (title)
    "1;33": 0x00E5C000,   # bold yellow (pane headers)
    "1;30": 0x00555555,   # dim (separator)
}
DEFAULT = COLORS["0"]


def _arr(count):
    return (ctypes.c_uint32 * max(1, count))()


def _release_fbcon():
    """Drop the kernel framebuffer console from the DRM device. When Linux is
    booted by wolfBoot the console lands on the DP framebuffer, so fbcon holds
    DRM master and SETCRTC fails with EACCES. Unbinding it (root, best effort)
    lets this renderer master the CRTC. Harmless if fbcon is not on the fb."""
    import glob
    for name in glob.glob("/sys/class/vtconsole/vtcon*/name"):
        try:
            with open(name) as f:
                if "frame buffer device" not in f.read():
                    continue
            with open(name.replace("name", "bind"), "w") as f:
                f.write("0")
        except OSError:
            pass


class DrmFramebuffer:
    def __init__(self, path="/dev/dri/card0", scale=2):
        _release_fbcon()
        self.fd = os.open(path, os.O_RDWR)
        self.scale = scale

        res = DrmModeRes()
        fcntl.ioctl(self.fd, GETRESOURCES, res)
        # Arrays for every category the kernel reported: leaving any pointer
        # NULL while its count is nonzero makes the kernel write to 0 (EFAULT).
        fbs = _arr(res.count_fbs)
        crtcs = _arr(res.count_crtcs)
        conns = _arr(res.count_connectors)
        encs_r = _arr(res.count_encoders)
        res.fb_id_ptr = ctypes.addressof(fbs)
        res.crtc_id_ptr = ctypes.addressof(crtcs)
        res.connector_id_ptr = ctypes.addressof(conns)
        res.encoder_id_ptr = ctypes.addressof(encs_r)
        fcntl.ioctl(self.fd, GETRESOURCES, res)

        # Find a connected connector and its preferred (first) mode
        self.mode = None
        self.conn_id = None
        for i in range(res.count_connectors):
            conn = DrmModeGetConnector()
            conn.connector_id = conns[i]
            fcntl.ioctl(self.fd, GETCONNECTOR, conn)
            n_modes = conn.count_modes
            modes = (DrmModeModeinfo * max(1, n_modes))()
            encs = _arr(conn.count_encoders)
            props = _arr(conn.count_props)
            propv = (ctypes.c_uint64 * max(1, conn.count_props))()
            conn.modes_ptr = ctypes.addressof(modes)
            conn.encoders_ptr = ctypes.addressof(encs)
            conn.props_ptr = ctypes.addressof(props)
            conn.prop_values_ptr = ctypes.addressof(propv)
            fcntl.ioctl(self.fd, GETCONNECTOR, conn)
            if conn.connection != DRM_MODE_CONNECTED and \
                    not os.environ.get("DRM_FORCE"):
                # DRM_FORCE=1 accepts a disconnected connector: the LT8912B
                # adapter's hotplug detect is unreliable, and the CEA fallback
                # modes need no EDID, so a forced modeset can still light a
                # display HPD never reported.
                continue
            self.conn_id = conn.connector_id
            if conn.count_modes > 0:
                self.mode = DrmModeModeinfo()
                ctypes.memmove(ctypes.addressof(self.mode),
                               ctypes.addressof(modes[0]),
                               ctypes.sizeof(DrmModeModeinfo))
            else:
                # Connected but modeless (EDID failed through the bridge):
                # fall back to a standard CEA mode. SETCRTC below validates
                # it; __init__ retries the next fallback on failure.
                self.mode = None
            enc = DrmModeGetEncoder()
            enc.encoder_id = conn.encoder_id if conn.encoder_id else encs[0]
            fcntl.ioctl(self.fd, GETENCODER, enc)
            self.crtc_id = enc.crtc_id if enc.crtc_id else crtcs[0]
            break
        if self.conn_id is None:
            raise RuntimeError("no connected DRM connector")

        candidates = [self.mode] if self.mode is not None else             [_make_mode(*fm) for fm in FALLBACK_MODES]

        err = None
        for mode in candidates:
            try:
                self._setup(mode)
                return
            except OSError as e:
                err = e
                self._release_buffers()
        raise RuntimeError("no mode accepted by SETCRTC: %s" % err)

    def _release_buffers(self):
        """Drop every framebuffer and dumb handle allocated by _setup().

        SETCRTC rejects a mode often enough that the fallback list exists, and
        each rejected attempt would otherwise strand its buffers in the kernel
        for the lifetime of the fd - the mmap alone is not enough, since the
        GEM handle outlives it."""
        for mm in getattr(self, "_maps", []):
            try:
                mm.close()
            except Exception:
                pass
        for fb_id in getattr(self, "_fb_ids", []):
            try:
                fcntl.ioctl(self.fd, RMFB, (ctypes.c_uint32 * 1)(fb_id))
            except OSError:
                pass
        for handle in getattr(self, "_handles", []):
            try:
                req = DrmModeDestroyDumb()
                req.handle = handle
                fcntl.ioctl(self.fd, DESTROY_DUMB, req)
            except OSError:
                pass
        self._maps = []
        self._fb_ids = []
        self._handles = []

    def _create_dumb(self):
        """Allocate one dumb buffer + framebuffer, return (fb_id, mmap)."""
        dumb = DrmModeCreateDumb()
        dumb.width = self.width
        dumb.height = self.height
        dumb.bpp = 32
        fcntl.ioctl(self.fd, CREATE_DUMB, dumb)
        # Record the handle before anything else can fail: ADDFB, MAP_DUMB and
        # mmap below all raise OSError on a mode the CRTC rejects, and the
        # fallback loop catches that - without this the buffer would be
        # stranded in the kernel for the lifetime of the fd.
        self._handles.append(dumb.handle)
        self.pitch = dumb.pitch

        fb = DrmModeFbCmd()
        fb.width, fb.height = self.width, self.height
        fb.pitch, fb.bpp, fb.depth = dumb.pitch, 32, 24
        fb.handle = dumb.handle
        fcntl.ioctl(self.fd, ADDFB, fb)

        mreq = DrmModeMapDumb()
        mreq.handle = dumb.handle
        fcntl.ioctl(self.fd, MAP_DUMB, mreq)
        mm = mmap.mmap(self.fd, dumb.size, mmap.MAP_SHARED,
                       mmap.PROT_READ | mmap.PROT_WRITE, offset=mreq.offset)
        return fb.fb_id, mm

    def _setcrtc(self, fb_id):
        crtc = DrmModeCrtc()
        conn_arr = (ctypes.c_uint32 * 1)(self.conn_id)
        crtc.set_connectors_ptr = ctypes.addressof(conn_arr)
        crtc.count_connectors = 1
        crtc.crtc_id = self.crtc_id
        crtc.fb_id = fb_id
        crtc.mode_valid = 1
        ctypes.memmove(ctypes.addressof(crtc.mode),
                       ctypes.addressof(self.mode),
                       ctypes.sizeof(DrmModeModeinfo))
        fcntl.ioctl(self.fd, SETCRTC, crtc)

    # Monospace TrueType faces shipped by the distributions this has run on.
    # No font is vendored here; DRM_FONT or a font.ttf dropped next to this
    # file overrides the search, and the 8x8 bitmap covers a board with none.
    FONT_SEARCH = (
        "/usr/share/fonts/truetype/noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
        "/usr/share/fonts/liberation/LiberationMono-Regular.ttf",
    )

    def _find_font(self):
        env = os.environ.get("DRM_FONT")
        if env:
            return env
        local = os.path.join(os.path.dirname(__file__), "font.ttf")
        if os.path.exists(local):
            return local
        for path in self.FONT_SEARCH:
            if os.path.exists(path):
                return path
        return local

    def _setup(self, mode):
        self.mode = mode
        self.width = self.mode.hdisplay
        self.height = self.mode.vdisplay
        self._handles = []

        # Double-buffered: render the whole frame into the off-screen back
        # buffer, then SETCRTC-flip the scanout to it. The display only ever
        # shows a complete frame, so the repaint is never visible - the fix
        # for "you can watch it redraw" (single-buffering paints the live
        # scanned-out buffer in place).
        self.fb_id, self.map = self._create_dumb()
        self._fb_ids = [self.fb_id]
        self._maps = [self.map]
        self._front = 0
        try:
            fb1, mm1 = self._create_dumb()
            self._fb_ids.append(fb1)
            self._maps.append(mm1)
        except OSError:
            pass  # single-buffered fallback if a second buffer won't allocate
        self._setcrtc(self.fb_id)

        # Prefer a crisp TrueType renderer (Pillow) when a font is present;
        # fall back to the built-in 8x8 bitmap otherwise. The bitmap path
        # scaled 2x is blocky and slow; Pillow draws antialiased glyphs and
        # blits the whole frame in one memcpy.
        self.pil = None
        font_px = int(os.environ.get("DRM_FONT_PX", "18"))
        font_path = self._find_font()
        try:
            from PIL import Image, ImageDraw, ImageFont
            if os.path.exists(font_path):
                self._pil_mod = (Image, ImageDraw, ImageFont)
                self._font = ImageFont.truetype(font_path, font_px)
                self._img = Image.new("RGB", (self.width, self.height),
                                      (16, 16, 16))
                self._draw = ImageDraw.Draw(self._img)
                # Monospace cell metrics from the font itself.
                self.cell_w = int(round(self._font.getlength("M"))) or font_px
                asc, desc = self._font.getmetrics()
                self.cell_h = asc + desc
                self.cols = self.width // self.cell_w
                self.rows = self.height // self.cell_h
                self.pil = True
        except Exception as e:
            self.pil = None
            if os.path.exists(font_path):
                import sys
                sys.stderr.write("drmfb: font present but PIL path "
                                 "unavailable (%s); using blocky bitmap "
                                 "fallback\n" % e)
                sys.stderr.flush()

        if not self.pil:
            # Built-in 8x8 bitmap fallback.
            self.cell_w = 8 * self.scale
            self.cell_h = 8 * self.scale
            self.cols = self.width // self.cell_w
            self.rows = self.height // self.cell_h

    def _put_glyph(self, cx, cy, ch, color):
        glyph = FONT.get(ord(ch))
        if glyph is None:
            glyph = FONT[ord('?')]
        s = self.scale
        x0 = cx * self.cell_w
        y0 = cy * self.cell_h
        pitch = self.pitch
        for gy in range(8):
            bits = glyph[gy]
            rowbytes = bytearray()
            for gx in range(8):
                v = color if (bits >> gx) & 1 else 0x00101010
                rowbytes += int(v).to_bytes(4, "little") * s
            for sy in range(s):
                off = (y0 + gy * s + sy) * pitch + x0 * 4
                self.map[off:off + len(rowbytes)] = rowbytes

    def draw_lines(self, lines, ansi_re):
        """Render a list of strings. Understands only the SGR codes in COLORS;
        everything else is stripped by ansi_re before drawing.

        Only cells that changed since the last frame are redrawn. Pushing the
        whole 1920x1080 buffer from Python every frame is what made the refresh
        crawl; between frames almost every cell is identical (only the
        benchmark lines scroll), so the per-cell (char,color) cache turns a
        full repaint into a few dozen glyph writes. """
        if self.pil:
            self._draw_lines_pil(lines, ansi_re)
            return
        if not hasattr(self, "_cache"):
            self._cache = {}
        for cy in range(self.rows):
            raw = lines[cy] if cy < len(lines) else ""
            color = DEFAULT
            cx = 0
            i = 0
            while cx < self.cols:
                ch = " "
                if i < len(raw):
                    if raw[i] == "\x1b":
                        m = ansi_re.match(raw, i)
                        if m:
                            if m.group(0).endswith("m"):
                                color = COLORS.get(m.group(0)[2:-1], DEFAULT)
                            i = m.end()
                            continue
                        i += 1
                        continue
                    ch = raw[i]
                    i += 1
                key = (cx, cy)
                cell = (ch, color)
                if self._cache.get(key) != cell:
                    self._put_glyph(cx, cy, ch, color)
                    self._cache[key] = cell
                cx += 1

    @staticmethod
    def _rgb(color):
        return ((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF)

    def _draw_lines_pil(self, lines, ansi_re):
        """Crisp path: render the whole frame into a PIL image with a real
        monospace font, then blit it to the off-screen back buffer and flip.
        Runs a segment at a time so per-run SGR colors are honored."""
        # Skip the whole frame when nothing changed - most ticks only the
        # left benchmark pane scrolls, and an unchanged frame need not repaint
        # or flip at all.
        sig = tuple(lines)
        if getattr(self, "_last_sig", None) == sig:
            return
        self._last_sig = sig
        draw = self._draw
        draw.rectangle((0, 0, self.width, self.height), fill=(16, 16, 16))
        font = self._font
        cw, ch = self.cell_w, self.cell_h
        for cy in range(self.rows):
            raw = lines[cy] if cy < len(lines) else ""
            color = DEFAULT
            cx = 0
            i = 0
            seg = []
            seg_x = 0
            while i < len(raw) and cx < self.cols:
                if raw[i] == "\x1b":
                    m = ansi_re.match(raw, i)
                    if m:
                        if m.group(0).endswith("m"):
                            if seg:
                                draw.text((seg_x * cw, cy * ch), "".join(seg),
                                          font=font, fill=self._rgb(color))
                                seg = []
                            color = COLORS.get(m.group(0)[2:-1], DEFAULT)
                            seg_x = cx
                        i = m.end()
                        continue
                    i += 1
                    continue
                if not seg:
                    seg_x = cx
                seg.append(raw[i])
                cx += 1
                i += 1
            if seg:
                draw.text((seg_x * cw, cy * ch), "".join(seg),
                          font=font, fill=self._rgb(color))
        # Blit into the back buffer: PIL RGB -> XRGB8888 little-endian
        # (memory bytes B,G,R,X) = PIL "BGRX".
        back = 1 - self._front if len(self._maps) > 1 else 0
        bmap = self._maps[back]
        raw = self._img.tobytes("raw", "BGRX")
        if self.pitch == self.width * 4:
            bmap[0:len(raw)] = raw
        else:
            rb = self.width * 4
            for y in range(self.height):
                bmap[y * self.pitch:y * self.pitch + rb] = \
                    raw[y * rb:y * rb + rb]
        # Flip the scanout to the freshly rendered buffer (atomic frame swap).
        if len(self._maps) > 1:
            self._setcrtc(self._fb_ids[back])
            self._front = back
