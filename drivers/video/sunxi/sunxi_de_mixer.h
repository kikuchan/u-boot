/* --------------------------
 * DE 2.0
 * -------------------------- */
#define DE2_CLOCK_BASE			0x000000
#define DE2_MIXER_BASE			0x100000

#define DE2_MAX_OVERLAYS		5

#define DE2_CLK_GATE			(DE2_CLOCK_BASE + 0x00) // SCLK_GATE
#define DE2_CLK_BUS			(DE2_CLOCK_BASE + 0x04) // HCLK_GATE
#define DE2_CLK_RST			(DE2_CLOCK_BASE + 0x08) // AHB_RESET
#define DE2_CLK_DIV			(DE2_CLOCK_BASE + 0x0c) // SCLK_DIV
#define DE2_CLK_SEL			(DE2_CLOCK_BASE + 0x10) // DE2TCON_MUX

#define DE2_GLB_BASE			(DE2_MIXER_BASE + 0x00000)
#define DE2_BLD_BASE			(DE2_MIXER_BASE + 0x01000)
#define DE2_OVL_BASE(ch)		(DE2_MIXER_BASE + 0x02000 + (ch) * 0x1000)

#define DE2_VSU_BASE			(DE2_MIXER_BASE + 0x20000)
#define DE2_GSU1_BASE			(DE2_MIXER_BASE + 0x30000)
#define DE2_GSU2_BASE			(DE2_MIXER_BASE + 0x40000)
#define DE2_GSU3_BASE			(DE2_MIXER_BASE + 0x50000)
#define DE2_FCE_BASE			(DE2_MIXER_BASE + 0xa0000)
#define DE2_BWS_BASE			(DE2_MIXER_BASE + 0xa2000)
#define DE2_LTI_BASE			(DE2_MIXER_BASE + 0xa4000)
#define DE2_PEAK_BASE			(DE2_MIXER_BASE + 0xa6000)
#define DE2_ASE_BASE			(DE2_MIXER_BASE + 0xa8000)
#define DE2_FCC_BASE			(DE2_MIXER_BASE + 0xaa000)
#define DE2_DCSC_BASE			(DE2_MIXER_BASE + 0xb0000)

#define DE2_GLB_CTL			(DE2_GLB_BASE + 0x00)
#define DE2_GLB_STS			(DE2_GLB_BASE + 0x04)
#define DE2_GLB_DBUFFER			(DE2_GLB_BASE + 0x08)
#define DE2_GLB_SIZE			(DE2_GLB_BASE + 0x0c)

#define DE2_BLD_FILL_COLOR_CTL		(DE2_BLD_BASE + 0x000)
#define DE2_BLD_FILL_COLOR(n)		(DE2_BLD_BASE + 0x004 + (n) * 0x14)
#define DE2_BLD_CH_ISIZE(n)		(DE2_BLD_BASE + 0x008 + (n) * 0x14)
#define DE2_BLD_CH_OFFSET(n)		(DE2_BLD_BASE + 0x00c + (n) * 0x14)
#define DE2_BLD_CH_RTCTL		(DE2_BLD_BASE + 0x080)
#define DE2_BLD_PREMUL_CTL		(DE2_BLD_BASE + 0x084)
#define DE2_BLD_BK_COLOR		(DE2_BLD_BASE + 0x088)
#define DE2_BLD_SIZE			(DE2_BLD_BASE + 0x08c)
#define DE2_BLD_CTL(n)			(DE2_BLD_BASE + 0x090 + (n) * 0x04)
#define DE2_BLD_KEY_CTL			(DE2_BLD_BASE + 0x0b0)
#define DE2_BLD_KEY_CON			(DE2_BLD_BASE + 0x0b4)
#define DE2_BLD_KEY_MAX(n)		(DE2_BLD_BASE + 0x0c0 + (n) * 0x04)
#define DE2_BLD_KEY_MIN(n)		(DE2_BLD_BASE + 0x0e0 + (n) * 0x04)
#define DE2_BLD_OUT_COLOR		(DE2_BLD_BASE + 0x0fc)

