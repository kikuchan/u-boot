// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Amarula Solutions.
 * Author: Jagan Teki <jagan@amarulasolutions.com>
 */

#include <clk-uclass.h>
#include <dm.h>
#include <dm/devres.h>
#include <errno.h>
#include <log.h>
#include <reset.h>
#include <asm/io.h>
#include <clk/sunxi.h>
#include <dm/device-internal.h>
#include <linux/bitops.h>
#include <linux/log2.h>

extern U_BOOT_DRIVER(sunxi_reset);

static int sunxi_clk_update_muldiv_by(struct sunxi_clk_bitinfo *muldiv,
				      u32 value, uint shift, uint *mul,
				      uint *div)
{
	u32 v = (value >> shift) & ((1 << muldiv->width) - 1);

	switch (muldiv->flags) {
	case SUNXI_CLK_BITINFO_MUL:
		*mul *= v + 1;
		break;
	case SUNXI_CLK_BITINFO_DIV:
		*div *= v + 1;
		break;
	case SUNXI_CLK_BITINFO_DIVEXP:
		*div *= (1 << v);
		break;
	default:
		return -EINVAL;
	}

	if (muldiv->min >= 0 && muldiv->min > v)
		return -ERANGE;

	if (muldiv->max >= 0 && muldiv->max < v)
		return -ERANGE;

	return 0;
}

static ulong sunxi_clk_apply_muldiv(const struct ccu_clk_gate *gate,
				    ulong source, u32 value)
{
	uint mul = 1;
	uint div = 1;
	int err = 0;

	for (int i = 0; i < gate->num_muldiv; i++) {
		struct sunxi_clk_bitinfo *muldiv = gate->muldiv[i];

		switch (sunxi_clk_update_muldiv_by(muldiv, value, muldiv->shift,
						   &mul, &div)) {
		case -EINVAL:
			return -EINVAL;
		case -ERANGE:
			err = -ERANGE;
			break;
		default:
			break;
		}
	}

	return source * mul / div;
}

static int sunxi_clk_find_best_reg_value(const struct ccu_clk_gate *gate,
					 ulong target, ulong source, u32 *value,
					 u32 *mask)
{
	int err;
	uint mul, div;
	uint shift = 0;
	ulong best_delta = ULONG_MAX;
	ulong best_i = 0;
	ulong delta;
	ulong N = 1;

	for (int i = 0; i < gate->num_muldiv; i++)
		N *= 1 << gate->muldiv[i]->width;

	for (ulong i = 0; i < N; i++) {
		mul = 1;
		div = 1;
		shift = 0;
		for (int j = 0; j < gate->num_muldiv; j++) {
			struct sunxi_clk_bitinfo *muldiv = gate->muldiv[j];

			err = sunxi_clk_update_muldiv_by(muldiv, i, shift, &mul,
							 &div);
			if (err)
				goto skip;

			shift += muldiv->width;
		}

		delta = abs(target / mul - source / div) * mul;
		if (delta < best_delta) {
			best_delta = delta;
			best_i = i;
		}
skip:;
	}

	shift = 0;
	*value = 0;
	*mask = 0;
	for (int j = 0; j < gate->num_muldiv; j++) {
		int v = (best_i >> shift) & ((1 << gate->muldiv[j]->width) - 1);
		*value |= v << gate->muldiv[j]->shift;
		*mask |= ((1 << gate->muldiv[j]->width) - 1)
			 << gate->muldiv[j]->shift;
		shift += gate->muldiv[j]->width;
	}

	return 0;
}

static inline const struct ccu_clk_gate *clk_to_gate(struct clk *clk)
{
	struct ccu_plat *plat = dev_get_plat(clk->dev);

	if (clk->id < plat->desc->num_gates)
		return &plat->desc->gates[clk->id];

	return NULL;
}

static inline void *clk_to_reg(struct clk *clk)
{
	const struct ccu_clk_gate *gate = clk_to_gate(clk);
	struct ccu_plat *plat = dev_get_plat(clk->dev);

	if (gate && (gate->flags & CCU_CLK_F_IS_VALID) && gate->off >= 0)
		return plat->base + gate->off;

	return NULL;
}

static int sunxi_clk_ops_request(struct clk *clk);
static ulong sunxi_clk_get_parent_rate(struct clk *clk);

int sunxi_clk_set_mux(struct clk *clk, uint mux)
{
	const struct ccu_clk_gate *gate = clk_to_gate(clk);

	if (gate->mux.shift < 0)
		return -EINVAL;

	if (gate->mux.count <= mux)
		return -EINVAL;

	clrsetbits_32(clk_to_reg(clk),
		      GENMASK(gate->mux.shift + gate->mux.width - 1,
			      gate->mux.shift),
		      mux << gate->mux.shift);

	return 0;
}

