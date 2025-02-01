#include <dm/device_compat.h>
#include <fs.h>
#include <env.h>
#include <panel-firmware.h>

static const u8 panel_firmware_magic[15] = {
	0x50, 0x41, 0x4e, 0x45, 0x4c, 0x2d, 0x46, 0x49,
	0x52, 0x4d, 0x57, 0x41, 0x52, 0x45, 0x00,
};

struct display_timing timing;

#define DRM_MODE_FLAG_PHSYNC (1 << 0)
#define DRM_MODE_FLAG_NHSYNC (1 << 1)
#define DRM_MODE_FLAG_PVSYNC (1 << 2)
#define DRM_MODE_FLAG_NVSYNC (1 << 3)
#define DRM_MODE_FLAG_INTERLACE (1 << 4)
#define DRM_MODE_FLAG_DBLSCAN (1 << 5)
#define DRM_MODE_FLAG_DBLCLK (1 << 12)

#define DRM_BUS_FLAG_DE_LOW (1 << 0)
#define DRM_BUS_FLAG_DE_HIGH (1 << 1)
#define DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE (1 << 2)
#define DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE (1 << 3)

int panel_firmware_validate(struct panel_firmware *firmware)
{
	struct panel_firmware_header *hdr = firmware->buffer;

	if (IS_ERR(firmware))
		return -EINVAL;

	if (firmware->size < sizeof(*hdr))
		return -EINVAL;

	if (memcmp(hdr->magic, panel_firmware_magic, sizeof(hdr->magic)))
		return -EINVAL;

	if (hdr->file_format_version != 0x01)
		return -EINVAL;

	return 0;
}

int panel_firmware_read_config(struct panel_firmware *firmware,
			       struct panel_config *out)
{
	struct panel_firmware_header *hdr = firmware->buffer;
	struct panel_firmware_config *config = firmware->buffer + sizeof(*hdr);

	if (IS_ERR(firmware) || !out)
		return -EINVAL;

	out->width_mm = be16_to_cpu(config->width_mm);
	out->height_mm = be16_to_cpu(config->height_mm);
	out->rotation = be16_to_cpu(config->rotation);

	out->reset_delay = be16_to_cpu(config->reset_delay);
	out->init_delay = be16_to_cpu(config->init_delay);
	out->sleep_delay = be16_to_cpu(config->sleep_delay);
	out->backlight_delay = be16_to_cpu(config->backlight_delay);

	out->dsi_lanes = be16_to_cpu(config->dsi_lanes);
	out->dsi_format = be16_to_cpu(config->dsi_format);
	out->dsi_mode_flags = be32_to_cpu(config->dsi_mode_flags);

	out->bus_flags = be32_to_cpu(config->bus_flags);

	return 0;
}

int panel_firmware_read_display_timing(struct panel_firmware *firmware,
				       struct display_timing *out)
{
	struct panel_firmware_header *hdr = firmware->buffer;
	struct panel_firmware_config *config = firmware->buffer + sizeof(*hdr);
	struct panel_firmware_panel_timing *timing =
		firmware->buffer + sizeof(*hdr) + sizeof(*config);
	u32 flags;
	u8 idx;

	if (IS_ERR(firmware) || !out)
		return -EINVAL;

	idx = config->preferred_timing;

	if (idx >= config->num_timings)
		return -ENOENT;

	out->pixelclock.typ = be32_to_cpu(timing[idx].dclk) * 1000;
	out->hactive.typ = be16_to_cpu(timing[idx].hactive);
	out->hfront_porch.typ = be16_to_cpu(timing[idx].hfp);
	out->hback_porch.typ = be16_to_cpu(timing[idx].hbp);
	out->hsync_len.typ = be16_to_cpu(timing[idx].hslen);
	out->vactive.typ = be16_to_cpu(timing[idx].vactive);
	out->vfront_porch.typ = be16_to_cpu(timing[idx].vfp);
	out->vback_porch.typ = be16_to_cpu(timing[idx].vbp);
	out->vsync_len.typ = be16_to_cpu(timing[idx].vslen);

	flags = be32_to_cpu(timing[idx].flags);

