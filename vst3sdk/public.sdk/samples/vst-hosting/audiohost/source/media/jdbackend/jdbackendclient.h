//-----------------------------------------------------------------------------
// Project     : VST SDK
//
// Category    : AudioHost
// Filename    : public.sdk/samples/vst-hosting/audiohost/source/media/jdbackend/jdbackendclient.h
// Created by  : rations 11.2026
// Description : Audio Host Example for VST 3 on the Haiku/RenkuOS Media Kit or JACK
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
// Haiku-only extras of the media server in jdbackendclient.cpp.
//
// The media server runs on one of JackDAW's audio backends: the native Media
// Kit (built in, the default) or JACK (an add-on, jack.so, loaded only when
// chosen, so the host does not need libjack). Choose one with selectBackend
// before creating the audio client.
//
// Media Kit: one media node, named after the audio client (Cortex shows it).
// Its "Master" output (the plug-in's first two output channels) is connected
// to the system mixer when it starts. MIDI inputs are Midi Kit endpoints
// "<name> MIDI in N".
// JACK: one client named after the audio client; its first two outputs are
// connected to the first two physical playback ports when it starts.
// Either way inputs are not connected automatically: a host connects them
// with these calls. On the Media Kit a source patched to the node in Cortex
// shows up in audioSources() and still has to be connected to an input.
//
// There is one media server per process (each backend is one node or client
// per process). All calls are non-real-time and are meant for the thread that
// creates the audio client.
//------------------------------------------------------------------------
namespace Steinberg {
namespace Vst {
namespace HostAudio {

//! Chooses the backend for the next audio client: "mediakit" (the default) or
//! "jack". For JACK the jack.so add-on is looked for in $<envVar>, next to the
//! executable in backends/, and in <appName>/ in the add-ons directories.
//! False, with a message for the user in error, if it cannot be loaded.
bool selectBackend (const std::string& which, const std::string& appName,
                    const std::string& envVar, std::string& error);
//! The selected backend's display name ("Media Kit", "JACK").
std::string backendName ();

//! True while the media server runs; otherwise lastError says why.
bool isRunning ();
std::string lastError ();

uint32_t sampleRate ();
//! The current cycle length in frames (it can change while running).
uint32_t blockSize ();
//! The backend's xrun notices. On the Media Kit: cycles that started more
//! than a period late, or outputs that had no free buffer; the node runs ahead
//! of the mixer by its event latency, so a late cycle is not necessarily an
//! audible dropout. On JACK: the server's xruns.
int32_t xrunCount ();

int32_t numAudioInputs ();
int32_t numMidiInputs ();

//! Capture sources, one per channel ("media:<node> / <output> / <channel>" on
//! the Media Kit, "system:capture_1" ... on JACK).
std::vector<std::string> audioSources ();
//! MIDI sources ("midi:<endpoint>" on the Media Kit, JACK MIDI ports on JACK).
std::vector<std::string> midiSources ();
//! The sources connected to audio input `input` (0-based) through these calls.
std::vector<std::string> audioInputConnections (int32_t input);
//! The sources connected to MIDI input `input` (0-based) through these calls.
std::vector<std::string> midiInputConnections (int32_t input);

//! Connects one source channel to audio input `input` (0-based).
bool connectAudioInput (int32_t input, const std::string& source);
bool disconnectAudioInput (int32_t input, const std::string& source);
//! Connects a MIDI source to MIDI input `input` (0-based).
bool connectMidiInput (int32_t input, const std::string& source);
bool disconnectMidiInput (int32_t input, const std::string& source);

//------------------------------------------------------------------------
} // HostAudio
} // Vst
} // Steinberg