#define DE2_OVL_UI_ATTR_CTL(ch)		(DE2_OVL_BASE(ch) + 0x000 + (0) * 0x20)
#define DE2_OVL_UI_MBSIZE(ch)		(DE2_OVL_BASE(ch) + 0x004 + (0) * 0x20)
#define DE2_OVL_UI_COOR(ch)		(DE2_OVL_BASE(ch) + 0x008 + (0) * 0x20)
#define DE2_OVL_UI_PITCH(ch)		(DE2_OVL_BASE(ch) + 0x00c + (0) * 0x20)
#define DE2_OVL_UI_TOP_LADD(ch)		(DE2_OVL_BASE(ch) + 0x010 + (0) * 0x20)
#define DE2_OVL_UI_SIZE(ch)		(DE2_OVL_BASE(ch) + 0x088 + (0) * 0x20)

/* --------------------------
 * DE 3.3
 * -------------------------- */
#define DE33_CLOCK_BASE			0x008000
#define DE33_TOP_BASE			0x008100
#define DE33_ENGINE_BASE		0x100000
#define DE33_DISP_BASE			0x280000

#define DE33_MIXER_BASE			(DE33_ENGINE_BASE + 0xc0000)
#define DE33_BLEND_BASE			(DE33_DISP_BASE + 0x1000)

#define DE33_BLD_ENABLE_CTL		(DE33_BLEND_BASE + 0x000)
#define DE33_BLD_FILL_COLOR(n)		(DE33_BLEND_BASE + 0x004 + (n) * 0x14)
#define DE33_BLD_CH_ISIZE(n)		(DE33_BLEND_BASE + 0x008 + (n) * 0x14)
#define DE33_BLD_CH_OFFSET(n)		(DE33_BLEND_BASE + 0x00c + (n) * 0x14)
#define DE33_BLD_CH_RTCTL		(DE33_BLEND_BASE + 0x080)
#define DE33_BLD_PREMUL_CTL		(DE33_BLEND_BASE + 0x084)
#define DE33_BLD_BK_COLOR		(DE33_BLEND_BASE + 0x088)
#define DE33_BLD_SIZE			(DE33_BLEND_BASE + 0x08c)
#define DE33_BLD_CTL(n)			(DE33_BLEND_BASE + 0x090 + (n) * 0x04)
#define DE33_BLD_KEY_CTL		(DE33_BLEND_BASE + 0x0b0)
#define DE33_BLD_KEY_CON		(DE33_BLEND_BASE + 0x0b4)
#define DE33_BLD_KEY_MAX(n)		(DE33_BLEND_BASE + 0x0c0 + (n) * 0x04)
#define DE33_BLD_KEY_MIN(n)		(DE33_BLEND_BASE + 0x0e0 + (n) * 0x04)
#define DE33_BLD_OUT_COLOR		(DE33_BLEND_BASE + 0x0fc)

#define DE33_OVL_BASE(ch)		(DE33_MIXER_BASE + 0x800 + (ch) * 0x800)

#define DE33_OVL_UI_ATTR_CTL(ch)	(DE33_OVL_BASE(ch) + 0x000 + (0) * 0x20)
#define DE33_OVL_UI_MBSIZE(ch)		(DE33_OVL_BASE(ch) + 0x004 + (0) * 0x20)
#define DE33_OVL_UI_COOR(ch)		(DE33_OVL_BASE(ch) + 0x008 + (0) * 0x20)
#define DE33_OVL_UI_PITCH(ch)		(DE33_OVL_BASE(ch) + 0x00c + (0) * 0x20)
#define DE33_OVL_UI_TOP_LADD(ch)	(DE33_OVL_BASE(ch) + 0x010 + (0) * 0x20)
#define DE33_OVL_UI_SIZE(ch)		(DE33_OVL_BASE(ch) + 0x088 + (0) * 0x20)
