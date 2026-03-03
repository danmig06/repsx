#include <psx/gpu.h>
#include <psx/irq.h>
#include <psx/timer.h>
#include <psx/sched.h>

#include "util.h"
#include "rdef/gpu.h"
#include "gpucmds.h"

#include <stdbool.h>
#include <string.h>
#include <stdio.h>

// TODO: do some math and add dynamic delay calculation for PAL/NTSC interlaced/progressive
#define CPU_CLOCKS_PER_VSYNC 564480
#define CPU_CLOCKS_PER_HDRAW 1629 // (CPVSYNC / 260) - CPSCAN
#define HBLANK_DURATION 542 // (CPVSYNC / 260) - CPHDRAW
#define OVERSCAN_SIZE 8

static void gpu_hblank(struct psx_sched*, struct psx_sev*);
static void gpu_hblank_end(struct psx_sched*, struct psx_sev*);
static void gpu_vblank(struct psx_sched*, struct psx_sev*);

static struct psx_sev hblank_event = {
	.id = PSX_SEV_ID_HBLANK,
	.eta = CPU_CLOCKS_PER_HDRAW,
	.trigger = gpu_hblank,
};

static struct psx_sev hblank_end_event = {
	.id = PSX_SEV_ID_HBLANK_END,
	.eta = HBLANK_DURATION,
	.trigger = gpu_hblank_end,
};

static struct psx_sev vblank_event = {
	.id = PSX_SEV_ID_VBLANK,
	.eta = CPU_CLOCKS_PER_VSYNC,
	.trigger = gpu_vblank,
};

static void gpu_hblank(struct psx_sched* sched, struct psx_sev* self) {
	psx_tmr_hsync(sched->sys->timer);
	psx_sched_add_ev(sched, &hblank_end_event);
	psx_sched_remove_ev(sched, self->id);
}

static int g_dotclock_divider_table[] = { 10, 8, 5, 4, 7 };

static int get_dotclock_divider(struct psx_gpu* gpu) {
	if(gpu->gpustat & GPUSTAT_HRES2) {
		return g_dotclock_divider_table[4];
	} else {
		return g_dotclock_divider_table[GPUSTAT_HRES1_GET(gpu->gpustat)];
	}
}

static void gpu_hblank_end(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_gpu* gpu = sched->sys->gpu;

	if(!(gpu->gpustat & GPUSTAT_VINTERLACE)) {
		if(gpu->scanline_count == PSX_GPU_VISIBLE_SCANS_NTSC - 1) {
			gpu->gpustat ^= GPUSTAT_SCNODD;
		}
	} else if(gpu->scanline_count < PSX_GPU_VISIBLE_SCANS_NTSC) {
		if(gpu->scanline_count & 1) {
			gpu->gpustat |= GPUSTAT_SCNODD;
		} else {
			gpu->gpustat &= ~GPUSTAT_SCNODD;
		}
	} else {
		gpu->gpustat &= ~GPUSTAT_SCNODD;
	}

	gpu->scanline_count++;
	psx_sched_add_ev(sched, &hblank_event);
	psx_sched_remove_ev(sched, self->id);
}

static void gpu_vblank(struct psx_sched* sched, struct psx_sev* self) {
	struct psx_gpu* gpu = sched->sys->gpu;

	gpu->scanline_count = 0;
	psx_tmr_vsync(sched->sys->timer);	
	psx_irq_raise(sched->sys->irq, PSX_IRQ_ID_VBLANK);
	
	int dotclock_div = get_dotclock_divider(gpu);
	int h_shift = (gpu->gpustat & GPUSTAT_VINTERLACE) && (gpu->gpustat & GPUSTAT_VRES);
	int overscan = OVERSCAN_SIZE << h_shift;
	int w = (((gpu->display_area.x2 - gpu->display_area.x1) / dotclock_div) + 2) & (~3);
	int h = (gpu->display_area.y2 - gpu->display_area.y1) << h_shift;
	gpu->renderer.update(&gpu->renderer, gpu->display_area.x, gpu->display_area.y + overscan, w, h - (overscan * 2), (gpu->gpustat & GPUSTAT_RGB24EN));
	psx_sched_remove_ev(sched, self->id);
	psx_sched_add_ev(sched, &vblank_event);
}

void psx_gpu_init(struct psx_gpu* gpu) {
	psx_gpu_reset(gpu);
	// setup hblank/vblank event loop
	psx_sched_add_ev(gpu->sys->sched, &hblank_event);
	psx_sched_add_ev(gpu->sys->sched, &vblank_event);
}

