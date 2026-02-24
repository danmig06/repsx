#include <psx/mdec.h>

#include "rdef/mdec.h"
#include "util.h"
#include "log.h"

#include <string.h>

#define WORD_SIZE(o, s) ((sizeof(*o) * s) / 4)
#define SE10(n) (((int16_t)((n) << 6)) >> 6)
#define RL_SIZE 64 * 2
// the theoretical maximum is 128kb according to some PsyQ docs, but some games like the RE games
// attempt to decompress a larger input, and may max out the hardware's 0xffff word limit
#define INPUT_WORD_SIZE 0x10000

enum {
	BLOCK_TYPE_Y1 = 0,
	BLOCK_TYPE_Y2 = 1,
	BLOCK_TYPE_Y3 = 2,
	BLOCK_TYPE_Y4 = 3,
	BLOCK_TYPE_Y  = 4,
	BLOCK_TYPE_CR = 4,
	BLOCK_TYPE_CB = 5,
};

/*
static uint8_t g_zigzag[] = {
	0 ,1 ,5 ,6 ,14,15,27,28,
	2 ,4 ,7 ,13,16,26,29,42,
	3 ,8 ,12,17,25,30,41,43,
	9 ,11,18,24,31,40,44,53,
	10,19,23,32,39,45,52,54,
	20,22,33,38,46,51,55,60,
	21,34,37,47,50,56,59,61,
	35,36,48,49,57,58,62,63
};
*/

static uint8_t g_reverse_zigzag[] = {
     0,  1,  8, 16,  9,  2,  3, 10,
    17, 24, 32, 25, 18, 11,  4,  5,
    12, 19, 26, 33, 40, 48, 41, 34,
    27, 20, 13,  6,  7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36,
    29, 22, 15, 23, 30, 37, 44, 51,
    58, 59, 52, 45, 38, 31, 39, 46,
    53, 60, 61, 54, 47, 55, 62, 63
};

void psx_mdec_init(struct psx_mdec* mdec) {
	memset(mdec->lqtab, 0, sizeof(mdec->lqtab));
	memset(mdec->cqtab, 0, sizeof(mdec->lqtab));
	memset(mdec->scale_table, 0, sizeof(mdec->scale_table));
	memset(&mdec->dec, 0, sizeof(mdec->dec));
	memset(&mdec->block, 0, sizeof(mdec->block));
	mdec->mb_data = malloc(INPUT_WORD_SIZE * sizeof(uint32_t));
	memset(mdec->mb_data, 0, INPUT_WORD_SIZE * sizeof(uint32_t));
	psx_mdec_reset(mdec);
}

void psx_mdec_reset(struct psx_mdec* mdec) {
	// STAT_DATA_OUT_EMPTY | BLOCK_TYPE_Y
	// log_error("MDEC: reset");
	mdec->regs.stat = 0x80040000;
	// abort current command
	mdec->regs.command = 0;
	mdec->receiving_data = false;
	mdec->block.offset = 0;
	mdec->block.size = 0;
	mdec->block.index = 0;
}

