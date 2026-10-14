/*
 * tfa9894_livedata.c - read F0 / temperature / resistance from a TFA9894.
 *
 * The NXP reference driver asks the DSP for this data through the FW_PAR_ID
 * memtrack framework parameter.  The Lenovo TB371FC firmware does not
 * implement that parameter: a SET_MEMTRACK dsp_msg comes back with status 6
 * (invalid parameter), which dsp_msg maps to 206 (RPC base + invalid param).
 * That failure is not specific to this unit - the original NXP read path uses
 * the same frame and fails identically on this ROM.
 *
 * Instead we read the three values straight out of DSP XMEM, which the
 * hardware exposes through the normal memory-access registers.  The stock
 * livedata map lists exactly these addresses in XMEM (track 0x02):
 *   F0 @0x6fc, temperature @0x705, ReT @0x70b.
 *
 * Values are returned raw (unscaled), which is what the caller needs to
 * compare amplifiers against each other.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include "tfa_device.h"
#include "tfa_service.h"
#include "tfa9894.h"

/* XMEM addresses of the three livedata items (high nibble 0 => XMEM). */
#define TFA9894_XMEM_F0		0x6fc
#define TFA9894_XMEM_TEMP	0x705
#define TFA9894_XMEM_RET	0x70b

int tfa9894_read_livedata(struct tfa_device *tfa, struct tfa9894_livedata *out)
{
	int f0, temp, res;
	enum Tfa98xx_Error err;

	if (!tfa || !out)
		return -EINVAL;

	err = tfa98xx_dsp_read_mem(tfa, TFA9894_XMEM_F0, 1, &f0);
	if (err != Tfa98xx_Error_Ok)
		return -EIO;
	err = tfa98xx_dsp_read_mem(tfa, TFA9894_XMEM_TEMP, 1, &temp);
	if (err != Tfa98xx_Error_Ok)
		return -EIO;
	err = tfa98xx_dsp_read_mem(tfa, TFA9894_XMEM_RET, 1, &res);
	if (err != Tfa98xx_Error_Ok)
		return -EIO;

	out->f0 = f0;
	out->temp = temp;
	out->ret = res;
	return 0;
}
