// license:BSD-3-Clause
// copyright-holders:

/*
unknown Sunwise tarot card game

TOP BOARD (S-8808A)
=========
ROMs 1-12
main:
Z80 CTC-D
LH0083A Z80A-DMA
Z0840004PSC Z80 CPU
D8255AC-2
6116 RAM
Oki M6242
32.768 kHz osc

sound:
Z80 CTC-D
D8255AC-2
Z0840004PSC Z80 CPU
YM3812
5816 RAM
4 MHz osc

LOWER BOARD (S-8809A)
==========
ROMs A-E
3x 6148 RAM
video output
5x 5816 RAM
18 MHz osc

TODO:
- Identify irq sources ($24 timer, $26 VBLANK?, $20 or $22 quadrature encoder);
- Data ROM bank;

*/

#include "emu.h"

#include "cpu/z80/z80.h"
#include "machine/i8255.h"
#include "machine/msm6242.h"
#include "machine/z80ctc.h"
#include "machine/z80dma.h"
#include "sound/ymopl.h"

#include "emupal.h"
#include "screen.h"
#include "speaker.h"
#include "tilemap.h"


// configurable logging
#define LOG_PORTS     (1U << 1)
#define LOG_DMA       (1U << 2)

//#define VERBOSE (LOG_GENERAL | LOG_PORTS | LOG_DMA)
#define VERBOSE (LOG_GENERAL)

#include "logmacro.h"

#define LOGPORTS(...)     LOGMASKED(LOG_PORTS,     __VA_ARGS__)
#define LOGDMA(...)       LOGMASKED(LOG_DMA,       __VA_ARGS__)


namespace {

class anoworld_state : public driver_device
{
public:
	anoworld_state(const machine_config &mconfig, device_type type, const char *tag)
		: driver_device(mconfig, type, tag)
		, m_maincpu(*this, "maincpu")
		, m_audiocpu(*this, "audiocpu")
		, m_dma(*this, "dma")
		, m_palette(*this, "palette")
		, m_gfxdecode(*this, "gfxdecode")
		, m_databank(*this, "databank")
		, m_audiobank(*this, "audiobank")
	{
	}

	void unktarot(machine_config &config) ATTR_COLD;

protected:
	virtual void machine_start() override ATTR_COLD;

private:
	required_device<z80_device> m_maincpu;
	required_device<cpu_device> m_audiocpu;
	required_device<z80dma_device> m_dma;
	required_device<palette_device> m_palette;
	required_device<gfxdecode_device> m_gfxdecode;

	required_memory_bank m_databank;
	required_memory_bank m_audiobank;
	std::unique_ptr<uint8_t[]> m_paletteram;

	void main_program_map(address_map &map) ATTR_COLD;
	void audio_program_map(address_map &map) ATTR_COLD;
	void main_io_map(address_map &map) ATTR_COLD;
	void audio_io_map(address_map &map) ATTR_COLD;

	void data_bank_w(offs_t offset, u8 data);

	void dma_busreq_w(int state);
	u8 dma_memory_r(offs_t offset);
	void dma_memory_w(offs_t offset, u8 data);
	u8 dma_io_r(offs_t offset);
	void dma_io_w(offs_t offset, u8 data);

	uint32_t screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect);
};


uint32_t anoworld_state::screen_update(screen_device &screen, bitmap_rgb32 &bitmap, const rectangle &cliprect)
{
	bitmap.fill(rgb_t::black(), cliprect);

	return 0;
}

void anoworld_state::data_bank_w(offs_t offset, u8 data)
{
	// guess, also bit 6 actively used
	m_databank->set_entry(data & 0x3f);
	LOG("data_bank_w: %02x\n", data);
}

void anoworld_state::dma_busreq_w(int state)
{
	// HACK: grant bus request immediately
	LOGDMA("dma_busreq_w %d\n", state);
	m_maincpu->set_input_line(INPUT_LINE_HALT, state);
	m_dma->bai_w(state);
}

u8 anoworld_state::dma_memory_r(offs_t offset)
{
	LOGDMA("dma_memory_r %04X\n", offset);
	return m_maincpu->space(AS_PROGRAM).read_byte(offset);
}

