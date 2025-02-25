// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2016 Allwinnertech Co., Ltd.
 * Copyright (C) 2017-2018 Bootlin
 *
 * Maxime Ripard <maxime.ripard@bootlin.com>
 */

#include <backlight.h>
#include <clk.h>
#include <clk/sunxi.h>
#include <display.h>
#include <div64.h>
#include <dm/device_compat.h>
#include <dm/device-internal.h>
#include <dm/devres.h>
#include <dm.h>
#include <dm/lists.h>
#include <dsi_host.h>
#include <fdtdec.h>
#include <linux/bitops.h>
#include <linux/iopoll.h>
#include <linux/time.h>
#include <log.h>
#include <mipi_dsi.h>
#include <panel.h>
#include <regmap.h>
#include <reset.h>
#include <syscon.h>
#include <video_bridge.h>
#include <video.h>

#define CONFIG_VIDEO_SUNXI_TCON_USE_CH0

#define SUN4I_TCON_GCTL_REG 0x0
#define SUN4I_TCON_GCTL_TCON_ENABLE BIT(31)
#define SUN4I_TCON_GCTL_IOMAP_MASK BIT(0)
#define SUN4I_TCON_GCTL_IOMAP_TCON1 (1 << 0)
#define SUN4I_TCON_GCTL_IOMAP_TCON0 (0 << 0)

#define SUN4I_TCON_GINT0_REG 0x4
#define SUN4I_TCON_GINT0_VBLANK_ENABLE(pipe) BIT(31 - (pipe))
#define SUN4I_TCON_GINT0_TCON0_TRI_FINISH_ENABLE BIT(27)
#define SUN4I_TCON_GINT0_TCON0_TRI_COUNTER_ENABLE BIT(26)
#define SUN4I_TCON_GINT0_VBLANK_INT(pipe) BIT(15 - (pipe))
#define SUN4I_TCON_GINT0_TCON0_TRI_FINISH_INT BIT(11)
#define SUN4I_TCON_GINT0_TCON0_TRI_COUNTER_INT BIT(10)

#define SUN4I_TCON_GINT1_REG 0x8

#define SUN4I_TCON_FRM_CTL_REG 0x10
#define SUN4I_TCON0_FRM_CTL_EN BIT(31)
#define SUN4I_TCON0_FRM_CTL_MODE_R BIT(6)
#define SUN4I_TCON0_FRM_CTL_MODE_G BIT(5)
#define SUN4I_TCON0_FRM_CTL_MODE_B BIT(4)

#define SUN4I_TCON0_FRM_SEED_PR_REG 0x14
#define SUN4I_TCON0_FRM_SEED_PG_REG 0x18
#define SUN4I_TCON0_FRM_SEED_PB_REG 0x1c
#define SUN4I_TCON0_FRM_SEED_LR_REG 0x20
#define SUN4I_TCON0_FRM_SEED_LG_REG 0x24
#define SUN4I_TCON0_FRM_SEED_LB_REG 0x28
#define SUN4I_TCON0_FRM_TBL0_REG 0x2c
#define SUN4I_TCON0_FRM_TBL1_REG 0x30
#define SUN4I_TCON0_FRM_TBL2_REG 0x34
#define SUN4I_TCON0_FRM_TBL3_REG 0x38

#define SUN4I_TCON0_CTL_REG 0x40
#define SUN4I_TCON0_CTL_TCON_ENABLE BIT(31)
#define SUN4I_TCON0_CTL_IF_MASK GENMASK(25, 24)
#define SUN4I_TCON0_CTL_IF_8080 (1 << 24)
#define SUN4I_TCON0_CTL_CLK_DELAY_MASK GENMASK(8, 4)
#define SUN4I_TCON0_CTL_CLK_DELAY(delay) \
	((delay << 4) & SUN4I_TCON0_CTL_CLK_DELAY_MASK)
#define SUN4I_TCON0_CTL_SRC_SEL_MASK GENMASK(2, 0)

#define SUN4I_TCON0_DCLK_REG 0x44
#define SUN4I_TCON0_DCLK_GATE_MASK GENMASK(31, 28)
#define SUN4I_TCON0_DCLK_GATE_ENABLE BIT(31)
#define SUN4I_TCON0_DCLK_DIV_MASK GENMASK(6, 0)

#define SUN4I_TCON0_BASIC0_REG 0x48
#define SUN4I_TCON0_BASIC0_X(width) ((((width) - 1) & 0xfff) << 16)
#define SUN4I_TCON0_BASIC0_Y(height) (((height) - 1) & 0xfff)

#define SUN4I_TCON0_BASIC1_REG 0x4c
#define SUN4I_TCON0_BASIC1_H_TOTAL(total) ((((total) - 1) & 0x1fff) << 16)
#define SUN4I_TCON0_BASIC1_H_BACKPORCH(bp) (((bp) - 1) & 0xfff)

#define SUN4I_TCON0_BASIC2_REG 0x50
#define SUN4I_TCON0_BASIC2_V_TOTAL(total) (((total) & 0x1fff) << 16)
#define SUN4I_TCON0_BASIC2_V_BACKPORCH(bp) (((bp) - 1) & 0xfff)

#define SUN4I_TCON0_BASIC3_REG 0x54
#define SUN4I_TCON0_BASIC3_H_SYNC(width) ((((width) - 1) & 0x7ff) << 16)
#define SUN4I_TCON0_BASIC3_V_SYNC(height) (((height) - 1) & 0x7ff)

