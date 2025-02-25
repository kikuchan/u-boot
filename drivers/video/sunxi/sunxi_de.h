struct sunxi_de_plat {
	int (*attach)(struct udevice *dev, ulong base, int stride, int bpp);
	int (*resize)(struct udevice *dev);
};

int sunxi_de_attach_mixer(struct udevice *dev, struct udevice *mixer);