void anoworld_state::dma_memory_w(offs_t offset, u8 data)
{
	LOGDMA("dma_memory_w %04X <- %02X\n", offset, data);
	m_maincpu->space(AS_PROGRAM).write_byte(offset, data);
}

u8 anoworld_state::dma_io_r(offs_t offset)
{
	LOGDMA("dma_io_r %04X\n", offset);
	return m_maincpu->space(AS_IO).read_byte(offset);
}

void anoworld_state::dma_io_w(offs_t offset, u8 data)
{
	LOGDMA("dma_io_w %04X <- %02X\n", offset, data);
	m_maincpu->space(AS_IO).write_byte(offset, data);
}

void anoworld_state::main_program_map(address_map &map)
{
	map(0x0000, 0x7fff).rom();
	map(0x8000, 0x9fff).ram();
	map(0xa000, 0xafff).ram();
//  map(0xb000, 0xbfff).ram(); // interleaved video tilemap + palette words (RGB444)? Or banked thru port $42 bit 4?
	map(0xb000, 0xbfff).lrw8(
		NAME([this] (offs_t offset) {
			return m_paletteram[bitswap<12>(offset ^ 2, 1, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 0)];
		}),
		NAME([this] (offs_t offset, u8 data) {
			const u16 pal_offset = bitswap<12>(offset ^ 2, 1, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 0);
			m_paletteram[pal_offset] = data;
			const u16 datax = m_paletteram[pal_offset & ~1] | (m_paletteram[pal_offset | 1] << 8);
			const u8 r = (datax >> 0) & 0xf;
			const u8 g = (datax >> 4) & 0xf;
			const u8 b = (datax >> 8) & 0xf;

			m_palette->set_pen_color(pal_offset >> 1, pal4bit(r), pal4bit(g), pal4bit(b));
		})
	);

	map(0xc000, 0xffff).bankr(m_databank);
}

void anoworld_state::main_io_map(address_map &map)
{
	map.global_mask(0xff);
	map.unmap_value_high();

	map(0x00, 0x00).rw(m_dma, FUNC(z80dma_device::read), FUNC(z80dma_device::write));
	map(0x10, 0x13).rw("ctc0", FUNC(z80ctc_device::read), FUNC(z80ctc_device::write));
	map(0x20, 0x2f).rw("rtc", FUNC(msm6242_device::read), FUNC(msm6242_device::write));
	map(0x30, 0x33).rw("ppi0", FUNC(i8255_device::read), FUNC(i8255_device::write));
	map(0x40, 0x43).rw("ppi1", FUNC(i8255_device::read), FUNC(i8255_device::write));
	map(0x50, 0x50).noprw(); // ? (bits 4 and 5 checked in service routines $20 and $22)
	map(0x60, 0x60).noprw(); // ?
}

static const z80_daisy_config main_daisy_chain[] =
{
	{ "dma" },
	{ "ctc0" },
	{ nullptr }
};

void anoworld_state::audio_program_map(address_map &map)
{
	map(0x0000, 0x1fff).rom();
	map(0x4000, 0x47ff).ram();
	map(0xc000, 0xffff).bankr(m_audiobank);
}

void anoworld_state::audio_io_map(address_map &map)
{
	map.global_mask(0xff);

	//map(0x00, 0x03).rw("ctc1", FUNC(z80ctc_device::read), FUNC(z80ctc_device::write)); //TODO: maybe
	//map(0x04, 0x07).rw("ppi1", FUNC(i8255_device::read), FUNC(i8255_device::write)); //TODO: maybe. 0x05 seems to set the audio bank
	//map(0x08, 0x09).w // YM?
	//map(0x0c, 0x0c).r? // latch? irq ack?
}


