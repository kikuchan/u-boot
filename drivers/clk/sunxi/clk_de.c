// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (C) 2025, Hironori KIKUCHI <kikuchan98@gmail.com>
 */

#include <clk-uclass.h>
#include <dm.h>
#include <linux/bitops.h>
#include <clk/sunxi.h>
#include <dt-bindings/clock/sun8i-de2.h>
#include <dt-bindings/reset/sun8i-de2.h>

static struct ccu_clk_gate de33_gates[] = {
	[CLK_MIXER0] = GATE_PARENT(0x00, BIT(0), NO_MUX("mod")),
	[CLK_MIXER1] = GATE_PARENT(0x00, BIT(1), NO_MUX("mod")),
	[CLK_BUS_MIXER0] = GATE(0x04, BIT(0)),
	[CLK_BUS_MIXER1] = GATE(0x04, BIT(1)),
};

static struct ccu_reset de33_resets[] = {
	[RST_MIXER0] = RESET(0x08, BIT(0)),
};

static struct ccu_clk_gate de2_gates[] = {
	[CLK_MIXER0] = GATE_PARENT(0x00, BIT(0), NO_MUX("mod")),
	[CLK_MIXER1] = GATE_PARENT(0x00, BIT(1), NO_MUX("mod")),
	[CLK_WB] = GATE_PARENT(0x00, BIT(2), NO_MUX("mod")),
	[CLK_ROT] = GATE_PARENT(0x00, BIT(3), NO_MUX("mod")),

	[CLK_BUS_MIXER0] = GATE(0x04, BIT(0)),
	[CLK_BUS_MIXER1] = GATE(0x04, BIT(1)),
	[CLK_BUS_WB] = GATE(0x04, BIT(2)),
	[CLK_BUS_ROT] = GATE(0x04, BIT(3)),
};

static struct ccu_reset de2_resets[] = {
	[RST_MIXER0] = RESET(0x08, BIT(0)),
};

const struct ccu_desc sunxi_de33_ccu_desc = {
	.gates = de33_gates,
	.resets = de33_resets,
	.num_gates = ARRAY_SIZE(de33_gates),
	.num_resets = ARRAY_SIZE(de33_resets),
};

const struct ccu_desc sunxi_de2_ccu_desc = {
	.gates = de2_gates,
	.resets = de2_resets,
	.num_gates = ARRAY_SIZE(de2_gates),
	.num_resets = ARRAY_SIZE(de2_resets),
};
