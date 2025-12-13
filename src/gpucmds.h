#ifndef PSX_GPU_CMD_H
#define PSX_GPU_CMD_H

#define GPU_CMD_IDX 0
#define GPU_CMD_DATA_IDX 1
#define GPU_CMD_POLYLINE_V0 2
#define GPU_CMD_POLYLINE_C0 3
#define GPU_CMD_POLYLINE_V1 4
#define GPU_CMD_POLYLINE_C1 5
#define GPU_CMD_POLYLINE_STATE 6

typedef struct cmd_poly_flags { 
	bool is_raw: 1;
	bool is_transparent: 1;
	bool is_textured: 1;
	bool is_quad: 1;
	bool is_gouraud_shaded: 1;
	uint32_t num: 3;
} cmd_poly_flags_t; 

typedef struct {
	uint32_t unused0: 1;
	bool is_transparent: 1;
	uint32_t unused1: 1;
	bool polyline: 1;
	bool is_gouraud_shaded: 1;
	uint32_t num: 3;
} cmd_line_flags_t;

typedef struct cmd_rect_flags { 
	bool is_raw: 1; 
	bool is_transparent: 1; 
	bool is_textured: 1; 
	uint32_t geometry: 2;
	uint32_t num: 3;
} cmd_rect_flags_t;

void gp0_nop(struct psx_gpu* gpu);
void gp0_cache_clear(struct psx_gpu* gpu);
void gp0_poly(struct psx_gpu* gpu);
void gp0_line(struct psx_gpu* gpu);
void gp0_rect(struct psx_gpu* gpu);
void gp0_fillvram(struct psx_gpu* gpu);
void gp0_shaded_opaque_quad(struct psx_gpu* gpu);
void gp0_blit(struct psx_gpu* gpu);
void gp0_image_load(struct psx_gpu* gpu);
void gp0_image_store(struct psx_gpu* gpu);
void gp0_draw_mode(struct psx_gpu* gpu);
void gp0_set_texture_window(struct psx_gpu* gpu);
void gp0_draw_area_tl(struct psx_gpu* gpu);
void gp0_draw_area_br(struct psx_gpu* gpu);
void gp0_set_draw_offset(struct psx_gpu* gpu);
void gp0_set_mask_bit(struct psx_gpu* gpu);

void gp1_reset(struct psx_gpu* gpu, uint32_t cmd);
void gp1_buffer_clear(struct psx_gpu* gpu, uint32_t cmd);
void gp1_irq_ack(struct psx_gpu* gpu, uint32_t cmd);
void gp1_display_mode(struct psx_gpu* gpu, uint32_t cmd);
void gp1_dma_direction(struct psx_gpu* gpu, uint32_t cmd);
void gp1_set_display_area(struct psx_gpu* gpu, uint32_t cmd);
void gp1_set_display_hrange(struct psx_gpu* gpu, uint32_t cmd);
void gp1_set_display_vrange(struct psx_gpu* gpu, uint32_t cmd);
void gp1_set_display_disable(struct psx_gpu* gpu, uint32_t cmd);
void gp1_read_register(struct psx_gpu* gpu, uint32_t cmd);

#endif // #ifndef PSX_GPU_CMD_H
