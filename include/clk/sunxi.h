// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright (C) 2018 Amarula Solutions.
 * Author: Jagan Teki <jagan@amarulasolutions.com>
 */

#ifndef _CLK_SUNXI_H
#define _CLK_SUNXI_H

#include <linux/bitops.h>

/**
 * enum ccu_flags - ccu clock/reset flags
 *
 * @CCU_CLK_F_IS_VALID:		is given clock gate is valid?
 * @CCU_RST_F_IS_VALID:		is given reset control is valid?
 */
enum ccu_flags {
	CCU_CLK_F_IS_VALID = BIT(0),
	CCU_RST_F_IS_VALID = BIT(1),
};

#define SUNXI_CLK_BITINFO_MUL (0)
#define SUNXI_CLK_BITINFO_DIV (1)
#define SUNXI_CLK_BITINFO_DIVEXP (2)

struct sunxi_clk_bitinfo {
	int flags;

	int shift;
	int width;

	int min;
	int max;
};

struct sunxi_clk_muxinfo {
	int shift;
	int width;

	const char **parents;
	int count;
};

/**
 * struct ccu_clk_gate - ccu clock gate
 * @off:	gate offset
 * @bit:	gate bit
 * @flags:	ccu clock gate flags
 */
struct ccu_clk_gate {
	enum ccu_flags flags;

	int off;
	u32 bit;

	struct sunxi_clk_muxinfo mux;

	struct sunxi_clk_bitinfo **muldiv;
	int num_muldiv;

	int fixed_div;
};

#define CLKREF(v) (const char *)(-(long)(v))
#define CLKUNREF(v) (-(long)(v))

#define PLL_FACTOR_N_BIT(_u, _l, _min, _max)                           \
	&(struct sunxi_clk_bitinfo)                                    \
	{                                                              \
		.flags = SUNXI_CLK_BITINFO_MUL, .shift = (_l),         \
		.width = (_u) - (_l) + 1, .min = (_min), .max = (_max) \
	}

#define PLL_INPUT_DIV_M_BIT(a)                                       \
	&(struct sunxi_clk_bitinfo){ .flags = SUNXI_CLK_BITINFO_DIV, \
				     .shift = a,                     \
				     .width = 1,                     \
				     .min = -1,                      \
				     .max = -1 }

#define PLL_OUTPUT_DIV_D_BIT(a)                                      \
	&(struct sunxi_clk_bitinfo){ .flags = SUNXI_CLK_BITINFO_DIV, \
				     .shift = a,                     \
				     .width = 1,                     \
				     .min = -1,                      \
				     .max = -1 }

#define DIV_FACTOR_N_BIT(_u, _l)                                         \
	&((struct sunxi_clk_bitinfo){ .flags = SUNXI_CLK_BITINFO_DIVEXP, \
				      .shift = (_l),                     \
				      .width = (_u) - (_l) + 1,          \
				      .min = -1,                         \
				      .max = -1 })

#define DIV_FACTOR_M_BIT(_u, _l)                                      \
	&((struct sunxi_clk_bitinfo){ .flags = SUNXI_CLK_BITINFO_DIV, \
				      .shift = (_l),                  \
				      .width = (_u) - (_l) + 1,       \
				      .min = -1,                      \
				      .max = -1 })

#define MUX_BIT(_u, _l, ...)                                  \
	((struct sunxi_clk_muxinfo){                          \
		.shift = (_l),                                \
		.width = (_u) - (_l) + 1,                     \
		.parents = ((const char *[]){ __VA_ARGS__ }), \
		.count = ARRAY_SIZE(((const char *[]){ __VA_ARGS__ })) })

#define NO_MUX(a) MUX_BIT(-1, -1, a)

#define GATE(_off, _bit)                     \
	{                                    \
		.flags = CCU_CLK_F_IS_VALID, \
		.off = _off,                 \
		.bit = _bit,                 \
	}

#define GATE_PARENT(_off, _bit, _mux, ...)                                 \
	{                                                                  \
		.flags = CCU_CLK_F_IS_VALID,                               \
		.off = _off,                                               \
		.bit = _bit,                                               \
		.mux = _mux,                                               \
		.muldiv = ((struct sunxi_clk_bitinfo *[]){ __VA_ARGS__ }), \
		.num_muldiv = ARRAY_SIZE(                                  \
			((struct sunxi_clk_bitinfo *[]){ __VA_ARGS__ })),  \
		.fixed_div = 1,                                            \
	}

#define GATE_REF_DIV(_parent, _div)             \
	{                                       \
		.flags = CCU_CLK_F_IS_VALID,    \
		.off = -1,                      \
		.mux = NO_MUX(CLKREF(_parent)), \
		.fixed_div = _div,              \
	}

#define GATE_DUMMY                           \
	{                                    \
		.flags = CCU_CLK_F_IS_VALID, \
		.off = -1,                   \
	}

/**
 * struct ccu_reset - ccu reset
 * @off:	reset offset
 * @bit:	reset bit
 * @flags:	ccu reset control flags
 */
struct ccu_reset {
	u16 off;
	u32 bit;
	enum ccu_flags flags;
};

#define RESET(_off, _bit)                    \
	{                                    \
		.off = _off,                 \
		.bit = _bit,                 \
		.flags = CCU_RST_F_IS_VALID, \
	}

/**
 * struct ccu_desc - clock control unit descriptor
 *
 * @gates:	clock gates
 * @resets:	reset unit
 */
struct ccu_desc {
	const struct ccu_clk_gate *gates;
	const struct ccu_reset *resets;
	u8 num_gates;
	u8 num_resets;
};

/**
 * struct ccu_plat - sunxi clock control unit platform data
 *
 * @base:	base address
 * @desc:	ccu descriptor
 */
struct ccu_plat {
	void *base;
	const struct ccu_desc *desc;

	struct clk **clocks;
};

extern struct clk_ops sunxi_clk_ops;

int sunxi_clk_set_mux(struct clk *clk, uint mux);
int sunxi_clk_get_mux(struct clk *clk);

/* internal function */
int sunxi_clk_find_best_muldiv(const struct ccu_clk_gate *gate, ulong target,
			       ulong source, u32 *result, u32 *mask);

#endif /* _CLK_SUNXI_H */
