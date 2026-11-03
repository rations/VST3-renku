//-----------------------------------------------------------------------------
// Project     : VST SDK
//
// Category    : AudioHost
// Filename    : public.sdk/samples/vst-hosting/audiohost/source/media/mediakit/mediakitclient.h
// Created by  : rations 11.2026
// Description : Audio Host Example for VST 3 using the Haiku/RenkuOS Media Kit
//
//-----------------------------------------------------------------------------
// This file is part of a Steinberg SDK. It is subject to the license terms
// in the LICENSE file found in the top-level directory of this distribution
// and at www.steinberg.net/sdklicenses.
// No part of the SDK, including this file, may be copied, modified, propagated,
// or distributed except according to the terms contained in the LICENSE file.
//-----------------------------------------------------------------------------

#pragma once

#include <cstdint>
#include <string>
#include <vector>

//------------------------------------------------------------------------
// Haiku-only extras of the Media Kit media server (mediakitclient.cpp).
//
// The media server makes one media node, named after the audio client
// (Cortex shows it). Its "Master" output (the plug-in's first two output
// channels) is connected to the system mixer when it starts. Inputs are not
// connected automatically, as with JACK: a host connects them with these
// calls, or the user patches them in Cortex. MIDI inputs are Midi Kit
// endpoints "<name> MIDI in N", patched in PatchBay or connected here.
//
// The Media Kit node is one per process, so there is one media server.
// All calls are non-real-time and are meant for the thread that created the
// audio client.
//------------------------------------------------------------------------
namespace Steinberg {
namespace Vst {
namespace MediaKit {

//! True once the media server has opened its node; otherwise lastError says why.
bool isRunning ();
std::string lastError ();

uint32_t sampleRate ();
//! The current cycle length in frames (it follows the system mixer's buffer size).
uint32_t blockSize ();
//! The backend's xrun notices: cycles that started more than a period late, or
//! outputs that had no free buffer. The node runs ahead of the mixer by its event
//! latency, so a late cycle is not necessarily an audible dropout.
int32_t xrunCount ();

int32_t numAudioInputs ();
int32_t numMidiInputs ();

//! Capture sources ("media:<node> / <output> / <channel>"), one per channel.
std::vector<std::string> audioSources ();
//! MIDI producers on the roster ("midi:<endpoint>").
std::vector<std::string> midiSources ();

//! Connects one source channel to audio input `input` (0-based). Only one
//! source per input.
bool connectAudioInput (int32_t input, const std::string& source);
bool disconnectAudioInput (int32_t input, const std::string& source);
//! Connects a MIDI producer to MIDI input `input` (0-based).
bool connectMidiInput (int32_t input, const std::string& source);

//------------------------------------------------------------------------
} // MediaKit
} // Vst
} // Steinberg
