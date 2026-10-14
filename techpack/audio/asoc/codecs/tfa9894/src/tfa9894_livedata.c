/*
 * tfa9894_livedata.c - read F0 / temperature / resistance from a TFA9894.
 *
 * Clean-room implementation of the speaker livedata read, redone for the
 * TFA9894 single-channel case.  The stock/NXP code path builds a stereo
 * ("primary + secondary") memtrack structure whenever more than one device
 * is present, but a TFA9894 (rev 0x94) only ever has the three primary
 * items, so that branch reads past the end of its device-type table on a
 * 4-amplifier board.  This version reads exactly the three TFA9894 items.
 *
 * DSP item addresses come from the stock livedata map for TFA9894:
 *   F0 @0x6fc, temperature @0x705, ReT @0x70b.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>

#include "tfa_device.h"
#include "tfa_service.h"
#include "tfa_dsp_fw.h"
#include "tfa98xx_tfafieldnames.h"
#include "tfa9894.h"

#define TFA9894_LIVEDATA_ITEMS		3
#define TFA9894_ITEM_BYTES		3	/* 24-bit DSP word per item */

/* Address of each livedata item inside the DSP memory map. */
static const unsigned short tfa9894_livedata_addr[TFA9894_LIVEDATA_ITEMS] = {
	0x6fc,	/* fRes_P */
	0x705,	/* T_P */
	0x70b,	/* ReT_P */
};

/* All three TFA9894 livedata items live in track 0x02. */
#define TFA9894_LIVEDATA_TRACK		0x02

int tfa9894_read_livedata(struct tfa_device *tfa, struct tfa9894_livedata *out)
{
	/* 3 header bytes + 3 length bytes + 3 items * 3 bytes + slack */
	unsigned char buffer[6 + TFA9894_LIVEDATA_ITEMS * TFA9894_ITEM_BYTES];
	int item_bytes = TFA9894_LIVEDATA_ITEMS * TFA9894_ITEM_BYTES;
	int data[TFA9894_LIVEDATA_ITEMS];
	enum Tfa98xx_Error ret;
	int i;

	if (!tfa || !out)
		return -EINVAL;

	/* Only a running DSP can answer a livedata read. */
	if ((TFA_GET_BF(tfa, MANSTATE) != 9) || (TFA_GET_BF(tfa, CFE) == 0)) {
		pr_debug("tfa9894: livedata read while DSP not running\n");
		return -EAGAIN;
	}

	memset(buffer, 0, sizeof(buffer));

	/* Message header: module + parameter id, then the payload length. */
	buffer[0] = 0;
	buffer[1] = MODULE_FRAMEWORK + 128;
	buffer[2] = FW_PAR_ID_SET_MEMTRACK;
	buffer[3] = (item_bytes >> 16) & 0xff;
	buffer[4] = (item_bytes >> 8) & 0xff;
	buffer[5] = item_bytes & 0xff;

	/* Each item is described by (track, address hi, address lo). */
	for (i = 0; i < TFA9894_LIVEDATA_ITEMS; i++) {
		unsigned short addr = tfa9894_livedata_addr[i];
		int off = 6 + i * TFA9894_ITEM_BYTES;

		buffer[off + 0] = TFA9894_LIVEDATA_TRACK;
		buffer[off + 1] = (addr >> 8) & 0xff;
		buffer[off + 2] = addr & 0xff;
	}

	/* Ask the DSP to populate the tracker structure. */
	ret = dsp_msg(tfa, item_bytes + 6, buffer);
	if (ret != Tfa98xx_Error_Ok) {
		pr_err("tfa9894: livedata setup failed (%d)\n", ret);
		return -EIO;
	}

	/* Read it back: 3 bytes elapsed time, then 3 bytes per item. */
	memset(buffer, 0, sizeof(buffer));
	ret = tfa_dsp_cmd_id_write_read(tfa, MODULE_FRAMEWORK,
					FW_PAR_ID_GET_MEMTRACK,
					item_bytes + 3, buffer);
	if (ret != Tfa98xx_Error_Ok) {
		pr_err("tfa9894: livedata read failed (%d)\n", ret);
		return -EIO;
	}

	tfa98xx_convert_bytes2data(item_bytes, &buffer[3], data);

	out->f0 = data[0];
	out->temp = data[1];
	out->ret = data[2];
	return 0;
}