int sunxi_clk_get_mux(struct clk *clk)
{
	const struct ccu_clk_gate *gate = clk_to_gate(clk);
	u32 value;

	if (gate->mux.shift < 0)
		return 0;

	value = readl(clk_to_reg(clk));

	return (value >> gate->mux.shift) & ((1 << gate->mux.width) - 1);
}

static struct clk *sunxi_clk_get(struct udevice *clkdev, int id)
{
	struct ccu_plat *plat = dev_get_plat(clkdev);
	struct clk *clk;

	if (id < plat->desc->num_gates && plat->clocks[id])
		return plat->clocks[id];

	clk = devm_kzalloc(clkdev, sizeof(*clk), GFP_KERNEL);
	if (unlikely(!clk))
		return NULL;

	clk->dev = clkdev;
	clk->id = id;
	clk->data = 0;

	plat->clocks[id] = clk;

	sunxi_clk_ops_request(clk);

	return clk;
}
static inline struct clk **clk_to_parents(struct clk *clk)
{
	return (struct clk **)clk->data;
}

struct clk *sunxi_clk_get_parent(struct clk *clk)
{
	const struct ccu_clk_gate *gate = clk_to_gate(clk);
	struct clk **parents = clk_to_parents(clk);

	// MUX
	if (gate && (gate->flags & CCU_CLK_F_IS_VALID) && gate->mux.count > 0) {
		int mux = sunxi_clk_get_mux(clk);

		if (mux < gate->mux.count)
			return parents[mux];
	}

	return NULL;
}

static int sunxi_clk_set_gate(struct clk *clk, bool on)
{
	const struct ccu_clk_gate *gate = clk_to_gate(clk);
	void *reg = clk_to_reg(clk);
	struct clk *parent;

	if (!gate || !(gate->flags & CCU_CLK_F_IS_VALID)) {
		log_warning("%s.%03ld: unhandled\n", clk->dev->name, clk->id);
		return 0;
	}

	if (on && (parent = sunxi_clk_get_parent(clk)))
		clk_enable(parent);

	if (gate->off < 0)
		return 0; /* No gate */

	clrsetbits_32(reg, gate->bit, on ? gate->bit : 0);

	return 0;
}

static bool sunxi_clk_has_muldiv(struct clk *clk)
{
	const struct ccu_clk_gate *gate = clk_to_gate(clk);

	return gate && gate->num_muldiv > 0;
}

static ulong sunxi_clk_get_rate(struct clk *clk)
{
	const struct ccu_clk_gate *gate = clk_to_gate(clk);
	ulong rate = sunxi_clk_get_parent_rate(clk);
	u32 val;
	int fixed_div = gate->fixed_div > 0 ? gate->fixed_div : 1;

	if (sunxi_clk_has_muldiv(clk)) {
		val = readl(clk_to_reg(clk));
		return sunxi_clk_apply_muldiv(gate, rate, val) / fixed_div;
	}

	return rate / fixed_div;
}

static ulong sunxi_clk_get_parent_rate(struct clk *clk)
{
	struct clk *parent;

	parent = sunxi_clk_get_parent(clk);
	if (parent)
		return clk_get_rate(parent);

	return 0;
}

static ulong sunxi_clk_set_rate_common(struct clk *clk, ulong rate, bool set)
{
	const struct ccu_clk_gate *gate = clk_to_gate(clk);
	struct clk *parent;
	ulong parent_rate;
	u32 value, mask;
	int fixed_div = gate->fixed_div > 0 ? gate->fixed_div : 1;

	if (!sunxi_clk_has_muldiv(clk)) {
		parent = sunxi_clk_get_parent(clk);
		if (!parent) {
			if (set)
				log_warning(
					"Set rate on a simple gate: %s.%03ld, rate = %ld",
					clk->dev->name, clk->id, rate);
			return 0;
		}
		return (set ? clk_set_rate : clk_round_rate)(parent,
							     rate * fixed_div) /
		       fixed_div;
	}

	parent_rate = sunxi_clk_get_parent_rate(clk);

	sunxi_clk_find_best_reg_value(gate, rate * fixed_div, parent_rate,
				      &value, &mask);
	if (set)
		clrsetbits_32(clk_to_reg(clk), mask, value);

	return sunxi_clk_apply_muldiv(gate, parent_rate, value) / fixed_div;
}

bool sunxi_clk_is_sunxi_dev(struct udevice *dev)
{
	return dev_get_driver_ops(dev) == &sunxi_clk_ops;
}

static ulong sunxi_clk_ops_round_rate(struct clk *clk, ulong rate)
{
	return sunxi_clk_set_rate_common(clk, rate, false);
}

