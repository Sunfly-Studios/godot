/**************************************************************************/
/*  GodotTTS.java                                                         */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

package org.godotengine.godot.tts;

import org.godotengine.godot.GodotLib;

import android.app.Activity;
import android.content.Context;
import android.os.Build;
import android.os.Bundle;
import android.speech.tts.TextToSpeech;
import android.speech.tts.UtteranceProgressListener;
import android.util.Log;

import androidx.annotation.Keep;
import android.annotation.TargetApi;

import java.util.HashMap;
import java.util.Iterator;
import java.util.LinkedList;
import java.util.Set;

/**
 * Wrapper for Android Text to Speech API and custom utterance query implementation.
 * <p>
 * A [GodotTTS] provides the following features:
 * <p>
 * <ul>
 * <li>Access to the Android Text to Speech API.
 * <li>Utterance pause / resume functions, unsupported by Android TTS API.
 * </ul>
 */
@Keep
public class GodotTTS extends UtteranceProgressListener {
	// Note: These constants must be in sync with DisplayServer::TTSUtteranceEvent enum from "servers/display_server.h".
	final private static int EVENT_START = 0;
	final private static int EVENT_END = 1;
	final private static int EVENT_CANCEL = 2;
	final private static int EVENT_BOUNDARY = 3;

	private final Context context;
	private TextToSpeech synth;
	private LinkedList<GodotUtterance> queue;
	final private Object lock = new Object();
	private GodotUtterance lastUtterance;

	private boolean speaking;
	private boolean paused;
	private boolean ttsInitialized = false;

	// Shim class.
	private static class CompatibilityTtsMethodsShim {
		@TargetApi(Build.VERSION_CODES.LOLLIPOP)
		static void setVoiceByName(TextToSpeech synth, String voiceName) {
			java.util.Set<android.speech.tts.Voice> voices = synth.getVoices();
			if (voices != null) {
				for (android.speech.tts.Voice v : voices) {
					if (v.getName().equals(voiceName)) {
						synth.setVoice(v);
						break;
					}
				}
			}
		}

		@TargetApi(Build.VERSION_CODES.LOLLIPOP)
		static String[] getVoiceList(TextToSpeech synth) {
			java.util.Set<android.speech.tts.Voice> voices = synth.getVoices();
			if (voices == null) {
				return new String[0];
			}
			String[] list = new String[voices.size()];
			int i = 0;
			for (android.speech.tts.Voice v : voices) {
				list[i++] = v.getLocale().toString() + ";" + v.getName();
			}
			return list;
		}

		@TargetApi(Build.VERSION_CODES.LOLLIPOP)
		static void speak(TextToSpeech synth, String text, int mode, float volume, String utteranceId) {
			Bundle params = new Bundle();
			params.putFloat(TextToSpeech.Engine.KEY_PARAM_VOLUME, volume);
			synth.speak(text, mode, params, utteranceId);
		}
	}

	public GodotTTS(Context context) {
		this.context = context;
	}

	@SuppressWarnings("deprecation")
	private void speakLegacy(String text, int mode, float volume, String utteranceId) {
		HashMap<String, String> params = new HashMap<>();
		params.put(TextToSpeech.Engine.KEY_PARAM_VOLUME, String.valueOf(volume));
		params.put(TextToSpeech.Engine.KEY_PARAM_UTTERANCE_ID, utteranceId);
		synth.speak(text, mode, params);
	}

