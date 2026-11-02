/* SPDX-License-Identifier: MIT
 * Narrow ALSA writer for the TB371FC 16-coefficient control.
 * Freestanding ARM64 ELF: no Android/NDK or libc runtime dependency.
 * A single ELEM_WRITE ioctl carries the ENTIRE matrix, followed by readback.
 */
#include <stddef.h>
#include <asm-generic/ioctl.h>
#include <sound/asound.h>

static volatile long initialized=1;
static struct snd_ctl_elem_value element;
static long expected[16];
static inline long call(long number,long a,long b,long c,long d)
{
 register long x0 __asm__("x0")=a;
 register long x1 __asm__("x1")=b;
 register long x2 __asm__("x2")=c;
 register long x3 __asm__("x3")=d;
 register long x8 __asm__("x8")=number;
 __asm__ volatile("svc 0" : "+r"(x0) : "r"(x1),"r"(x2),"r"(x3),"r"(x8) : "memory","cc");
 return x0;
}
static void message(const char *s)
{
 unsigned long n=0;
 while(s[n])n++;
 call(64,2,(long)s,n,0);
}
static __attribute__((noreturn)) void fail(const char *s,long error)
{
 char digits[24];unsigned long n=0,value=error<0?-error:error;
 message(s);message(" errno=");
 do {digits[n++]='0'+value%10;value/=10;}while(value);
 while(n)call(64,2,(long)&digits[--n],1,0);
 message("\n");call(93,1,0,0,0);__builtin_unreachable();
}
static int equal(const char *a,const char *b)
{
 while(*a&&*a==*b){a++;b++;}
 return *a==*b;
}
__attribute__((noreturn)) void entry(unsigned long *stack)
{
 long argc=stack[0],fd,ret,sum;
 char **argv=(char **)(stack+1);
 int check=argc==18&&equal(argv[1],"--check"),first=check?2:1,i;
 const char name[]="SpkRot 0 Matrix";
 _Static_assert(sizeof(long)==8,"ARM64 ALSA ABI");
 _Static_assert(offsetof(struct snd_ctl_elem_value,value)==72,"ALSA value offset");
 _Static_assert(sizeof(struct snd_ctl_elem_value)==1224,"ALSA value size");
 if(initialized!=1)fail("initialization failed",5);
 if(argc!=17&&!check)fail("usage: matrix-write [--check] 16 weights",22);
 for(i=0;i<16;i++) {
  const char *p=argv[first+i];long value=0;
  if(!*p)fail("empty weight",22);
  while(*p){if(*p<'0'||*p>'9')fail("non-numeric weight",22);value=value*10+*p++-'0';if(value>16384)fail("weight out of range",22);}
  expected[i]=value;element.value.integer.value[i]=value;
 }
 for(i=0;i<4;i++){
  sum=expected[i*4]+expected[i*4+1]+expected[i*4+2]+expected[i*4+3];
  if(sum!=16384)fail("row must sum to16384",22);
 }
 if(check){message("VALIDATED 16 coefficients\n");call(93,0,0,0,0);__builtin_unreachable();}
 element.id.iface=SNDRV_CTL_ELEM_IFACE_MIXER;
 for(i=0;i<(int)sizeof(name);i++)element.id.name[i]=name[i];
 fd=call(56,-100,(long)"/dev/snd/controlC0",2,0);
 if(fd<0)fail("control open failed",fd);
 ret=call(29,fd,SNDRV_CTL_IOCTL_ELEM_WRITE,(long)&element,0);
 if(ret<0){call(57,fd,0,0,0);fail("atomic matrix write failed",ret);}
 ret=call(29,fd,SNDRV_CTL_IOCTL_ELEM_READ,(long)&element,0);
 call(57,fd,0,0,0);
 if(ret<0)fail("matrix readback failed",ret);
 for(i=0;i<16;i++)if(element.value.integer.value[i]!=expected[i])fail("matrix readback mismatch",5);
 message("COMMITTED 16 coefficients\n");
 call(93,0,0,0,0);__builtin_unreachable();
}
__asm__(".global _start\n_start:\nmov x0, sp\nbl entry\n");