static ulong sunxi_clk_ops_set_rate(struct clk *clk, ulong rate)
{
	return sunxi_clk_set_rate_common(clk, rate, true);
}

static ulong sunxi_clk_ops_get_rate(struct clk *clk)
{
	return sunxi_clk_get_rate(clk);
}

static int sunxi_clk_ops_enable(struct clk *clk)
{
	return sunxi_clk_set_gate(clk, true);
}

static int sunxi_clk_ops_disable(struct clk *clk)
{
	return sunxi_clk_set_gate(clk, false);
}

static int sunxi_clk_ops_request(struct clk *clk)
{
	struct ccu_plat *plat = dev_get_plat(clk->dev);
	const struct ccu_clk_gate *gate = clk_to_gate(clk);
	struct clk *parent;

	if (gate->mux.count > 0) {
		struct clk **parents = devm_kcalloc(clk->dev, gate->mux.count,
						    sizeof(struct clk *),
						    GFP_KERNEL);

		for (int i = 0; i < gate->mux.count; i++) {
			if (gate->mux.parents[i] >
			    CLKREF(plat->desc->num_gates)) {
				/* self reference */
				parent = sunxi_clk_get(
					clk->dev,
					CLKUNREF(gate->mux.parents[i]));
				if (!parent) {
					log_err("Can't get the parent clock: %ld",
						CLKUNREF(gate->mux.parents[i]));
					parent = NULL;
					//return -EINVAL;
				}
			} else {
				/* external reference */
				parent = devm_clk_get(clk->dev,
						      gate->mux.parents[i]);
				if (IS_ERR(parent)) {
					log_err("Can't get the parent clock: %s",
						gate->mux.parents[i]);
					parent = NULL;
					//return -EINVAL;
				}
			}

			parents[i] = parent;
		}

		clk->data = (ulong)parents;
	}

	return 0;
}

struct clk_ops sunxi_clk_ops = {
	.request = sunxi_clk_ops_request,
	.round_rate = sunxi_clk_ops_round_rate,
	.set_rate = sunxi_clk_ops_set_rate,
	.get_rate = sunxi_clk_ops_get_rate,
	.enable = sunxi_clk_ops_enable,
	.disable = sunxi_clk_ops_disable,
};

static int sunxi_clk_bind(struct udevice *dev)
{
	/* Reuse the platform data for the reset driver. */
	return device_bind(dev, DM_DRIVER_REF(sunxi_reset), "reset",
			   dev_get_plat(dev), dev_ofnode(dev), NULL);
}

static int sunxi_clk_probe(struct udevice *dev)
{
	struct clk_bulk clk_bulk;
	struct reset_ctl_bulk rst_bulk;
	int ret;

	ret = clk_get_bulk(dev, &clk_bulk);
	if (!ret)
		clk_enable_bulk(&clk_bulk);

	ret = reset_get_bulk(dev, &rst_bulk);
	if (!ret)
		reset_deassert_bulk(&rst_bulk);

	return 0;
}

static int sunxi_clk_of_to_plat(struct udevice *dev)
{
	struct ccu_plat *plat = dev_get_plat(dev);

	plat->base = dev_read_addr_ptr(dev);
	if (!plat->base)
		return -ENOMEM;

	plat->desc = (const struct ccu_desc *)dev_get_driver_data(dev);
	if (!plat->desc)
		return -EINVAL;

	plat->clocks = devm_kcalloc(dev, plat->desc->num_gates,
				    sizeof(struct clk *), GFP_KERNEL);

	return 0;
}

extern const struct ccu_desc a10_ccu_desc;
extern const struct ccu_desc a10s_ccu_desc;
extern const struct ccu_desc a23_ccu_desc;
extern const struct ccu_desc a31_ccu_desc;
extern const struct ccu_desc a31_r_ccu_desc;
extern const struct ccu_desc a64_ccu_desc;
extern const struct ccu_desc a80_ccu_desc;
extern const struct ccu_desc a80_mmc_clk_desc;
extern const struct ccu_desc a83t_ccu_desc;
extern const struct ccu_desc d1_ccu_desc;
extern const struct ccu_desc f1c100s_ccu_desc;
extern const struct ccu_desc h3_ccu_desc;
extern const struct ccu_desc h6_ccu_desc;
extern const struct ccu_desc h616_ccu_desc;
extern const struct ccu_desc a100_ccu_desc;
extern const struct ccu_desc h6_r_ccu_desc;
extern const struct ccu_desc r40_ccu_desc;
extern const struct ccu_desc v3s_ccu_desc;
extern const struct ccu_desc sunxi_de2_ccu_desc;
extern const struct ccu_desc sunxi_de33_ccu_desc;
extern const struct ccu_desc sunxi_tcon_top_desc;

