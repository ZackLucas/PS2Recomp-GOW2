import ctypes, sys, time
x = ctypes.CDLL("libX11.so.6"); t = ctypes.CDLL("libXtst.so.6")
x.XOpenDisplay.restype = ctypes.c_void_p
x.XDefaultRootWindow.restype = ctypes.c_ulong
x.XKeysymToKeycode.restype = ctypes.c_ubyte
d = ctypes.c_void_p(x.XOpenDisplay(None))
root = x.XDefaultRootWindow(d)

class Attr(ctypes.Structure):
    _fields_ = [("x", ctypes.c_int), ("y", ctypes.c_int), ("w", ctypes.c_int), ("h", ctypes.c_int), ("bw", ctypes.c_int),
                ("depth", ctypes.c_int), ("visual", ctypes.c_void_p), ("root", ctypes.c_ulong), ("cls", ctypes.c_int),
                ("bitgrav", ctypes.c_int), ("wingrav", ctypes.c_int), ("backing", ctypes.c_int), ("planes", ctypes.c_ulong),
                ("pixel", ctypes.c_ulong), ("save", ctypes.c_int), ("cmap", ctypes.c_ulong), ("mapcmap", ctypes.c_int),
                ("mapstate", ctypes.c_int), ("alleventmask", ctypes.c_long), ("yourevent", ctypes.c_long),
                ("override", ctypes.c_int), ("screen", ctypes.c_void_p)]

def windows():
    rr, pr = ctypes.c_ulong(), ctypes.c_ulong(); ch = ctypes.POINTER(ctypes.c_ulong)(); n = ctypes.c_uint()
    x.XQueryTree(d, root, ctypes.byref(rr), ctypes.byref(pr), ctypes.byref(ch), ctypes.byref(n))
    out = []
    for i in range(n.value):
        a = Attr(); x.XGetWindowAttributes(d, ch[i], ctypes.byref(a))
        nm = ctypes.c_char_p()
        x.XFetchName(d, ch[i], ctypes.byref(nm))
        out.append((ch[i], a.x, a.y, a.w, a.h, a.mapstate, nm.value.decode() if nm.value else ""))
    return out

if sys.argv[1] == "list":
    for w in windows(): print("win %#x pos=(%d,%d) size=%dx%d mapstate=%d name=%r" % w)
elif sys.argv[1] == "key":
    # focus the biggest viewable window, then send the key combination
    ws = [w for w in windows() if w[5] == 2]
    ws.sort(key=lambda w: -w[3] * w[4])
    if not ws: print("no viewable window"); sys.exit(1)
    wid = ws[0][0]
    x.XSetInputFocus(d, ctypes.c_ulong(wid), 1, 0)   # RevertToParent, CurrentTime
    x.XFlush(d); time.sleep(0.3)
    KS = {"Shift": 0xffe1, "F8": 0xffc5}
    codes = [x.XKeysymToKeycode(d, ctypes.c_ulong(KS[k])) for k in sys.argv[2].split("+")]
    for c in codes: t.XTestFakeKeyEvent(d, c, 1, 0)
    x.XFlush(d); time.sleep(0.2)
    for c in reversed(codes): t.XTestFakeKeyEvent(d, c, 0, 0)
    x.XFlush(d)
    print("sent", sys.argv[2], "to window", hex(wid))
