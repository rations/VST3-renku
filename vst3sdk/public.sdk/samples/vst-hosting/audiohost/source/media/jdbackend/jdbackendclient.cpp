//-----------------------------------------------------------------------------
// Project     : VST SDK
//
// Category    : AudioHost
// Filename    : public.sdk/samples/vst-hosting/audiohost/source/media/jdbackend/jdbackendclient.cpp
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

// The counterpart of jack/jackclient.cpp for Haiku. It does not talk to an
// audio system itself: it drives one of JackDAW's audio backends (the table of
// JACK-shaped calls in engine/audio_backend.h), kept unchanged in haiku/jdbackend
// of this repository: the Media Kit node (one media node that is a buffer
// producer and consumer, slaved to the system time source, period taken from
// the system mixer) or JACK.
//
// Threads: the backend calls process and buffer_size on its audio thread (the
// node's control thread, or JACK's process thread), one at a time, so the
// IAudioClient is never re-entered. The sample rate and the largest block are
// given to the IAudioClient before the backend starts, and a later period
// change only changes numSamples (a longer period than the plug-in was
// prepared for is processed in pieces), so the plug-in is not restarted when
// the buffer size changes.

#include "public.sdk/samples/vst-hosting/audiohost/source/media/imediaserver.h"
#include "public.sdk/samples/vst-hosting/audiohost/source/media/jdbackend/jdbackendclient.h"

#include "engine/audio_backend.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>

//------------------------------------------------------------------------
namespace Steinberg {
namespace Vst {

//------------------------------------------------------------------------
//  JdBackendClient
//------------------------------------------------------------------------
class JdBackendClient : public IMediaServer
{
public:
//--------------------------------------------------------------------
	explicit JdBackendClient (const JdBackend* backend) : backend (backend) {}
	~JdBackendClient () override;

	// IMediaServer interface
	bool registerAudioClient (IAudioClient* client) override;
	bool registerMidiClient (IMidiClient* client) override;

	bool initialize (const AudioClientName& name);

	// backend callbacks (audio thread, except shutdown)
	int process (jd_nframes_t nframes);
	void xrun () { xruns.fetch_add (1, std::memory_order_relaxed); }
	void shutdown () { serverGone.store (true, std::memory_order_relaxed); }

	const JdBackend* backend = nullptr;
	std::string error;
	std::vector<JdPort*> audioInputPorts;
	std::vector<std::vector<std::string>> inputConnections; // made through HostAudio::
	std::atomic<int32_t> xruns {0};
	std::atomic<bool> serverGone {false};

	static constexpr int32_t kMaxMidiPorts = 16;
	JdPort* midiInputPorts[kMaxMidiPorts] {};
	std::atomic<int32_t> numMidiPorts {0};
	std::vector<std::string> midiConnections[kMaxMidiPorts]; // made through HostAudio::

//--------------------------------------------------------------------
private:
	void processMidi (jd_nframes_t nframes);
	void connectPlayback ();

