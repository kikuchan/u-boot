// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (c) 2016 Allwinnertech Co., Ltd.
 * Copyright (C) 2017-2018 Bootlin
 *
 * Maxime Ripard <maxime.ripard@bootlin.com>
 */

#include <linux/bitops.h>
#include <linux/bitrev.h>
#include <linux/bug.h>
#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/time.h>

#include <asm/io.h>
#include <clk.h>
#include <dm/device_compat.h>
#include <dm/device-internal.h>
#include <dm.h>
#include <dm/lists.h>
#include <dsi_host.h>
#include <errno.h>
#include <generic-phy.h>
#include <phy-mipi-dphy.h>
#include <regmap.h>
#include <reset.h>
#include <u-boot/crc.h>
#include <video_bridge.h>
#include <video.h>

#define SUN6I_DSI_CTL_REG 0x000
#define SUN6I_DSI_CTL_EN BIT(0)

#define SUN6I_DSI_BASIC_CTL_REG 0x00c
#define SUN6I_DSI_BASIC_CTL_TRAIL_INV(n) (((n) & 0xf) << 4)
#define SUN6I_DSI_BASIC_CTL_TRAIL_FILL BIT(3)
#define SUN6I_DSI_BASIC_CTL_HBP_DIS BIT(2)
#define SUN6I_DSI_BASIC_CTL_HSA_HSE_DIS BIT(1)
#define SUN6I_DSI_BASIC_CTL_VIDEO_BURST BIT(0)

#define SUN6I_DSI_BASIC_CTL0_REG 0x010
#define SUN6I_DSI_BASIC_CTL0_HS_EOTP_EN BIT(18)
#define SUN6I_DSI_BASIC_CTL0_CRC_EN BIT(17)
#define SUN6I_DSI_BASIC_CTL0_ECC_EN BIT(16)
#define SUN6I_DSI_BASIC_CTL0_INST_ST BIT(0)

#define SUN6I_DSI_BASIC_CTL1_REG 0x014
#define SUN6I_DSI_BASIC_CTL1_VIDEO_ST_DELAY(n) (((n) & 0x1fff) << 4)
#define SUN6I_DSI_BASIC_CTL1_VIDEO_FILL BIT(2)
#define SUN6I_DSI_BASIC_CTL1_VIDEO_PRECISION BIT(1)
#define SUN6I_DSI_BASIC_CTL1_VIDEO_MODE BIT(0)

#define SUN6I_DSI_BASIC_SIZE0_REG 0x018
#define SUN6I_DSI_BASIC_SIZE0_VBP(n) (((n) & 0xfff) << 16)
#define SUN6I_DSI_BASIC_SIZE0_VSA(n) ((n) & 0xfff)

#define SUN6I_DSI_BASIC_SIZE1_REG 0x01c
#define SUN6I_DSI_BASIC_SIZE1_VT(n) (((n) & 0xfff) << 16)
#define SUN6I_DSI_BASIC_SIZE1_VACT(n) ((n) & 0xfff)

#define SUN6I_DSI_INST_FUNC_REG(n) (0x020 + (n) * 0x04)
#define SUN6I_DSI_INST_FUNC_INST_MODE(n) (((n) & 0xf) << 28)
#define SUN6I_DSI_INST_FUNC_ESCAPE_ENTRY(n) (((n) & 0xf) << 24)
#define SUN6I_DSI_INST_FUNC_TRANS_PACKET(n) (((n) & 0xf) << 20)
#define SUN6I_DSI_INST_FUNC_LANE_CEN BIT(4)
#define SUN6I_DSI_INST_FUNC_LANE_DEN(n) ((n) & 0xf)

#define SUN6I_DSI_INST_LOOP_SEL_REG 0x040

#define SUN6I_DSI_INST_LOOP_NUM_REG(n) (0x044 + (n) * 0x10)
#define SUN6I_DSI_INST_LOOP_NUM_N1(n) (((n) & 0xfff) << 16)
#define SUN6I_DSI_INST_LOOP_NUM_N0(n) ((n) & 0xfff)

#define SUN6I_DSI_INST_JUMP_SEL_REG 0x048

#define SUN6I_DSI_INST_JUMP_CFG_REG(n) (0x04c + (n) * 0x04)
#define SUN6I_DSI_INST_JUMP_CFG_TO(n) (((n) & 0xf) << 20)
#define SUN6I_DSI_INST_JUMP_CFG_POINT(n) (((n) & 0xf) << 16)
#define SUN6I_DSI_INST_JUMP_CFG_NUM(n) ((n) & 0xffff)

#define SUN6I_DSI_TRANS_START_REG 0x060

#define SUN6I_DSI_TRANS_ZERO_REG 0x078

#define SUN6I_DSI_TCON_DRQ_REG 0x07c
#define SUN6I_DSI_TCON_DRQ_ENABLE_MODE BIT(28)
#define SUN6I_DSI_TCON_DRQ_SET(n) ((n) & 0x3ff)

#define SUN6I_DSI_PIXEL_CTL0_REG 0x080
#define SUN6I_DSI_PIXEL_CTL0_PD_PLUG_DISABLE BIT(16)
#define SUN6I_DSI_PIXEL_CTL0_FORMAT(n) ((n) & 0xf)

#define SUN6I_DSI_PIXEL_CTL1_REG 0x084

#define SUN6I_DSI_PIXEL_PH_REG 0x090
#define SUN6I_DSI_PIXEL_PH_ECC(n) (((n) & 0xff) << 24)
#define SUN6I_DSI_PIXEL_PH_WC(n) (((n) & 0xffff) << 8)
#define SUN6I_DSI_PIXEL_PH_VC(n) (((n) & 3) << 6)
#define SUN6I_DSI_PIXEL_PH_DT(n) ((n) & 0x3f)

#define SUN6I_DSI_PIXEL_PF0_REG 0x098
#define SUN6I_DSI_PIXEL_PF0_CRC_FORCE(n) ((n) & 0xffff)

#define SUN6I_DSI_PIXEL_PF1_REG 0x09c
#define SUN6I_DSI_PIXEL_PF1_CRC_INIT_LINEN(n) (((n) & 0xffff) << 16)
#define SUN6I_DSI_PIXEL_PF1_CRC_INIT_LINE0(n) ((n) & 0xffff)

#define SUN6I_DSI_SYNC_HSS_REG 0x0b0

#define SUN6I_DSI_SYNC_HSE_REG 0x0b4

#define SUN6I_DSI_SYNC_VSS_REG 0x0b8

#define SUN6I_DSI_SYNC_VSE_REG 0x0bc

#define SUN6I_DSI_BLK_HSA0_REG 0x0c0

#define SUN6I_DSI_BLK_HSA1_REG 0x0c4
#define SUN6I_DSI_BLK_PF(n) (((n) & 0xffff) << 16)
#define SUN6I_DSI_BLK_PD(n) ((n) & 0xff)