static void yuv_to_rgb(struct psx_mdec* mdec, uint16_t xx, uint16_t yy, uint8_t* out) {
	int16_t r, g, b, l;
	bool is_signed = (mdec->regs.stat & STAT_OUTSIGN) != 0;
	bool is_24bit = STAT_OUTDEPTH_GET(mdec->regs.stat) == 2;
	bool set_mask_bit = (mdec->regs.stat & STAT_OUTMASK) != 0;
	for(int x = 0; x < 8; x++) {
		for(int y = 0; y < 8; y++) {
			r = mdec->block.cr[((x + xx) / 2) + (((y + yy) / 2) * 8)];
			b = mdec->block.cb[((x + xx) / 2) + (((y + yy) / 2) * 8)];
			g = (-0.3437f * (float)b) + (-0.7143f * (float)r);
			r = 1.402f * (float)r;
			b = 1.772f * (float)b;
			l = mdec->block.y[x + (y * 8)];
			r = SAT(l + r, -128, 127);
			g = SAT(l + g, -128, 127);
			b = SAT(l + b, -128, 127);
			if(!is_signed) {
				r ^= 0x80;
				g ^= 0x80;
				b ^= 0x80;
			}

			uint32_t off = (x + xx) + ((y + yy) * 16);
			if(is_24bit) {
				out[off * 3] = r & 0xff;
				out[(off * 3) + 1] = g & 0xff;
				out[(off * 3) + 2] = b & 0xff;
			} else {
				r = (r & 0xff) >> 3;
				g = (g & 0xff) >> 3;
				b = (b & 0xff) >> 3;
				uint16_t pixel = r | g << 5 | b << 10;
				if(set_mask_bit) {
					pixel |= 0x8000;
				}
				out[off * 2] = pixel & 0xff;
				out[(off * 2) + 1] = pixel >> 8;
			}
		}
	}
}

static void mdec_idct(struct psx_mdec* mdec, int16_t* rl) {
	int16_t tmp_buf[8 * 8];
	int32_t sum;
	int16_t* src = rl;
	int16_t* dst = tmp_buf;
	for(int pass = 0; pass < 2; pass++) {
		for(int x = 0; x < 8; x++) {
			for(int y = 0; y < 8; y++) {
				sum = 0;
				for(int z = 0; z < 8; z++) {
					sum += src[y + (z * 8)] * (mdec->scale_table[x + (z * 8)] / 8);
				}
				dst[x + (y * 8)] = (sum + 0x0fff) / 0x2000;
			}
		}
		int16_t* t = src;
		src = dst;
		dst = t;
	}
}

static void rl_decode_block(struct psx_mdec* mdec, int16_t* blk, uint8_t* qt) {
	int k = 0;
	for(int i = 0; i < 64; i++) {
		blk[i] = 0;
	}

	uint16_t n = mdec->mb_data[mdec->dec.offset++];
	while(n == PSX_MDEC_EOB) {
		n = mdec->mb_data[mdec->dec.offset++];
	}

	int q_scale = (n >> 10) & 0x3f;
	int16_t val = SE10(n & 0x3ff) * qt[k];

	while(k < 64) {
		if(!q_scale) {
			val = SE10(n & 0x3ff) * 2;
		}
		val = SAT(val, -0x400, 0x3ff);

		if(q_scale > 0) {
			blk[g_reverse_zigzag[k]] = val;
		} else {
			blk[k] = val;
		}

		n = mdec->mb_data[mdec->dec.offset];
		if(k >= 63) {
			break;
		}
		mdec->dec.offset++;
		k += ((n >> 10) & 0x3f) + 1;
		val = (SE10(n & 0x3ff) * qt[k] * q_scale + 4) / 8;
	}

	mdec_idct(mdec, blk);
}