void psx_gpu_reset(struct psx_gpu* gpu) {
	gpu->gpuread = 0;
	memset(&gpu->gpustat, 0, sizeof(gpu->gpustat));
	memset(&gpu->tex_window, 0, sizeof(gpu->tex_window));
	memset(&gpu->cmd, 0, sizeof(gpu->cmd));
	memset(&gpu->draw_off, 0, sizeof(gpu->draw_off));
	gpu->gpustat = (GPUSTAT_DMA_READY | GPUSTAT_CMD_READY | GPUSTAT_VRAM_READY);
}

static void gpu_do_gp0(struct psx_gpu* gpu, uint32_t cmd) {
	if(gpu->cmd.words_left == 0) {
		memset(&gpu->cmd, 0, sizeof(gpu->cmd));
		uint8_t cmd_num = cmd >> 24;
		bool is_render_cmd = (cmd_num >> 5) < 4 && (cmd_num >> 5) > 0;
		// these attributes are always in the same positions, when used
		bool is_textured = (cmd_num & BIT(2)) != 0;
		bool is_gouraud_shaded = (cmd_num & BIT(4)) != 0;
		
		// if not, then it is a misc or blit command
		if(is_render_cmd) {
			switch(cmd_num >> 5) {
			case 1: {
				bool is_quad = (cmd_num & BIT(3)) != 0;
				// 1 color if shaded and 1 texture uv if textured for 4/3 vertices, 
				// the first color is in the command word, so we have to account for it
				int additional_attributes = is_gouraud_shaded + is_textured + 1;
				gpu->cmd.words_left = (additional_attributes * ((is_quad) ? 4 : 3)) - is_gouraud_shaded;
				gpu->cmd.execute = gp0_poly;
				break;
			}
			case 2: {
				// 2 vertices + 1 color if gouraud shaded
				gpu->cmd.words_left = 2 + (is_gouraud_shaded);
				gpu->cmd.execute = gp0_line;
				break;
			}
			case 3: {
				uint8_t geometry = (cmd_num >> 3) & 3;
				// top-left vertex + texture uv + size descriptor if geometry is variable, CLUT/Texpage come from GPUSTAT
				gpu->cmd.words_left = (geometry == 0) + is_textured + 1;
				gpu->cmd.execute = gp0_rect;
				break;
			}
			default:
				log_error("Unhandled GP0 render command 0x%08x", cmd);
				return;
			}
		} else {
			switch(cmd_num) {
			case 0x00: return;
			case 0x01: 
				gpu->cmd.execute = gp0_cache_clear; 
				break;
			case 0x02: 
				gpu->cmd.execute = gp0_fillvram;
				gpu->cmd.words_left = 2;
				break;
			case 0x80: 
				gpu->cmd.execute = gp0_blit; 
				gpu->cmd.words_left = 3;
				break;
			case 0xa0: 
				gpu->cmd.execute = gp0_image_load; 
				gpu->cmd.words_left = 2;
				break;
			case 0xc0: 
				gpu->cmd.execute = gp0_image_store; 
				gpu->cmd.words_left = 2;
				break;
			case 0xe1: 
				gpu->cmd.execute = gp0_draw_mode;
				break;
			case 0xe2: 
				gpu->cmd.execute = gp0_set_texture_window; 
				break;
			case 0xe3: 
				gpu->cmd.execute = gp0_draw_area_tl; 
				break;
			case 0xe4: 
				gpu->cmd.execute = gp0_draw_area_br;
				break;
			case 0xe5: 
				gpu->cmd.execute = gp0_set_draw_offset;
				break;
			case 0xe6: 
				gpu->cmd.execute = gp0_set_mask_bit;
				break;
			default:
				log_error("Unhandled GP0 command 0x%08x", cmd);
				return;
			}
		}

		// adjust for current command, now that we know it's valid
		gpu->cmd.words_left += 1;
	}
	
	if(gpu->cmd.receiving_data) {
		gpu->cmd.buf[GPU_CMD_DATA_IDX] = cmd;
		gpu->cmd.words_left--;

		gpu->cmd.update(gpu);
		if(gpu->cmd.words_left == 0) {
			gpu->cmd.receiving_data = false;
		}
	} else {
		gpu->cmd.buf[gpu->cmd.idx++] = cmd;
		gpu->cmd.words_left--;
		
		if(gpu->cmd.words_left == 0) {
			gpu->cmd.execute(gpu);
		}
	}
}

void gpu_do_gp1(struct psx_gpu* gpu, uint32_t cmd) {
	uint8_t cmd_num = cmd >> 24;
	switch(cmd_num) {
	case 0x00: gp1_reset(gpu, cmd); break;
	case 0x01: gp1_buffer_clear(gpu, cmd); break;
	case 0x02: gp1_irq_ack(gpu, cmd); break;
	case 0x03: gp1_set_display_disable(gpu, cmd); break;
	case 0x04: gp1_dma_direction(gpu, cmd); break;
	case 0x05: gp1_set_display_area(gpu, cmd); break;
	case 0x06: gp1_set_display_hrange(gpu, cmd); break;
	case 0x07: gp1_set_display_vrange(gpu, cmd); break;
	case 0x08: gp1_display_mode(gpu, cmd); break;
	case 0x10: gp1_read_register(gpu, cmd); break;
	default:
		log_error("Unhandled GP1 command 0x%08x", cmd);
		break;
	}
}