#define SUN6I_DSI_BLK_HBP0_REG 0x0c8

#define SUN6I_DSI_BLK_HBP1_REG 0x0cc

#define SUN6I_DSI_BLK_HFP0_REG 0x0d0

#define SUN6I_DSI_BLK_HFP1_REG 0x0d4

#define SUN6I_DSI_BLK_HBLK0_REG 0x0e0

#define SUN6I_DSI_BLK_HBLK1_REG 0x0e4

#define SUN6I_DSI_BLK_VBLK0_REG 0x0e8

#define SUN6I_DSI_BLK_VBLK1_REG 0x0ec

#define SUN6I_DSI_BURST_LINE_REG 0x0f0
#define SUN6I_DSI_BURST_LINE_SYNC_POINT(n) (((n) & 0xffff) << 16)
#define SUN6I_DSI_BURST_LINE_NUM(n) ((n) & 0xffff)

#define SUN6I_DSI_BURST_DRQ_REG 0x0f4
#define SUN6I_DSI_BURST_DRQ_EDGE1(n) (((n) & 0xffff) << 16)
#define SUN6I_DSI_BURST_DRQ_EDGE0(n) ((n) & 0xffff)

#define SUN6I_DSI_CMD_CTL_REG 0x200
#define SUN6I_DSI_CMD_CTL_RX_OVERFLOW BIT(26)
#define SUN6I_DSI_CMD_CTL_RX_FLAG BIT(25)
#define SUN6I_DSI_CMD_CTL_TX_FLAG BIT(9)

#define SUN6I_DSI_CMD_RX_REG(n) (0x240 + (n) * 0x04)

#define SUN6I_DSI_DEBUG_DATA_REG 0x2f8

#define SUN6I_DSI_CMD_TX_REG(n) (0x300 + (n) * 0x04)

#define SUN6I_DSI_SYNC_POINT 40

#define SUN6I_DSI_MAX_LONG_MSG_SIZE (32 - 4 - 2)

#define SUN6I_DSI_TCON_DIV 4

#define MIPI_DSI_MODE_NO_EOT_PACKET MIPI_DSI_MODE_EOT_PACKET
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

struct sunxi_mipi_dsi_variant {
	bool has_mod_clk;
	bool set_mod_clk;
};

struct sunxi_mipi_dsi_host {
	struct mipi_dsi_host host;
	struct mipi_dsi_device *device;
	struct regmap *regs;

	unsigned int lane_mbps; /* per lane */
	u32 channel;
	unsigned int max_data_lanes;

	struct clk *bus;
	struct clk *mod;
	struct reset_ctl *rst;

	struct phy dphy;
	const struct mipi_dsi_phy_ops *phy_ops;

	const struct sunxi_mipi_dsi_variant *variant;
};

enum sunxi_mipi_dsi_start_inst {
	DSI_START_LPRX,
	DSI_START_LPTX,
	DSI_START_HSC,
	DSI_START_HSD,
};

enum sunxi_mipi_dsi_inst_id {
	DSI_INST_ID_LP11 = 0,
	DSI_INST_ID_TBA,
	DSI_INST_ID_HSC,
	DSI_INST_ID_HSD,
	DSI_INST_ID_LPDT,
	DSI_INST_ID_HSCEXIT,
	DSI_INST_ID_NOP,
	DSI_INST_ID_DLY,
	DSI_INST_ID_END = 15,
};

enum sunxi_mipi_dsi_inst_mode {
	DSI_INST_MODE_STOP = 0,
	DSI_INST_MODE_TBA,
	DSI_INST_MODE_HS,
	DSI_INST_MODE_ESCAPE,
	DSI_INST_MODE_HSCEXIT,
	DSI_INST_MODE_NOP,
};

enum sunxi_mipi_dsi_inst_escape {
	DSI_INST_ESCA_LPDT = 0,
	DSI_INST_ESCA_ULPS,
	DSI_INST_ESCA_UN1,
	DSI_INST_ESCA_UN2,
	DSI_INST_ESCA_RESET,
	DSI_INST_ESCA_UN3,
	DSI_INST_ESCA_UN4,
	DSI_INST_ESCA_UN5,
};

enum sunxi_mipi_dsi_inst_packet {
	DSI_INST_PACK_PIXEL = 0,
	DSI_INST_PACK_COMMAND,
};

static inline struct sunxi_mipi_dsi_host *
host_to_mipi_dsi(struct mipi_dsi_host *host)
{
	return container_of(host, struct sunxi_mipi_dsi_host, host);
}

static const u32 sunxi_mipi_dsi_ecc_array[] = {
	[0] = (BIT(0) | BIT(1) | BIT(2) | BIT(4) | BIT(5) | BIT(7) | BIT(10) |
	       BIT(11) | BIT(13) | BIT(16) | BIT(20) | BIT(21) | BIT(22) |
	       BIT(23)),
	[1] = (BIT(0) | BIT(1) | BIT(3) | BIT(4) | BIT(6) | BIT(8) | BIT(10) |
	       BIT(12) | BIT(14) | BIT(17) | BIT(20) | BIT(21) | BIT(22) |
	       BIT(23)),
	[2] = (BIT(0) | BIT(2) | BIT(3) | BIT(5) | BIT(6) | BIT(9) | BIT(11) |
	       BIT(12) | BIT(15) | BIT(18) | BIT(20) | BIT(21) | BIT(22)),
	[3] = (BIT(1) | BIT(2) | BIT(3) | BIT(7) | BIT(8) | BIT(9) | BIT(13) |
	       BIT(14) | BIT(15) | BIT(19) | BIT(20) | BIT(21) | BIT(23)),
	[4] = (BIT(4) | BIT(5) | BIT(6) | BIT(7) | BIT(8) | BIT(9) | BIT(16) |
	       BIT(17) | BIT(18) | BIT(19) | BIT(20) | BIT(22) | BIT(23)),
	[5] = (BIT(10) | BIT(11) | BIT(12) | BIT(13) | BIT(14) | BIT(15) |
	       BIT(16) | BIT(17) | BIT(18) | BIT(19) | BIT(21) | BIT(22) |
	       BIT(23)),
};

static u32 sunxi_mipi_dsi_ecc_compute(unsigned int data)
{
	int i;
	u8 ecc = 0;

	for (i = 0; i < ARRAY_SIZE(sunxi_mipi_dsi_ecc_array); i++) {
		u32 field = sunxi_mipi_dsi_ecc_array[i];
		bool init = false;
		u8 val = 0;
		int j;

		for (j = 0; j < 24; j++) {
			if (!(BIT(j) & field))
				continue;

			if (!init) {
				val = (BIT(j) & data) ? 1 : 0;
				init = true;
			} else {
				val ^= (BIT(j) & data) ? 1 : 0;
			}
		}

		ecc |= val << i;
	}

	return ecc;
}