#define SUN4I_TCON0_HV_IF_REG 0x58

#define SUN4I_TCON0_CPU_IF_REG 0x60
#define SUN4I_TCON0_CPU_IF_MODE_MASK GENMASK(31, 28)
#define SUN4I_TCON0_CPU_IF_MODE_DSI (1 << 28)
#define SUN4I_TCON0_CPU_IF_TRI_FIFO_FLUSH BIT(16)
#define SUN4I_TCON0_CPU_IF_TRI_FIFO_EN BIT(2)
#define SUN4I_TCON0_CPU_IF_TRI_EN BIT(0)

#define SUN4I_TCON0_CPU_WR_REG 0x64
#define SUN4I_TCON0_CPU_RD0_REG 0x68
#define SUN4I_TCON0_CPU_RDA_REG 0x6c
#define SUN4I_TCON0_TTL0_REG 0x70
#define SUN4I_TCON0_TTL1_REG 0x74
#define SUN4I_TCON0_TTL2_REG 0x78
#define SUN4I_TCON0_TTL3_REG 0x7c
#define SUN4I_TCON0_TTL4_REG 0x80

#define SUN4I_TCON0_LVDS_IF_REG 0x84
#define SUN4I_TCON0_LVDS_IF_EN BIT(31)
#define SUN4I_TCON0_LVDS_IF_BITWIDTH_MASK BIT(26)
#define SUN4I_TCON0_LVDS_IF_BITWIDTH_18BITS (1 << 26)
#define SUN4I_TCON0_LVDS_IF_BITWIDTH_24BITS (0 << 26)
#define SUN4I_TCON0_LVDS_IF_CLK_SEL_MASK BIT(20)
#define SUN4I_TCON0_LVDS_IF_CLK_SEL_TCON0 (1 << 20)
#define SUN4I_TCON0_LVDS_IF_CLK_POL_MASK BIT(4)
#define SUN4I_TCON0_LVDS_IF_CLK_POL_NORMAL (1 << 4)
#define SUN4I_TCON0_LVDS_IF_CLK_POL_INV (0 << 4)
#define SUN4I_TCON0_LVDS_IF_DATA_POL_MASK GENMASK(3, 0)
#define SUN4I_TCON0_LVDS_IF_DATA_POL_NORMAL (0xf)
#define SUN4I_TCON0_LVDS_IF_DATA_POL_INV (0)

#define SUN4I_TCON0_IO_POL_REG 0x88
#define SUN4I_TCON0_IO_POL_DCLK_PHASE(phase) ((phase & 3) << 28)
#define SUN4I_TCON0_IO_POL_DE_NEGATIVE BIT(27)
#define SUN4I_TCON0_IO_POL_DCLK_DRIVE_NEGEDGE BIT(26)
#define SUN4I_TCON0_IO_POL_HSYNC_POSITIVE BIT(25)
#define SUN4I_TCON0_IO_POL_VSYNC_POSITIVE BIT(24)

#define SUN4I_TCON0_IO_TRI_REG 0x8c
#define SUN4I_TCON0_IO_TRI_HSYNC_DISABLE BIT(25)
#define SUN4I_TCON0_IO_TRI_VSYNC_DISABLE BIT(24)
#define SUN4I_TCON0_IO_TRI_DATA_PINS_DISABLE(pins) GENMASK(pins, 0)

#define SUN4I_TCON1_CTL_REG 0x90
#define SUN4I_TCON1_CTL_TCON_ENABLE BIT(31)
#define SUN4I_TCON1_CTL_INTERLACE_ENABLE BIT(20)
#define SUN4I_TCON1_CTL_CLK_DELAY_MASK GENMASK(8, 4)
#define SUN4I_TCON1_CTL_CLK_DELAY(delay) \
	((delay << 4) & SUN4I_TCON1_CTL_CLK_DELAY_MASK)
#define SUN4I_TCON1_CTL_SRC_SEL_MASK GENMASK(1, 0)

#define SUN4I_TCON1_BASIC0_REG 0x94
#define SUN4I_TCON1_BASIC0_X(width) ((((width) - 1) & 0xfff) << 16)
#define SUN4I_TCON1_BASIC0_Y(height) (((height) - 1) & 0xfff)

#define SUN4I_TCON1_BASIC1_REG 0x98
#define SUN4I_TCON1_BASIC1_X(width) ((((width) - 1) & 0xfff) << 16)
#define SUN4I_TCON1_BASIC1_Y(height) (((height) - 1) & 0xfff)

#define SUN4I_TCON1_BASIC2_REG 0x9c
#define SUN4I_TCON1_BASIC2_X(width) ((((width) - 1) & 0xfff) << 16)
#define SUN4I_TCON1_BASIC2_Y(height) (((height) - 1) & 0xfff)

#define SUN4I_TCON1_BASIC3_REG 0xa0
#define SUN4I_TCON1_BASIC3_H_TOTAL(total) ((((total) - 1) & 0x1fff) << 16)
#define SUN4I_TCON1_BASIC3_H_BACKPORCH(bp) (((bp) - 1) & 0xfff)

