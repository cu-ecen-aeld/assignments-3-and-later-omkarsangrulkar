# Faulty Driver Kernel Oops Analysis

## Command Run
```
echo "hello_world" > /dev/faulty
```

## Oops Output
```
Unable to handle kernel NULL pointer dereference at virtual address 0000000000000000
Mem abort info:
  ESR = 0x0000000096000045
  EC = 0x25: DABT (current EL), IL = 32 bits
  SET = 0, FnV = 0
  EA = 0, S1PTW = 0
  FSC = 0x05: level 1 translation fault
Data abort info:
  ISV = 0, ISS = 0x00000045
  CM = 0, WnR = 1
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041b6e000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: scull(O) faulty(O) hello(O)
CPU: 0 PID: 122 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008db3d20
x29: ffffffc008db3d80 x28: ffffff8001ba5cc0 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 000000000000000c x22: 000000000000000c x21: ffffffc008db3dc0
x20: 000000557cead940 x19: ffffff8001bd0800 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc000785000 x3 : ffffffc008db3dc0
x2 : 000000000000000c x1 : 0000000000000000 x0 : 0000000000000000
Call trace:
faulty_write+0x10/0x20 [faulty]
ksys_write+0x74/0x110
__arm64_sys_write+0x1c/0x30
invoke_syscall+0x54/0x130
el0_svc_common.constprop.0+0x44/0xf0
do_el0_svc+0x2c/0xc0
el0_svc+0x2c/0x90
el0t_64_sync_handler+0xf4/0x120
el0t_64_sync+0x18c/0x190
Code: d2800001 d2800000 d503233f d50323bf (b900003f)
---[ end trace 0000000000000000 ]---
```

## Analysis

### Cause
The oops is caused by a **NULL pointer dereference** in the `faulty` kernel module's write function.
The faulting address is `0000000000000000` — a deliberate write to address NULL.

Looking at `misc-modules/faulty.c`, the `faulty_write` function contains:
```c
*(int *)0 = 0;
```
This intentionally dereferences a NULL pointer, triggering the kernel oops.

### Key Oops Fields Explained

| Field | Value | Meaning |
|-------|-------|---------|
| `pc` | `faulty_write+0x10/0x20 [faulty]` | Program counter — fault occurred at offset 0x10 inside `faulty_write` |
| `ESR` | `0x96000045` | Exception Syndrome Register — indicates a data abort (write fault) |
| `EC = 0x25` | DABT current EL | Data Abort at current Exception Level (kernel mode) |
| `WnR = 1` | Write fault | The fault was caused by a **write** operation |
| `FSC = 0x05` | Level 1 translation fault | The virtual address had no page table mapping |

### Call Trace Breakdown
```
faulty_write+0x10/0x20 [faulty]   ← fault happens here
ksys_write+0x74/0x110             ← kernel write syscall handler
__arm64_sys_write+0x1c/0x30       ← ARM64 syscall entry
invoke_syscall+0x54/0x130
el0_svc_common.constprop.0+0x44/0xf0
el0_svc+0x2c/0x90
el0t_64_sync+0x18c/0x190          ← user space triggered via write() syscall
```
The user ran `echo "hello_world" > /dev/faulty`, which called the `write()` syscall,
which dispatched to `faulty_write`, which immediately faulted at offset `+0x10`.

### Locating the Exact Faulty Line
Using the offset `faulty_write+0x10` from the oops, we can pinpoint the exact
instruction using objdump on the unstripped module:
```bash
objdump -d faulty.ko | grep -A 10 "<faulty_write>"
```
Or with addr2line:
```bash
addr2line -e faulty.ko 0x10
```
Both methods point directly to the `*(int *)0 = 0` NULL dereference line in `faulty.c`.

### Summary
The `faulty` driver is an intentionally broken driver used for educational purposes
to demonstrate how kernel oops messages can be used to identify the exact location
of a bug in a kernel module. The oops output, combined with the PC offset and
call trace, gives us everything needed to find the offending line without
needing to run a debugger.