static u16 sunxi_mipi_dsi_crc_compute(u8 const *buffer, size_t len)
{
	return bitrev16(crc16_ccitt(0xffff, buffer, len));
}

static u16 sunxi_mipi_dsi_crc_repeat(u8 pd, u8 *buffer, size_t len)
{
	memset(buffer, pd, len);

	return sunxi_mipi_dsi_crc_compute(buffer, len);
}

static u32 sunxi_mipi_dsi_build_sync_pkt(u8 dt, u8 vc, u8 d0, u8 d1)
{
	u32 val = dt & 0x3f;

	val |= (vc & 3) << 6;
	val |= (d0 & 0xff) << 8;
	val |= (d1 & 0xff) << 16;
	val |= sunxi_mipi_dsi_ecc_compute(val) << 24;

	return val;
}

static u32 sunxi_mipi_dsi_build_blk0_pkt(u8 vc, u16 wc)
{
	return sunxi_mipi_dsi_build_sync_pkt(MIPI_DSI_BLANKING_PACKET, vc,
					     wc & 0xff, wc >> 8);
}

static u32 sunxi_mipi_dsi_build_blk1_pkt(u16 pd, u8 *buffer, size_t len)
{
	u32 val = SUN6I_DSI_BLK_PD(pd);

	return val |
	       SUN6I_DSI_BLK_PF(sunxi_mipi_dsi_crc_repeat(pd, buffer, len));
}

static void sunxi_mipi_dsi_inst_abort(struct sunxi_mipi_dsi_host *dsi)
{
	regmap_update_bits(dsi->regs, SUN6I_DSI_BASIC_CTL0_REG,
			   SUN6I_DSI_BASIC_CTL0_INST_ST, 0);
}

static void sunxi_mipi_dsi_inst_commit(struct sunxi_mipi_dsi_host *dsi)
{
	regmap_update_bits(dsi->regs, SUN6I_DSI_BASIC_CTL0_REG,
			   SUN6I_DSI_BASIC_CTL0_INST_ST,
			   SUN6I_DSI_BASIC_CTL0_INST_ST);
}

static int
sunxi_mipi_dsi_inst_wait_for_completion(struct sunxi_mipi_dsi_host *dsi)
{
	u32 val;

	return regmap_read_poll_timeout(dsi->regs, SUN6I_DSI_BASIC_CTL0_REG,
					val,
					!(val & SUN6I_DSI_BASIC_CTL0_INST_ST),
					100, 5000);
}

static void sunxi_mipi_dsi_inst_setup(struct sunxi_mipi_dsi_host *dsi,
				      enum sunxi_mipi_dsi_inst_id id,
				      enum sunxi_mipi_dsi_inst_mode mode,
				      bool clock, u8 data,
				      enum sunxi_mipi_dsi_inst_packet packet,
				      enum sunxi_mipi_dsi_inst_escape escape)
{
	regmap_write(dsi->regs, SUN6I_DSI_INST_FUNC_REG(id),
		     SUN6I_DSI_INST_FUNC_INST_MODE(mode) |
			     SUN6I_DSI_INST_FUNC_ESCAPE_ENTRY(escape) |
			     SUN6I_DSI_INST_FUNC_TRANS_PACKET(packet) |
			     (clock ? SUN6I_DSI_INST_FUNC_LANE_CEN : 0) |
			     SUN6I_DSI_INST_FUNC_LANE_DEN(data));
}

static void sunxi_mipi_dsi_inst_init(struct sunxi_mipi_dsi_host *dsi,
				     struct mipi_dsi_device *device)
{
	u8 lanes_mask = GENMASK(device->lanes - 1, 0);

	sunxi_mipi_dsi_inst_setup(dsi, DSI_INST_ID_LP11, DSI_INST_MODE_STOP,
				  true, lanes_mask, 0, 0);

	sunxi_mipi_dsi_inst_setup(dsi, DSI_INST_ID_TBA, DSI_INST_MODE_TBA,
				  false, 1, 0, 0);

	sunxi_mipi_dsi_inst_setup(dsi, DSI_INST_ID_HSC, DSI_INST_MODE_HS, true,
				  0, DSI_INST_PACK_PIXEL, 0);

	sunxi_mipi_dsi_inst_setup(dsi, DSI_INST_ID_HSD, DSI_INST_MODE_HS, false,
				  lanes_mask, DSI_INST_PACK_PIXEL, 0);

	sunxi_mipi_dsi_inst_setup(dsi, DSI_INST_ID_LPDT, DSI_INST_MODE_ESCAPE,
				  false, 1, DSI_INST_PACK_COMMAND,
				  DSI_INST_ESCA_LPDT);

	sunxi_mipi_dsi_inst_setup(dsi, DSI_INST_ID_HSCEXIT,
				  DSI_INST_MODE_HSCEXIT, true, 0, 0, 0);

	sunxi_mipi_dsi_inst_setup(dsi, DSI_INST_ID_NOP, DSI_INST_MODE_STOP,
				  false, lanes_mask, 0, 0);

	sunxi_mipi_dsi_inst_setup(dsi, DSI_INST_ID_DLY, DSI_INST_MODE_NOP, true,
				  lanes_mask, 0, 0);

	regmap_write(dsi->regs, SUN6I_DSI_INST_JUMP_CFG_REG(0),
		     SUN6I_DSI_INST_JUMP_CFG_POINT(DSI_INST_ID_NOP) |
			     SUN6I_DSI_INST_JUMP_CFG_TO(DSI_INST_ID_HSCEXIT) |
			     SUN6I_DSI_INST_JUMP_CFG_NUM(1));
};

#define VTOTAL(mode)                                   \
	((mode)->vactive.typ + (mode)->vsync_len.typ + \
	 (mode)->vback_porch.typ + (mode)->vfront_porch.typ)
#define HTOTAL(mode)                                   \
	((mode)->hactive.typ + (mode)->hsync_len.typ + \
	 (mode)->hback_porch.typ + (mode)->hfront_porch.typ)

static u16 sunxi_mipi_dsi_get_video_start_delay(struct sunxi_mipi_dsi_host *dsi,
						struct display_timing *mode)
{
	u16 delay = VTOTAL(mode) - mode->vfront_porch.typ + 1;

	if (delay > VTOTAL(mode))
		delay = delay % VTOTAL(mode);

	return max_t(u16, delay, 1);
}

static u16 sunxi_mipi_dsi_get_line_num(struct sunxi_mipi_dsi_host *dsi,
				       struct display_timing *mode)
{
	struct mipi_dsi_device *device = dsi->device;
	unsigned int Bpp = mipi_dsi_pixel_format_to_bpp(device->format) / 8;

	return HTOTAL(mode) * Bpp / device->lanes;
}