#define SUN4I_TCON1_BASIC4_REG 0xa4
#define SUN4I_TCON1_BASIC4_V_TOTAL(total) (((total) & 0x1fff) << 16)
#define SUN4I_TCON1_BASIC4_V_BACKPORCH(bp) (((bp) - 1) & 0xfff)

#define SUN4I_TCON1_BASIC5_REG 0xa8
#define SUN4I_TCON1_BASIC5_H_SYNC(width) ((((width) - 1) & 0x3ff) << 16)
#define SUN4I_TCON1_BASIC5_V_SYNC(height) (((height) - 1) & 0x3ff)

#define SUN4I_TCON1_IO_POL_REG 0xf0
/* there is no documentation about this bit */
#define SUN4I_TCON1_IO_POL_UNKNOWN BIT(26)
#define SUN4I_TCON1_IO_POL_HSYNC_POSITIVE BIT(25)
#define SUN4I_TCON1_IO_POL_VSYNC_POSITIVE BIT(24)

#define SUN4I_TCON1_IO_TRI_REG 0xf4

#define SUN4I_TCON_ECC_FIFO_REG 0xf8
#define SUN4I_TCON_ECC_FIFO_EN BIT(3)

#define SUN4I_TCON_CEU_CTL_REG 0x100
#define SUN4I_TCON_CEU_MUL_RR_REG 0x110
#define SUN4I_TCON_CEU_MUL_RG_REG 0x114
#define SUN4I_TCON_CEU_MUL_RB_REG 0x118
#define SUN4I_TCON_CEU_ADD_RC_REG 0x11c
#define SUN4I_TCON_CEU_MUL_GR_REG 0x120
#define SUN4I_TCON_CEU_MUL_GG_REG 0x124
#define SUN4I_TCON_CEU_MUL_GB_REG 0x128
#define SUN4I_TCON_CEU_ADD_GC_REG 0x12c
#define SUN4I_TCON_CEU_MUL_BR_REG 0x130
#define SUN4I_TCON_CEU_MUL_BG_REG 0x134
#define SUN4I_TCON_CEU_MUL_BB_REG 0x138
#define SUN4I_TCON_CEU_ADD_BC_REG 0x13c
#define SUN4I_TCON_CEU_RANGE_R_REG 0x140
#define SUN4I_TCON_CEU_RANGE_G_REG 0x144
#define SUN4I_TCON_CEU_RANGE_B_REG 0x148

#define SUN4I_TCON0_CPU_TRI0_REG 0x160
#define SUN4I_TCON0_CPU_TRI0_BLOCK_SPACE(space) ((((space) - 1) & 0xfff) << 16)
#define SUN4I_TCON0_CPU_TRI0_BLOCK_SIZE(size) (((size) - 1) & 0xfff)

#define SUN4I_TCON0_CPU_TRI1_REG 0x164
#define SUN4I_TCON0_CPU_TRI1_BLOCK_NUM(num) (((num) - 1) & 0xffff)

#define SUN4I_TCON0_CPU_TRI2_REG 0x168
#define SUN4I_TCON0_CPU_TRI2_START_DELAY(delay) (((delay) & 0xffff) << 16)
#define SUN4I_TCON0_CPU_TRI2_TRANS_START_SET(set) ((set) & 0xfff)

#define SUN4I_TCON_SAFE_PERIOD_REG 0x1f0
#define SUN4I_TCON_SAFE_PERIOD_NUM(num) (((num) & 0xfff) << 16)
#define SUN4I_TCON_SAFE_PERIOD_MODE(mode) ((mode) & 0x3)

#define SUN4I_TCON_MUX_CTRL_REG 0x200

#define SUN4I_TCON0_LVDS_ANA0_REG 0x220
#define SUN4I_TCON0_LVDS_ANA0_DCHS BIT(16)
#define SUN4I_TCON0_LVDS_ANA0_PD (BIT(20) | BIT(21))
#define SUN4I_TCON0_LVDS_ANA0_EN_MB BIT(22)
#define SUN4I_TCON0_LVDS_ANA0_REG_C (BIT(24) | BIT(25))
#define SUN4I_TCON0_LVDS_ANA0_REG_V (BIT(26) | BIT(27))
#define SUN4I_TCON0_LVDS_ANA0_CK_EN (BIT(29) | BIT(28))

#define SUN6I_TCON0_LVDS_ANA0_EN_MB BIT(31)
#define SUN6I_TCON0_LVDS_ANA0_EN_LDO BIT(30)
#define SUN6I_TCON0_LVDS_ANA0_EN_DRVC BIT(24)
#define SUN6I_TCON0_LVDS_ANA0_EN_DRVD(x) (((x) & 0xf) << 20)
#define SUN6I_TCON0_LVDS_ANA0_C(x) (((x) & 3) << 17)
#define SUN6I_TCON0_LVDS_ANA0_V(x) (((x) & 3) << 8)
#define SUN6I_TCON0_LVDS_ANA0_PD(x) (((x) & 3) << 4)

#define SUN4I_TCON0_LVDS_ANA1_REG 0x224
#define SUN4I_TCON0_LVDS_ANA1_INIT (0x1f << 26 | 0x1f << 10)
#define SUN4I_TCON0_LVDS_ANA1_UPDATE (0x1f << 16 | 0x1f << 00)