	bool opened = false;
	bool monoToStereo = false; // a single output channel also feeds out_2
	jd_nframes_t maxBlock = 0; // the largest block the plug-in is prepared for
	std::vector<JdPort*> audioOutputPorts;
	std::vector<float*> audioOutputPointers;
	std::vector<float*> audioInputPointers;
	std::vector<float*> pieceOutputPointers;
	std::vector<float*> pieceInputPointers;
	IAudioClient::Buffers buffers {nullptr, 0, nullptr, 0, 0};
	std::atomic<IAudioClient*> audioClient {nullptr};
	std::atomic<IMidiClient*> midiClient {nullptr};
	int64_t frameCounter = 0;
};

//------------------------------------------------------------------------
// The backend the next media server uses, and the one media server of this
// process (each backend is one node or client per process). Set and cleared
// on the thread that creates and destroys the audio client; the HostAudio::
// calls below are made from that thread too.
static const JdBackend* gSelectedBackend = nullptr;
static JdBackendClient* gClient = nullptr;
static std::string gLastError;

//------------------------------------------------------------------------
static const JdBackend* selectedBackend ()
{
	if (!gSelectedBackend)
		gSelectedBackend = jackdaw_mediakit_backend_entry (JD_BACKEND_ABI);
	return gSelectedBackend;
}

//------------------------------------------------------------------------
static int jdbackend_on_process (jd_nframes_t nframes, void* arg)
{
	return reinterpret_cast<JdBackendClient*> (arg)->process (nframes);
}

//------------------------------------------------------------------------
static void jdbackend_on_xrun (void* arg)
{
	reinterpret_cast<JdBackendClient*> (arg)->xrun ();
}

//------------------------------------------------------------------------
static void jdbackend_on_shutdown (void* arg)
{
	reinterpret_cast<JdBackendClient*> (arg)->shutdown ();
}

//------------------------------------------------------------------------
IMediaServerPtr createMediaServer (const AudioClientName& name)
{
	auto client = std::make_shared<JdBackendClient> (selectedBackend ());
	if (!client->initialize (name))
		fprintf (stderr, "[audio] %s\n", client->error.data ());
	return client;
}

//------------------------------------------------------------------------
JdBackendClient::~JdBackendClient ()
{
	if (opened)
		backend->close (); // stops the cycles; all ports go with it
	if (gClient == this)
		gClient = nullptr;
}

//------------------------------------------------------------------------
bool JdBackendClient::initialize (const AudioClientName& name)
{
	if (gClient)
	{
		error = "only one audio client per process";
		gLastError = error;
		return false;
	}
	if (!backend)
	{
		error = "the audio backend does not match its header";
		gLastError = error;
		return false;
	}

	if (backend == jackdaw_mediakit_backend_entry (JD_BACKEND_ABI))
		jackdaw_mediakit_set_node_name (name.data ());

	JdBackendCallbacks callbacks {};
	callbacks.process = jdbackend_on_process;
	callbacks.xrun = jdbackend_on_xrun;
	callbacks.shutdown = jdbackend_on_shutdown;
	char message[256] = "";
	if (backend->open (name.data (), &callbacks, this, message, sizeof (message)) != 0)
	{
		error = message[0] ? message : "could not open the audio backend";
		gLastError = error;
		return false;
	}
	opened = true;
	gClient = this;
	gLastError.clear ();
	return true;
}

//------------------------------------------------------------------------
bool JdBackendClient::registerAudioClient (IAudioClient* client)
{
	if (!opened || audioClient.load () || !client)
		return false;

	// One mono port per channel: out_1, out_2, ... (on the Media Kit out_1 and
	// out_2 are the node's "Master" output) and in_1, in_2, ...
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
	inputConnections.assign (audioInputPorts.size (), {});

	// Each array is sized by its own port count.
	audioOutputPointers.assign (audioOutputPorts.size (), nullptr);
	audioInputPointers.assign (audioInputPorts.size (), nullptr);
	pieceOutputPointers.assign (audioOutputPorts.size (), nullptr);
	pieceInputPointers.assign (audioInputPorts.size (), nullptr);
	buffers.numOutputs = monoToStereo ? 1 : static_cast<int32_t> (audioOutputPointers.size ());
	buffers.numInputs = static_cast<int32_t> (audioInputPointers.size ());

	// The rate first: AudioClient only prepares its buffers once it has both,
	// and does so in setBlockSize.
	const auto rate = backend->sample_rate ();
	maxBlock = std::max<jd_nframes_t> (backend->buffer_size (), JD_MAX_PERIOD);
	if (!client->setSamplerate (static_cast<SampleRate> (rate)) ||
	    !client->setBlockSize (static_cast<int32> (maxBlock)))
	{
		error = "the plug-in refused the sample rate or block size";
		gLastError = error;
		return false;
	}

	audioClient.store (client, std::memory_order_release);
	if (backend->activate () != 0)
	{
		audioClient.store (nullptr, std::memory_order_release);
		error = "could not start the audio backend";
		gLastError = error;
		return false;
	}
	connectPlayback ();
	return true;
}

//------------------------------------------------------------------------
// JACK: the first two outputs go to the first two physical playback ports, as
// jackclient.cpp does. The Media Kit lists none (the node connects its Master
// output to the system mixer itself).
void JdBackendClient::connectPlayback ()
{
	const char** ports = backend->get_ports (JD_PORT_AUDIO, JD_PORT_INPUT | JD_PORT_PHYSICAL);
	if (!ports)
		return;
	for (size_t i = 0; i < audioOutputPorts.size () && i < 2 && ports[i]; ++i)
	{
		const char* name = backend->port_name (audioOutputPorts[i]);
		if (name && backend->connect (name, ports[i]) != 0)
			fprintf (stderr, "[audio] could not connect %s to %s\n", name, ports[i]);
	}
	backend->free_ports (ports);
}

//------------------------------------------------------------------------
bool JdBackendClient::registerMidiClient (IMidiClient* client)
{
	if (!opened || midiClient.load () || !client)
		return false;

	// The backend may already be running: a port is published to the process
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
void JdBackendClient::processMidi (jd_nframes_t nframes)
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
int JdBackendClient::process (jd_nframes_t nframes)
{
	auto* client = audioClient.load (std::memory_order_acquire);
	for (size_t i = 0; i < audioOutputPorts.size (); ++i)
		audioOutputPointers[i] = static_cast<float*> (backend->port_buffer (audioOutputPorts[i], nframes));
	for (size_t i = 0; i < audioInputPorts.size (); ++i)
		audioInputPointers[i] = static_cast<float*> (backend->port_buffer (audioInputPorts[i], nframes));

	processMidi (nframes);

	// In pieces of at most the block the plug-in was prepared for (JACK can be
	// given a longer period while running). MIDI events all go to the first.
	bool ok = client != nullptr;
	for (jd_nframes_t done = 0; ok && done < nframes;)
	{
		const jd_nframes_t n = std::min (nframes - done, maxBlock);
		for (size_t i = 0; i < pieceOutputPointers.size (); ++i)
			pieceOutputPointers[i] = audioOutputPointers[i] ? audioOutputPointers[i] + done : nullptr;
		for (size_t i = 0; i < pieceInputPointers.size (); ++i)
			pieceInputPointers[i] = audioInputPointers[i] ? audioInputPointers[i] + done : nullptr;
		buffers.outputs = pieceOutputPointers.data ();
		buffers.inputs = pieceInputPointers.data ();
		buffers.numSamples = static_cast<int32_t> (n);
		ok = client->process (buffers, frameCounter + done);
		done += n;
	}

	// A stopped or not yet started plug-in, or one that fails, is silent.
	if (!ok)
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
namespace HostAudio {

//------------------------------------------------------------------------
bool selectBackend (const std::string& which, const std::string& appName,
                    const std::string& envVar, std::string& error)
{
	char message[512] = "";
	const JdBackend* backend =
	    audio_backend_load_for (which.data (), appName.data (),
	                            envVar.empty () ? nullptr : envVar.data (), message, sizeof (message));
	if (!backend)
	{
		error = message[0] ? message : "unknown audio backend";
		return false;
	}
	gSelectedBackend = backend;
	return true;
}

//------------------------------------------------------------------------
std::string backendName ()
{
	const JdBackend* backend = gClient ? gClient->backend : selectedBackend ();
	return backend && backend->name ? backend->name : "";
}

//------------------------------------------------------------------------
static std::vector<std::string> portList (int type, int flags)
{
	std::vector<std::string> result;
	if (!gClient)
		return result;
	const auto* backend = gClient->backend;
	if (const char** ports = backend->get_ports (type, flags))
	{
		for (int i = 0; ports[i]; ++i)
			result.emplace_back (ports[i]);
		backend->free_ports (ports);
	}
	return result;
}

//------------------------------------------------------------------------
bool isRunning ()
{
	return gClient != nullptr && !gClient->serverGone.load (std::memory_order_relaxed);
}

//------------------------------------------------------------------------
std::string lastError ()
{
	if (gClient && gClient->serverGone.load (std::memory_order_relaxed))
		return "the audio server went away";
	return gLastError;
}

//------------------------------------------------------------------------
uint32_t sampleRate ()
{
	return gClient ? gClient->backend->sample_rate () : 0;
}

//------------------------------------------------------------------------
uint32_t blockSize ()
{
	return gClient ? gClient->backend->buffer_size () : 0;
}

//------------------------------------------------------------------------
int32_t xrunCount ()
{
	return gClient ? gClient->xruns.load (std::memory_order_relaxed) : 0;
}

//------------------------------------------------------------------------
int32_t numAudioInputs ()
{
	return gClient ? static_cast<int32_t> (gClient->audioInputPorts.size ()) : 0;
}

//------------------------------------------------------------------------
int32_t numMidiInputs ()
{
	return gClient ? gClient->numMidiPorts.load (std::memory_order_acquire) : 0;
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
std::vector<std::string> audioInputConnections (int32_t input)
{
	if (input < 0 || input >= numAudioInputs ())
		return {};
	return gClient->inputConnections[static_cast<size_t> (input)];
}

//------------------------------------------------------------------------
std::vector<std::string> midiInputConnections (int32_t input)
{
	if (input < 0 || input >= numMidiInputs ())
		return {};
	return gClient->midiConnections[input];
}

//------------------------------------------------------------------------
static bool connectPort (JdPort* port, const std::string& source, bool connect)
{
	if (!port)
		return false;
	const auto* backend = gClient->backend;
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
	if (!connectPort (gClient->audioInputPorts[static_cast<size_t> (input)], source, true))
		return false;
	auto& list = gClient->inputConnections[static_cast<size_t> (input)];
	if (std::find (list.begin (), list.end (), source) == list.end ())
		list.push_back (source);
	return true;
}

//------------------------------------------------------------------------
bool disconnectAudioInput (int32_t input, const std::string& source)
{
	if (input < 0 || input >= numAudioInputs ())
		return false;
	if (!connectPort (gClient->audioInputPorts[static_cast<size_t> (input)], source, false))
		return false;
	auto& list = gClient->inputConnections[static_cast<size_t> (input)];
	list.erase (std::remove (list.begin (), list.end (), source), list.end ());
	return true;
}

//------------------------------------------------------------------------
bool connectMidiInput (int32_t input, const std::string& source)
{
	if (input < 0 || input >= numMidiInputs ())
		return false;
	if (!connectPort (gClient->midiInputPorts[input], source, true))
		return false;
	auto& list = gClient->midiConnections[input];
	if (std::find (list.begin (), list.end (), source) == list.end ())
		list.push_back (source);
	return true;
}

//------------------------------------------------------------------------
bool disconnectMidiInput (int32_t input, const std::string& source)
{
	if (input < 0 || input >= numMidiInputs ())
		return false;
	if (!connectPort (gClient->midiInputPorts[input], source, false))
		return false;
	auto& list = gClient->midiConnections[input];
	list.erase (std::remove (list.begin (), list.end (), source), list.end ());
	return true;
}

//------------------------------------------------------------------------
} // HostAudio
} // Vst
} // Steinberg