static u16 sunxi_mipi_dsi_get_drq_edge0(struct sunxi_mipi_dsi_host *dsi,
					struct display_timing *mode,
					u16 line_num, u16 edge1)
{
	u16 edge0 = edge1;

	edge0 += (mode->hactive.typ + 40) * SUN6I_DSI_TCON_DIV / 8;

	if (edge0 > line_num)
		return edge0 - line_num;

	return 1;
}

static u16 sunxi_mipi_dsi_get_drq_edge1(struct sunxi_mipi_dsi_host *dsi,
					struct display_timing *mode,
					u16 line_num)
{
	struct mipi_dsi_device *device = dsi->device;
	unsigned int Bpp = mipi_dsi_pixel_format_to_bpp(device->format) / 8;
	unsigned int hbp = mode->hback_porch.typ;
	u16 edge1;

	edge1 = SUN6I_DSI_SYNC_POINT;
	edge1 += (mode->hactive.typ + hbp + 20) * Bpp / device->lanes;

	if (edge1 > line_num)
		return line_num;

	return edge1;
}

static void sunxi_mipi_dsi_setup_burst(struct sunxi_mipi_dsi_host *dsi,
				       struct display_timing *mode)
{
	struct mipi_dsi_device *device = dsi->device;
	u32 val = 0;

	if (device->mode_flags & MIPI_DSI_MODE_VIDEO_BURST) {
		u16 line_num = sunxi_mipi_dsi_get_line_num(dsi, mode);
		u16 edge0, edge1;

		edge1 = sunxi_mipi_dsi_get_drq_edge1(dsi, mode, line_num);
		edge0 = sunxi_mipi_dsi_get_drq_edge0(dsi, mode, line_num,
						     edge1);

		regmap_write(dsi->regs, SUN6I_DSI_BURST_DRQ_REG,
			     SUN6I_DSI_BURST_DRQ_EDGE0(edge0) |
				     SUN6I_DSI_BURST_DRQ_EDGE1(edge1));

		regmap_write(dsi->regs, SUN6I_DSI_BURST_LINE_REG,
			     SUN6I_DSI_BURST_LINE_NUM(line_num) |
				     SUN6I_DSI_BURST_LINE_SYNC_POINT(
					     SUN6I_DSI_SYNC_POINT));

		val = SUN6I_DSI_TCON_DRQ_ENABLE_MODE;
	} else if (mode->hfront_porch.typ > 20) {
		/* Maaaaaagic */
		u16 drq = mode->hfront_porch.typ - 20;

		drq *= mipi_dsi_pixel_format_to_bpp(device->format);
		drq /= 32;

		val = (SUN6I_DSI_TCON_DRQ_ENABLE_MODE |
		       SUN6I_DSI_TCON_DRQ_SET(drq));
	}

	regmap_write(dsi->regs, SUN6I_DSI_TCON_DRQ_REG, val);
}

static void sunxi_mipi_dsi_setup_inst_loop(struct sunxi_mipi_dsi_host *dsi,
					   struct display_timing *mode)
{
	struct mipi_dsi_device *device = dsi->device;
	u16 delay = 50 - 1;

	if (device->mode_flags & MIPI_DSI_MODE_VIDEO_BURST) {
		u32 hsync_porch = (HTOTAL(mode) - mode->hactive.typ) * 150;

		delay = (hsync_porch / ((mode->pixelclock.typ / 1000000) * 8));
		delay -= 50;
	}

	regmap_write(dsi->regs, SUN6I_DSI_INST_LOOP_SEL_REG,
		     2 << (4 * DSI_INST_ID_LP11) | 3 << (4 * DSI_INST_ID_DLY));

	regmap_write(dsi->regs, SUN6I_DSI_INST_LOOP_NUM_REG(0),
		     SUN6I_DSI_INST_LOOP_NUM_N0(50 - 1) |
			     SUN6I_DSI_INST_LOOP_NUM_N1(delay));
	regmap_write(dsi->regs, SUN6I_DSI_INST_LOOP_NUM_REG(1),
		     SUN6I_DSI_INST_LOOP_NUM_N0(50 - 1) |
			     SUN6I_DSI_INST_LOOP_NUM_N1(delay));
}

static void sunxi_mipi_dsi_setup_format(struct sunxi_mipi_dsi_host *dsi,
					struct display_timing *mode)
{
	struct mipi_dsi_device *device = dsi->device;
	u32 val = SUN6I_DSI_PIXEL_PH_VC(device->channel);
	u8 dt, fmt;
	u16 wc;

	/*
	 * TODO: The format defines are only valid in video mode and
	 * change in command mode.
	 */
	switch (device->format) {
	case MIPI_DSI_FMT_RGB888:
		dt = MIPI_DSI_PACKED_PIXEL_STREAM_24;
		fmt = 8;
		break;
	case MIPI_DSI_FMT_RGB666:
		dt = MIPI_DSI_PIXEL_STREAM_3BYTE_18;
		fmt = 9;
		break;
	case MIPI_DSI_FMT_RGB666_PACKED:
		dt = MIPI_DSI_PACKED_PIXEL_STREAM_18;
		fmt = 10;
		break;
	case MIPI_DSI_FMT_RGB565:
		dt = MIPI_DSI_PACKED_PIXEL_STREAM_16;
		fmt = 11;
		break;
	default:
		return;
	}
	val |= SUN6I_DSI_PIXEL_PH_DT(dt);

	wc = mode->hactive.typ * mipi_dsi_pixel_format_to_bpp(device->format) /
	     8;
	val |= SUN6I_DSI_PIXEL_PH_WC(wc);
	val |= SUN6I_DSI_PIXEL_PH_ECC(sunxi_mipi_dsi_ecc_compute(val));

	regmap_write(dsi->regs, SUN6I_DSI_PIXEL_PH_REG, val);

	regmap_write(dsi->regs, SUN6I_DSI_PIXEL_PF0_REG,
		     SUN6I_DSI_PIXEL_PF0_CRC_FORCE(0xffff));

	regmap_write(dsi->regs, SUN6I_DSI_PIXEL_PF1_REG,
		     SUN6I_DSI_PIXEL_PF1_CRC_INIT_LINE0(0xffff) |
			     SUN6I_DSI_PIXEL_PF1_CRC_INIT_LINEN(0xffff));

	regmap_write(dsi->regs, SUN6I_DSI_PIXEL_CTL0_REG,
		     SUN6I_DSI_PIXEL_CTL0_PD_PLUG_DISABLE |
			     SUN6I_DSI_PIXEL_CTL0_FORMAT(fmt));
}

static void sunxi_mipi_dsi_setup_timings(struct sunxi_mipi_dsi_host *dsi,
					 struct display_timing *mode)
{
	struct mipi_dsi_device *device = dsi->device;
	int Bpp = mipi_dsi_pixel_format_to_bpp(device->format) / 8;
	u16 hbp = 0, hfp = 0, hsa = 0, hblk = 0, vblk = 0;
	u32 basic_ctl = 0;
	size_t bytes;
	u8 *buffer;