#define SUN4I_TCON1_FILL_CTL_REG 0x300
#define SUN4I_TCON1_FILL_BEG0_REG 0x304
#define SUN4I_TCON1_FILL_END0_REG 0x308
#define SUN4I_TCON1_FILL_DATA0_REG 0x30c
#define SUN4I_TCON1_FILL_BEG1_REG 0x310
#define SUN4I_TCON1_FILL_END1_REG 0x314
#define SUN4I_TCON1_FILL_DATA1_REG 0x318
#define SUN4I_TCON1_FILL_BEG2_REG 0x31c
#define SUN4I_TCON1_FILL_END2_REG 0x320
#define SUN4I_TCON1_FILL_DATA2_REG 0x324
#define SUN4I_TCON1_GAMMA_TABLE_REG 0x400

#define SUN4I_TCON_MAX_CHANNELS 2
#define SUN6I_DSI_TCON_DIV 4

#define VTOTAL(mode)                                   \
	((mode)->vactive.typ + (mode)->vsync_len.typ + \
	 (mode)->vback_porch.typ + (mode)->vfront_porch.typ)
#define HTOTAL(mode)                                   \
	((mode)->hactive.typ + (mode)->hsync_len.typ + \
	 (mode)->hback_porch.typ + (mode)->hfront_porch.typ)

struct sunxi_tcon {
	struct regmap *regs;

	struct reset_ctl *lcd_rst;
	struct clk *clk;
	struct clk *sclk0;
	struct clk *sclk1;

	struct clk *dclk;

	struct mipi_dsi_device dsi_device;
	struct udevice *dsi_host;
	struct udevice *panel;

	const struct sunxi_tcon_quirks *quirks;
};

struct sunxi_tcon_quirks {
	bool has_channel_0; /* a83t does not have channel 0 on second TCON */
	bool has_channel_1; /* a33 does not have channel 1 */
	bool has_lvds_alt; /* Does the LVDS clock have a parent other than the TCON clock? */
	bool needs_de_be_mux; /* sun6i needs mux to select backend */
	bool needs_edp_reset; /* a80 edp reset needed for tcon0 access */
	bool supports_lvds; /* Does the TCON support an LVDS output? */
	bool dsi_passive; /* DSI passive */
	bool polarity_in_ch0; /* some tcon1 channels have polarity bits in tcon0 pol register */
	u8 dclk_min_div; /* minimum divider for TCON0 DCLK */

	/* callback to handle tcon muxing options */
	int (*set_mux)(struct sunxi_tcon *, void *);
	/* handler for LVDS setup routine */
	void (*setup_lvds_phy)(struct sunxi_tcon *tcon, const void *);
};

static void sunxi_tcon_channel_set_status(struct sunxi_tcon *tcon, int channel,
					  bool enabled)
{
	struct clk *clk;

	switch (channel) {
	case 0:
		regmap_update_bits(tcon->regs, SUN4I_TCON0_CTL_REG,
				   SUN4I_TCON0_CTL_TCON_ENABLE,
				   enabled ? SUN4I_TCON0_CTL_TCON_ENABLE : 0);
		clk = tcon->sclk0;
		break;
	case 1:
		regmap_update_bits(tcon->regs, SUN4I_TCON1_CTL_REG,
				   SUN4I_TCON1_CTL_TCON_ENABLE,
				   enabled ? SUN4I_TCON1_CTL_TCON_ENABLE : 0);
		clk = tcon->sclk1;
		break;
	default:
		return;
	}

	if (enabled) {
		clk_prepare_enable(clk);
	} else {
		clk_disable_unprepare(clk);
	}

	regmap_update_bits(tcon->regs, SUN4I_TCON_GCTL_REG,
			   SUN4I_TCON_GCTL_TCON_ENABLE,
			   enabled ? SUN4I_TCON_GCTL_TCON_ENABLE : 0);
}

static int sunxi_tcon_get_clk_delay(const struct display_timing *mode,
				    int channel)
{
	int delay = mode->vfront_porch.typ + mode->vsync_len.typ +
		    mode->vback_porch.typ;

	if (mode->flags & DISPLAY_FLAGS_INTERLACED)
		delay /= 2;

	if (channel == 1)
		delay -= 2;

	delay = min(delay, 30);

	return delay;
}

static void sunxi_tcon_set_dclk_rate(struct sunxi_tcon *tcon, ulong target,
				     int min_d, int max_d)
{
	ulong best_delta = LONG_MAX;
	ulong best_d = min_d;
	struct clk *clksrc = tcon->sclk0;

	for (int d = min_d; d < max_d; d++) {
		ulong rate = clk_round_rate(clksrc, target * d);
		if (rate <= 0)
			break;
		ulong delta = abs(rate - target * d);
		if (delta < best_delta) {
			best_delta = delta;
			best_d = d;
			if (best_delta < 10)
				break;
		}
	}

	ulong rate = target * best_d;

	if (!rate)
		return;

	clk_set_rate(clksrc, rate);

	regmap_update_bits(tcon->regs, SUN4I_TCON0_DCLK_REG,
			   SUN4I_TCON0_DCLK_DIV_MASK, best_d);
	regmap_update_bits(tcon->regs, SUN4I_TCON0_DCLK_REG,
			   SUN4I_TCON0_DCLK_GATE_MASK,
			   SUN4I_TCON0_DCLK_GATE_ENABLE);
}