static INPUT_PORTS_START( unktarot )
	PORT_START("IN0")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(1)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(1)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_PLAYER(1)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_PLAYER(1)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_BUTTON5 ) PORT_PLAYER(1)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_BUTTON6 ) PORT_PLAYER(1)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_BUTTON7 ) PORT_PLAYER(1)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_BUTTON8 ) PORT_PLAYER(1)

	PORT_START("IN1")
	PORT_BIT( 0x01, IP_ACTIVE_LOW, IPT_BUTTON1 ) PORT_PLAYER(2)
	PORT_BIT( 0x02, IP_ACTIVE_LOW, IPT_BUTTON2 ) PORT_PLAYER(2)
	PORT_BIT( 0x04, IP_ACTIVE_LOW, IPT_BUTTON3 ) PORT_PLAYER(2)
	PORT_BIT( 0x08, IP_ACTIVE_LOW, IPT_BUTTON4 ) PORT_PLAYER(2)
	PORT_BIT( 0x10, IP_ACTIVE_LOW, IPT_BUTTON5 ) PORT_PLAYER(2)
	PORT_BIT( 0x20, IP_ACTIVE_LOW, IPT_BUTTON6 ) PORT_PLAYER(2)
	PORT_BIT( 0x40, IP_ACTIVE_LOW, IPT_BUTTON7 ) PORT_PLAYER(2)
	PORT_BIT( 0x80, IP_ACTIVE_LOW, IPT_BUTTON8 ) PORT_PLAYER(2)

	PORT_START("DSW")
	PORT_DIPUNKNOWN_DIPLOC(0x01, 0x01, "SW:1")
	PORT_DIPUNKNOWN_DIPLOC(0x02, 0x02, "SW:2")
	PORT_DIPUNKNOWN_DIPLOC(0x04, 0x04, "SW:3")
	PORT_DIPUNKNOWN_DIPLOC(0x08, 0x08, "SW:4")
	PORT_DIPUNKNOWN_DIPLOC(0x10, 0x10, "SW:5")
	PORT_DIPUNKNOWN_DIPLOC(0x20, 0x20, "SW:6")
	PORT_DIPUNKNOWN_DIPLOC(0x40, 0x40, "SW:7")
	PORT_DIPUNKNOWN_DIPLOC(0x80, 0x80, "SW:8")
INPUT_PORTS_END


const gfx_layout gfx_8x8x1 =
{
	8,8,
	RGN_FRAC(1,1),
	1,
	{ RGN_FRAC(0,1) },
	{ STEP8(7,-1) },
	{ STEP8(0,8) },
	8*8
};

const gfx_layout gfx_8x8x4 =
{
	8,8,
	RGN_FRAC(1,4),
	4,
	{ RGN_FRAC(3,4), RGN_FRAC(2,4), RGN_FRAC(1,4), RGN_FRAC(0,4) },
	{ STEP8(7,-1) },
	{ STEP8(0,8) },
	8*8
};

static GFXDECODE_START( gfx_unktarot )
	GFXDECODE_ENTRY( "chars", 0, gfx_8x8x1, 0, 16 ) // TODO: identify how it gathers palette
	GFXDECODE_ENTRY( "tiles", 0, gfx_8x8x4, 0, 16 * 8 )
GFXDECODE_END

void anoworld_state::machine_start()
{
	m_paletteram = make_unique_clear<uint8_t[]>(0x1000);

	m_databank->configure_entries(0, 0x40, memregion("data")->base(), 0x4000);
	m_databank->set_entry(0);
	m_audiobank->configure_entries(0, 32, memregion("audiocpu")->base(), 0x4000);
	m_audiobank->set_entry(1);
}

/*
 * Irq table:
 * $+10 (DMA) writes to $9e61
 * $+20 (CTC ch0, counter mode) increments or decrements $9e88
 * $+22 (CTC ch1, counter mode) increments or decrements $9e8a
 * $+24 (CTC ch2, timer mode) writes to $86a9
 * $+26 (CTC ch3, counter mode) writes to $9e65
 */


