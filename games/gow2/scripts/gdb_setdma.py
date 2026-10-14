import gdb
class B(gdb.Breakpoint):
    def stop(self):
        f=gdb.selected_frame()
        rdx=int(f.read_register("rdx")); rdi=int(f.read_register("rdi"))
        inf=gdb.selected_inferior()
        a0=int.from_bytes(inf.read_memory(rdx+16,4),'little'); n=int.from_bytes(inf.read_memory(rdx+20,4),'little')
        # IopRpcBridge has m_memory: read IOP RAM via evaluating? use readRam via memory object not accessible; print words via finish later
        gdb.write(f"SETDMA desc=0x{a0:x} n={n}\n")
        FinishB(f, rdx)
        return False
class FinishB(gdb.FinishBreakpoint):
    def __init__(self, frame, rdx):
        super().__init__(frame, internal=True); self.rdx=rdx
    def stop(self):
        inf=gdb.selected_inferior()
        v0=int.from_bytes(inf.read_memory(self.rdx+8,4),'little')
        gdb.write(f"  -> v0=0x{v0:x}\n"); return False
B("ps2x::iop::detail::IopRpcBridge::dispatchSifManImport")
