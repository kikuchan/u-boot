// SPDX-License-Identifier: GPL-2.0+
/*
 * Generic MIPI-DSI/DPI(+SPI) Panel Driver
 *
 * Supported panels:
 * - A generic MIPI-DSI panel which implements basic DCS
 * - A generic MIPI-DPI panel which implements basic DCS over SPI
 *
 * Copyright (C) 2025, Hironori KIKUCHI <kikuchan98@gmail.com>
 */

#include <asm/gpio.h>
#include <backlight.h>
#include <dm/device_compat.h>
#include <env.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <log.h>
#include <mipi_dsi.h>
#include <panel-firmware.h>
#include <panel.h>
#include <power/regulator.h>
#include <spi.h>

struct mipi_panel_priv {
	struct udevice *power_reg;
	struct udevice *io_reg;
	struct udevice *backlight;
	struct gpio_desc reset;

	struct panel_config config;
	struct display_timing timing;
	struct panel_commands commands;

	int display_id;
};

static void mipi_spi_write9(struct udevice *dev, u16 data)
{
#ifdef CONFIG_SPI
	u8 buf[2] = { data >> 1, data << 7 };

	dm_spi_xfer(dev, 9, buf, NULL, SPI_XFER_BEGIN | SPI_XFER_END);
#endif
}

static u8 mipi_spi_read8(struct udevice *dev)
{
#ifdef CONFIG_SPI
	u8 buf[1];

	// It's 8-bit when read-in
	dm_spi_xfer(dev, 8, NULL, buf, SPI_XFER_BEGIN | SPI_XFER_END);
	return buf[0];
#else
	return 0;
#endif
}

static int mipi_spi_dcs_write_buffer(struct udevice *dev, const u8 *data,
				     ulong len)
{
	if (len == 0)
		return -EINVAL;

	// Command
	mipi_spi_write9(dev, *data++);

	// Data
	while (--len > 0)
		mipi_spi_write9(dev, 0x100 | *data++);

	return 0;
}

static int mipi_spi_dcs_read(struct udevice *dev, u8 cmd, u8 *data, ulong len)
{
	if (len == 0)
		return -EINVAL;

	// Command
	mipi_spi_write9(dev, cmd);

	// Data receive
	while (len-- > 0)
		*data++ = mipi_spi_read8(dev);

	return 0;
}

static int mipi_dcs_write_buffer(struct udevice *dev, const u8 *data, ulong len)
{
	struct mipi_dsi_panel_plat *dsi = dev_get_plat(dev);
	int ret;

	if (!dsi)
		return mipi_spi_dcs_write_buffer(dev, data, len);

#ifdef CONFIG_VIDEO_MIPI_DSI
	ret = mipi_dsi_dcs_write_buffer(dsi->device, data, len);

	return ret < 0 ? ret : 0;
#else
	return -ENOSYS;
#endif
}

static int mipi_dcs_read_buffer(struct udevice *dev, u8 cmd, u8 *data,
				ulong len)
{
	struct mipi_dsi_panel_plat *dsi = dev_get_plat(dev);
	int ret;

	if (!dsi)
		return mipi_spi_dcs_read(dev, cmd, data, len);

#ifdef CONFIG_VIDEO_MIPI_DSI
	ret = mipi_dsi_set_maximum_return_packet_size(dsi->device, len);
	if (ret)
		return ret;

	ret = mipi_dsi_dcs_read(dsi->device, cmd, data, len);

	return ret < 0 ? ret : 0;
#else
	return -ENOSYS;
#endif
}

static int mipi_dcs_write(struct udevice *dev, u8 cmd)
{
	return mipi_dcs_write_buffer(dev, &cmd, 1);
}