	out->flags = 0;
	if (flags & DRM_MODE_FLAG_PHSYNC)
		out->flags |= DISPLAY_FLAGS_HSYNC_HIGH;
	if (flags & DRM_MODE_FLAG_NHSYNC)
		out->flags |= DISPLAY_FLAGS_HSYNC_LOW;
	if (flags & DRM_MODE_FLAG_PVSYNC)
		out->flags |= DISPLAY_FLAGS_VSYNC_HIGH;
	if (flags & DRM_MODE_FLAG_NVSYNC)
		out->flags |= DISPLAY_FLAGS_VSYNC_LOW;
	if (flags & DRM_MODE_FLAG_INTERLACE)
		out->flags |= DISPLAY_FLAGS_INTERLACED;
	if (flags & DRM_MODE_FLAG_DBLSCAN)
		out->flags |= DISPLAY_FLAGS_DOUBLESCAN;
	if (flags & DRM_MODE_FLAG_DBLCLK)
		out->flags |= DISPLAY_FLAGS_DOUBLECLK;

	flags = be32_to_cpu(config->bus_flags);

	if (flags & DRM_BUS_FLAG_DE_LOW)
		out->flags |= DISPLAY_FLAGS_DE_LOW;
	if (flags & DRM_BUS_FLAG_DE_HIGH)
		out->flags |= DISPLAY_FLAGS_DE_HIGH;
	if (flags & DRM_BUS_FLAG_PIXDATA_DRIVE_POSEDGE)
		out->flags |= DISPLAY_FLAGS_PIXDATA_POSEDGE;
	if (flags & DRM_BUS_FLAG_PIXDATA_DRIVE_NEGEDGE)
		out->flags |= DISPLAY_FLAGS_PIXDATA_NEGEDGE;

	return 0;
}

int panel_commands_set(struct panel_commands *cmds, const u8 *buffer,
		       ulong size)
{
	if (cmds->data)
		free(cmds->data);

	cmds->size = size;
	cmds->data = malloc(cmds->size);
	if (!cmds->data)
		return -ENOMEM;

	// TODO: validation
	memcpy(cmds->data, buffer, size);

	return 0;
}

int panel_firmware_read_commands(struct panel_firmware *firmware,
				 struct panel_commands *cmds)
{
	struct panel_firmware_header *hdr = firmware->buffer;
	struct panel_firmware_config *config = firmware->buffer + sizeof(*hdr);
	struct panel_firmware_panel_timing *timing =
		firmware->buffer + sizeof(*hdr) + sizeof(*config);
	ulong offset;

	if (!cmds || IS_ERR(firmware))
		return -EINVAL;

	offset = sizeof(*hdr) + sizeof(*config) +
		 config->num_timings * sizeof(*timing);

	if (firmware->size < offset)
		return -EINVAL;

	if (firmware->size == offset)
		return -ENODATA;

	return panel_commands_set(cmds, firmware->buffer + offset,
				  firmware->size - offset);
}

struct panel_firmware *panel_firmware_open(const u8 *fwname)
{
	struct panel_firmware *firmware;
	const char *ifname = env_get("panelfwifname");
	const char *devpart = env_get("panelfwdevpart");
	const char *fwdir = env_get("panelfwdir");
	const char *fwsuffix = env_get("panelfwsuffix");
	char filename[1024];
	u8 *buffer;
	loff_t size, len = 0;
	int err;

	if (!ifname)
		ifname = "mmc";
	if (!devpart)
		devpart = "0";
	if (!fwdir)
		fwdir = "/usr/lib/firmware/panels/";
	if (!fwsuffix)
		fwsuffix = ".panel";

	err = fs_set_blk_dev(ifname, devpart, FS_TYPE_ANY);
	if (err)
		return ERR_PTR(err);

	snprintf(filename, sizeof(filename), "%s%s%s", fwdir, fwname, fwsuffix);

	pr_info("panel-firmware: Try loading %s %s %s\n", ifname, devpart, filename);

	err = fs_size(filename, &size);
	if (err)
		return ERR_PTR(err);

	buffer = malloc(size);
	if (!buffer)
		return ERR_PTR(-ENOMEM);

	fs_set_blk_dev(ifname, devpart, FS_TYPE_ANY);
	err = fs_read(filename, (ulong)buffer, 0, size, &len);
	if (err)
		goto err;

	firmware = malloc(sizeof(*firmware));
	if (!firmware) {
		err = -ENOMEM;
		goto err;
	}

	firmware->size = size;
	firmware->buffer = buffer;

	pr_info("panel-firmware: %s: Loaded.\n", fwname);

	return firmware;

err:
	free(buffer);
	return ERR_PTR(err);
}

void panel_firmware_close(struct panel_firmware *firmware)
{
	if (!firmware || IS_ERR(firmware))
		return;

	free(firmware->buffer);
	free(firmware);
}