#ifdef CONFIG_VIDEO_MIPI_DSI
static void sunxi_tcon0_mode_set_dsi(struct sunxi_tcon *tcon,
				     const struct display_timing *mode,
				     struct mipi_dsi_device *dsi)
{
	u8 bpp = mipi_dsi_pixel_format_to_bpp(dsi->format);
	u8 lanes = dsi->lanes;
	u32 block_space, start_delay;
	u32 tcon_div;

	/*
	 * dclk is required to run at 1/4 the DSI per-lane bit rate.
	 */
	sunxi_tcon_set_dclk_rate(
		tcon, mode->pixelclock.typ * (bpp / lanes) / SUN6I_DSI_TCON_DIV,
		SUN6I_DSI_TCON_DIV, SUN6I_DSI_TCON_DIV);

	/* Set the resolution */
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC0_REG,
		     SUN4I_TCON0_BASIC0_X(mode->hactive.typ) |
			     SUN4I_TCON0_BASIC0_Y(mode->vactive.typ));

	/* Set dithering if needed */
	// sunxi_tcon0_mode_set_dithering(tcon, sunxi_tcon_get_connector(encoder));

	regmap_update_bits(tcon->regs, SUN4I_TCON0_CTL_REG,
			   SUN4I_TCON0_CTL_IF_MASK, SUN4I_TCON0_CTL_IF_8080);

	regmap_write(tcon->regs, SUN4I_TCON_ECC_FIFO_REG,
		     SUN4I_TCON_ECC_FIFO_EN);

	regmap_write(tcon->regs, SUN4I_TCON0_CPU_IF_REG,
		     SUN4I_TCON0_CPU_IF_MODE_DSI |
			     SUN4I_TCON0_CPU_IF_TRI_FIFO_FLUSH |
			     SUN4I_TCON0_CPU_IF_TRI_FIFO_EN |
			     SUN4I_TCON0_CPU_IF_TRI_EN);

	/*
	 * This looks suspicious, but it works...
	 *
	 * The datasheet says that this should be set higher than 20 *
	 * pixel cycle, but it's not clear what a pixel cycle is.
	 */
	regmap_read(tcon->regs, SUN4I_TCON0_DCLK_REG, &tcon_div);
	tcon_div &= SUN4I_TCON0_DCLK_DIV_MASK;
	block_space = HTOTAL(mode) * bpp /
		      (tcon_div * (tcon->quirks->dsi_passive ? 4 : lanes));
	block_space -= mode->hactive.typ + 40;

	regmap_write(
		tcon->regs, SUN4I_TCON0_CPU_TRI0_REG,
		SUN4I_TCON0_CPU_TRI0_BLOCK_SPACE(block_space) |
			SUN4I_TCON0_CPU_TRI0_BLOCK_SIZE(mode->hactive.typ));

	regmap_write(tcon->regs, SUN4I_TCON0_CPU_TRI1_REG,
		     SUN4I_TCON0_CPU_TRI1_BLOCK_NUM(mode->vactive.typ));

	start_delay = (VTOTAL(mode) - mode->vactive.typ - 8 - 1);
	start_delay = start_delay * HTOTAL(mode);
	start_delay = start_delay / (mode->pixelclock.typ / 1000000) / 8;
	regmap_write(tcon->regs, SUN4I_TCON0_CPU_TRI2_REG,
		     SUN4I_TCON0_CPU_TRI2_TRANS_START_SET(10) |
			     SUN4I_TCON0_CPU_TRI2_START_DELAY(start_delay));

	regmap_write(tcon->regs, SUN4I_TCON_SAFE_PERIOD_REG,
		     SUN4I_TCON_SAFE_PERIOD_NUM(
			     (mode->pixelclock.typ / 1000000) * 15) |
			     SUN4I_TCON_SAFE_PERIOD_MODE(3));

	/* Enable the output on the pins */
	regmap_write(tcon->regs, SUN4I_TCON0_IO_TRI_REG, 0xe0000000);
}
#endif