static void mdec_decode_block(struct psx_mdec* mdec) {
	mdec->block.n_available = 0;
	if(mdec->block.is_monochrome) {
		bool is_signed = (mdec->regs.stat & STAT_OUTSIGN) != 0;
		int16_t y;
		for(int i = 0; i < PSX_MDEC_MB_BUFFER_SIZE; i++) {
			rl_decode_block(mdec, mdec->block.y, mdec->lqtab);
			// y_to_mono
			for(int k = 0; k < 64; k++) {
				y = mdec->block.y[k];
				y = SAT(y & 0x1ff, -128, 127);
				if(!is_signed) {
					y ^= 0x80;
				}
				mdec->dec.buf[(i * PSX_MDEC_MB_SIZE) + k] = y;
			}
			mdec->block.n_available++;
		}
	} else {
		uint8_t* out_buf;
		for(int i = 0; i < PSX_MDEC_MB_BUFFER_SIZE; i++) {
			if(mdec->dec.offset >= INPUT_WORD_SIZE) {
				break;
			}
			out_buf = &mdec->dec.buf[i * PSX_MDEC_MB_SIZE];
			rl_decode_block(mdec, mdec->block.cr, mdec->cqtab);
			rl_decode_block(mdec, mdec->block.cb, mdec->cqtab);
			rl_decode_block(mdec, mdec->block.y, mdec->lqtab);
			yuv_to_rgb(mdec, 0, 0, out_buf);
			rl_decode_block(mdec, mdec->block.y, mdec->lqtab);
			yuv_to_rgb(mdec, 8, 0, out_buf);
			rl_decode_block(mdec, mdec->block.y, mdec->lqtab);
			yuv_to_rgb(mdec, 0, 8, out_buf);
			rl_decode_block(mdec, mdec->block.y, mdec->lqtab);
			yuv_to_rgb(mdec, 8, 8, out_buf);
			mdec->block.n_available++;
		}
	}
	mdec->block.index = 0;
	/*
	log_error("MDEC: Finished decoding %u-bit data input=(0x%04x -> 0x%04x remaining, %d halfwords consumed)",
	     (STAT_OUTDEPTH_GET(mdec->regs.stat) == 3) ? 15 : 24,
	     mdec->input.size,
	     mdec->input.size - mdec->dec.offset,
	     mdec->dec.offset - prev_offset
	);
	*/
}

static uint32_t mdec_read_block(struct psx_mdec* mdec) {
	if(mdec->block.offset == mdec->block.size) {
		mdec->block.offset = 0;
		mdec->block.index++;
		if(mdec->block.index == mdec->block.n_available) {
			mdec_decode_block(mdec);
		}
	}
	size_t read_offset = (mdec->block.index * PSX_MDEC_MB_SIZE) + mdec->block.offset;
	mdec->block.offset += 4;
	uint32_t word = *(uint32_t*)(&mdec->dec.buf[read_offset]);
	return word;
}

void psx_mdec_direct_in(struct psx_mdec* mdec, uint32_t word) {
	/*
	if(!mdec->input.dst) {
		log_error("MDEC: input destination not set");
		return;
	}
	*/
	mdec->input.dst[mdec->input.offset] = word;
	mdec->input.offset++;
	if(mdec->input.is_data) {
		uint16_t words_left = STAT_NWORDS_GET(mdec->regs.stat);
		STAT_NWORDS_SET(mdec->regs.stat, words_left - 1);
	}

	if(mdec->input.offset == mdec->input.size) {
		mdec->receiving_data = false;
		if(mdec->input.is_data) {
			// log_error("MDEC: macroblock upload finished (0x%x words received)", mdec->input.size);
			mdec->input.dst = NULL;
			mdec->block.index = 0;
			mdec->block.n_available = 0;
			mdec->block.offset = 0;
			mdec->block.is_monochrome = STAT_OUTDEPTH_GET(mdec->regs.stat) < 2;
			if(mdec->block.is_monochrome) {
				mdec->block.size = 8 * 8 * 2;
				STAT_CURBLK_SET(mdec->regs.stat, BLOCK_TYPE_Y);
			} else {
				mdec->block.size = 16 * 16 * ((STAT_OUTDEPTH_GET(mdec->regs.stat) == 2) ? 3 : 2);
				STAT_CURBLK_SET(mdec->regs.stat, BLOCK_TYPE_Y1);
			}
			mdec->dec.offset = 0;
			mdec_decode_block(mdec);
			mdec->regs.stat &= ~STAT_OUT_EMPTY;
		}
		mdec->regs.stat |= STAT_IN_FULL;
		mdec->regs.stat &= ~(STAT_BUSY | STAT_DATA_IN_REQ);
		if(mdec->regs.ctrl & CTRL_DATA_OUT_EN) {
			mdec->regs.stat |= STAT_DATA_OUT_REQ;
		}
	}
}

