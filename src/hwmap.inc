sys->memory->map[0] = (struct psx_region){ "phys",        0x00000000, PSX_MEM_PHYS_SIZE * 4,	sys->memory->phys, 
					   .read32 = psx_phys_read32,   .write32 = psx_phys_write32,
					   .read16 = psx_phys_read16,   .write16 = psx_phys_write16,
					   .read8  = psx_phys_read8,    .write8  = psx_phys_write8   };

sys->memory->map[1] = (struct psx_region){ "expansion1",  0x1f000000, 0x00800000,		NULL,
					   .read32 = psx_null_read32,   .write32 = psx_null_write32,
					   .read16 = psx_null_read16,   .write16 = psx_null_write16,
					   .read8  = psx_null_read8,    .write8  = psx_null_write8   };

sys->memory->map[2] = (struct psx_region){ "scratch",     0x1f800000, PSX_MEM_SCRATCH_SIZE,	sys->memory->scratch,
					   .read32 = psx_phys_read32,   .write32 = psx_phys_write32,
					   .read16 = psx_phys_read16,   .write16 = psx_phys_write16,
					   .read8  = psx_phys_read8,    .write8  = psx_phys_write8   };

sys->memory->map[3] = (struct psx_region){ "memctrl1",    0x1f801000, 0x00000024,		sys->mc,
					   .read32 = psx_memctl_read32, .write32 = psx_memctl_write32, 
					   .read16 = psx_memctl_read16, .write16 = psx_memctl_write16,
					   .read8  = psx_memctl_read8,  .write8  = psx_memctl_write8 };

sys->memory->map[4] = (struct psx_region){ "sio",         0x1f801040, 0x00000020,		sys->sio,
					   .read32 = psx_sio_read32,   .write32 = psx_sio_write32, 
					   .read16 = psx_sio_read16,   .write16 = psx_sio_write16,
					   .read8  = psx_sio_read8,    .write8  = psx_sio_write8     };

sys->memory->map[5] = (struct psx_region){ "memctrl2",    0x1f801060, 0x00000004,		sys->mc,
					   .read32 = psx_memctl_read32, .write32 = psx_memctl_write32, 
					   .read16 = psx_memctl_read16, .write16 = psx_memctl_write16,
					   .read8  = psx_memctl_read8,  .write8  = psx_memctl_write8 };

sys->memory->map[6] = (struct psx_region){ "irqctl",      0x1f801070, 0x00000008,		sys->irq,
					   .read32 = psx_irq_read32,   .write32 = psx_irq_write32, 
					   .read16 = psx_irq_read16,   .write16 = psx_irq_write16,
					   .read8  = psx_irq_read8,    .write8  = psx_irq_write8     };

sys->memory->map[7] = (struct psx_region){ "dma",         0x1f801080, 0x00000080,		sys->dmac,
					   .read32 = psx_dmac_read32,   .write32 = psx_dmac_write32, 
					   .read16 = psx_dmac_read16,   .write16 = psx_dmac_write16,
					   .read8  = psx_dmac_read8,    .write8  = psx_dmac_write8   };

sys->memory->map[8] = (struct psx_region){ "timer",       0x1f801100, 0x00000040,		sys->timer,
					   .read32 = psx_tmr_read32,   .write32 = psx_tmr_write32, 
					   .read16 = psx_tmr_read16,   .write16 = psx_tmr_write16,
					   .read8  = psx_null_read8,   .write8  = psx_null_write8    };

sys->memory->map[9] = (struct psx_region){ "cdrom",       0x1f801800, 0x00000004,		sys->cdrom,
					   .read32 = psx_null_read32,   .write32 = psx_null_write32, 
					   .read16 = psx_cdr_read16,    .write16 = psx_null_write16,
					   .read8  = psx_cdr_read8,     .write8  = psx_cdr_write8    };

sys->memory->map[10] = (struct psx_region){ "gpu",        0x1f801810, 0x00000008,		sys->gpu,
					   .read32 = psx_gpu_read32,    .write32 = psx_gpu_write32, 
					   .read16 = psx_gpu_read16,    .write16 = psx_gpu_write16,
					   .read8  = psx_gpu_read8,     .write8  = psx_gpu_write8    };

sys->memory->map[11] = (struct psx_region){ "mdec",       0x1f801820, 0x00000008,		sys->mdec,
					   .read32 = psx_mdec_read32,   .write32 = psx_mdec_write32, 
					   .read16 = psx_null_read16,   .write16 = psx_null_write16,
					   .read8  = psx_null_read8,    .write8  = psx_null_write8   };

sys->memory->map[12] = (struct psx_region){ "spu",        0x1f801c00, 0x00000280,		sys->spu,
					   .read32 = psx_spu_read32,    .write32 = psx_spu_write32, 
					   .read16 = psx_spu_read16,    .write16 = psx_spu_write16,
					   .read8  = psx_spu_read8,     .write8  = psx_spu_write8    };

sys->memory->map[13] = (struct psx_region){ "expansion2", 0x1f802000, 0x00002000,		NULL,
					   .read32 = psx_null_read32,   .write32 = psx_null_write32, 
					   .read16 = psx_null_read16,   .write16 = psx_null_write16,
					   .read8  = psx_null_read8,    .write8  = psx_null_write8   };

sys->memory->map[14] = (struct psx_region){ "bios",       0x1fc00000, 0x00080000,		sys->bios,
					   .read32 = psx_bios_read32,   .write32 = psx_bios_write32,
					   .read16 = psx_bios_read16,   .write16 = psx_bios_write16,
					   .read8  = psx_bios_read8,    .write8  = psx_bios_write8   };

sys->memory->map[15] = (struct psx_region){ "memctrl3",   0x1ffe0000, 0x00000200,		sys->mc,
					   .read32 = psx_memctl_read32, .write32 = psx_memctl_write32,
					   .read16 = psx_memctl_read16, .write16 = psx_memctl_write16,
					   .read8  = psx_memctl_read8,  .write8  = psx_memctl_write8 };