void anoworld_state::unktarot(machine_config &config)
{
	Z80(config, m_maincpu, 4_MHz_XTAL);
	m_maincpu->set_addrmap(AS_PROGRAM, &anoworld_state::main_program_map);
	m_maincpu->set_addrmap(AS_IO, &anoworld_state::main_io_map);
	m_maincpu->set_daisy_config(main_daisy_chain);

	Z80(config, m_audiocpu, 4_MHz_XTAL).set_disable();
	m_audiocpu->set_addrmap(AS_PROGRAM, &anoworld_state::audio_program_map);
	m_audiocpu->set_addrmap(AS_IO, &anoworld_state::audio_io_map);

	z80ctc_device &ctc0(Z80CTC(config, "ctc0", 4_MHz_XTAL));
	ctc0.intr_callback().set_inputline("maincpu", 0);

	Z80DMA(config, m_dma, 4_MHz_XTAL);
	m_dma->out_busreq_callback().set(FUNC(anoworld_state::dma_busreq_w));
	m_dma->out_int_callback().set_inputline("maincpu", 0);
	m_dma->in_mreq_callback().set(FUNC(anoworld_state::dma_memory_r));
	m_dma->out_mreq_callback().set(FUNC(anoworld_state::dma_memory_w));
	m_dma->in_iorq_callback().set(FUNC(anoworld_state::dma_io_r));
	m_dma->out_iorq_callback().set(FUNC(anoworld_state::dma_io_w));

	z80ctc_device &ctc1(Z80CTC(config, "ctc1", 4_MHz_XTAL));
	ctc1.intr_callback().set([this] (int state) { LOGPORTS("%s: CTC1 INTR handler %d\n", machine().describe_context(), state); });
	ctc1.zc_callback<0>().set([this] (int state) { LOGPORTS("%s: CTC1 ZC0 handler %d\n", machine().describe_context(), state); });
	ctc1.zc_callback<1>().set([this] (int state) { LOGPORTS("%s: CTC1 ZC1 handler %d\n", machine().describe_context(), state); });
	ctc1.zc_callback<2>().set([this] (int state) { LOGPORTS("%s: CTC1 ZC2 handler %d\n", machine().describe_context(), state); });

	i8255_device &ppi0(I8255A(config, "ppi0")); // NEC D8255AC-2
	ppi0.in_pa_callback().set([this] () { LOGPORTS("%s: PPI0 port A in\n", machine().describe_context()); return uint8_t(0); });
	ppi0.in_pb_callback().set([this] () { LOGPORTS("%s: PPI0 port B in\n", machine().describe_context()); return uint8_t(0); });
	ppi0.in_pc_callback().set([this] () { LOGPORTS("%s: PPI0 port C in\n", machine().describe_context()); return uint8_t(0); });
	ppi0.out_pa_callback().set([this] (uint8_t data) { LOGPORTS("%s: PPI0 port A out %02x\n", machine().describe_context(), data); });
	ppi0.out_pb_callback().set([this] (uint8_t data) { LOGPORTS("%s: PPI0 port B out %02x\n", machine().describe_context(), data); });
	ppi0.out_pc_callback().set([this] (uint8_t data) { LOGPORTS("%s: PPI0 port C out %02x\n", machine().describe_context(), data); });

	i8255_device &ppi1(I8255A(config, "ppi1")); // NEC D8255AC-2
	ppi1.out_pa_callback().set(FUNC(anoworld_state::data_bank_w));
	ppi1.out_pb_callback().set([this] (uint8_t data) { LOGPORTS("%s: PPI1 port B out %02x\n", machine().describe_context(), data); });
	ppi1.out_pc_callback().set([this] (uint8_t data) { LOGPORTS("%s: PPI1 port C out %02x\n", machine().describe_context(), data); });

	msm6242_device &rtc(MSM6242(config, "rtc", 32.768_kHz_XTAL));
	rtc.out_int_handler().set("ctc0", FUNC(z80ctc_device::trg3)); // source guessed

	screen_device &screen(SCREEN(config, "screen", SCREEN_TYPE_RASTER)); // TODO: everything
	screen.set_refresh_hz(60);
	screen.set_vblank_time(ATTOSECONDS_IN_USEC(0));
	screen.set_size(64*8, 32*8);
	screen.set_visarea(0*8, 64*8-1, 2*8, 30*8-1);
	screen.set_screen_update(FUNC(anoworld_state::screen_update));

	GFXDECODE(config, m_gfxdecode, m_palette, gfx_unktarot);

	PALETTE(config, m_palette).set_entries(0x800); //.set_format(palette_device::xRGB_444, 0x800);

	SPEAKER(config, "mono").front_center();

	ym3812_device &ym(YM3812(config, "ym", 4_MHz_XTAL));
	ym.irq_handler().set([this] (int state) { LOGPORTS("%s: YM IRQ handler %d\n", machine().describe_context(), state); });
	ym.add_route(ALL_OUTPUTS, "mono", 0.80);
}