static const struct udevice_id sunxi_clk_ids[] = {
#ifdef CONFIG_CLK_SUN4I_A10
	{ .compatible = "allwinner,sun4i-a10-ccu",
	  .data = (ulong)&a10_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN5I_A10S
	{ .compatible = "allwinner,sun5i-a10s-ccu",
	  .data = (ulong)&a10s_ccu_desc },
	{ .compatible = "allwinner,sun5i-a13-ccu",
	  .data = (ulong)&a10s_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN6I_A31
	{ .compatible = "allwinner,sun6i-a31-ccu",
	  .data = (ulong)&a31_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN4I_A10
	{ .compatible = "allwinner,sun7i-a20-ccu",
	  .data = (ulong)&a10_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN8I_A23
	{ .compatible = "allwinner,sun8i-a23-ccu",
	  .data = (ulong)&a23_ccu_desc },
	{ .compatible = "allwinner,sun8i-a33-ccu",
	  .data = (ulong)&a23_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN8I_A83T
	{ .compatible = "allwinner,sun8i-a83t-ccu",
	  .data = (ulong)&a83t_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN6I_A31_R
	{ .compatible = "allwinner,sun8i-a83t-r-ccu",
	  .data = (ulong)&a31_r_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN8I_H3
	{ .compatible = "allwinner,sun8i-h3-ccu",
	  .data = (ulong)&h3_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN6I_A31_R
	{ .compatible = "allwinner,sun8i-h3-r-ccu",
	  .data = (ulong)&a31_r_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN8I_R40
	{ .compatible = "allwinner,sun8i-r40-ccu",
	  .data = (ulong)&r40_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN8I_V3S
	{ .compatible = "allwinner,sun8i-v3-ccu",
	  .data = (ulong)&v3s_ccu_desc },
	{ .compatible = "allwinner,sun8i-v3s-ccu",
	  .data = (ulong)&v3s_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN9I_A80
	{ .compatible = "allwinner,sun9i-a80-ccu",
	  .data = (ulong)&a80_ccu_desc },
	{ .compatible = "allwinner,sun9i-a80-mmc-config-clk",
	  .data = (ulong)&a80_mmc_clk_desc },
#endif
#ifdef CONFIG_CLK_SUN50I_A64
	{ .compatible = "allwinner,sun50i-a64-ccu",
	  .data = (ulong)&a64_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN6I_A31_R
	{ .compatible = "allwinner,sun50i-a64-r-ccu",
	  .data = (ulong)&a31_r_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN8I_H3
	{ .compatible = "allwinner,sun50i-h5-ccu",
	  .data = (ulong)&h3_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN20I_D1
	{ .compatible = "allwinner,sun20i-d1-ccu",
	  .data = (ulong)&d1_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN50I_H6
	{ .compatible = "allwinner,sun50i-h6-ccu",
	  .data = (ulong)&h6_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN50I_H6_R
	{ .compatible = "allwinner,sun50i-h6-r-ccu",
	  .data = (ulong)&h6_r_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN50I_H616
	{ .compatible = "allwinner,sun50i-h616-ccu",
	  .data = (ulong)&h616_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN50I_H6_R
	{ .compatible = "allwinner,sun50i-h616-r-ccu",
	  .data = (ulong)&h6_r_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUN50I_A100
	{ .compatible = "allwinner,sun50i-a100-ccu",
	  .data = (ulong)&a100_ccu_desc },
#endif
#ifdef CONFIG_CLK_SUNIV_F1C100S
	{ .compatible = "allwinner,suniv-f1c100s-ccu",
	  .data = (ulong)&f1c100s_ccu_desc },
#endif
#ifdef CONFIG_VIDEO_SUNXI_DE
	/* Display Engine */
	{ .compatible = "allwinner,sun8i-v3s-de2-clk",
	  .data = (ulong)&sunxi_de2_ccu_desc },
	{ .compatible = "allwinner,sun50i-a64-de2-clk",
	  .data = (ulong)&sunxi_de2_ccu_desc },
	{ .compatible = "allwinner,sun50i-h616-de33-clk",
	  .data = (ulong)&sunxi_de33_ccu_desc },

	/* TCON_TOP */
	{ .compatible = "allwinner,sun50i-a100-tcon-top",
	  .data = (ulong)&sunxi_tcon_top_desc },
#endif
	{ }
};

U_BOOT_DRIVER(sunxi_clk) = {
	.name		= "sunxi_clk",
	.id		= UCLASS_CLK,
	.of_match	= sunxi_clk_ids,
	.bind		= sunxi_clk_bind,
	.probe		= sunxi_clk_probe,
	.of_to_plat	= sunxi_clk_of_to_plat,
	.plat_auto	= sizeof(struct ccu_plat),
	.ops		= &sunxi_clk_ops,
};