/*
 * The commands are encoded and stored in a byte array:
 *
 *     0x00             : sleep 10 ms
 *     0x01 <M>         : MIPI command <M> with no arguments
 *     0x02 <M> <a>     : MIPI command <M> with 1 argument <a>
 *     0x03 <M> <a> <b> : MIPI command <M> with 2 arguments <a> <b>
 *            :
 *     0x7f <M> <...>   : MIPI command <M> with 126 arguments <...>
 *     0x80             : sleep 100 ms
 *     0x81 - 0xff      : reserved
 *
 * Example:
 *     command 0x11
 *     sleep 10ms
 *     command 0xb1 arguments 0x01 0x2c 0x2d
 *     command 0x29
 *     sleep 130ms
 *
 * Byte sequence:
 *     0x01 0x11
 *     0x00
 *     0x04 0xb1 0x01 0x2c 0x2d
 *     0x01 0x29
 *     0x80 0x00 0x00 0x00
 */

static int mipi_dcs_write_commands(struct udevice *dev, const u8 *data,
				   ulong size)
{
	ulong i = 0;
	int err;

	while (i < size) {
		u8 inst = data[i++];
		bool ext = (inst & 0x80) ? true : false;
		u8 len = inst & 0x7f;

		if (len == 0x00) {
			mdelay(ext ? 100 : 10);
			continue;
		}

		err = mipi_dcs_write_buffer(dev, data + i, len);
		if (err)
			return err;

		i += len;
	}

	return 0;
}

static void mipi_panel_reset(struct udevice *dev)
{
	struct mipi_panel_priv *priv = dev_get_priv(dev);

	dm_gpio_set_value(&priv->reset, true);
	mdelay(priv->config.reset_delay);
	dm_gpio_set_value(&priv->reset, false);
	mdelay(priv->config.init_delay);
}

static void mipi_dcs_sleep_out(struct udevice *dev)
{
	struct mipi_panel_priv *priv = dev_get_priv(dev);

	mipi_dcs_write(dev, 0x11);
	mdelay(priv->config.sleep_delay);
}

static void mipi_dcs_sleep_in(struct udevice *dev)
{
	mipi_dcs_write(dev, 0x10);
}

static void mipi_dcs_display_on(struct udevice *dev)
{
	mipi_dcs_write(dev, 0x29);
}

static void mipi_dcs_display_off(struct udevice *dev)
{
	mipi_dcs_write(dev, 0x28);
}

static int mipi_panel_read_display_id(struct udevice *dev)
{
	u8 id[3] = {};
	int err;

	err = mipi_dcs_read_buffer(dev, 0x04, id, sizeof(id));
	if (err)
		return err;

	return (id[0] << 16) | (id[1] << 8) | (id[2]);
}

static int mipi_panel_enable(struct udevice *dev)
{
	int err;
	struct mipi_panel_priv *priv = dev_get_priv(dev);
#ifdef CONFIG_VIDEO_MIPI_DSI
	struct mipi_dsi_panel_plat *dsi = dev_get_plat(dev);

	if (dsi && dsi->device) {
		int err = mipi_dsi_attach(dsi->device);
		if (err)
			return err;
	}
#endif

	mipi_panel_reset(dev);

	priv->display_id = mipi_panel_read_display_id(dev);
	if (priv->display_id >= 0) {
		dev_info(dev, "Display ID: 0x%06x\n", priv->display_id);
	} else {
		dev_warn(dev, "Display ID: Unknown: %d\n", priv->display_id);
	}

	if (!priv->timing.hactive.typ || !priv->timing.vactive.typ) {
		dev_info(dev, "Display is not ready yet\n");
		return -EINVAL; /* Not ready */
	}

	if (priv->commands.data) {
		err = mipi_dcs_write_commands(dev, priv->commands.data,
					      priv->commands.size);
		if (err)
			dev_warn(dev, "Cannot initialize the panel %d\n", err);
	}

	mipi_dcs_sleep_out(dev);
	mipi_dcs_display_on(dev);

	if (priv->backlight) {
		mdelay(priv->config.backlight_delay);

		backlight_enable(priv->backlight);
		backlight_set_brightness(priv->backlight, BACKLIGHT_DEFAULT);
	}

	return 0;
}

