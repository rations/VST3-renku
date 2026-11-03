//-----------------------------------------------------------------------------
// Project     : VST SDK
//
// Category    : AudioHost
// Filename    : public.sdk/samples/vst-hosting/audiohost/source/media/mediakit/mediakitclient.cpp
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

// The Media Kit counterpart of jack/jackclient.cpp. It does not talk to the
// Media Kit itself: it drives JackDAW's Media Kit backend (one media node
// that is a buffer producer and consumer, slaved to the system time source,
// period taken from the system mixer), which offers the JACK-shaped calls
// of engine/audio_backend.h. That code is kept unchanged in haiku/jdbackend
// of this repository.
//
// Threads: every callback (process, buffer_size) runs on the node's control
// thread, one at a time, so the IAudioClient is never re-entered. The sample
// rate and the largest block are given to the IAudioClient before the node
// starts, and a later period change only changes numSamples (the backend
// never goes above JD_MAX_PERIOD or its period at open, whichever is
// longer), so the plug-in is not restarted when the buffer size changes.

#include "public.sdk/samples/vst-hosting/audiohost/source/media/imediaserver.h"
#include "public.sdk/samples/vst-hosting/audiohost/source/media/mediakit/mediakitclient.h"

#include "engine/audio_backend.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

//------------------------------------------------------------------------
namespace Steinberg {
namespace Vst {

//------------------------------------------------------------------------
//  MediaKitClient
//------------------------------------------------------------------------
class MediaKitClient : public IMediaServer
{
public:
//--------------------------------------------------------------------
	MediaKitClient () = default;
	~MediaKitClient () override;

	// IMediaServer interface
	bool registerAudioClient (IAudioClient* client) override;
	bool registerMidiClient (IMidiClient* client) override;

	bool initialize (const AudioClientName& name);

	// backend callbacks (node control thread)
	int process (jd_nframes_t nframes);
	void xrun () { xruns.fetch_add (1, std::memory_order_relaxed); }

	const JdBackend* backend = nullptr;
	std::string error;
	std::vector<JdPort*> audioInputPorts;
	std::atomic<int32_t> xruns {0};

	static constexpr int32_t kMaxMidiPorts = 16;
	JdPort* midiInputPorts[kMaxMidiPorts] {};
	std::atomic<int32_t> numMidiPorts {0};

//--------------------------------------------------------------------
private:
	void processMidi (jd_nframes_t nframes);

