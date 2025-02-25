// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner DE mixer driver
 *
 * Copyright (C) 2025, Hironori KIKUCHI <kikuchan98@gmail.com>
 */

#include <asm/arch/clock.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <clk.h>
#include <display.h>
#include <dm/device_compat.h>
#include <dm/device-internal.h>
#include <dm.h>
#include <edid.h>
#include <efi_loader.h>
#include <fdtdec.h>
#include <fdt_support.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <log.h>
#include <panel.h>
#include <part.h>
#include <regmap.h>
#include <reset.h>
#include <sunxi_gpio.h>
#include <video.h>

#include "sunxi_de.h"
#include "sunxi_tcon.h"
#include "sunxi_de_mixer.h"

struct sunxi_de_mixer_cfg {
	bool is_de2;
	bool is_de33;

	ulong mod_rate;

	int ovl_ui_ch;
};

struct sunxi_de_mixer_priv {
	struct regmap *regs;

	struct clk *clk_mod;
	struct clk *clk_bus;

	struct reset_ctl_bulk *resets;

	const struct sunxi_de_mixer_cfg *quirks;
};

static void sunxi_de_mixer_prepare(struct udevice *dev)
{
	struct sunxi_de_mixer_priv *priv = dev_get_priv(dev);

	reset_deassert_bulk(priv->resets);

	if (priv->quirks->mod_rate)
		clk_set_rate(priv->clk_mod, priv->quirks->mod_rate);

	clk_enable(priv->clk_bus);
	clk_enable(priv->clk_mod);
}

static int sunxi_de2_mixer_attach(struct udevice *dev, ulong base, int stride,
				  int bpp)
{
	struct sunxi_de_mixer_priv *priv = dev_get_priv(dev);
	struct regmap *regs = priv->regs;
	int ovl_ui_ch = priv->quirks->ovl_ui_ch;
	int ch;

	// Disable overlays
	for (ch = 0; ch < DE2_MAX_OVERLAYS; ch++) {
		regmap_write(regs, DE2_OVL_BASE(ch), 0);
	}

	// Disable unused units
	regmap_write(regs, DE2_VSU_BASE, 0);
	regmap_write(regs, DE2_GSU1_BASE, 0);
	regmap_write(regs, DE2_GSU2_BASE, 0);
	regmap_write(regs, DE2_GSU3_BASE, 0);
	regmap_write(regs, DE2_FCE_BASE, 0);
	regmap_write(regs, DE2_BWS_BASE, 0);
	regmap_write(regs, DE2_LTI_BASE, 0);
	regmap_write(regs, DE2_PEAK_BASE, 0);
	regmap_write(regs, DE2_ASE_BASE, 0);
	regmap_write(regs, DE2_FCC_BASE, 0);

	// GLB
	regmap_write(regs, DE2_GLB_CTL, 1);
	regmap_write(regs, DE2_GLB_STS, 0);

	// OVL_UI
	regmap_write(regs, DE2_OVL_UI_PITCH(ovl_ui_ch), stride);
	regmap_write(regs, DE2_OVL_UI_TOP_LADD(ovl_ui_ch), base);

	return 0;
}

