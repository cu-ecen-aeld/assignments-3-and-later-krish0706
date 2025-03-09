# Faulty Character Driver analysis

After device initialisation, where the "faulty" device driver has been initialised, trying to write to `/dev/faulty` with the 
command `echo "hello, world" > /dev/faulty` causes the following error message and a reboot of the system is required:

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
user pgtable: 4k pages, 39-bit VAs, pgdp=0000000041b7e000
[0000000000000000] pgd=0000000000000000, p4d=0000000000000000, pud=0000000000000000
Internal error: Oops: 0000000096000045 [#1] SMP
Modules linked in: faulty(O) scull(O) hello(O) [last unloaded: faulty(O)]
CPU: 0 PID: 166 Comm: sh Tainted: G           O       6.1.44 #1
Hardware name: linux,dummy-virt (DT)
pstate: 80000005 (Nzcv daif -PAN -UAO -TCO -DIT -SSBS BTYPE=--)
pc : faulty_write+0x10/0x20 [faulty]
lr : vfs_write+0xc8/0x390
sp : ffffffc008dfbd20
x29: ffffffc008dfbd80 x28: ffffff8001b33500 x27: 0000000000000000
x26: 0000000000000000 x25: 0000000000000000 x24: 0000000000000000
x23: 000000000000000e x22: 000000000000000e x21: ffffffc008dfbdc0
x20: 0000005562995b30 x19: ffffff8001bb7800 x18: 0000000000000000
x17: 0000000000000000 x16: 0000000000000000 x15: 0000000000000000
x14: 0000000000000000 x13: 0000000000000000 x12: 0000000000000000
x11: 0000000000000000 x10: 0000000000000000 x9 : 0000000000000000
x8 : 0000000000000000 x7 : 0000000000000000 x6 : 0000000000000000
x5 : 0000000000000001 x4 : ffffffc00078c000 x3 : ffffffc008dfbdc0
x2 : 000000000000000e x1 : 0000000000000000 x0 : 0000000000000000
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

This results in an oops message and a stack dump which can be analysed to locate the source of the bug. The first line of the message tells us that the error is caused due to a null pointer dereference.  Then we can also see what kernel modules were loaded in at the time of the error. Next, we see the register states when the error occured, in the program counter (pc) we can see that the error occured when the program was in the `faulty_write` function, the `0x10/0x20` tells us that the error occured at an offset of 16 bytes in a 32 byte function. This is supported by the call trace at the end of the message which shows that the last function called before the error was the `faulty_write` function. So we can narrow down the error to the `faulty_write` function which is called when we try to write to `dev/faulty`. 

On observing the `faulty_write` function we can see the source of error, we can see that the first line in the fuction is causing a null pointer dereference: 
```
ssize_t faulty_write (struct file *filp, const char __user *buf, size_t count,
		loff_t *pos)
{
	/* make a simple fault by dereferencing a NULL pointer */
	*(int *)0 = 0;
	return 0;
}
```