	bool opened = false;
	bool monoToStereo = false; // a single output channel also feeds out_2
	std::vector<JdPort*> audioOutputPorts;
	std::vector<float*> audioOutputPointers;
	std::vector<float*> audioInputPointers;
	IAudioClient::Buffers buffers {nullptr, 0, nullptr, 0, 0};
	std::atomic<IAudioClient*> audioClient {nullptr};
	std::atomic<IMidiClient*> midiClient {nullptr};
	int64_t frameCounter = 0;
};

//------------------------------------------------------------------------
// The one media server of this process (the Media Kit node is one per
// process). Set and cleared on the thread that creates and destroys the
// audio client; the MediaKit:: calls below are made from that thread too.
static MediaKitClient* gMediaKitClient = nullptr;
static std::string gLastError;

//------------------------------------------------------------------------
static int mediakit_on_process (jd_nframes_t nframes, void* arg)
{
	return reinterpret_cast<MediaKitClient*> (arg)->process (nframes);
}

//------------------------------------------------------------------------
static void mediakit_on_xrun (void* arg)
{
	reinterpret_cast<MediaKitClient*> (arg)->xrun ();
}

//------------------------------------------------------------------------
IMediaServerPtr createMediaServer (const AudioClientName& name)
{
	auto client = std::make_shared<MediaKitClient> ();
	if (!client->initialize (name))
		fprintf (stderr, "[MediaKit] %s\n", client->error.data ());
	return client;
}

//------------------------------------------------------------------------
MediaKitClient::~MediaKitClient ()
{
	if (opened)
		backend->close (); // stops the cycles; all ports go with the node
	if (gMediaKitClient == this)
		gMediaKitClient = nullptr;
}

//------------------------------------------------------------------------
bool MediaKitClient::initialize (const AudioClientName& name)
{
	if (gMediaKitClient)
	{
		error = "only one Media Kit audio client per process";
		gLastError = error;
		return false;
	}
	backend = jackdaw_mediakit_backend_entry (JD_BACKEND_ABI);
	if (!backend)
	{
		error = "the Media Kit backend does not match its header";
		gLastError = error;
		return false;
	}

	jackdaw_mediakit_set_node_name (name.data ());

	JdBackendCallbacks callbacks {};
	callbacks.process = mediakit_on_process;
	callbacks.xrun = mediakit_on_xrun;
	char message[256] = "";
	if (backend->open (name.data (), &callbacks, this, message, sizeof (message)) != 0)
	{
		error = message[0] ? message : "could not open the Media Kit node";
		gLastError = error;
		return false;
	}
	opened = true;
	gMediaKitClient = this;
	gLastError.clear ();
	return true;
}

//------------------------------------------------------------------------
bool MediaKitClient::registerAudioClient (IAudioClient* client)
{
	if (!opened || audioClient.load () || !client)
		return false;

	// One mono port per channel: out_1, out_2, ... (out_1 and out_2 are the
	// node's "Master" output) and in_1, in_2, ...
	const auto ioSetup = client->getIOSetup ();
	char portName[32];
	for (size_t i = 0; i < ioSetup.outputs.size (); ++i)
	{
		snprintf (portName, sizeof (portName), "out_%u", (unsigned)(i + 1));
		auto port = backend->port_register (portName, JD_PORT_AUDIO, JD_PORT_OUTPUT);
		if (!port)
		{
			error = std::string ("could not register ") + portName;
			gLastError = error;
			return false;
		}
		audioOutputPorts.push_back (port);
	}
	if (audioOutputPorts.size () == 1)
	{
		auto port = backend->port_register ("out_2", JD_PORT_AUDIO, JD_PORT_OUTPUT);
		if (port)
		{
			audioOutputPorts.push_back (port);
			monoToStereo = true;
		}
	}
	for (size_t i = 0; i < ioSetup.inputs.size (); ++i)
	{
		snprintf (portName, sizeof (portName), "in_%u", (unsigned)(i + 1));
		auto port = backend->port_register (portName, JD_PORT_AUDIO, JD_PORT_INPUT);
		if (!port)
		{
			error = std::string ("could not register ") + portName;
			gLastError = error;
			return false;
		}
		audioInputPorts.push_back (port);
	}

	// Each array is sized by its own port count.
	audioOutputPointers.assign (audioOutputPorts.size (), nullptr);
	audioInputPointers.assign (audioInputPorts.size (), nullptr);
	buffers.outputs = audioOutputPointers.data ();
	buffers.numOutputs = monoToStereo ? 1 : static_cast<int32_t> (audioOutputPointers.size ());
	buffers.inputs = audioInputPointers.data ();
	buffers.numInputs = static_cast<int32_t> (audioInputPointers.size ());

	// The rate first: AudioClient only prepares its buffers once it has both,
	// and does so in setBlockSize.
	const auto rate = backend->sample_rate ();
	const auto period = backend->buffer_size ();
	const auto maxBlock = std::max<jd_nframes_t> (period, JD_MAX_PERIOD);
	if (!client->setSamplerate (static_cast<SampleRate> (rate)) ||
	    !client->setBlockSize (static_cast<int32> (maxBlock)))
	{
		error = "the plug-in refused the Media Kit sample rate or block size";
		gLastError = error;
		return false;
	}

	audioClient.store (client, std::memory_order_release);
	if (backend->activate () != 0)
	{
		audioClient.store (nullptr, std::memory_order_release);
		error = "could not start the Media Kit node";
		gLastError = error;
		return false;
	}
	return true;
}

//------------------------------------------------------------------------
bool MediaKitClient::registerMidiClient (IMidiClient* client)
{
	if (!opened || midiClient.load () || !client)
		return false;

	// The node may already be running: a port is published to the process
	// callback only after it is registered.
	const auto ioSetup = client->getMidiIOSetup ();
	char portName[32];
	for (size_t i = 0; i < ioSetup.inputs.size (); ++i)
	{
		const int32_t index = numMidiPorts.load (std::memory_order_relaxed);
		if (index >= kMaxMidiPorts)
			break;
		snprintf (portName, sizeof (portName), "midi_in_%u", (unsigned)(i + 1));
		auto port = backend->port_register (portName, JD_PORT_MIDI, JD_PORT_INPUT);
		if (!port)
			break;
		midiInputPorts[index] = port;
		numMidiPorts.store (index + 1, std::memory_order_release);
	}
	midiClient.store (client, std::memory_order_release);
	return true;
}

//------------------------------------------------------------------------
void MediaKitClient::processMidi (jd_nframes_t nframes)
{
	auto* client = midiClient.load (std::memory_order_acquire);
	const int32_t count = numMidiPorts.load (std::memory_order_acquire);
	for (int32_t portIndex = 0; portIndex < count; ++portIndex)
	{
		void* portBuffer = backend->port_buffer (midiInputPorts[portIndex], nframes);
		if (!portBuffer)
			continue;
		const uint32_t numEvents = backend->midi_event_count (portBuffer);
		for (uint32_t i = 0; i < numEvents; ++i)
		{
			JdMidiEvent event;
			if (backend->midi_event_get (&event, portBuffer, i) != 0 || event.size == 0 ||
			    !client)
				continue;
			const uint8_t status = event.buffer[0];
			if (status < 0x80 || status >= 0xF0) // running status, system messages
				continue;
			const IMidiClient::MidiData data0 = event.size > 1 ? event.buffer[1] : 0;
			const IMidiClient::MidiData data1 = event.size > 2 ? event.buffer[2] : 0;
			client->onEvent ({static_cast<IMidiClient::MidiData> (status & 0xF0),
			                  static_cast<IMidiClient::MidiData> (status & 0x0F), data0, data1,
			                  static_cast<int64_t> (event.time)},
			                 portIndex);
		}
	}
}

//------------------------------------------------------------------------
int MediaKitClient::process (jd_nframes_t nframes)
{
	auto* client = audioClient.load (std::memory_order_acquire);
	for (size_t i = 0; i < audioOutputPorts.size (); ++i)
		audioOutputPointers[i] = static_cast<float*> (backend->port_buffer (audioOutputPorts[i], nframes));
	for (size_t i = 0; i < audioInputPorts.size (); ++i)
		audioInputPointers[i] = static_cast<float*> (backend->port_buffer (audioInputPorts[i], nframes));
	buffers.numSamples = static_cast<int32_t> (nframes);

	processMidi (nframes);

	// A stopped or not yet started plug-in, or one that fails, is silent.
	if (!client || !client->process (buffers, frameCounter))
	{
		for (auto* out : audioOutputPointers)
			if (out)
				memset (out, 0, nframes * sizeof (float));
	}
	else if (monoToStereo && audioOutputPointers[0] && audioOutputPointers[1])
	{
		memcpy (audioOutputPointers[1], audioOutputPointers[0], nframes * sizeof (float));
	}
	frameCounter += nframes;
	return 0;
}

//------------------------------------------------------------------------
namespace MediaKit {

//------------------------------------------------------------------------
static std::vector<std::string> portList (int type, int flags)
{
	std::vector<std::string> result;
	if (!gMediaKitClient)
		return result;
	const auto* backend = gMediaKitClient->backend;
	if (const char** ports = backend->get_ports (type, flags))
	{
		for (int i = 0; ports[i]; ++i)
			result.emplace_back (ports[i]);
		backend->free_ports (ports);
	}
	return result;
}

//------------------------------------------------------------------------
bool isRunning () { return gMediaKitClient != nullptr; }
std::string lastError () { return gLastError; }

//------------------------------------------------------------------------
uint32_t sampleRate ()
{
	return gMediaKitClient ? gMediaKitClient->backend->sample_rate () : 0;
}

//------------------------------------------------------------------------
uint32_t blockSize ()
{
	return gMediaKitClient ? gMediaKitClient->backend->buffer_size () : 0;
}

//------------------------------------------------------------------------
int32_t xrunCount ()
{
	return gMediaKitClient ? gMediaKitClient->xruns.load (std::memory_order_relaxed) : 0;
}

//------------------------------------------------------------------------
int32_t numAudioInputs ()
{
	return gMediaKitClient ? static_cast<int32_t> (gMediaKitClient->audioInputPorts.size ()) : 0;
}

//------------------------------------------------------------------------
int32_t numMidiInputs ()
{
	return gMediaKitClient ? gMediaKitClient->numMidiPorts.load (std::memory_order_acquire) : 0;
}

//------------------------------------------------------------------------
std::vector<std::string> audioSources ()
{
	return portList (JD_PORT_AUDIO, JD_PORT_OUTPUT | JD_PORT_PHYSICAL);
}

//------------------------------------------------------------------------
std::vector<std::string> midiSources ()
{
	return portList (JD_PORT_MIDI, JD_PORT_OUTPUT);
}

//------------------------------------------------------------------------
static bool connectPort (JdPort* port, const std::string& source, bool connect)
{
	if (!port)
		return false;
	const auto* backend = gMediaKitClient->backend;
	const char* destination = backend->port_name (port);
	if (!destination)
		return false;
	return (connect ? backend->connect (source.data (), destination)
	                : backend->disconnect (source.data (), destination)) == 0;
}

//------------------------------------------------------------------------
bool connectAudioInput (int32_t input, const std::string& source)
{
	if (input < 0 || input >= numAudioInputs ())
		return false;
	return connectPort (gMediaKitClient->audioInputPorts[input], source, true);
}

//------------------------------------------------------------------------
bool disconnectAudioInput (int32_t input, const std::string& source)
{
	if (input < 0 || input >= numAudioInputs ())
		return false;
	return connectPort (gMediaKitClient->audioInputPorts[input], source, false);
}

//------------------------------------------------------------------------
bool connectMidiInput (int32_t input, const std::string& source)
{
	if (input < 0 || input >= numMidiInputs ())
		return false;
	return connectPort (gMediaKitClient->midiInputPorts[input], source, true);
}

//------------------------------------------------------------------------
} // MediaKit
} // Vst
} // Steinberg
