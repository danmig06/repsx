#ifndef RDEF_SPU
#define RDEF_SPU

#include "../util.h"

enum {
	PHASE_ATTACK = 1,
	PHASE_DECAY = 2,
	PHASE_SUSTAIN = 3,
	PHASE_RELEASE = 0
};

// ADSR - Attack/Decay/Sustain/Release Control Register
enum {
	// Attack Fields
	// ADSR_ATK_DIRECTION is set to always increase
	ADSR_ATK_STEP = BIT_RANGE(8, 2),
#define ADSR_ATK_STEP_GET(o) GET_BITS(o, ADSR_ATK_STEP, 8)
#define ADSR_ATK_STEP_SET(o, v) SET_BITS(o, v, ADSR_ATK_STEP, 8)

	ADSR_ATK_SH   = BIT_RANGE(10, 5),
#define ADSR_ATK_SH_GET(o) GET_BITS(o, ADSR_ATK_SH, 10)
#define ADSR_ATK_SH_SET(o, v) SET_BITS(o, v, ADSR_ATK_SH, 10)

	ADSR_ATK_MODE = BIT(15),

	// Decay Fields
	// ADSR_DEC_MODE is set to always be exponential
	// ADSR_DEC_DIRECTION is set to always decrease
	// ADSR_DEC_STEP is set to always be -8
	ADSR_DEC_SH   = BIT_RANGE(4, 4),
#define ADSR_DEC_SH_GET(o) GET_BITS(o, ADSR_DEC_SH, 4)
#define ADSR_DEC_SH_SET(o, v) SET_BITS(o, v, ADSR_DEC_SH, 4)


	// Sustain Fields
	// the "Level" field is only used for Sustain
	ADSR_STN_LVL  = BIT_RANGE(0, 4),
#define ADSR_STN_LVL_GET(o) GET_BITS(o, ADSR_STN_LVL, 0)
#define ADSR_STN_LVL_SET(o, v) SET_BITS(o, v, ADSR_STN_LVL, 0)

	ADSR_STN_STEP = BIT_RANGE(22, 2),
#define ADSR_STN_STEP_GET(o) GET_BITS(o, ADSR_STN_STEP, 22)
#define ADSR_STN_STEP_SET(o, v) SET_BITS(o, v, ADSR_STN_STEP, 22)

	ADSR_STN_SH   = BIT_RANGE(24, 5),
#define ADSR_STN_SH_GET(o) GET_BITS(o, ADSR_STN_SH, 24)
#define ADSR_STN_SH_SET(o, v) SET_BITS(o, v, ADSR_STN_SH, 24)

	ADSR_STN_MODE = BIT(31),
	ADSR_STN_DIR  = BIT(30),
	
	// Release Fields
	// ADSR_REL_STEP is set to always be -8
	// ADSR_REL_DIRECTION is set to always decrease
	ADSR_REL_SH   = BIT_RANGE(16, 5),
#define ADSR_REL_SH_GET(o) GET_BITS(o, ADSR_REL_SH, 16)
#define ADSR_REL_SH_SET(o, v) SET_BITS(o, v, ADSR_REL_SH, 16)

	ADSR_REL_MODE = BIT(21),
};

// Sweep Mode (in Voice Volume registers, when BIT(15) == 1)
enum {
	SWEEP_STEP  = BIT_RANGE(0, 2),
#define SWEEP_STEP_GET(o) GET_BITS(o, SWEEP_STEP, 0)
#define SWEEP_STEP_SET(o, v) GET_BITS(o, v, SWEEP_STEP, 0)

	SWEEP_SHIFT = BIT_RANGE(2, 5),
#define SWEEP_SHIFT_GET(o) GET_BITS(o, SWEEP_SHIFT, 2)
#define SWEEP_SHIFT_SET(o, v) GET_BITS(o, v, SWEEP_SHIFT, 2)

	SWEEP_PHASE = BIT(12),
	SWEEP_DIR   = BIT(13),
	SWEEP_MODE  = BIT(14),
	SWEEP_EN    = BIT(15)
};

// used to index in ENDX
#define VOICE_KEY_OFF(k, n) ((k) & BIT(n))
// used to index in NON
#define VOICE_SOURCE(k, n) ((k) & BIT(n))
#define PMON_VOICE_GET(o, n) ((o) & BIT(n))
#define CDEXT_LVOL_GET(o) (((int16_t)((o) << 16)) >> 16)
#define CDEXT_RVOL_GET(o) ((int16_t)((o) >> 16))

// SPUCNT - SPU CoNTrol register
enum {
	CNT_CD_EN      = BIT(0),
	CNT_EXT_EN     = BIT(1),
	CNT_CD_REVB    = BIT(2),
	CNT_EXT_REVB   = BIT(3),
	CNT_TRN_MODE   = BIT_RANGE(4, 2),
#define CNT_TRN_MODE_GET(o) GET_BITS(o, CNT_TRN_MODE, 4)
#define CNT_TRN_MODE_SET(o, v) SET_BITS(o, v, CNT_TRN_MODE, 4)

	CNT_IRQ_EN     = BIT(6),
	CNT_REVB_EN    = BIT(7),
	CNT_NOISE_STEP = BIT_RANGE(8, 2),
#define CNT_NOISE_STEP_GET(o) GET_BITS(o, CNT_NOISE_STEP, 8)
#define CNT_NOISE_STEP_SET(o, v) SET_BITS(o, v, CNT_NOISE_STEP, 8)

	CNT_NOISE_SH   = BIT_RANGE(10, 4),
#define CNT_NOISE_SH_GET(o) GET_BITS(o, CNT_NOISE_SH, 10)
#define CNT_NOISE_SH_SET(o, v) SET_BITS(o, v, CNT_NOISE_SH, 10)

	CNT_UNMUTE     = BIT(14),
	CNT_ENABLE     = BIT(15)
};

// SPUSTAT - SPU STATus register
enum {
	STAT_SPU_MODE  = BIT_RANGE(0, 6),
#define STAT_SPU_MODE_GET(o) GET_BITS(o, STAT_SPU_MODE, 0)
#define STAT_SPU_MODE_SET(o, v) SET_BITS(o, v, STAT_SPU_MODE, 0)

	STAT_IRQ       = BIT(6),
	STAT_DMA_EN    = BIT(7),
	STAT_DMA_RD    = BIT(8),
	STAT_DMA_WR    = BIT(9),
	STAT_TRN_BUSY  = BIT(10),
	STAT_WR_REGION = BIT(11)
};

// ADPCM Block Header Fields
enum {
	ADP_SHIFT = BIT_RANGE(0, 4),
#define ADP_SHIFT_GET(o) GET_BITS(o, ADP_SHIFT, 0)
#define ADP_SHIFT_SET(o, v) GET_BITS(o, v, ADP_SHIFT, 0)

	ADP_FILTER = BIT_RANGE(4, 3),
#define ADP_FILTER_GET(o) GET_BITS(o, ADP_FILTER, 4)
#define ADP_FILTER_SET(o, v) GET_BITS(o, v, ADP_FILTER, 4)

	ADP_LOOP_END = BIT(8),
	ADP_LOOP_REPEAT = BIT(9),
	ADP_LOOP_START = BIT(10)
};

#endif
