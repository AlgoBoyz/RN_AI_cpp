"""Parse minidump: extract exception + find faulting module + simple stack trace."""
import struct, sys, io, os

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', errors='replace')

def read_struct(f, offset, fmt):
    f.seek(offset)
    size = struct.calcsize(fmt)
    return struct.unpack(fmt, f.read(size))

def get_string(f, rva):
    """Read string at RVA (stored as length-prefixed UTF-16)."""
    try:
        length = read_struct(f, rva, '<I')[0]
        if length > 1024:
            return "<too long>"
        f.seek(rva + 4)
        return f.read(length).decode('utf-16-le', errors='replace').rstrip('\x00')
    except:
        return f"<bad rva=0x{rva:X}>"

def parse_dump(path):
    with open(path, 'rb') as f:
        sig = read_struct(f, 0, '<I')[0]
        if sig != 0x504D444D:
            print("Not a valid minidump")
            return

        num_streams = read_struct(f, 8, '<I')[0]
        stream_dir_rva = read_struct(f, 12, '<I')[0]

        # Parse stream directory
        streams = {}
        for i in range(num_streams):
            off = stream_dir_rva + i * 12
            stype, ssize, srva = read_struct(f, off, '<III')
            streams[stype] = (srva, ssize)

        # --- Exception Stream (type 6) ---
        EXC = 6
        exc_addr = 0
        exc_code = 0
        exc_thread_id = 0
        if EXC in streams:
            rva, _ = streams[EXC]
            exc_thread_id = read_struct(f, rva, '<I')[0]
            exc_rec = rva + 8
            exc_code = read_struct(f, exc_rec, '<I')[0]
            exc_flags = read_struct(f, exc_rec + 4, '<I')[0]
            exc_addr = read_struct(f, exc_rec + 16, '<Q')[0]
            num_params = read_struct(f, exc_rec + 8, '<I')[0]

            codes = {
                0xC0000005: 'ACCESS_VIOLATION',
                0xC0000094: 'INTEGER_DIVIDE_BY_ZERO',
                0xC00000FD: 'STACK_OVERFLOW',
                0x80000003: 'BREAKPOINT',
                0xC0000409: 'STACK_BUFFER_OVERRUN',
                0xE06D7363: 'C++ EH (msvcrt)',
            }
            exc_name = codes.get(exc_code, 'UNKNOWN')
            print(f"Exception: 0x{exc_code:08X} ({exc_name})")
            print(f"Faulting IP: 0x{exc_addr:016X}")
            print(f"Thread ID: {exc_thread_id}")

            if exc_code == 0xC0000005 and num_params >= 2:
                params_off = exc_rec + 24
                atype = read_struct(f, params_off, '<Q')[0]
                aaddr = read_struct(f, params_off + 8, '<Q')[0]
                types = {0: 'READ', 1: 'WRITE', 8: 'EXECUTE'}
                print(f"Access: {types.get(atype, str(atype))} at address 0x{aaddr:016X}")

        # --- Module List (type 4) - find faulting module ---
        MODS = 4
        if MODS in streams:
            rva, _ = streams[MODS]
            num_modules = read_struct(f, rva, '<I')[0]
            for i in range(num_modules):
                mod_off = rva + 4 + i * 108
                base = read_struct(f, mod_off, '<Q')[0]
                size_m = read_struct(f, mod_off + 8, '<I')[0]
                name_rva = read_struct(f, mod_off + 24, '<I')[0]

                if base <= exc_addr < base + size_m:
                    name = get_string(f, name_rva)
                    offset = exc_addr - base
                    print(f"\nFaulting module: {name}")
                    print(f"Module base: 0x{base:016X}, size: 0x{size_m:X}")
                    print(f"Offset: 0x{offset:X}")

                    # Try to resolve symbol using export table if it's our exe
                    if 'winrt.exe' in name:
                        # Find our PDB-adjacent exe module info
                        pass
                    break

        # --- Thread list + context ---
        THREADS = 3
        if THREADS in streams and EXC in streams:
            rva, _ = streams[THREADS]
            num_threads = read_struct(f, rva, '<I')[0]
            for i in range(num_threads):
                t_off = rva + 4 + i * 48
                tid = read_struct(f, t_off, '<I')[0]
                if tid == exc_thread_id:
                    ctx_rva = read_struct(f, t_off + 24, '<I')[0]
                    ctx_size = read_struct(f, t_off + 28, '<I')[0]
                    if ctx_size >= 200:
                        rip = read_struct(f, ctx_rva + 0xF8, '<Q')[0]
                        rsp = read_struct(f, ctx_rva + 0x98, '<Q')[0]
                        rax = read_struct(f, ctx_rva + 0x78, '<Q')[0]
                        rcx = read_struct(f, ctx_rva + 0x80, '<Q')[0]
                        print(f"\nRegisters (crash thread):")
                        print(f"  RIP=0x{rip:016X}  RSP=0x{rsp:016X}")
                        print(f"  RAX=0x{rax:016X}  RCX=0x{rcx:016X}")

        # --- Memory64List (type 9) - try to read stack near RSP ---
        MEM64 = 9
        if MEM64 in streams and THREADS in streams:
            rva, _ = streams[THREADS]
            # re-read RSP
            exc_rva, _ = streams[EXC]
            exc_tid = read_struct(f, exc_rva, '<I')[0]
            num_threads = read_struct(f, rva, '<I')[0]
            for i in range(num_threads):
                t_off = rva + 4 + i * 48
                tid = read_struct(f, t_off, '<I')[0]
                if tid == exc_tid:
                    ctx_rva = read_struct(f, t_off + 24, '<I')[0]
                    rsp = read_struct(f, ctx_rva + 0x98, '<Q')[0]
                    break

            mem_rva, mem_size = streams[MEM64]
            num_mem = read_struct(f, mem_rva, '<Q')[0]
            base_rva_of_mem = read_struct(f, mem_rva + 8, '<Q')[0]

            # Read first ~20 entries of memory descriptors
            for i in range(min(num_mem, 200)):
                desc_off = mem_rva + 16 + i * 16
                start = read_struct(f, desc_off, '<Q')[0]
                data_size = read_struct(f, desc_off + 8, '<Q')[0]

                if start <= rsp < start + data_size:
                    data_rva = base_rva_of_mem + sum(
                        read_struct(f, mem_rva + 16 + j * 16 + 8, '<Q')[0]
                        for j in range(i)
                    )
                    offset_in_region = rsp - start
                    data_rva += offset_in_region

                    print(f"\nStack dump (from RSP):")
                    for j in range(30):
                        addr = rsp + j * 8
                        if addr < start or addr >= start + data_size:
                            break
                        val = read_struct(f, data_rva + j * 8, '<Q')[0]
                        marker = ""
                        if 0x00007FF000000000 <= val <= 0x00007FFFFFFFFFFF:
                            marker = "  <-- module addr"
                        print(f"  RSP+{j*8:3d}: 0x{val:016X}{marker}")

                    # Try to resolve stack addresses to modules
                    module_list = []
                    mod_rva, _ = streams[4]
                    nmod = read_struct(f, mod_rva, '<I')[0]
                    for mi in range(nmod):
                        moff = mod_rva + 4 + mi * 108
                        mbase = read_struct(f, moff, '<Q')[0]
                        msize = read_struct(f, moff + 8, '<I')[0]
                        mnrva = read_struct(f, moff + 24, '<I')[0]
                        mname = get_string(f, mnrva)
                        # Only keep short name
                        short = os.path.basename(mname)
                        module_list.append((mbase, mbase + msize, short))

                    print(f"\nPossible call stack (stack values in module ranges):")
                    for j in range(30):
                        addr = rsp + j * 8
                        if addr < start or addr >= start + data_size:
                            break
                        val = read_struct(f, data_rva + j * 8, '<Q')[0]
                        for mbase, mend, mname in module_list:
                            if mbase <= val < mend:
                                print(f"  RSP+{j*8:3d}: 0x{val:016X}  {mname}+0x{val-mbase:X}")
                                break
                    break

if __name__ == '__main__':
    dump = sys.argv[1] if len(sys.argv) > 1 else r'C:\Users\yisac\AppData\Local\CrashDumps\winrt.exe.26712.dmp'
    parse_dump(dump)