static void sunxi_tcon0_mode_set_rgb(struct sunxi_tcon *tcon,
				     const struct display_timing *mode)
{
	unsigned int bp, hsync, vsync;
	u8 clk_delay;
	u32 val = 0;

	sunxi_tcon_set_dclk_rate(tcon, mode->pixelclock.typ,
				 tcon->quirks->dclk_min_div, 127);

	/* Set the resolution */
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC0_REG,
		     SUN4I_TCON0_BASIC0_X(mode->hactive.typ) |
			     SUN4I_TCON0_BASIC0_Y(mode->vactive.typ));

	/* Set dithering if needed */
	// sunxi_tcon0_mode_set_dithering(tcon, connector);

	/* Adjust clock delay */
	clk_delay = sunxi_tcon_get_clk_delay(mode, 0);
	regmap_update_bits(tcon->regs, SUN4I_TCON0_CTL_REG,
			   SUN4I_TCON0_CTL_CLK_DELAY_MASK,
			   SUN4I_TCON0_CTL_CLK_DELAY(clk_delay));

	/*
	 * This is called a backporch in the register documentation,
	 * but it really is the back porch + hsync
	 */
	bp = mode->hback_porch.typ + mode->hsync_len.typ;

	/* Set horizontal display timings */
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC1_REG,
		     SUN4I_TCON0_BASIC1_H_TOTAL(HTOTAL(mode)) |
			     SUN4I_TCON0_BASIC1_H_BACKPORCH(bp));

	/*
	 * This is called a backporch in the register documentation,
	 * but it really is the back porch + hsync
	 */
	bp = mode->vback_porch.typ + mode->vsync_len.typ;

	/* Set vertical display timings */
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC2_REG,
		     SUN4I_TCON0_BASIC2_V_TOTAL(VTOTAL(mode) * 2) |
			     SUN4I_TCON0_BASIC2_V_BACKPORCH(bp));

	/* Set Hsync and Vsync length */
	hsync = mode->hsync_len.typ;
	vsync = mode->vsync_len.typ;
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC3_REG,
		     SUN4I_TCON0_BASIC3_V_SYNC(vsync) |
			     SUN4I_TCON0_BASIC3_H_SYNC(hsync));

	/* Setup the polarity of the various signals */
	if (mode->flags & DISPLAY_FLAGS_HSYNC_HIGH)
		val |= SUN4I_TCON0_IO_POL_HSYNC_POSITIVE;

	if (mode->flags & DISPLAY_FLAGS_VSYNC_HIGH)
		val |= SUN4I_TCON0_IO_POL_VSYNC_POSITIVE;

	if (mode->flags & DISPLAY_FLAGS_DE_LOW)
		val |= SUN4I_TCON0_IO_POL_DE_NEGATIVE;

	if (mode->flags & DISPLAY_FLAGS_PIXDATA_NEGEDGE)
		val |= SUN4I_TCON0_IO_POL_DCLK_DRIVE_NEGEDGE;

	regmap_update_bits(tcon->regs, SUN4I_TCON0_IO_POL_REG,
			   SUN4I_TCON0_IO_POL_HSYNC_POSITIVE |
				   SUN4I_TCON0_IO_POL_VSYNC_POSITIVE |
				   SUN4I_TCON0_IO_POL_DCLK_DRIVE_NEGEDGE |
				   SUN4I_TCON0_IO_POL_DE_NEGATIVE,
			   val);

	/* Map output pins to channel 0 */
	regmap_update_bits(tcon->regs, SUN4I_TCON_GCTL_REG,
			   SUN4I_TCON_GCTL_IOMAP_MASK,
			   SUN4I_TCON_GCTL_IOMAP_TCON0);

	/* Enable the output on the pins */
	regmap_write(tcon->regs, SUN4I_TCON0_IO_TRI_REG, 0);
}

static int sunxi_tcon_init_clocks(struct udevice *dev)
{
	struct sunxi_tcon *tcon = dev_get_priv(dev);

	tcon->clk = devm_clk_get(dev, "ahb");
	if (IS_ERR(tcon->clk)) {
		dev_err(dev, "Couldn't get the TCON bus clock\n");
		return PTR_ERR(tcon->clk);
	}
	clk_prepare_enable(tcon->clk);

	if (tcon->quirks->has_channel_0) {
		tcon->sclk0 = devm_clk_get(dev, "tcon-ch0");
		if (IS_ERR(tcon->sclk0)) {
			dev_err(dev, "Couldn't get the TCON channel 0 clock\n");
			return PTR_ERR(tcon->sclk0);
		}
		clk_prepare_enable(tcon->sclk0);
	}

	if (tcon->quirks->has_channel_1) {
		tcon->sclk1 = devm_clk_get(dev, "tcon-ch1");
		if (IS_ERR(tcon->sclk1)) {
			dev_err(dev, "Couldn't get the TCON channel 1 clock\n");
			return PTR_ERR(tcon->sclk1);
		}
	}

	return 0;
}

static int sunxi_tcon_init_regmap(struct udevice *dev, struct sunxi_tcon *tcon)
{
	tcon->regs = devm_regmap_init(dev, NULL, NULL, NULL);
	if (IS_ERR(tcon->regs)) {
		dev_err(dev, "Couldn't create the TCON regmap: %ld\n",
			PTR_ERR(tcon->regs));
		return PTR_ERR(tcon->regs);
	}

	/* Make sure the TCON is disabled and all IRQs are off */
	regmap_write(tcon->regs, SUN4I_TCON_GCTL_REG, 0);
	regmap_write(tcon->regs, SUN4I_TCON_GINT0_REG, 0);
	regmap_write(tcon->regs, SUN4I_TCON_GINT1_REG, 0);

	/* Disable IO lines and set them to tristate */
	regmap_write(tcon->regs, SUN4I_TCON0_IO_TRI_REG, ~0);
	regmap_write(tcon->regs, SUN4I_TCON1_IO_TRI_REG, ~0);

	return 0;
}

#ifdef CONFIG_VIDEO_MIPI_DSI
static const struct mipi_dsi_phy_ops sunxi_tcon_dsi_phy_ops = {};
#endif

static struct udevice *sunxi_tcon_get_device(void)
{
	struct udevice *dev;
	int err;

	err = uclass_get_device_by_driver(UCLASS_NOP, DM_DRIVER_GET(sunxi_tcon),
					  &dev);
	if (err) {
		log_err("Unable to get TCON driver\n");
		return NULL;
	}

	return dev;
}

int sunxi_tcon_get_display_timing(struct display_timing *timing)
{
	struct udevice *dev = sunxi_tcon_get_device();
	struct sunxi_tcon *tcon = dev_get_priv(dev);

	if (!tcon)
		return -EINVAL;

	return panel_get_display_timing(tcon->panel, timing);
}