	private void updateTTS() {
		// Protect the LinkedList from concurrent modification
		synchronized (lock) {
			if (!ttsInitialized) {
				// Wait until OS is ready
				return;
			}

			if (!speaking && queue.size() > 0) {
				int mode = TextToSpeech.QUEUE_FLUSH;
				GodotUtterance message = queue.pollFirst();

				if (message == null) {
					return;
				}

				if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
					CompatibilityTtsMethodsShim.setVoiceByName(synth, message.voice);
				}

				synth.setPitch(message.pitch);
				synth.setSpeechRate(message.rate);

				lastUtterance = message;
				lastUtterance.start = 0;
				lastUtterance.offset = 0;
				paused = false;

				if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
					CompatibilityTtsMethodsShim.speak(synth, message.text, mode, message.volume / 100.f, String.valueOf(message.id));
				} else {
					speakLegacy(message.text, mode, message.volume / 100.f, String.valueOf(message.id));
				}
				speaking = true;
			}
		}
	}

	/**
	 * Called by TTS engine when the TTS service is about to speak the specified range.
	 */
	@Override
	public void onRangeStart(String utteranceId, int start, int end, int frame) {
		synchronized (lock) {
			if (lastUtterance != null && Integer.parseInt(utteranceId) == lastUtterance.id) {
				lastUtterance.offset = start;
				GodotLib.ttsCallback(EVENT_BOUNDARY, lastUtterance.id, start + lastUtterance.start);
			}
		}
	}

	/**
	 * Called by TTS engine when an utterance was canceled in progress.
	 */
	@Override
	public void onStop(String utteranceId, boolean interrupted) {
		synchronized (lock) {
			if (lastUtterance != null && !paused && Integer.parseInt(utteranceId) == lastUtterance.id) {
				GodotLib.ttsCallback(EVENT_CANCEL, lastUtterance.id, 0);
				speaking = false;
				updateTTS();
			}
		}
	}

	/**
	 * Called by TTS engine when an utterance has begun to be spoken..
	 */
	@Override
	public void onStart(String utteranceId) {
		synchronized (lock) {
			if (lastUtterance != null && lastUtterance.start == 0 && Integer.parseInt(utteranceId) == lastUtterance.id) {
				GodotLib.ttsCallback(EVENT_START, lastUtterance.id, 0);
			}
		}
	}

	/**
	 * Called by TTS engine when an utterance was successfully finished.
	 */
	@Override
	public void onDone(String utteranceId) {
		synchronized (lock) {
			if (lastUtterance != null && !paused && Integer.parseInt(utteranceId) == lastUtterance.id) {
				GodotLib.ttsCallback(EVENT_END, lastUtterance.id, 0);
				speaking = false;
				updateTTS();
			}
		}
	}

	/**
	 * Called by TTS engine when an error has occurred during processing.
	 */
	@Override
	public void onError(String utteranceId, int errorCode) {
		synchronized (lock) {
			if (lastUtterance != null && !paused && Integer.parseInt(utteranceId) == lastUtterance.id) {
				GodotLib.ttsCallback(EVENT_CANCEL, lastUtterance.id, 0);
				speaking = false;
				updateTTS();
			}
		}
	}

	/**
	 * Called by TTS engine when an error has occurred during processing (pre API level 21 version).
	 */
	@Override
	public void onError(String utteranceId) {
		synchronized (lock) {
			if (lastUtterance != null && !paused && Integer.parseInt(utteranceId) == lastUtterance.id) {
				GodotLib.ttsCallback(EVENT_CANCEL, lastUtterance.id, 0);
				speaking = false;
				updateTTS();
			}
		}
	}

	/**
	 * Initialize synth and query.
	 */
	public void init() {
		queue = new LinkedList<GodotUtterance>();

		// Provide an explicit listener to track when TTS is safely bound.
		synth = new TextToSpeech(context, status -> {
			if (status == TextToSpeech.SUCCESS) {
				ttsInitialized = true;
				synth.setOnUtteranceProgressListener(GodotTTS.this);
				// If anything was queued during startup, flush it now
				updateTTS();
			} else {
				Log.e("GodotTTS", "Android TextToSpeech failed to initialize.");
			}
		});
	}

	/**
	 * Adds an utterance to the queue.
	 */
	public void speak(String text, String voice, int volume, float pitch, float rate, int utterance_id, boolean interrupt) {
		synchronized (lock) {
			GodotUtterance message = new GodotUtterance(text, voice, volume, pitch, rate, utterance_id);
			queue.addLast(message);

			if (isPaused()) {
				resumeSpeaking();
			} else {
				updateTTS();
			}
		}
	}

	/**
	 * Puts the synthesizer into a paused state.
	 */
	public void pauseSpeaking() {
		synchronized (lock) {
			if (!paused) {
				paused = true;
				synth.stop();
			}
		}
	}

	/**
	 * Resumes the synthesizer if it was paused.
	 */
	public void resumeSpeaking() {
		synchronized (lock) {
			if (lastUtterance != null && paused) {
				int mode = TextToSpeech.QUEUE_FLUSH;

				if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
					CompatibilityTtsMethodsShim.setVoiceByName(synth, lastUtterance.voice);
				}
				synth.setPitch(lastUtterance.pitch);
				synth.setSpeechRate(lastUtterance.rate);

				lastUtterance.start = lastUtterance.offset;
				lastUtterance.offset = 0;
				paused = false;

				String textToSpeak = lastUtterance.text.substring(lastUtterance.start);
				if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
					CompatibilityTtsMethodsShim.speak(synth, textToSpeak, mode, lastUtterance.volume / 100.f, String.valueOf(lastUtterance.id));
				} else {
					speakLegacy(textToSpeak, mode, lastUtterance.volume / 100.f, String.valueOf(lastUtterance.id));
				}
				speaking = true;
			} else {
				paused = false;
			}
		}
	}

	/**
	 * Stops synthesis in progress and removes all utterances from the queue.
	 */
	public void stopSpeaking() {
		synchronized (lock) {
			for (GodotUtterance u : queue) {
				GodotLib.ttsCallback(EVENT_CANCEL, u.id, 0);
			}
			queue.clear();

			if (lastUtterance != null) {
				GodotLib.ttsCallback(EVENT_CANCEL, lastUtterance.id, 0);
			}
			lastUtterance = null;

			paused = false;
			speaking = false;

			synth.stop();
		}
	}

	/**
	 * Returns voice information.
	 */
	public String[] getVoices() {
		if (!ttsInitialized || synth == null) {
			return new String[0];
		}

		if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
			return CompatibilityTtsMethodsShim.getVoiceList(synth);
		} else {
			// Pre-Lollipop fallback
			return new String[0];
		}
	}

	/**
	 * Returns true if the synthesizer is generating speech, or have utterance waiting in the queue.
	 */
	public boolean isSpeaking() {
		return speaking;
	}

	/**
	 * Returns true if the synthesizer is in a paused state.
	 */
	public boolean isPaused() {
		return paused;
	}
}
