/*
 *  Copyright (C) 2021-2022  The DOSBox Staging Team
 *  Copyright (C) 2002-2021  The DOSBox Team
 * 
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "dosbox.h"

#include <cassert>
#include <queue>

#include "bit_view.h"
#include "inout.h"
#include "mixer.h"
#include "pic.h"
#include "setup.h"

class Disney {
public:
	Disney(const std::string_view filter_pref);
	~Disney();

private:
	Disney()                          = delete;
	Disney(const Disney &)            = delete;
	Disney &operator=(const Disney &) = delete;

	AudioFrame Render();
	void RenderUpToNow();
	void AudioCallback(const uint16_t requested_frames);

	bool IsFull() const;
	uint8_t ReadStatus(io_port_t, io_width_t);
	void WriteData(io_port_t, const io_val_t value, io_width_t);
	void WriteControl(io_port_t, io_val_t, io_width_t);

	// The DSS is an LPT DAC with a 16-level FIFO running at 7kHz
	static constexpr auto dac_rate_hz       = 7000;
	static constexpr auto ms_per_frame      = 1000.0 / dac_rate_hz;
	static constexpr uint16_t parallel_port = 0x378;
	static constexpr uint8_t power_on_bits  = 0b1111;
	static constexpr uint8_t power_off_bits = 0b0000;
	static constexpr uint8_t max_fifo_size  = 16;

	// Managed objects
	mixer_channel_t channel               = {};
	std::queue<uint8_t> fifo              = {};
	std::queue<AudioFrame> render_queue   = {};
	IO_ReadHandleObject read_handler      = {};
	IO_WriteHandleObject write_handler[2] = {};

	// Runtime state
	double last_rendered_ms = 0.0;

	union StatusRegister {
		uint8_t data = 0;
		bit_view<0, 4> power;
		bit_view<6, 1> fifo_full;
	} status = {};
};

bool Disney::IsFull() const
{
	return fifo.size() >= max_fifo_size;
}

AudioFrame Disney::Render()
{
	// Our FIFO should never be empty
	assert(fifo.size());

	const float sample = lut_u8to16[fifo.front()];
	const AudioFrame frame{sample, sample};

	if (fifo.size() > 1)
		fifo.pop();

	return frame;
}

void Disney::RenderUpToNow()
{
	const auto now = PIC_FullIndex();

	// Wake up the channel and update the last rendered time datum.
	assert(channel);
	if (channel->WakeUp()) {
		last_rendered_ms = now;
		return;
	}
	// Keep rendering until we're current
	while (last_rendered_ms < now) {
		last_rendered_ms += ms_per_frame;
		render_queue.emplace(Render());
	}
}

void Disney::WriteData(io_port_t, const io_val_t data, io_width_t)
{
	RenderUpToNow();
	if (!IsFull())
		fifo.emplace(check_cast<uint8_t>(data));
}

void Disney::WriteControl(io_port_t, io_val_t, io_width_t)
{
	RenderUpToNow();
}

uint8_t Disney::ReadStatus(io_port_t, io_width_t)
{
	status.fifo_full = IsFull();
	return status.data;
}

void Disney::AudioCallback(const uint16_t requested_frames)
{
	assert(channel);

	auto frames_remaining = requested_frames;

	// First, add any frames we've queued since the last callback
	while (frames_remaining && render_queue.size()) {
		channel->AddSamples_sfloat(1, &render_queue.front()[0]);
		render_queue.pop();
		--frames_remaining;
	}
	// If the queue's run dry, render the remainder and sync-up our time datum
	while (frames_remaining) {
		const auto frame = Render();
		channel->AddSamples_sfloat(1, &frame[0]);
		--frames_remaining;
	}
	last_rendered_ms = PIC_FullIndex();
}

std::unique_ptr<Disney> disney = {};

Disney::Disney(const std::string_view filter_pref)
{
	// Prime the FIFO with a single silent sample
	fifo.emplace(Mixer_GetSilentDOSSample<uint8_t>());

	using namespace std::placeholders;
	const auto audio_callback = std::bind(&Disney::AudioCallback, this, _1);

	// Setup the mixer callback
	channel = MIXER_AddChannel(audio_callback,
	                           dac_rate_hz,
	                           "DISNEY",
	                           {ChannelFeature::Sleep,
	                            ChannelFeature::ReverbSend,
	                            ChannelFeature::ChorusSend,
	                            ChannelFeature::DigitalAudio});
	assert(channel);

	if (filter_pref == "on") {
		// Disney only supports a single fixed 7kHz sample rate. We'll
		// apply a gentle 6dB/oct LPF at a bit below half the sample
		// rate to tame the harshest aliased frequencies while still
		// retaining a good dose of the "raw crunchy DAC sound".
		constexpr auto lowpass_order = 1;
		constexpr auto lowpass_freq = static_cast<uint16_t>(dac_rate_hz * 0.45);
		channel->ConfigureLowPassFilter(lowpass_order, lowpass_freq);
		channel->SetLowPassFilter(FilterState::On);
	} else {
		if (filter_pref != "off")
			LOG_WARNING("DISNEY: Invalid filter setting '%s', using off",
			            filter_pref.data());
		channel->SetLowPassFilter(FilterState::Off);
	}

	// Register port handlers for 8-bit IO
	const auto write_data = std::bind(&Disney::WriteData, this, _1, _2, _3);
	write_handler[0].Install(parallel_port, write_data, io_width_t::byte);

	const auto write_control = std::bind(&Disney::WriteControl, this, _1, _2, _3);
	write_handler[1].Install(parallel_port + 2, write_control, io_width_t::byte);

	const auto read_status = std::bind(&Disney::ReadStatus, this, _1, _2);
	read_handler.Install(parallel_port + 1, read_status, io_width_t::byte, 2);

	status.power = power_on_bits;
	LOG_MSG("DISNEY: Disney Sound Source running at %dkHz on LPT1 port %03xh", dac_rate_hz / 1000, parallel_port);
}

Disney::~Disney()
{
	LOG_MSG("DISNEY: Shutting down on LPT1 port %03xh", parallel_port);

	// Stop the game from accessing the IO ports
	read_handler.Uninstall();
	write_handler[0].Uninstall();
	write_handler[1].Uninstall();

	channel->Enable(false);

	fifo         = {};
	render_queue = {};
	status.power = power_off_bits;
}

void DISNEY_ShutDown([[maybe_unused]] Section *sec)
{
	disney.reset();
}

void DISNEY_Init(Section *sec)
{
	Section_prop *section = static_cast<Section_prop *>(sec);
	assert(section);

	if (!section->Get_bool("disney")) {
		DISNEY_ShutDown(nullptr);
		return;
	}
	disney = std::make_unique<Disney>(section->Get_string("disney_filter"));

	sec->AddDestroyFunction(&DISNEY_ShutDown, true);
}
