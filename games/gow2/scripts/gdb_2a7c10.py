import gdb
state={"enq":False,"n":0}
def u32(addr): return int.from_bytes(gdb.selected_inferior().read_memory(addr,4),'little')
class Enq(gdb.Breakpoint):
    def stop(self):
        state["enq"]=True; gdb.write("ENQ\n"); return False
class C10(gdb.Breakpoint):
    def stop(self):
        if not state["enq"] or state["n"]>40: return False
        f=gdb.selected_frame(); rsi=int(f.read_register("rsi")); rdi=int(f.read_register("rdi"))
        ra=u32(rsi+31*16); pend=u32(rdi+0x2D9240)
        state["n"]+=1
        Fin(f, rsi, ra, pend); return False
class Fin(gdb.FinishBreakpoint):
    def __init__(self, frame, rsi, ra, pend):
        super().__init__(frame, internal=True); self.rsi=rsi; self.ra=ra; self.pend=pend
    def stop(self):
        gdb.write(f"C10 ra={self.ra:x} pendBefore={self.pend:x} ret={u32(self.rsi+2*16):x}\n"); return False
class A90(gdb.Breakpoint):
    def stop(self):
        if state["enq"] and state["n"]<=40:
            f=gdb.selected_frame(); rsi=int(f.read_register("rsi"))
            gdb.write(f"A7990 pc={u32(rsi+512):x} ra={u32(rsi+31*16):x}\n")
        return False
Enq("sub_002A8B50_0x2a8b50"); C10("sub_002A7C10_0x2a7c10"); A90("sub_002A7990_0x2a7990")
