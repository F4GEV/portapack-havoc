/*
 * Copyright (C) 2015 Jared Boone, ShareBrained Technology, Inc.
 * Copyright (C) 2016 Furrtek
 * 
 * This file is part of PortaPack.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#include "ui_numbers.hpp"
#include "string_format.hpp"

#include "portapack.hpp"
#include "hackrf_hal.hpp"
#include "portapack_shared_memory.hpp"

#include <cstring>
#include <stdio.h>

using namespace portapack;

namespace ui {

void NumbersStationView::focus() {
	if (file_error)
		nav_.display_modal("No voices", "No valid voices found in\nthe /numbers directory.", ABORT, nullptr);
	else
		button_exit.focus();
}

NumbersStationView::~NumbersStationView() {
	transmitter_model.disable();
	baseband::shutdown();
}

void NumbersStationView::on_tuning_frequency_changed(rf::Frequency f) {
	transmitter_model.set_tuning_frequency(f);
}

void NumbersStationView::prepare_audio() {
	uint8_t code;
	
	/*if (sample_counter >= sample_duration) {
		if (segment == ANNOUNCE) {
			if (!announce_loop) {
				segment = MESSAGE;
			} else {
				reader->open("/numbers/announce.wav");
				sample_duration = sound_sizes[10];
				announce_loop--;
			}
		}
		
		if (segment == MESSAGE) {
			code = symfield_code.get_sym(code_index);
			
			if (code_index == 25)
				transmitter_model.disable();
			
			if (code >= 10) {
				memset(audio_buffer, 0, 1024);
				if (code == 10) {
					pause = 11025;		// p: 0.25s @ 44100Hz
				} else if (code == 11) {
					pause = 33075;		// P: 0.75s @ 44100Hz
				} else if (code == 12) {
					transmitter_model.disable();
				}
			} else {
				reader->open("/numbers/" + file_names[code] + ".wav");
				sample_duration = sound_sizes[code];
			}
			code_index++;
		}		
		sample_counter = 0;
	}
	
	if (!pause) {
		auto bytes_read = reader->read(audio_buffer, 1024).value();
		
		// Unsigned to signed, pretty stupid :/
		for (size_t n = 0; n < bytes_read; n++)
			audio_buffer[n] -= 0x80;
		for (size_t n = bytes_read; n < 1024; n++)
			audio_buffer[n] = 0;
		
		sample_counter += 1024;
	} else {
		if (pause >= 1024) {
			pause -= 1024;
		} else {
			sample_counter = sample_duration;
			pause = 0;
		}
	}
	
	baseband::set_fifo_data(audio_buffer);*/
}

void NumbersStationView::start_tx() {
	//sample_duration = sound_sizes[10];		// Announce
	sample_counter = sample_duration;
	
	code_index = 0;
	announce_loop = 2;
	segment = ANNOUNCE;
	
	prepare_audio();
	
	transmitter_model.set_sampling_rate(1536000U);
	transmitter_model.set_rf_amp(true);
	transmitter_model.set_baseband_bandwidth(1750000);
	transmitter_model.enable();
	
	baseband::set_audiotx_data(
		(1536000 / 44100) - 1,
		12000,
		1,
		false,
		0
	);
}

void NumbersStationView::on_tick_second() {
	if (check_armed.value()) {
		armed_blink = not armed_blink;
		
		if (armed_blink)
			check_armed.set_style(&style_red);
		else
			check_armed.set_style(&style());
		
		check_armed.set_dirty();
	}
}

void NumbersStationView::on_voice_changed(size_t index) {
	std::string flags_string = "";
	
	if (voices[index].accent) {
		flags_string = "^";
	}
	if (voices[index].announce) {
		flags_string += "A";
	}
	text_voice_flags.set(flags_string);
}

NumbersStationView::NumbersStationView(
	NavigationView& nav
) : nav_ (nav)
{
	std::vector<std::filesystem::path> directory_list;
	using option_t = std::pair<std::string, int32_t>;
	using options_t = std::vector<option_t>;
	options_t voice_options;
	uint32_t c, i, ia;
	bool valid;
	//uint8_t y, m, d, dayofweek;
	
	reader = std::make_unique<WAVFileReader>();
	
	// Search for valid voice directories
	directory_list = scan_root_directories("/numbers");
	if (!directory_list.size()) {
		file_error = true;
		return;
	}
	// This is awfully repetitive
	for (const auto& dir : directory_list) {
		i = 0;
		for (const auto& file_name : file_names) {
			valid = false;
			if (reader->open("/numbers/" + dir.string() + "/" + file_name + ".wav")) {
				// Check format (mono, 8 bits)
				if ((reader->channels() == 1) && (reader->bits_per_sample() == 8))
					valid = true;
			}
			if (!valid) {
				if (i < 10)
					i = 0;	// Missingno, invalid voice
				break;
			}
			i++;
		}
		if (i) {
			// Voice ok, are there accent files ?
			ia = 0;
			for (const auto& file_name : file_names) {
				valid = false;
				if (reader->open("/numbers/" + dir.string() + "/" + file_name + "a.wav")) {
					// Check format (mono, 8 bits)
					if ((reader->channels() == 1) && (reader->bits_per_sample() == 8))
						valid = true;
				}
				if (!valid)
					break;
				ia++;
			}
			
			voices.push_back({ dir.string(), (ia >= 10), (i == 11) });
		}
	}
	if (!voices.size()) {
		file_error = true;
		return;
	}
	
	baseband::run_image(portapack::spi_flash::image_tag_audio_tx);
	
	add_children({
		&labels,
		&symfield_code,
		&check_armed,
		&options_voices,
		&text_voice_flags,
		//&button_tx_now,
		&button_exit
	});
	
	for (const auto& voice : voices)
		voice_options.emplace_back(voice.dir.substr(0, 4), 0);
	
	options_voices.set_options(voice_options);
	options_voices.on_change = [this](size_t i, int32_t) {
		this->on_voice_changed(i);
	};
	options_voices.set_selected_index(0);

	check_armed.set_value(false);
	
	check_armed.on_select = [this](Checkbox&, bool v) {
		if (v) {
			armed_blink = false;
			signal_token_tick_second = rtc_time::signal_tick_second += [this]() {
				this->on_tick_second();
			};
		} else {
			check_armed.set_style(&style());
			rtc_time::signal_tick_second -= signal_token_tick_second;
		}
	};
	
	for (c = 0; c < 25; c++)
		symfield_code.set_symbol_list(c, "0123456789pPE");
	
	// DEBUG
	symfield_code.set_sym(0, 10);
	symfield_code.set_sym(1, 3);
	symfield_code.set_sym(2, 4);
	symfield_code.set_sym(3, 11);
	symfield_code.set_sym(4, 6);
	symfield_code.set_sym(5, 1);
	symfield_code.set_sym(6, 9);
	symfield_code.set_sym(7, 7);
	symfield_code.set_sym(8, 8);
	symfield_code.set_sym(9, 0);
	symfield_code.set_sym(10, 12);	// End

/*
	rtc::RTC datetime;
	rtcGetTime(&RTCD1, &datetime);
	
	// Thanks, Sakamoto-sama !
	y = datetime.year();
	m = datetime.month();
	d = datetime.day();
	y -= m < 3;
	dayofweek = (y + y/4 - y/100 + y/400 + month_table[m-1] + d) % 7;
	
	text_title.set(day_of_week[dayofweek]);
*/

	button_exit.on_select = [&nav](Button&){
		nav.pop();
	};
}

} /* namespace ui */