	/* Do all timing calculations up front to allocate buffer space */

	if (device->mode_flags & MIPI_DSI_MODE_VIDEO_BURST) {
		hblk = mode->hactive.typ * Bpp;
		basic_ctl = SUN6I_DSI_BASIC_CTL_VIDEO_BURST |
			    SUN6I_DSI_BASIC_CTL_HSA_HSE_DIS |
			    SUN6I_DSI_BASIC_CTL_HBP_DIS;

		if (device->lanes == 4)
			basic_ctl |= SUN6I_DSI_BASIC_CTL_TRAIL_FILL |
				     SUN6I_DSI_BASIC_CTL_TRAIL_INV(0xc);
	} else {
		/*
		 * A sync period is composed of a blanking packet (4
		 * bytes + payload + 2 bytes) and a sync event packet
		 * (4 bytes). Its minimal size is therefore 10 bytes
		 */
#define HSA_PACKET_OVERHEAD 10
		hsa = MAX(HSA_PACKET_OVERHEAD,
			  mode->hsync_len.typ * Bpp - HSA_PACKET_OVERHEAD);

		/*
		 * The backporch is set using a blanking packet (4
		 * bytes + payload + 2 bytes). Its minimal size is
		 * therefore 6 bytes
		 */
#define HBP_PACKET_OVERHEAD 6
		hbp = MAX(HBP_PACKET_OVERHEAD,
			  mode->hback_porch.typ * Bpp - HBP_PACKET_OVERHEAD);

		/*
		 * The frontporch is set using a sync event (4 bytes)
		 * and two blanking packets (each one is 4 bytes +
		 * payload + 2 bytes). Its minimal size is therefore
		 * 16 bytes
		 */
#define HFP_PACKET_OVERHEAD 16
		hfp = MAX(HFP_PACKET_OVERHEAD,
			  mode->hfront_porch.typ * Bpp - HFP_PACKET_OVERHEAD);

		/*
		 * The blanking is set using a sync event (4 bytes)
		 * and a blanking packet (4 bytes + payload + 2
		 * bytes). Its minimal size is therefore 10 bytes.
		 */
#define HBLK_PACKET_OVERHEAD 10
		hblk = MAX(HBLK_PACKET_OVERHEAD,
			   (HTOTAL(mode) - mode->hsync_len.typ) * Bpp -
				   HBLK_PACKET_OVERHEAD);

		/*
		 * And I'm not entirely sure what vblk is about. The driver in
		 * Allwinner BSP is using a rather convoluted calculation
		 * there only for 4 lanes. However, using 0 (the !4 lanes
		 * case) even with a 4 lanes screen seems to work...
		 */
		vblk = 0;
	}

	/* How many bytes do we need to send all payloads? */
	bytes = max_t(size_t, max(max(hfp, hblk), max(hsa, hbp)), vblk);
	buffer = kmalloc(bytes, GFP_KERNEL);
	if (WARN_ON(!buffer))
		return;

	regmap_write(dsi->regs, SUN6I_DSI_BASIC_CTL_REG, basic_ctl);

	regmap_write(dsi->regs, SUN6I_DSI_SYNC_HSS_REG,
		     sunxi_mipi_dsi_build_sync_pkt(MIPI_DSI_H_SYNC_START,
						   device->channel, 0, 0));

	regmap_write(dsi->regs, SUN6I_DSI_SYNC_HSE_REG,
		     sunxi_mipi_dsi_build_sync_pkt(MIPI_DSI_H_SYNC_END,
						   device->channel, 0, 0));

	regmap_write(dsi->regs, SUN6I_DSI_SYNC_VSS_REG,
		     sunxi_mipi_dsi_build_sync_pkt(MIPI_DSI_V_SYNC_START,
						   device->channel, 0, 0));

	regmap_write(dsi->regs, SUN6I_DSI_SYNC_VSE_REG,
		     sunxi_mipi_dsi_build_sync_pkt(MIPI_DSI_V_SYNC_END,
						   device->channel, 0, 0));

	regmap_write(dsi->regs, SUN6I_DSI_BASIC_SIZE0_REG,
		     SUN6I_DSI_BASIC_SIZE0_VSA(mode->vsync_len.typ) |
			     SUN6I_DSI_BASIC_SIZE0_VBP(mode->vback_porch.typ));

	regmap_write(dsi->regs, SUN6I_DSI_BASIC_SIZE1_REG,
		     SUN6I_DSI_BASIC_SIZE1_VACT(mode->vactive.typ) |
			     SUN6I_DSI_BASIC_SIZE1_VT(VTOTAL(mode)));

	/* sync */
	regmap_write(dsi->regs, SUN6I_DSI_BLK_HSA0_REG,
		     sunxi_mipi_dsi_build_blk0_pkt(device->channel, hsa));
	regmap_write(dsi->regs, SUN6I_DSI_BLK_HSA1_REG,
		     sunxi_mipi_dsi_build_blk1_pkt(0, buffer, hsa));

	/* backporch */
	regmap_write(dsi->regs, SUN6I_DSI_BLK_HBP0_REG,
		     sunxi_mipi_dsi_build_blk0_pkt(device->channel, hbp));
	regmap_write(dsi->regs, SUN6I_DSI_BLK_HBP1_REG,
		     sunxi_mipi_dsi_build_blk1_pkt(0, buffer, hbp));

	/* frontporch */
	regmap_write(dsi->regs, SUN6I_DSI_BLK_HFP0_REG,
		     sunxi_mipi_dsi_build_blk0_pkt(device->channel, hfp));
	regmap_write(dsi->regs, SUN6I_DSI_BLK_HFP1_REG,
		     sunxi_mipi_dsi_build_blk1_pkt(0, buffer, hfp));

	/* hblk */
	regmap_write(dsi->regs, SUN6I_DSI_BLK_HBLK0_REG,
		     sunxi_mipi_dsi_build_blk0_pkt(device->channel, hblk));
	regmap_write(dsi->regs, SUN6I_DSI_BLK_HBLK1_REG,
		     sunxi_mipi_dsi_build_blk1_pkt(0, buffer, hblk));

	/* vblk */
	regmap_write(dsi->regs, SUN6I_DSI_BLK_VBLK0_REG,
		     sunxi_mipi_dsi_build_blk0_pkt(device->channel, vblk));
	regmap_write(dsi->regs, SUN6I_DSI_BLK_VBLK1_REG,
		     sunxi_mipi_dsi_build_blk1_pkt(0, buffer, vblk));

	kfree(buffer);
}

