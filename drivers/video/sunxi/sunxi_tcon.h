#ifndef __SUNXI_TCON_H
#define __SUNXI_TCON_H
#include <display.h>

int sunxi_tcon_get_display_timing(struct display_timing *timing);
int sunxi_tcon_apply_timing(const struct display_timing *timing);

#endif
