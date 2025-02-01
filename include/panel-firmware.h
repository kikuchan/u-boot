#include <dm.h>
#include <mipi_dsi.h>

/*
 * The display controller configuration can be stored in a firmware file,
 * under the firmware directory of the system.
 *
 * The name of the file is `panel/<firmware-name>.bin`, where the
 * 'firmware-name' should be defined as the property of the device
 * in the Device Tree, otherwise the first `compatible` string is used.
 *
 * File Layout:
 *     A binary file composed with the following data:
 *         <header>
 *         <config>
 *         <timings>
 *         <init-sequence>
 *
 * The 'header':
 *     A `struct panel_mipi_firmware_header`.
 *     The `file_format_version` must be `1`.
 *
 * The 'config':
 *     A `struct panel_mipi_firmware_config`.
 *     The values are in big-endian.
 *
 * The 'timings':
 *     An array of `struct panel_mipi_firmware_panel_timing`.
 *     Its length is `num_timings` in the config.
 *     The values are in big-endian.
 *
 * The 'init-sequence':
 *     MIPI commands to execute when the display pipeline is enabled.
 *     This is used to configure the display controller, and it may
 *     contains panel specific parameters as well.
 *
 *     The commands are encoded and stored in a byte array:
 *
 *         0x00             : sleep 10 ms
 *         0x01 <M>         : MIPI command <M> with no arguments
 *         0x02 <M> <a>     : MIPI command <M> with 1 argument <a>
 *         0x03 <M> <a> <b> : MIPI command <M> with 2 arguments <a> <b>
 *                :
 *         0x7f <M> <...>   : MIPI command <M> with 126 arguments <...>
 *         0x80             : sleep 100 ms
 *         0x81 - 0xff      : reserved
 *
 *     Example:
 *         command 0x11
 *         sleep 10ms
 *         command 0xb1 arguments 0x01 0x2c 0x2d
 *         command 0x29
 *         sleep 130ms
 *
 *     Byte sequence:
 *         0x01 0x11
 *         0x00
 *         0x04 0xb1 0x01 0x2c 0x2d
 *         0x01 0x29
 *         0x80 0x00 0x00 0x00
 *
 */

struct panel_firmware_header {
	u8 magic[15];
	u8 file_format_version; /* must be 1 */
} __packed;

struct panel_firmware_config {
	u16 width_mm, height_mm;
	u16 rotation;
	u8 _reserved_1[2];
	u8 _reserved_2[8];

	u16 reset_delay; /* delay after the reset command, in ms */
	u16 init_delay; /* delay for sending the initial command sequence, in ms */
	u16 sleep_delay; /* delay after the sleep command, in ms */
	u16 backlight_delay; /* delay for enabling the backlight, in ms */
	u16 _reserved_3[4];

	u16 dsi_lanes; /* unsigned int */
	u16 dsi_format; /* enum mipi_dsi_pixel_format */
	u32 dsi_mode_flags; /* unsigned long */
	u32 bus_flags; /* struct drm_bus_flags */
	u8 _reserved_4[2];
	u8 preferred_timing;
	u8 num_timings;
} __packed;

struct panel_firmware_panel_timing {
	u16 hactive;
	u16 hfp;
	u16 hslen;
	u16 hbp;

	u16 vactive;
	u16 vfp;
	u16 vslen;
	u16 vbp;

	u32 dclk; /* in kHz */
	u32 flags; /* include/drm/drm_modes.h DRM_MODE_FLAG_* */

	u8 _reserved_1[8];
} __packed;

struct panel_config {
	unsigned int width_mm, height_mm;
	unsigned int rotation;

	unsigned int reset_delay; /* delay after the reset command, in ms */
	unsigned int init_delay; /* delay before the initial commands, in ms */
	unsigned int sleep_delay; /* delay after the sleep command, in ms */
	unsigned int backlight_delay; /* delay for enabling the backlight, in ms */

	unsigned int dsi_lanes;
	enum mipi_dsi_pixel_format dsi_format;
	unsigned int dsi_mode_flags;

	unsigned int bus_flags;
};

struct panel_commands {
	u8 *data;
	size_t size;
};

struct panel_firmware {
	void *buffer;
	size_t size;
};

int panel_firmware_validate(struct panel_firmware *firmware);
int panel_firmware_read_config(struct panel_firmware *firmware,
			       struct panel_config *out);
int panel_firmware_read_display_timing(struct panel_firmware *firmware,
				       struct display_timing *out);
int panel_firmware_read_commands(struct panel_firmware *firmware,
				 struct panel_commands *commands);

struct panel_firmware *panel_firmware_open(const u8 *filename);
void panel_firmware_close(struct panel_firmware *firmware);

int panel_commands_set(struct panel_commands *cmds, const u8 *buffer, ulong size);