static int sunxi_mipi_dsi_start(struct sunxi_mipi_dsi_host *dsi,
				enum sunxi_mipi_dsi_start_inst func)
{
	switch (func) {
	case DSI_START_LPTX:
		regmap_write(dsi->regs, SUN6I_DSI_INST_JUMP_SEL_REG,
			     DSI_INST_ID_LPDT << (4 * DSI_INST_ID_LP11) |
				     DSI_INST_ID_END << (4 * DSI_INST_ID_LPDT));
		break;
	case DSI_START_LPRX:
		regmap_write(dsi->regs, SUN6I_DSI_INST_JUMP_SEL_REG,
			     DSI_INST_ID_LPDT << (4 * DSI_INST_ID_LP11) |
				     DSI_INST_ID_DLY << (4 * DSI_INST_ID_LPDT) |
				     DSI_INST_ID_TBA << (4 * DSI_INST_ID_DLY) |
				     DSI_INST_ID_END << (4 * DSI_INST_ID_TBA));
		break;
	case DSI_START_HSC:
		regmap_write(dsi->regs, SUN6I_DSI_INST_JUMP_SEL_REG,
			     DSI_INST_ID_HSC << (4 * DSI_INST_ID_LP11) |
				     DSI_INST_ID_END << (4 * DSI_INST_ID_HSC));
		break;
	case DSI_START_HSD:
		regmap_write(dsi->regs, SUN6I_DSI_INST_JUMP_SEL_REG,
			     DSI_INST_ID_NOP << (4 * DSI_INST_ID_LP11) |
				     DSI_INST_ID_HSD << (4 * DSI_INST_ID_NOP) |
				     DSI_INST_ID_DLY << (4 * DSI_INST_ID_HSD) |
				     DSI_INST_ID_NOP << (4 * DSI_INST_ID_DLY) |
				     DSI_INST_ID_END
					     << (4 * DSI_INST_ID_HSCEXIT));
		break;
	default:
		regmap_write(dsi->regs, SUN6I_DSI_INST_JUMP_SEL_REG,
			     DSI_INST_ID_END << (4 * DSI_INST_ID_LP11));
		break;
	}

	sunxi_mipi_dsi_inst_abort(dsi);
	sunxi_mipi_dsi_inst_commit(dsi);

	if (func == DSI_START_HSC)
		regmap_update_bits(dsi->regs,
				   SUN6I_DSI_INST_FUNC_REG(DSI_INST_ID_LP11),
				   SUN6I_DSI_INST_FUNC_LANE_CEN, 0);

	return 0;
}

static u32 sunxi_mipi_dsi_dcs_build_pkt_hdr(struct sunxi_mipi_dsi_host *dsi,
					    const struct mipi_dsi_msg *msg)
{
	u32 pkt = msg->type;

	if (msg->type == MIPI_DSI_DCS_LONG_WRITE ||
	    msg->type == MIPI_DSI_GENERIC_LONG_WRITE) {
		pkt |= ((msg->tx_len) & 0xffff) << 8;
		pkt |= (((msg->tx_len) >> 8) & 0xffff) << 16;
	} else {
		pkt |= (((u8 *)msg->tx_buf)[0] << 8);
		if (msg->tx_len > 1)
			pkt |= (((u8 *)msg->tx_buf)[1] << 16);
	}

	pkt |= sunxi_mipi_dsi_ecc_compute(pkt) << 24;

	return pkt;
}

static int sunxi_mipi_dsi_dcs_write_short(struct sunxi_mipi_dsi_host *dsi,
					  const struct mipi_dsi_msg *msg)
{
	regmap_write(dsi->regs, SUN6I_DSI_CMD_TX_REG(0),
		     sunxi_mipi_dsi_dcs_build_pkt_hdr(dsi, msg));
	regmap_update_bits(dsi->regs, SUN6I_DSI_CMD_CTL_REG, 0xff, (4 - 1));

	sunxi_mipi_dsi_start(dsi, DSI_START_LPTX);

	return msg->tx_len;
}

static int sunxi_mipi_dsi_regmap_bulk_write(struct regmap *regs, ulong reg,
					    void *data, size_t count)
{
	int i, err;

	for (i = 0; i < count; i++) {
		err = regmap_write(regs, reg + i * regs->width,
				   *(uint *)(data + i * regs->width));
		if (err)
			return err;
	}
	return 0;
}

static int sunxi_mipi_dsi_regmap_bulk_read(struct regmap *regs, ulong reg,
					   void *data, size_t count)
{
	int i, err;

	for (i = 0; i < count * regs->width; i += regs->width) {
		err = regmap_read(regs, reg + i, (uint *)(data + i));
		if (err)
			return err;
	}
	return 0;
}

static int sunxi_mipi_dsi_dcs_write_long(struct sunxi_mipi_dsi_host *dsi,
					 const struct mipi_dsi_msg *msg)
{
	int ret, len = 0;
	u8 *bounce;
	u16 crc;

	if (msg->tx_len >= SUN6I_DSI_MAX_LONG_MSG_SIZE)
		return -EINVAL;

	regmap_write(dsi->regs, SUN6I_DSI_CMD_TX_REG(0),
		     sunxi_mipi_dsi_dcs_build_pkt_hdr(dsi, msg));

	bounce = kzalloc(ALIGN(msg->tx_len + sizeof(crc), 4), GFP_KERNEL);
	if (!bounce)
		return -ENOMEM;

	memcpy(bounce, msg->tx_buf, msg->tx_len);
	len += msg->tx_len;

	crc = sunxi_mipi_dsi_crc_compute(bounce, msg->tx_len);
	memcpy((u8 *)bounce + msg->tx_len, &crc, sizeof(crc));
	len += sizeof(crc);

	sunxi_mipi_dsi_regmap_bulk_write(dsi->regs, SUN6I_DSI_CMD_TX_REG(1),
					 bounce, DIV_ROUND_UP(len, 4));
	regmap_write(dsi->regs, SUN6I_DSI_CMD_CTL_REG, len + 4 - 1);
	kfree(bounce);

	sunxi_mipi_dsi_start(dsi, DSI_START_LPTX);

	ret = sunxi_mipi_dsi_inst_wait_for_completion(dsi);
	if (ret < 0) {
		sunxi_mipi_dsi_inst_abort(dsi);
		return ret;
	}

	/*
	 * TODO: There's some bits (reg 0x200, bits 8/9) that
	 * apparently can be used to check whether the data have been
	 * sent, but I couldn't get it to work reliably.
	 */
	return msg->tx_len;
}