static void update_gpuread(struct psx_gpu* gpu) {
	uint32_t off_x, off_y;
	uint32_t max_x = gpu->blit_state.start_x + gpu->blit_state.w;
	uint32_t max_y = gpu->blit_state.start_y + gpu->blit_state.h;
	uint32_t packet = 0;

	off_x = gpu->blit_state.x - gpu->blit_state.start_x;
	off_y = gpu->blit_state.y - gpu->blit_state.start_y;
	packet |= gpu->blit_state.texels[off_x + (off_y * gpu->blit_state.w)] & 0xffff;
	gpu->blit_state.x++;
	if(gpu->blit_state.x == max_x) {
		if(gpu->blit_state.y == max_y) {
			gpu->blit_state.is_read = false;
			gpu->renderer.dispose_vram(&gpu->renderer, gpu->blit_state.texels);
			gpu->blit_state.texels = NULL;
			gpu->gpuread = packet;
			gpu->gpustat &= ~GPUSTAT_VRAM_READY;
			return;
		}
		gpu->blit_state.x = gpu->blit_state.start_x;
		gpu->blit_state.y++;
	}

	off_x = gpu->blit_state.x - gpu->blit_state.start_x;
	off_y = gpu->blit_state.y - gpu->blit_state.start_y;
	packet |= gpu->blit_state.texels[off_x + (off_y * gpu->blit_state.w)] << 16;
	gpu->blit_state.x++;
	if(gpu->blit_state.x == max_x) {
		if(gpu->blit_state.y == max_y) {
			gpu->blit_state.is_read = false;
			gpu->renderer.dispose_vram(&gpu->renderer, gpu->blit_state.texels);
			gpu->blit_state.texels = NULL;
			gpu->gpuread = packet;
			gpu->gpustat &= ~GPUSTAT_VRAM_READY;
			return;
		}
		gpu->blit_state.x = gpu->blit_state.start_x;
		gpu->blit_state.y++;
	}

	gpu->gpuread = packet;
}

uint32_t psx_gpu_read32(struct psx_region* reg, uint32_t addr) {
	struct psx_gpu* gpu = reg->peripheral;
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;

	if(register_offset == 0) {
		if(gpu->blit_state.is_read && gpu->blit_state.texels != NULL) {
			update_gpuread(gpu);
		}
		return gpu->gpuread;
	} else if(register_offset == 4) {
		return gpu->gpustat;
	}

	return 0;
}

void psx_gpu_write32(struct psx_region* reg, uint32_t addr, uint32_t val) {
	struct psx_gpu* gpu = reg->peripheral;
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;

	if(register_offset == 0) {
		gpu_do_gp0(gpu, val);
		return;
	} else if(register_offset == 4) {
		gpu_do_gp1(gpu, val);
		return;
	}

	log_error("Unhandled GPU write32 0x%x:0x%08x", register_offset, val);
}

uint16_t psx_gpu_read16(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	uint16_t val = 0;

	if(register_offset < 4) {
		val = 0;
	} else if(register_offset >= 4) {
		uint8_t* regs = reg->peripheral;
		memcpy(&val, &regs[register_offset], sizeof(val));
	}

	return val;
}

void psx_gpu_write16(struct psx_region* reg, uint32_t addr, uint16_t val) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	
	if(register_offset < 4) {
		val = 0;
	} else if(register_offset >= 4) {
		uint8_t* regs = reg->peripheral;
		memcpy(&regs[register_offset], &val, sizeof(val));
	}

	log_error("GPU write16 0x%x:0x%08x", register_offset, val);
}

uint8_t psx_gpu_read8(struct psx_region* reg, uint32_t addr) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	uint8_t val = 0;

	if(register_offset < 4) {
		val = 0;
	} else if(register_offset >= 4) {
		uint8_t* regs = reg->peripheral;
		val = regs[register_offset];
	}

	return val;
}

void psx_gpu_write8(struct psx_region* reg, uint32_t addr, uint8_t val) {
	uint32_t register_offset = PSX_MEM_REAL_ADDR(addr) - reg->start;
	
	if(register_offset < 4) {
		val = 0;
	} else if(register_offset >= 4) {
		uint8_t* regs = reg->peripheral;
		regs[register_offset] = val;
	}

	log_error("GPU write8 0x%x:0x%08x", register_offset, val);
}

