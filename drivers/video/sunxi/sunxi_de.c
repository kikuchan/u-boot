// SPDX-License-Identifier: GPL-2.0+
/*
 * Allwinner DE base driver
 *
 * Copyright (C) 2025, Hironori KIKUCHI <kikuchan98@gmail.com>
 */

#include <dm/lists.h>
#include <dm/uclass-id.h>
#include <video_console.h>
#include <display.h>
#include <dm.h>
#include <edid.h>
#include <efi_loader.h>
#include <fdtdec.h>
#include <fdt_support.h>
#include <log.h>
#include <part.h>
#include <video.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/arch/clock.h>
#include <linux/bitops.h>
#include <linux/err.h>
#include <sunxi_gpio.h>
#include <panel.h>
#include <regmap.h>

#include "simplefb_common.h"
#include "sunxi_de.h"

DECLARE_GLOBAL_DATA_PTR;

enum {
	/* Maximum LCD size we support */
	LCD_MAX_WIDTH = 3840,
	LCD_MAX_HEIGHT = 2160,
	LCD_MAX_LOG2_BPP = VIDEO_BPP32,
};

struct sunxi_de_priv {
	struct udevice *mixer;
};

static void sunxi_de_acquire_sram(void)
{
	/* XXX: Set SRAM for video use */
	clrsetbits_32(SUNXI_SRAMC_BASE + 0x04, BIT(24), 0);
}

static int sunxi_de_ops_resize(struct udevice *dev)
{
	struct sunxi_de_priv *priv = dev_get_priv(dev);
	struct sunxi_de_plat *plat;

	if (!priv->mixer)
		return 0;

	plat = dev_get_plat(priv->mixer);
	if (plat && plat->resize)
		return plat->resize(priv->mixer);

	return 0;
}

int sunxi_de_attach_mixer(struct udevice *dev, struct udevice *mixer)
{
	struct sunxi_de_priv *priv = dev_get_priv(dev);
	struct sunxi_de_plat *plat = dev_get_plat(mixer);
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
	struct video_uc_plat *uc_plat = dev_get_uclass_plat(dev);

	priv->mixer = mixer;

	if (mixer && plat->attach)
		return plat->attach(priv->mixer, uc_plat->base,
				    uc_priv->line_length, 1 << uc_priv->bpix);

	return 0;
}

static int sunxi_de_probe(struct udevice *dev)
{
	struct video_priv *uc_priv = dev_get_uclass_priv(dev);
	struct udevice *child;

	/* Before relocation we don't need to do anything */
	if (!(gd->flags & GD_FLG_RELOC))
		return 0;

	uc_priv->xsize_max = uc_priv->xsize = LCD_MAX_WIDTH;
	uc_priv->ysize_max = uc_priv->ysize = LCD_MAX_HEIGHT;
	uc_priv->line_length =
		uc_priv->xsize_max * ((1 << LCD_MAX_LOG2_BPP) / 8);
	uc_priv->bpix = LCD_MAX_LOG2_BPP;

	if (ofnode_read_size(dev_ofnode(dev), "allwinner,sram") > 0)
		sunxi_de_acquire_sram();

	device_foreach_child_probe(child, dev);

	video_set_flush_dcache(dev, true);

	return 0;
}

static int sunxi_de_bind(struct udevice *dev)
{
	struct video_uc_plat *plat = dev_get_uclass_plat(dev);
	ofnode node;

	plat->size =
		LCD_MAX_WIDTH * LCD_MAX_HEIGHT * (1 << LCD_MAX_LOG2_BPP) / 8;

	ofnode_for_each_subnode(node, dev_ofnode(dev))
		lists_bind_fdt(dev, node, NULL, NULL, false);

	return 0;
}

static const struct video_ops sunxi_de_ops = {
	.resize = sunxi_de_ops_resize,
};

struct udevice_id sunxi_de_of_table[] = {
	{
		.compatible = "allwinner,sun50i-a64-de2",
		.data = 0,
	},
	{
		.compatible = "allwinner,sun50i-h616-de33",
		.data = 0,
	},
	{},
};

U_BOOT_DRIVER(sunxi_de) = {
	.name = "sunxi_de",
	.id = UCLASS_VIDEO,
	.of_match = sunxi_de_of_table,
	.ops = &sunxi_de_ops,
	.bind = sunxi_de_bind,
	.probe = sunxi_de_probe,
	.flags = DM_FLAG_PRE_RELOC,
	.priv_auto = sizeof(struct sunxi_de_priv),
};

#if 1
/*
 * Simplefb support.
 */
#if defined(CONFIG_OF_BOARD_SETUP) && defined(CONFIG_VIDEO_DT_SIMPLEFB)
int sunxi_simplefb_setup(void *blob)
{
	struct udevice *de;
	struct video_priv *de_priv;
	struct video_uc_plat *de_plat;
	int offset, ret;
	u64 start, size;
	const char *pipeline = NULL;

	/* Skip simplefb setting if DE is not present */
	ret = uclass_get_device_by_driver(UCLASS_VIDEO, DM_DRIVER_GET(sunxi_de),
					  &de);
	if (ret) {
		printf("DE not present\n");
		return 0;
	} else if (!device_active(de)) {
		printf("DE present but not probed\n");
		return 0;
	}

	// TODO:
	pipeline = "mixer0-lcd0";

	de_priv = dev_get_uclass_priv(de);
	de_plat = dev_get_uclass_plat(de);

	offset = sunxi_simplefb_fdt_match(blob, pipeline);
	if (offset < 0) {
		eprintf("Cannot setup simplefb: node not found\n");
		return 0; /* Keep older kernels working */
	}

	start = gd->bd->bi_dram[0].start;
	size = de_plat->base - start;
	ret = fdt_fixup_memory_banks(blob, &start, &size, 1);
	if (ret) {
		eprintf("Cannot setup simplefb: Error reserving memory\n");
		return ret;
	}

	ret = fdt_setup_simplefb_node(blob, offset, de_plat->base,
				      de_priv->xsize, de_priv->ysize,
				      de_priv->line_length, "x8r8g8b8");
	if (ret)
		eprintf("Cannot setup simplefb: Error setting properties\n");

	return ret;
}
#endif /* CONFIG_OF_BOARD_SETUP && CONFIG_VIDEO_DT_SIMPLEFB */

#endif