static int sunxi_mipi_dsi_dcs_read(struct sunxi_mipi_dsi_host *dsi,
				   const struct mipi_dsi_msg *msg)
{
	u32 val;
	int ret;
	u16 crc;
	int msglen = 0;
	u8 recvbuf[32];

	if (msg->rx_len >= SUN6I_DSI_MAX_LONG_MSG_SIZE)
		return -EINVAL;

	regmap_write(dsi->regs, SUN6I_DSI_CMD_TX_REG(0),
		     sunxi_mipi_dsi_dcs_build_pkt_hdr(dsi, msg));
	regmap_write(dsi->regs, SUN6I_DSI_CMD_CTL_REG, (4 - 1));

	sunxi_mipi_dsi_start(dsi, DSI_START_LPRX);

	ret = sunxi_mipi_dsi_inst_wait_for_completion(dsi);
	if (ret < 0) {
		sunxi_mipi_dsi_inst_abort(dsi);
		return ret;
	}

	ret = regmap_read_poll_timeout(dsi->regs, SUN6I_DSI_CMD_CTL_REG, val,
				       val & (SUN6I_DSI_CMD_CTL_RX_OVERFLOW |
					      SUN6I_DSI_CMD_CTL_RX_FLAG),
				       100, 5000);
	if (ret)
		return ret;

	if (val & SUN6I_DSI_CMD_CTL_RX_OVERFLOW)
		return -EIO;

	ret = sunxi_mipi_dsi_regmap_bulk_read(
		dsi->regs, SUN6I_DSI_CMD_RX_REG(0), recvbuf, 8);
	if (ret)
		return ret;

	switch (recvbuf[0]) {
	default:
	case MIPI_DSI_RX_ACKNOWLEDGE_AND_ERROR_REPORT:
		return -EIO;

	case MIPI_DSI_RX_DCS_LONG_READ_RESPONSE:
	case MIPI_DSI_RX_GENERIC_LONG_READ_RESPONSE:
		msglen = MIN(recvbuf[1] + (recvbuf[2] << 8), msg->rx_len);
		msglen = MIN(msglen, sizeof(recvbuf) - 4 - sizeof(crc));
		crc = sunxi_mipi_dsi_crc_compute(recvbuf + 4, msglen);
		val = recvbuf[4 + msglen + 0] + (recvbuf[4 + msglen + 1] << 8);
		if (val != crc) {
			log_warning("CRC mismatch: %04x vs %04x", val, crc);
			return -EIO;
		}
		memcpy(msg->rx_buf, recvbuf + 4, msglen);
		break;

	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_2BYTE:
	case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_2BYTE:
		((u8 *)msg->rx_buf)[1] = recvbuf[2];
		msglen++;
		fallthrough;
	case MIPI_DSI_RX_DCS_SHORT_READ_RESPONSE_1BYTE:
	case MIPI_DSI_RX_GENERIC_SHORT_READ_RESPONSE_1BYTE:
		((u8 *)msg->rx_buf)[0] = recvbuf[1];
		msglen++;
		break;
	}

	return msglen;
}

static bool sunxi_mipi_dsi_host_is_enabled(struct sunxi_mipi_dsi_host *dsi)
{
	u32 val;
	if (!regmap_read(dsi->regs, SUN6I_DSI_CTL_REG, &val))
		return false;

	return !!(val & SUN6I_DSI_CTL_EN);
}

static int sunxi_mipi_dsi_host_attach(struct mipi_dsi_host *host,
				      struct mipi_dsi_device *device)
{
	struct sunxi_mipi_dsi_host *dsi = host_to_mipi_dsi(host);

	if (device->lanes > dsi->max_data_lanes) {
		dev_err(device->dev,
			"the number of data lanes(%u) is too many\n",
			device->lanes);
		return -EINVAL;
	}

	dsi->channel = device->channel;
	dsi->device = device;

	return 0;
}

static int sunxi_mipi_dsi_host_detach(struct mipi_dsi_host *host,
				      struct mipi_dsi_device *device)
{
	struct sunxi_mipi_dsi_host *dsi = host_to_mipi_dsi(host);

	dsi->device = NULL;

	return 0;
}