static int mipi_panel_disable(struct udevice *dev)
{
	struct mipi_panel_priv *priv = dev_get_priv(dev);
#ifdef CONFIG_VIDEO_MIPI_DSI
	struct mipi_dsi_panel_plat *dsi = dev_get_plat(dev);
#endif

	mipi_dcs_display_off(dev);
	mipi_dcs_sleep_in(dev);

	if (priv->backlight)
		backlight_set_brightness(priv->backlight, BACKLIGHT_OFF);

#ifdef CONFIG_VIDEO_MIPI_DSI
	if (dsi && dsi->device)
		return mipi_dsi_detach(dsi->device);
#endif

	priv->display_id = -1;

	return 0;
}

static int mipi_panel_get_display_timing(struct udevice *dev,
					 struct display_timing *timing)
{
	struct mipi_panel_priv *priv = dev_get_priv(dev);

	*timing = priv->timing;

	return 0;
}

int mipi_panel_set_mode(struct udevice *dev,
			const struct display_timing *timing)
{
	struct mipi_panel_priv *priv = dev_get_priv(dev);

	priv->timing = *timing;

	return 0;
}

int mipi_panel_load_firmware_addr(struct udevice *dev, void *addr, ulong size)
{
	struct mipi_panel_priv *priv = dev_get_priv(dev);
	struct panel_firmware firmware = { addr, size };
	int err;

	err = panel_firmware_validate(&firmware);
	if (err)
		return err;

	panel_firmware_read_config(&firmware, &priv->config);
	panel_firmware_read_display_timing(&firmware, &priv->timing);
	panel_firmware_read_commands(&firmware, &priv->commands);

	return 0;
}

int mipi_panel_load_firmware_file(struct udevice *dev, const char *filename)
{
	struct panel_firmware *firmware;
	int err = 0;

	if (!filename)
		return -EINVAL;

	firmware = panel_firmware_open(filename);
	if (IS_ERR(firmware))
		return PTR_ERR(firmware);

	err = mipi_panel_load_firmware_addr(dev, firmware->buffer,
					    firmware->size);

	panel_firmware_close(firmware);

	return err;
}

static int mipi_panel_get_rotation(struct udevice *dev)
{
	struct mipi_panel_priv *priv = dev_get_priv(dev);

	return priv->config.rotation;
}

int mipi_panel_get_display_id(struct udevice *dev)
{
	struct mipi_panel_priv *priv = dev_get_priv(dev);

	return priv->display_id;
}

static int mipi_panel_of_to_plat(struct udevice *dev)
{
	struct mipi_panel_priv *priv = dev_get_priv(dev);
	const char *fwname = env_get("panelfwname");
	ulong addr = env_get_hex("panelfwaddr", 0);
	ulong size = env_get_hex("panelfwsize", 0);
	int err;

	if (CONFIG_IS_ENABLED(DM_REGULATOR)) {
		err = uclass_get_device_by_phandle(UCLASS_REGULATOR, dev,
						   "power-supply",
						   &priv->power_reg);
		if (err) {
			dev_err(dev, "Warning: cannot get power supply: %d\n",
				err);
			if (err != -ENOENT)
				return err;
		}

		err = uclass_get_device_by_phandle(UCLASS_REGULATOR, dev,
						   "io-supply", &priv->io_reg);
		if (err && err != -ENOENT) {
			dev_err(dev, "Warning: cannot get io supply: %d\n",
				err);
			return err;
		}
	}

	err = uclass_get_device_by_phandle(UCLASS_PANEL_BACKLIGHT, dev,
					   "backlight", &priv->backlight);
	if (err) {
		dev_err(dev, "Warning: cannot get backlight: %d\n", err);
		if (err != -ENOENT)
			return log_ret(err);
	}

	err = gpio_request_by_name(dev, "reset-gpios", 0, &priv->reset,
				   GPIOD_IS_OUT);
	if (err) {
		dev_err(dev, "Warning: cannot get reset GPIO: %d\n", err);
		if (err != -ENOENT)
			return log_ret(err);
	}

	/* Default values */
	priv->config.reset_delay = 1;
	priv->config.init_delay = 10;
	priv->config.sleep_delay = 120;
	priv->config.backlight_delay = 120;

	// Load firmware (ignore errors)
	if (addr && size)
		mipi_panel_load_firmware_addr(dev, (void *)addr, size);
	else
		mipi_panel_load_firmware_file(
			dev,
			fwname ? fwname : dev_read_string(dev, "compatible"));

	return 0;
}