static void sunxi_de2_mixer_apply_timing(struct udevice *dev,
					 const struct display_timing *mode)
{
	struct sunxi_de_mixer_priv *priv = dev_get_priv(dev);
	struct regmap *regs = priv->regs;
	int lcd_x = mode->hactive.typ;
	int lcd_y = mode->vactive.typ;
	u32 xysize = ((lcd_y - 1) << 16) | (lcd_x - 1);
	int ovl_ui_ch = priv->quirks->ovl_ui_ch;

	// GLB
	regmap_write(regs, DE2_GLB_CTL, 1);
	regmap_write(regs, DE2_GLB_STS, 0);
	regmap_write(regs, DE2_GLB_SIZE, xysize);

	// BLD
	regmap_write(regs, DE2_BLD_FILL_COLOR_CTL, 0x00000101);
	regmap_write(regs, DE2_BLD_FILL_COLOR(0), 0xff000080);
	regmap_write(regs, DE2_BLD_CH_ISIZE(0), xysize);
	regmap_write(regs, DE2_BLD_CH_OFFSET(0), 0);
	regmap_write(regs, DE2_BLD_CH_RTCTL, ovl_ui_ch);
	regmap_write(regs, DE2_BLD_PREMUL_CTL, 0);
	regmap_write(regs, DE2_BLD_BK_COLOR, 0xff800000);
	regmap_write(regs, DE2_BLD_SIZE, xysize);
	regmap_write(regs, DE2_BLD_CTL(0), 0x03010301);
	regmap_write(regs, DE2_BLD_CTL(1), 0x03010301);
	regmap_write(regs, DE2_BLD_CTL(2), 0x03010301);
	regmap_write(regs, DE2_BLD_CTL(3), 0x03010301);
	regmap_write(regs, DE2_BLD_KEY_CTL, 0);
	regmap_write(regs, DE2_BLD_OUT_COLOR,
		     mode->flags & DISPLAY_FLAGS_INTERLACED ? 2 : 0);

	// OVL
	regmap_write(regs, DE2_OVL_UI_ATTR_CTL(ovl_ui_ch), 0xff000401); // enable
	regmap_write(regs, DE2_OVL_UI_MBSIZE(ovl_ui_ch), xysize);
	regmap_write(regs, DE2_OVL_UI_SIZE(ovl_ui_ch), xysize);
	regmap_write(regs, DE2_OVL_UI_COOR(ovl_ui_ch), 0);

	// apply settings
	regmap_write(regs, DE2_GLB_DBUFFER, 1);
}

static int sunxi_de33_mixer_attach(struct udevice *dev, ulong base, int stride,
				   int bpp)
{
	struct sunxi_de_mixer_priv *priv = dev_get_priv(dev);
	struct regmap *regs = priv->regs;
	int ovl_ui_ch = priv->quirks->ovl_ui_ch;

	// DE: CLK Enable (Additional clocks must be enabled)
	regmap_write(regs, DE33_CLOCK_BASE + 0x24, 0);
	regmap_write(regs, DE33_CLOCK_BASE + 0x28, 0xa980);

	// OVL_UI
	regmap_write(regs, DE33_OVL_UI_PITCH(ovl_ui_ch), stride);
	regmap_write(regs, DE33_OVL_UI_TOP_LADD(ovl_ui_ch), base);

	// apply settings
	regmap_write(regs, DE33_TOP_BASE + 0x10, 1); // 0x01008110

	return 0;
}

static void sunxi_de33_mixer_apply_timing(struct udevice *dev,
					  const struct display_timing *mode)
{
	struct sunxi_de_mixer_priv *priv = dev_get_priv(dev);
	struct regmap *regs = priv->regs;
	int lcd_x = mode->hactive.typ;
	int lcd_y = mode->vactive.typ;
	u32 xysize = ((lcd_y - 1) << 16) | (lcd_x - 1);
	int ovl_ui_ch = priv->quirks->ovl_ui_ch;

	// DE-TOP
	regmap_write(regs, DE33_TOP_BASE + 0x00, 1);
	regmap_write(regs, DE33_TOP_BASE + 0x0c, 1);
	regmap_write(regs, DE33_TOP_BASE + 0x08, xysize);

	// BLD
	regmap_write(regs, DE33_BLD_ENABLE_CTL, 0x00000101);
	regmap_write(regs, DE33_BLD_FILL_COLOR(0), 0xff000000);
	regmap_write(regs, DE33_BLD_CH_ISIZE(0), xysize);
	regmap_write(regs, DE33_BLD_CH_RTCTL, ovl_ui_ch);
	regmap_write(regs, DE33_BLD_PREMUL_CTL, 0);
	regmap_write(regs, DE33_BLD_BK_COLOR, 0xff000000);
	regmap_write(regs, DE33_BLD_SIZE, xysize);
	regmap_write(regs, DE33_BLD_CTL(0), 0x03010301);
	regmap_write(regs, DE33_BLD_CTL(1), 0x03010301);
	regmap_write(regs, DE33_BLD_CTL(2), 0x03010301);
	regmap_write(regs, DE33_BLD_CTL(3), 0x03010301);
	regmap_write(regs, DE33_BLD_KEY_CTL, 0);
	regmap_write(regs, DE33_BLD_OUT_COLOR,
		     mode->flags & DISPLAY_FLAGS_INTERLACED ? 2 : 0);

	// OVL_UI
	regmap_write(regs, DE33_OVL_UI_ATTR_CTL(ovl_ui_ch), 0xff000401); // enable
	regmap_write(regs, DE33_OVL_UI_MBSIZE(ovl_ui_ch), xysize);
	regmap_write(regs, DE33_OVL_UI_SIZE(ovl_ui_ch), xysize);
	regmap_write(regs, DE33_OVL_UI_COOR(ovl_ui_ch), 0);

	// apply settings
	regmap_write(regs, DE33_TOP_BASE + 0x10, 1); // 0x01008110
}