static ssize_t sunxi_mipi_dsi_host_transfer(struct mipi_dsi_host *host,
					    const struct mipi_dsi_msg *msg)
{
	struct sunxi_mipi_dsi_host *dsi = host_to_mipi_dsi(host);
	int ret;

	ret = sunxi_mipi_dsi_inst_wait_for_completion(dsi);
	if (ret < 0)
		sunxi_mipi_dsi_inst_abort(dsi);

	regmap_write(dsi->regs, SUN6I_DSI_CMD_CTL_REG,
		     SUN6I_DSI_CMD_CTL_RX_OVERFLOW | SUN6I_DSI_CMD_CTL_RX_FLAG |
			     SUN6I_DSI_CMD_CTL_TX_FLAG);

	switch (msg->type) {
	case MIPI_DSI_DCS_SHORT_WRITE:
	case MIPI_DSI_DCS_SHORT_WRITE_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_1_PARAM:
	case MIPI_DSI_GENERIC_SHORT_WRITE_2_PARAM:
	case MIPI_DSI_SET_MAXIMUM_RETURN_PACKET_SIZE:
		ret = sunxi_mipi_dsi_dcs_write_short(dsi, msg);
		break;

	case MIPI_DSI_GENERIC_LONG_WRITE:
	case MIPI_DSI_DCS_LONG_WRITE:
		ret = sunxi_mipi_dsi_dcs_write_long(dsi, msg);
		break;

	case MIPI_DSI_DCS_READ:
	case MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM:
	case MIPI_DSI_GENERIC_READ_REQUEST_2_PARAM:
		ret = sunxi_mipi_dsi_dcs_read(dsi, msg);
		break;

	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static const struct mipi_dsi_host_ops sunxi_mipi_dsi_host_ops = {
	.attach = sunxi_mipi_dsi_host_attach,
	.detach = sunxi_mipi_dsi_host_detach,
	.transfer = sunxi_mipi_dsi_host_transfer,
};

static void sunxi_mipi_dsi_setup_dphy(struct sunxi_mipi_dsi_host *dsi,
				      struct display_timing *mode)
{
	struct mipi_dsi_device *device = dsi->device;
	struct phy_configure_opts_mipi_dphy config = {};

	phy_mipi_dphy_get_default_config(
		mode->pixelclock.typ,
		mipi_dsi_pixel_format_to_bpp(device->format), device->lanes,
		&config);

	generic_phy_set_mode(&dsi->dphy, PHY_MODE_MIPI_DPHY, 0);
	generic_phy_configure(&dsi->dphy, &config);

	generic_phy_power_on(&dsi->dphy);
}

static void sunxi_mipi_dsi_host_set_mode(struct sunxi_mipi_dsi_host *dsi,
					 unsigned long mode_flags,
					 struct display_timing *mode)
{
	const struct mipi_dsi_phy_ops *phy_ops = dsi->phy_ops;
	u32 val;
	u16 delay;

	if (!sunxi_mipi_dsi_host_is_enabled(dsi)) {
		/*
		 * Enable the DSI block.
		 */
		regmap_write(dsi->regs, SUN6I_DSI_CTL_REG, SUN6I_DSI_CTL_EN);

		val = SUN6I_DSI_BASIC_CTL0_ECC_EN | SUN6I_DSI_BASIC_CTL0_CRC_EN;
		if (!(mode_flags & MIPI_DSI_MODE_NO_EOT_PACKET))
			val |= SUN6I_DSI_BASIC_CTL0_HS_EOTP_EN;
		regmap_write(dsi->regs, SUN6I_DSI_BASIC_CTL0_REG, val);

		regmap_write(dsi->regs, SUN6I_DSI_TRANS_START_REG, 10);
		regmap_write(dsi->regs, SUN6I_DSI_TRANS_ZERO_REG, 0);

		sunxi_mipi_dsi_inst_init(dsi, dsi->device);
	}

	if (mode) {
		sunxi_mipi_dsi_setup_dphy(dsi, mode);

		delay = sunxi_mipi_dsi_get_video_start_delay(dsi, mode);
		regmap_write(dsi->regs, SUN6I_DSI_BASIC_CTL1_REG,
			     SUN6I_DSI_BASIC_CTL1_VIDEO_ST_DELAY(delay) |
				     SUN6I_DSI_BASIC_CTL1_VIDEO_FILL |
				     SUN6I_DSI_BASIC_CTL1_VIDEO_PRECISION |
				     SUN6I_DSI_BASIC_CTL1_VIDEO_MODE);

		sunxi_mipi_dsi_setup_burst(dsi, mode);
		sunxi_mipi_dsi_setup_inst_loop(dsi, mode);
		sunxi_mipi_dsi_setup_format(dsi, mode);
		sunxi_mipi_dsi_setup_timings(dsi, mode);
	}

	if (phy_ops && phy_ops->post_set_mode)
		phy_ops->post_set_mode(dsi->device, mode_flags);
}

static int sunxi_mipi_dsi_host_init(struct udevice *dev,
				    struct mipi_dsi_device *device,
				    struct display_timing *timings,
				    unsigned int max_data_lanes,
				    const struct mipi_dsi_phy_ops *phy_ops)
{
	struct sunxi_mipi_dsi_host *dsi = dev_get_priv(dev);

	dsi->phy_ops = phy_ops;
	dsi->max_data_lanes = max_data_lanes;
	dsi->device = device;
	device->host = &dsi->host;

	sunxi_mipi_dsi_host_set_mode(dsi, 0, timings);

	if (phy_ops && phy_ops->init)
		phy_ops->init(device);

	return 0;
}

static int sunxi_mipi_dsi_host_enable(struct udevice *dev)
{
	struct sunxi_mipi_dsi_host *dsi = dev_get_priv(dev);

	/* Switch to video mode for panel-bridge enable & panel enable */
	sunxi_mipi_dsi_host_set_mode(dsi, dsi->device->mode_flags, NULL);

	sunxi_mipi_dsi_start(dsi, DSI_START_HSC);
	udelay(1000);
	sunxi_mipi_dsi_start(dsi, DSI_START_HSD);

	return 0;
}

static int sunxi_mipi_dsi_host_disable(struct udevice *dev)
{
	struct sunxi_mipi_dsi_host *dsi = dev_get_priv(dev);

	/* Switch to video mode for panel-bridge enable & panel enable */
	sunxi_mipi_dsi_host_set_mode(dsi, 0, NULL);

	sunxi_mipi_dsi_inst_abort(dsi);

	return 0;
}

struct dsi_host_ops sunxi_dsi_host_ops = {
	.init = sunxi_mipi_dsi_host_init,
	.enable = sunxi_mipi_dsi_host_enable,
	.disable = sunxi_mipi_dsi_host_disable,
};

static int sunxi_mipi_dsi_host_probe(struct udevice *dev)
{
	struct sunxi_mipi_dsi_host *dsi = dev_get_priv(dev);
	int ret;

	dsi->variant =
		(const struct sunxi_mipi_dsi_variant *)dev_get_driver_data(dev);

	dsi->host.dev = (struct device *)dev;
	dsi->host.ops = &sunxi_mipi_dsi_host_ops;

	dsi->regs = devm_regmap_init(dev, NULL, NULL, NULL);
	if (IS_ERR(dsi->regs)) {
		dev_err(dev, "Couldn't map the DSI registers\n");
		return -EINVAL;
	}

	/* Reset */
	dsi->rst = devm_reset_control_get_by_index(dev, 0);
	if (IS_ERR(dsi->rst)) {
		ret = PTR_ERR(dsi->rst);
		dev_err(dev, "missing dsi hardware reset %d\n", ret);
		return ret;
	}
	reset_deassert(dsi->rst);

	dsi->bus = devm_clk_get(dev, "bus");
	if (IS_ERR(dsi->bus)) {
		ret = PTR_ERR(dsi->bus);
		dev_err(dev, "Couldn't get bus clock %d\n", ret);
		return ret;
	}

	if (dsi->variant->has_mod_clk) {
		dsi->mod = devm_clk_get(dev, "mod");
		if (IS_ERR(dsi->mod)) {
			ret = PTR_ERR(dsi->mod);
			dev_err(dev, "Couldn't get mod clock %d\n", ret);
			return ret;
		}
	}

	clk_enable(dsi->bus);
	clk_enable(dsi->mod);

	ret = generic_phy_get_by_name(dev, "dphy", &dsi->dphy);
	if (ret) {
		dev_err(dev, "Couldn't get DPHY\n");
		return ret;
	}

	generic_phy_init(&dsi->dphy);

	return 0;
}

static int sunxi_mipi_dsi_host_bind(struct udevice *dev)
{
	ofnode node;
	ofnode_for_each_subnode(node, dev_ofnode(dev))
		lists_bind_fdt(dev, node, NULL, NULL, false);

	return 0;
}

static const struct sunxi_mipi_dsi_variant sunxi_a31_mipi_dsi_variant = {
	.has_mod_clk = true,
	.set_mod_clk = true,
};

static const struct sunxi_mipi_dsi_variant sun50i_a64_mipi_dsi_variant = {
};

static const struct sunxi_mipi_dsi_variant sun50i_a100_mipi_dsi_variant = {
	.has_mod_clk = true,
};

static const struct udevice_id sunxi_mipi_dsi_host_of_table[] = {
	{
		.compatible = "allwinner,sunxi-a31-mipi-dsi",
		.data = (ulong)&sunxi_a31_mipi_dsi_variant,
	},
	{
		.compatible = "allwinner,sun50i-a64-mipi-dsi",
		.data = (ulong)&sun50i_a64_mipi_dsi_variant,
	},
	{
		.compatible = "allwinner,sun50i-a100-mipi-dsi",
		.data = (ulong)&sun50i_a100_mipi_dsi_variant,
	},
	{}
};

U_BOOT_DRIVER(sunxi_mipi_dsi_host) = {
	.name = "sunxi_mipi_dsi_host",
	.of_match = sunxi_mipi_dsi_host_of_table,
	.id = UCLASS_DSI_HOST,
	.probe = sunxi_mipi_dsi_host_probe,
	.bind = sunxi_mipi_dsi_host_bind,
	.ops = &sunxi_dsi_host_ops,
	.priv_auto = sizeof(struct sunxi_mipi_dsi_host),
};
