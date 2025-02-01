#ifndef MIPI_SPI_PANEL_H
#define MIPI_SPI_PANEL_H

int mipi_panel_set_mode(struct udevice *dev, struct display_timing *timing);
int mipi_panel_get_display_id(struct udevice *dev);
int mipi_panel_load_firmware_addr(struct udevice *dev, void *addr, ulong size);
int mipi_panel_load_firmware_file(struct udevice *dev, const char *filename);

#endif