static int mipi_panel_init(struct udevice *dev)
{
	struct mipi_panel_priv *priv = dev_get_priv(dev);
	int ret;

	if (priv->power_reg) {
		ret = regulator_set_enable_if_allowed(priv->power_reg, true);
		if (ret && ret != -ENOSYS) {
			dev_err(dev,
				"Failed to enable power regulator '%s' %d\n",
				priv->power_reg->name, ret);
			return ret;
		}
	}
	if (priv->io_reg) {
		ret = regulator_set_enable_if_allowed(priv->io_reg, true);
		if (ret && ret != -ENOSYS) {
			dev_err(dev, "Failed to enable io regulator '%s' %d\n",
				priv->io_reg->name, ret);
			return ret;
		}
	}

	priv->display_id = -1;

	return 0;
}

#ifdef CONFIG_SPI
static int mipi_dpi_spi_panel_probe(struct udevice *dev)
{
	int ret;

	ret = dm_spi_claim_bus(dev);
	if (ret) {
		dev_err(dev, "Failed to claim SPI bus: %d\n", ret);
		return ret;
	}

	return mipi_panel_init(dev);
}
#endif

#ifdef CONFIG_VIDEO_MIPI_DSI
static int mipi_dsi_panel_probe(struct udevice *dev)
{
	struct mipi_panel_priv *priv = dev_get_priv(dev);
	struct mipi_dsi_panel_plat *dsi = dev_get_plat(dev);

	/* fill characteristics of DSI data link */
	dsi->lanes = priv->config.dsi_lanes;
	dsi->format = priv->config.dsi_format;
	dsi->mode_flags = priv->config.dsi_mode_flags;

	if (!dsi->lanes) {
		dev_err(dev, "No DSI lane is defined\n");
		return -EINVAL;
	}

	return mipi_panel_init(dev);
}
#endif

static const struct panel_ops mipi_panel_ops = {
	.enable = mipi_panel_enable,
	.disable = mipi_panel_disable,
	.get_display_timing = mipi_panel_get_display_timing,
	.get_rotation = mipi_panel_get_rotation,
};

#ifdef CONFIG_SPI
static const struct udevice_id mipi_dpi_spi_panel_ids[] = {
	{ .compatible = "panel-mipi-dpi-spi" },
	{}
};

U_BOOT_DRIVER(mipi_panel) = {
	.name = "mipi_dpi_spi_panel",
	.id = UCLASS_PANEL,
	.of_match = mipi_dpi_spi_panel_ids,
	.ops = &mipi_panel_ops,
	.of_to_plat = mipi_panel_of_to_plat,
	.probe = mipi_dpi_spi_panel_probe,
	.priv_auto = sizeof(struct mipi_panel_priv),
};
#endif

#ifdef CONFIG_VIDEO_MIPI_DSI
static const struct udevice_id mipi_dsi_panel_ids[] = {
	{ .compatible = "panel-mipi-dsi" },
	{}
};

U_BOOT_DRIVER(mipi_dsi_panel) = {
	.name = "mipi_dsi_panel",
	.id = UCLASS_PANEL,
	.of_match = mipi_dsi_panel_ids,
	.ops = &mipi_panel_ops,
	.of_to_plat = mipi_panel_of_to_plat,
	.probe = mipi_dsi_panel_probe,
	.plat_auto = sizeof(struct mipi_dsi_panel_plat),
	.priv_auto = sizeof(struct mipi_panel_priv),
};
#endif