int sunxi_tcon_apply_timing(const struct display_timing *timing)
{
	struct udevice *dev = sunxi_tcon_get_device();
	struct sunxi_tcon *tcon = dev_get_priv(dev);
	struct udevice *video;
	int err;

#ifdef CONFIG_VIDEO_MIPI_DSI
	if (tcon->dsi_host) {
		err = dsi_host_init(tcon->dsi_host, &tcon->dsi_device,
				    (struct display_timing *)timing, 4,
				    &sunxi_tcon_dsi_phy_ops);
		if (err) {
			dev_err(dev, "Failed to initialize the dsi host\n");
			return err;
		}
		sunxi_tcon0_mode_set_dsi(tcon, timing, &tcon->dsi_device);
	} else
#endif
	{
		sunxi_tcon0_mode_set_rgb(tcon, timing);
	}

	err = panel_enable(tcon->panel);
	if (err) {
		dev_err(dev, "panel %s enable error %d\n", tcon->panel->name,
			err);
		return err;
	}

#if defined(CONFIG_VIDEO_SUNXI_TCON_USE_CH0)
	if (tcon->quirks->has_channel_0) {
		sunxi_tcon_channel_set_status(tcon, 0, true);
	}
#elif defined(CONFIG_VIDEO_SUNXI_TCON_USE_CH1)
	if (tcon->quirks->has_channel_1) {
		sunxi_tcon_channel_set_status(tcon, 1, true);
	}
#else
#error "Define either CONFIG_VIDEO_SUNXI_TCON_USE_CH0 or CONFIG_VIDEO_SUNXI_TCON_USE_CH1"
#endif

#ifdef CONFIG_VIDEO_MIPI_DSI
	if (tcon->dsi_host) {
		err = dsi_host_enable(tcon->dsi_host);
		if (err) {
			dev_err(dev, "failed to enable mipi dsi host\n");
			return err;
		}
	}
#endif

	/* Set the size on video driver */
	err = uclass_get_device(UCLASS_VIDEO, 0, &video);
	if (!err) {
		video_set_size(video, timing->hactive.typ, timing->vactive.typ,
			       panel_get_rotation(tcon->panel));
	}

	return 0;
}

static int sunxi_tcon_probe(struct udevice *dev)
{
	struct udevice *parent;
	struct sunxi_tcon *tcon = dev_get_priv(dev);
	int err;

	tcon->quirks =
		(const struct sunxi_tcon_quirks *)dev_get_driver_data(dev);

	err = sunxi_tcon_init_clocks(dev);
	if (err)
		return err;

	err = sunxi_tcon_init_regmap(dev, tcon);
	if (err)
		return err;

	tcon->lcd_rst = devm_reset_control_get(dev, "lcd");
	if (IS_ERR(tcon->lcd_rst)) {
		dev_err(dev, "Couldn't get our reset line: %ld\n",
			PTR_ERR(tcon->lcd_rst));
		return PTR_ERR(tcon->lcd_rst);
	}

	err = reset_deassert(tcon->lcd_rst);
	if (err) {
		dev_err(dev, "Couldn't deassert our reset line\n");
		return err;
	}

	// TODO: reset lines
	err = uclass_get_device(UCLASS_PANEL, 0, &tcon->panel);
	if (err) {
		dev_err(dev, "No panel is detected %d\n", err);
		return err;
	}

	parent = dev_get_parent(tcon->panel);
	if (parent && device_get_uclass_id(parent) == UCLASS_DSI_HOST) {
		struct mipi_dsi_panel_plat *dsi_panel;

		dsi_panel = dev_get_plat(tcon->panel);
		dsi_panel->device = &tcon->dsi_device;
		dsi_panel->device->lanes = dsi_panel->lanes;
		dsi_panel->device->format = dsi_panel->format;
		dsi_panel->device->mode_flags = dsi_panel->mode_flags;

		tcon->dsi_host = parent;

		dev_info(dev, "DSI mode\n");
	} else {
		dev_info(dev, "RGB mode\n");
	}

	return 0;
}

#if 0
static const struct sunxi_tcon_quirks sun4i_a10_quirks = {
	.has_channel_0		= true,
	.has_channel_1		= true,
	.dclk_min_div		= 4,
	.set_mux		= sun4i_a10_tcon_set_mux,
};

static const struct sunxi_tcon_quirks sun5i_a13_quirks = {
	.has_channel_0		= true,
	.has_channel_1		= true,
	.dclk_min_div		= 4,
	.set_mux		= sun5i_a13_tcon_set_mux,
};

static const struct sunxi_tcon_quirks sun6i_a31_quirks = {
	.has_channel_0		= true,
	.has_channel_1		= true,
	.has_lvds_alt		= true,
	.needs_de_be_mux	= true,
	.dclk_min_div		= 1,
	.set_mux		= sun6i_tcon_set_mux,
};

static const struct sunxi_tcon_quirks sun6i_a31s_quirks = {
	.has_channel_0		= true,
	.has_channel_1		= true,
	.needs_de_be_mux	= true,
	.dclk_min_div		= 1,
};

static const struct sunxi_tcon_quirks sun7i_a20_tcon0_quirks = {
	.supports_lvds		= true,
	.has_channel_0		= true,
	.has_channel_1		= true,
	.dclk_min_div		= 4,
	/* Same display pipeline structure as A10 */
	.set_mux		= sun4i_a10_tcon_set_mux,
	.setup_lvds_phy		= sun4i_tcon_setup_lvds_phy,
};