static int sunxi_de_mixer_resize(struct udevice *dev)
{
	struct sunxi_de_mixer_priv *priv = dev_get_priv(dev);
	struct display_timing timing;
	int err;

	err = sunxi_tcon_get_display_timing(&timing);
	if (err)
		return err;

	if (priv->quirks->is_de2)
		sunxi_de2_mixer_apply_timing(dev, &timing);
	else if (priv->quirks->is_de33)
		sunxi_de33_mixer_apply_timing(dev, &timing);
	else {
		log_err("Unknown engine\n");
		return -EINVAL;
	}

	sunxi_tcon_apply_timing(&timing);

	return 0;
}

static int sunxi_de_mixer_probe(struct udevice *dev)
{
	struct sunxi_de_mixer_priv *priv = dev_get_priv(dev);
	struct sunxi_de_plat *plat = dev_get_plat(dev);

	priv->quirks =
		(const struct sunxi_de_mixer_cfg *)dev_get_driver_data(dev);

	/* XXX: Use parent's reg settings */
	priv->regs = devm_regmap_init(dev->parent, NULL, NULL, NULL);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "Couldn't map the DE registers\n");
		return -EINVAL;
	}

	priv->clk_bus = devm_clk_get(dev, "bus");
	if (IS_ERR(priv->clk_bus)) {
		dev_err(dev, "Couldn't get bus clock\n");
		return -EINVAL;
	}

	priv->clk_mod = devm_clk_get(dev, "mod");
	if (IS_ERR(priv->clk_mod)) {
		dev_err(dev, "Couldn't get mod clock\n");
		return -EINVAL;
	}

	priv->resets = devm_reset_bulk_get(dev);
	if (IS_ERR(priv->resets)) {
		dev_err(dev, "Couldn't get our reset line\n");
		return -EINVAL;
	}

	sunxi_de_mixer_prepare(dev);

	if (priv->quirks->is_de2)
		plat->attach = sunxi_de2_mixer_attach;
	else if (priv->quirks->is_de33)
		plat->attach = sunxi_de33_mixer_attach;
	else {
		log_err("Unknown engine\n");
		return -EINVAL;
	}
	plat->resize = sunxi_de_mixer_resize;
	sunxi_de_attach_mixer(dev->parent, dev);

	video_resize(dev->parent);

	return 0;
}

struct sunxi_de_mixer_cfg sun50i_a64_mixer0_cfg = {
	.is_de2 = true,
	.mod_rate = 297000000,
};

struct sunxi_de_mixer_cfg sun50i_a100_mixer0_cfg = {
	.is_de2 = true,
	//.mod_rate = 300000000,
	.ovl_ui_ch = 2,
};

struct sunxi_de_mixer_cfg sun50i_h616_mixer0_cfg = {
	.is_de33 = true,
	.mod_rate = 600000000,
	.ovl_ui_ch = 1,
};

struct udevice_id sunxi_de_mixer_of_table[] = {
	{
		.compatible = "allwinner,sun50i-a64-de2-mixer-0",
		.data = (ulong)&sun50i_a64_mixer0_cfg,
	},
	{
		.compatible = "allwinner,sun50i-a100-de2-mixer-0",
		.data = (ulong)&sun50i_a100_mixer0_cfg,
	},
	{
		.compatible = "allwinner,sun50i-h616-de33-mixer-0",
		.data = (ulong)&sun50i_h616_mixer0_cfg,
	},
	{},
};

U_BOOT_DRIVER(sunxi_de_mixer) = {
	.name = "sunxi_de_mixer",
	.id = UCLASS_NOP,
	.of_match = sunxi_de_mixer_of_table,
	.probe = sunxi_de_mixer_probe,
	.priv_auto = sizeof(struct sunxi_de_mixer_priv),
	.plat_auto = sizeof(struct sunxi_de_plat),
};