ROM_START( unktarot )
	ROM_REGION( 0x08000, "maincpu", 0 )
	ROM_LOAD( "1.u5", 0x00000, 0x08000, CRC(eaf339d1) SHA1(8325046d2059ad890204e0373bcfbe1221e12bdf) )

	// these ROMs are located under the main CPU one
	ROM_REGION( 0x100000, "data", 0 )
	ROM_LOAD( "2.u6",  0x00000, 0x20000, CRC(42564c5b) SHA1(c991a09c29283a50e6e1638cc80eb9dd490fda3f) )
	ROM_LOAD( "3.u7",  0x20000, 0x20000, CRC(5692e175) SHA1(a0a3d01a7d7ff5400cef8e497897786c653155f3) )
	ROM_LOAD( "4.u8",  0x40000, 0x20000, CRC(e368af7c) SHA1(9cbd32b4398bfa5b8c5a8f0e81705dcfd8919553) )
	ROM_LOAD( "5.u9",  0x60000, 0x20000, CRC(179989dd) SHA1(d07b8b980b67327ed3bbfa1da737c0261bef9e17) )
	ROM_LOAD( "6.u14", 0x80000, 0x20000, CRC(91d821d1) SHA1(ce9829fd24f45ed18ea6180104426562eee118d2) )
	ROM_LOAD( "7.u15", 0xa0000, 0x20000, CRC(d94904db) SHA1(2cb7a13f56aefed5e70f50f3c3439a2d5049b581) )
	ROM_LOAD( "8.u16", 0xc0000, 0x20000, CRC(a4d8e251) SHA1(ed0df6b525d833e5c2e9c0aeaa2e34b210f958aa) )
	ROM_LOAD( "9.u17", 0xe0000, 0x20000, CRC(5c00d059) SHA1(17fae821c17eebe675feab1c97099c6a2241b0d1) )

	ROM_REGION( 0x80000, "audiocpu", ROMREGION_ERASE00 )
	ROM_LOAD( "10.u28", 0x00000, 0x20000, CRC(4bedeb7a) SHA1(d9ce1bc9efc26f8c90b98736ee41a3cb45fc3b68) )
	// empty u29
	ROM_LOAD( "11.u41", 0x20000, 0x20000, CRC(b0ffb442) SHA1(9524de6120adc8ec77b94e9a89992271ce12704b) )
	ROM_LOAD( "12.u42", 0x40000, 0x20000, CRC(83ae6756) SHA1(289d2ed72dbf5c1a941da67cb1f7fe9a0e3aaab2) )

	ROM_REGION( 0x8000, "chars", 0 )
	ROM_LOAD( "e.u60", 0x0000, 0x8000, CRC(f80ba6fe) SHA1(573d49cf52b47f112f2cfd0e259b8dc5852a3102) )

	ROM_REGION( 0x80000, "tiles", 0 )
	ROM_LOAD( "a.u56", 0x00000, 0x20000, CRC(698250cf) SHA1(aa2acb4a643d6fcff6fa406a6fe9c1ff3457ad51) )
	ROM_LOAD( "b.u57", 0x20000, 0x20000, CRC(ebcfa6fc) SHA1(00f2603c38e0e4ee86e14954afe282fafe8c1a8b) )
	ROM_LOAD( "c.u58", 0x40000, 0x20000, CRC(c6b572e8) SHA1(2fad7841a981bc98f2187237116dd61e272128d5) )
	ROM_LOAD( "d.u59", 0x60000, 0x20000, CRC(cbe4da16) SHA1(e673b3b7e95cda27f1a77e8ef44e13a58ba3d3dc) )
	// empty u66 to u69
ROM_END

} // anonymous namespace


GAME( 1989, unktarot, 0, unktarot, unktarot, anoworld_state, empty_init, ROT0, "Sunwise", "Another World (Japan)", MACHINE_IS_SKELETON ) // title screen GFXs in region 1 at 0x3051 onward
