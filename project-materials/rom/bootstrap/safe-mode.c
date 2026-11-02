/* SPDX-License-Identifier: GPL-2.0-only
 * Minimal read-only KSU UAPI 6 safe-mode query; no libc or Android SDK needed.
 * INSTALL_MAGIC values and CHECK_SAFEMODE ABI: ReSukiSU uapi/supercall.h.
 */
typedef unsigned long U;
static long sc(U n,U a,U b,U c,U d) {
 register U x8 __asm__("x8")=n,x0 __asm__("x0")=a,x1 __asm__("x1")=b;
 register U x2 __asm__("x2")=c,x3 __asm__("x3")=d;
 __asm__ volatile("svc #0":"+r"(x0):"r"(x8),"r"(x1),"r"(x2),"r"(x3):"memory","cc");
 return (long)x0;
}
void _start(void) {
 int fd=-1;
 unsigned char safe=255;
 /* Invalid Linux reboot magics: without the KSU hook this returns EINVAL. */
 sc(142,0xDEADBEEFUL,0xCAFEBABEUL,0,(U)&fd);
 long rc=fd<0 ? -1 : sc(29,(U)fd,0x80004b05UL,(U)&safe,0);
 if(fd>=0) sc(57,(U)fd,0,0,0);
 const char ok[]="NORMAL\n",on[]="SAFE_MODE\n",err[]="UNAVAILABLE\n";
 const char *p=rc<0||safe>1?err:safe?on:ok;
 U len=rc<0||safe>1?12:safe?10:7;
 sc(64,1,(U)p,len,0);
 sc(93,rc<0||safe>1?11:safe?10:0,0,0,0);
 __builtin_unreachable();
}