static const struct sunxi_tcon_quirks sun7i_a20_quirks = {
	.has_channel_0		= true,
	.has_channel_1		= true,
	.dclk_min_div		= 4,
	/* Same display pipeline structure as A10 */
	.set_mux		= sun4i_a10_tcon_set_mux,
};

static const struct sunxi_tcon_quirks sun8i_a33_quirks = {
	.has_channel_0		= true,
	.has_lvds_alt		= true,
	.dclk_min_div		= 1,
	.setup_lvds_phy		= sun6i_tcon_setup_lvds_phy,
	.supports_lvds		= true,
};

static const struct sunxi_tcon_quirks sun8i_a83t_lcd_quirks = {
	.supports_lvds		= true,
	.has_channel_0		= true,
	.dclk_min_div		= 1,
	.setup_lvds_phy		= sun6i_tcon_setup_lvds_phy,
};

static const struct sunxi_tcon_quirks sun8i_a83t_tv_quirks = {
	.has_channel_1		= true,
};

static const struct sunxi_tcon_quirks sun8i_r40_tv_quirks = {
	.has_channel_1		= true,
	.polarity_in_ch0	= true,
	.set_mux		= sun8i_r40_tcon_tv_set_mux,
};

static const struct sunxi_tcon_quirks sun8i_v3s_quirks = {
	.has_channel_0		= true,
	.dclk_min_div		= 1,
};

static const struct sunxi_tcon_quirks sun9i_a80_tcon_lcd_quirks = {
	.has_channel_0		= true,
	.needs_edp_reset	= true,
	.dclk_min_div		= 1,
};

static const struct sunxi_tcon_quirks sun9i_a80_tcon_tv_quirks = {
	.has_channel_1	= true,
	.needs_edp_reset = true,
};

static const struct sunxi_tcon_quirks sun20i_d1_lcd_quirks = {
	.has_channel_0		= true,
	.dclk_min_div		= 1,
	.set_mux		= sun8i_r40_tcon_tv_set_mux,
};
#endif

static const struct sunxi_tcon_quirks sun8i_r40_lcd_quirks = {
	.has_channel_0 = true,
	.dclk_min_div = 1,
};

static const struct sunxi_tcon_quirks sun50i_a100_lcd_quirks = {
	.has_channel_0 = true,
	.dclk_min_div = 6,
	.dsi_passive = true,
};

static const struct udevice_id sunxi_tcon_of_table[] = {
	/*
	{ .compatible = "allwinner,sun4i-a10-tcon", .data = (ulong)&sun4i_a10_quirks },
	{ .compatible = "allwinner,sun5i-a13-tcon", .data = (ulong)&sun5i_a13_quirks },
	{ .compatible = "allwinner,sun6i-a31-tcon", .data = (ulong)&sun6i_a31_quirks },
	{ .compatible = "allwinner,sun6i-a31s-tcon", .data = (ulong)&sun6i_a31s_quirks },
	{ .compatible = "allwinner,sun7i-a20-tcon", .data = (ulong)&sun7i_a20_quirks },
	{ .compatible = "allwinner,sun7i-a20-tcon0", .data = (ulong)&sun7i_a20_tcon0_quirks },
	{ .compatible = "allwinner,sun7i-a20-tcon1", .data = (ulong)&sun7i_a20_quirks },
	{ .compatible = "allwinner,sun8i-a23-tcon", .data = (ulong)&sun8i_a33_quirks },
	{ .compatible = "allwinner,sun8i-a33-tcon", .data = (ulong)&sun8i_a33_quirks },
	{ .compatible = "allwinner,sun8i-a83t-tcon-lcd", .data = (ulong)&sun8i_a83t_lcd_quirks },
	{ .compatible = "allwinner,sun8i-a83t-tcon-tv", .data = (ulong)&sun8i_a83t_tv_quirks },
	{ .compatible = "allwinner,sun8i-r40-tcon-tv", .data = (ulong)&sun8i_r40_tv_quirks },
	{ .compatible = "allwinner,sun8i-v3s-tcon", .data = (ulong)&sun8i_v3s_quirks },
	{ .compatible = "allwinner,sun9i-a80-tcon-lcd", .data = (ulong)&sun9i_a80_tcon_lcd_quirks },
	{ .compatible = "allwinner,sun9i-a80-tcon-tv", .data = (ulong)&sun9i_a80_tcon_tv_quirks },
	{ .compatible = "allwinner,sun20i-d1-tcon-lcd", .data = (ulong)&sun20i_d1_lcd_quirks },
	{ .compatible = "allwinner,sun20i-d1-tcon-tv", .data = (ulong)&sun8i_r40_tv_quirks },
*/
	{ .compatible = "allwinner,sun8i-r40-tcon-lcd",
	  .data = (ulong)&sun8i_r40_lcd_quirks },
	{ .compatible = "allwinner,sun50i-a100-tcon-lcd",
	  .data = (ulong)&sun50i_a100_lcd_quirks },
	{}
};

U_BOOT_DRIVER(sunxi_tcon) = {
	.name = "sunxi_tcon",
	.of_match = sunxi_tcon_of_table,
	.id = UCLASS_NOP,
	.probe = sunxi_tcon_probe,
	.priv_auto = sizeof(struct sunxi_tcon),
};