uint32_t psx_mdec_direct_out(struct psx_mdec* mdec) {
	if(mdec->dec.offset >= INPUT_WORD_SIZE) {
		log_error("MDEC: input buffer overrun");
		return 0xaaaaaaaa;
	}
	return mdec_read_block(mdec);
}

void mdec_do_cmd(struct psx_mdec* mdec, uint32_t cmd) {
	int cmd_num = cmd >> 29;
	switch(cmd_num) {
	case 0:
		STAT_NWORDS_SET(mdec->regs.stat, cmd & 0xffff);
		log_error("MDEC: Nop (setting 0x%x words)", cmd & 0xffff);
		break;
	case 1:
		mdec->input.dst = (uint32_t*)mdec->mb_data;
		mdec->input.size = cmd & 0xffff;
		STAT_NWORDS_SET(mdec->regs.stat, mdec->input.size - 1);
		mdec->receiving_data = true;
		mdec->input.is_data = true;
		mdec->regs.stat &= ~STAT_IN_FULL;
		mdec->regs.stat |= STAT_OUT_EMPTY;

		/*
		char* mb = (cmd & BIT(25)) ? "Mask ON," : "Mask OFF,";
		char* sign = (cmd & BIT(26)) ? "Signed," : "Unsigned,";
		log_error("MDEC: Decode Macroblock %s %s Depth=%d, 0x%x words", mb, sign, (cmd >> 27) & 3, cmd & 0xffff);
		*/
		break;
	case 2:
		log_error("MDEC: Set iqtab (luminance AND color=%d)", cmd & BIT(0));
		mdec->regs.stat &= ~(STAT_IN_FULL | STAT_OUT_EMPTY);
		mdec->input.dst = (uint32_t*)mdec->iqtab;
		uint32_t size;
		if(cmd & BIT(0)) {
			size = WORD_SIZE(mdec->iqtab, 128);
		} else {
			size = WORD_SIZE(mdec->lqtab, 64);
		}
		mdec->input.size = size;
		mdec->receiving_data = true;
		mdec->input.is_data = false;
		break;
	case 3:
		log_error("MDEC: Set Scale Table");
		mdec->regs.stat &= ~(STAT_IN_FULL | STAT_OUT_EMPTY);
		mdec->input.dst = (uint32_t*)mdec->scale_table;
		mdec->input.size = WORD_SIZE(mdec->scale_table, 64);
		mdec->receiving_data = true;
		mdec->input.is_data = false;
		break;
	default:
		log_error("MDEC: Invalid command");
		return;
	}
	// all valid commands copy bits 25-28 to stat bits 23-26
	mdec->regs.stat &= ~(STAT_OUTMASK | STAT_OUTSIGN | STAT_OUTDEPTH);
	mdec->regs.stat |= (cmd >> 2) & (STAT_OUTMASK | STAT_OUTSIGN | STAT_OUTDEPTH);
	mdec->regs.stat |= STAT_BUSY;
	if(mdec->regs.ctrl & CTRL_DATA_IN_EN) {
		mdec->regs.stat |= STAT_DATA_IN_REQ;
	}
	mdec->regs.command = cmd_num;
	mdec->input.offset = 0;
}

uint32_t psx_mdec_read32(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	struct psx_mdec* mdec = reg->peripheral;
	if(register_offset == 0) {
		// return pending data
		return mdec_read_block(mdec);
	} else if(register_offset == 4) {
		return mdec->regs.stat;
	}

	return 0;
}

void psx_mdec_write32(struct psx_region* reg, uint32_t addr, uint32_t val) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	struct psx_mdec* mdec = reg->peripheral;
	if(register_offset == 0) {
		// handle command
		if(mdec->receiving_data) {
			psx_mdec_direct_in(mdec, val);	
		} else {
			mdec_do_cmd(mdec, val);
		}
	} else if(register_offset == 4) {
		if(val & CTRL_RESET) {
			psx_mdec_reset(mdec);
		}
		mdec->regs.ctrl = val & ~CTRL_RESET;
	}
}
