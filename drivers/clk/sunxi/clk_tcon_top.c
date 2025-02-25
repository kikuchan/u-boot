// SPDX-License-Identifier: (GPL-2.0+ OR MIT)
/*
 * Copyright (C) 2025, Hironori KIKUCHI <kikuchan98@gmail.com>
 */

#include <clk-uclass.h>
#include <dm.h>
#include <linux/bitops.h>
#include <clk/sunxi.h>
#include <dt-bindings/clock/sun8i-tcon-top.h>

static struct ccu_clk_gate gates[] = {
	[CLK_TCON_TOP_TV0] = GATE(0x20, BIT(20)),
	[CLK_TCON_TOP_TV1] = GATE(0x20, BIT(24)),
	[CLK_TCON_TOP_DSI] = GATE_PARENT(0x20, BIT(16), NO_MUX("dsi")),
};

const struct ccu_desc sunxi_tcon_top_desc = {
	.gates = gates,
	.num_gates = ARRAY_SIZE(gates),
};
