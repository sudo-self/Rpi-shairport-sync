/*
 * jack output driver. This file is part of Shairport Sync.
 * Copyright (c) 2018 Mike Brady <mikebrady@iercom.net>
 *
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "audio.h"
#include "common.h"
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <jack/jack.h>
#include <jack/transport.h>
#include <jack/ringbuffer.h>

enum ift_type {
  IFT_frame_left_sample = 0,
  IFT_frame_right_sample,
} ift_type;

// Two-channel, 16bit audio:
static const int bytes_per_frame = 4;
// Four seconds buffer -- should be plenty
#define buffer_size 44100 * 4 * bytes_per_frame

static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
char *audio_lmb, *audio_umb, *audio_toq, *audio_eoq;

size_t audio_occupancy; // this is in frames, not bytes. A frame is a left and
                        // right sample, each 16 bits, hence 4 bytes
*/
pthread_t *open_client_if_necessary_thread = NULL;

int jack_init(int, char **);
void jack_deinit(void);
void jack_start(int, int);
int play(void *, int);
void jack_stop(void);
int jack_is_running(void);
int jack_delay(long *);
void jack_flush(void);

audio_output audio_jack = {.name = "jack",
                           .help = NULL,
                           .init = &jack_init,
                           .deinit = &jack_deinit,
                           .start = &jack_start,
                           .stop = &jack_stop,
                           .is_running = &jack_is_running,
                           .flush = &jack_flush,
                           .delay = &jack_delay,
                           .play = &play,
                           .volume = NULL,
                           .parameters = NULL,
                           .mute = NULL};

jack_port_t *left_port;
jack_port_t *right_port;

long offset = 0;

int client_is_open;
jack_client_t *client;
jack_nframes_t sample_rate;
jack_nframes_t jack_latency;

static jack_ringbuffer_t *jackbuf;
static int flush_please = 0;

jack_latency_range_t latest_left_latency_range, latest_right_latency_range;
int64_t time_of_latest_transfer;

int play(void *buf, int samples) {
  // debug(1,"jack_play of %d samples.",samples);
  // copy the samples into the queue
  size_t bytes_to_transfer, bytes_transferred;
  bytes_to_transfer = samples * bytes_per_frame;
  pthread_mutex_lock(&buffer_mutex); // it's ok to lock here since we're not in the realtime callback
    bytes_transferred = jack_ringbuffer_write(jackbuf, buf, bytes_to_transfer);
  pthread_mutex_unlock(&buffer_mutex);
  if (bytes_transferred < bytes_to_transfer) {
    debug(1, "JACK ringbuffer overrun. Only wrote %d of %d bytes.", bytes_transferred, bytes_to_transfer);
  }
/*
  size_t space_to_end_of_buffer = audio_umb - audio_eoq;
  if (space_to_end_of_buffer >= bytes_to_transfer) {
    memcpy(audio_eoq, buf, bytes_to_transfer);
    pthread_mutex_lock(&buffer_mutex);
    audio_occupancy += samples;
    audio_eoq += bytes_to_transfer;
    pthread_mutex_unlock(&buffer_mutex);
  } else {
    memcpy(audio_eoq, buf, space_to_end_of_buffer);
    buf += space_to_end_of_buffer;
    memcpy(audio_lmb, buf, bytes_to_transfer - space_to_end_of_buffer);
    pthread_mutex_lock(&buffer_mutex);
    audio_occupancy += samples;
    audio_eoq = audio_lmb + bytes_to_transfer - space_to_end_of_buffer;
    pthread_mutex_unlock(&buffer_mutex);
  }
*/
  return 0;
}

void deinterleave_and_convert_stream(const char *interleaved_frames,
                                     const jack_default_audio_sample_t *jack_frame_buffer,
                                     jack_nframes_t number_of_frames, enum ift_type side) {
  jack_nframes_t i;
  short *ifp = (short *)interleaved_frames;
  jack_default_audio_sample_t *fp = (jack_default_audio_sample_t *)jack_frame_buffer;
  if (side == IFT_frame_right_sample)
    ifp++;
  for (i = 0; i < number_of_frames; i++) {
    short sample = *ifp;
    jack_default_audio_sample_t converted_value;
    if (sample >= 0)
      converted_value = (1.0 * sample) / SHRT_MAX;
    else
      converted_value = -(1.0 * sample) / SHRT_MIN;
    *fp = converted_value;
    ifp++;
    ifp++;
    fp++;
  }
}

static inline jack_default_audio_sample_t sample_conv(short sample) {
  return ((sample < 0) ? (-1.0 * sample / SHRT_MIN) : (1.0 * sample / SHRT_MAX));
}

void deinterleave_and_convert(const char *interleaved_frames,
                              jack_default_audio_sample_t * const jack_buffer_left,
                              jack_default_audio_sample_t * const jack_buffer_right,
                              jack_nframes_t nframes) {
  jack_nframes_t i;
  short *ifp = (short *)interleaved_frames; // we're dealing with 16bit audio here
  jack_default_audio_sample_t *fpl = jack_buffer_left;
  jack_default_audio_sample_t *fpr = jack_buffer_right;
  for (i=0; i<nframes; i++) {
    fpl[i] = sample_conv(*ifp++);
    fpr[i] = sample_conv(*ifp++);
  }
}

int jack_stream_write_cb(jack_nframes_t nframes, __attribute__((unused)) void *arg) {

  jack_default_audio_sample_t *left_buffer =
      (jack_default_audio_sample_t *)jack_port_get_buffer(left_port, nframes);
  jack_default_audio_sample_t *right_buffer =
      (jack_default_audio_sample_t *)jack_port_get_buffer(right_port, nframes);

  jack_ringbuffer_data_t v[2] = { 0 };
  jack_nframes_t i, thisbuf;
  int frames_written = 0;
  int frames_required = 0;

  jack_ringbuffer_get_read_vector(jackbuf, v); // an array of two elements because of possible ringbuffer wrap-around
  for (i=0; i<2; i++) {
    thisbuf = v[i].len / bytes_per_frame;
    if (thisbuf > nframes) {
      frames_required = nframes;
    } else {
      frames_required = thisbuf;
    }
    deinterleave_and_convert(v[i].buf, &left_buffer[frames_written], &right_buffer[frames_written], frames_required);
    frames_written = frames_required;
    nframes -= frames_written;
  }
  jack_ringbuffer_read_advance(jackbuf, frames_written * bytes_per_frame);

  // debug(1,"transferring %u frames",written);
  // Why are we doing this? The port latency should never change unless the JACK graph is reordered. Should happen on a graph reorder callback only.
  // jack_port_get_latency_range(left_port, JackPlaybackLatency, &latest_left_latency_range);
  // jack_port_get_latency_range(right_port, JackPlaybackLatency, &latest_right_latency_range);

  // now, if there are any more frames to put into the buffer, fill them with
  // silence
  while (nframes > 0) {
    left_buffer[frames_written] = 0.0;
    right_buffer[frames_written] = 0.0;
    frames_written++;
    nframes--;
  }
  time_of_latest_transfer = get_absolute_time_in_fp(); // this is the only writer, no locking necessary.

/*
  size_t frames_we_can_transfer = nframes;
  // lock
  pthread_mutex_lock(&buffer_mutex);
  if (audio_occupancy < frames_we_can_transfer) {
    frames_we_can_transfer = audio_occupancy;
    // This means we effectively have underflow from the Shairport Sync source.
    // In fact, it may be that there is nothing at all coming from the source,
    // but the Shairport Sync client is open and active, so it must continue to output something.
  }

  if (frames_we_can_transfer * 2 * 2 <= (size_t)(audio_umb - audio_toq)) {
    // the bytes are all in a row in the audio buffer
    deinterleave_and_convert_stream(audio_toq, &left_buffer[0], frames_we_can_transfer,
                                    IFT_frame_left_sample);
    deinterleave_and_convert_stream(audio_toq, &right_buffer[0], frames_we_can_transfer,
                                    IFT_frame_right_sample);
    audio_toq += frames_we_can_transfer * 2 * 2;
  } else {
    // the bytes are in two places in the audio buffer
    size_t first_portion_to_write = (audio_umb - audio_toq) / (2 * 2);
    if (first_portion_to_write != 0) {
      deinterleave_and_convert_stream(audio_toq, &left_buffer[0], first_portion_to_write,
                                      IFT_frame_left_sample);
      deinterleave_and_convert_stream(audio_toq, &right_buffer[0], first_portion_to_write,
                                      IFT_frame_right_sample);
    }
    deinterleave_and_convert_stream(audio_lmb, &left_buffer[first_portion_to_write],
                                    frames_we_can_transfer - first_portion_to_write,
                                    IFT_frame_left_sample);
    deinterleave_and_convert_stream(audio_lmb, &right_buffer[first_portion_to_write],
                                    frames_we_can_transfer - first_portion_to_write,
                                    IFT_frame_right_sample);
    audio_toq = audio_lmb + (frames_we_can_transfer - first_portion_to_write) * 2 * 2;
  }
  audio_occupancy -= frames_we_can_transfer;
  jack_port_get_latency_range(left_port, JackPlaybackLatency, &latest_left_latency_range);
  jack_port_get_latency_range(right_port, JackPlaybackLatency, &latest_right_latency_range);
  time_of_latest_transfer = get_absolute_time_in_fp();
  pthread_mutex_unlock(&buffer_mutex);
  // unlock

*/
  return 0;
}

// FIXME: set_graph_order_callback(), recompute latencies here!

void default_jack_error_callback(const char *desc) { debug(2, "jackd error: \"%s\"", desc); }

void default_jack_info_callback(const char *desc) { inform("jackd information: \"%s\"", desc); }

void default_jack_set_latency_callback(jack_latency_callback_mode_t mode,
                                       __attribute__((unused)) void *arg) {
  if (mode == JackPlaybackLatency) {
    jack_latency_range_t left_latency_range, right_latency_range;
    jack_port_get_latency_range(left_port, JackPlaybackLatency, &left_latency_range);
    jack_port_get_latency_range(right_port, JackPlaybackLatency, &right_latency_range);

    jack_nframes_t b_latency = (left_latency_range.min + left_latency_range.max) / 2;
    if (b_latency == 0)
      b_latency = (right_latency_range.min + right_latency_range.max) / 2;
    // jack_latency = b_latency; // actually, we are not interested in the latency of the jack
    // devices connected...
    // FIXME: yes, we are.
    jack_latency = 0;
    debug(1, "playback latency callback: %" PRIu32 ".", jack_latency);
  }
}

int jack_is_running() {
  int reply = -1; // meaning jack is not running
  // if the client is open and initialised, see if the status is "rolling"
  if (client_is_open) {

    // check if the ports have a zero latency -- if they both have, then it's disconnected.

    jack_latency_range_t left_latency_range, right_latency_range;
    jack_port_get_latency_range(left_port, JackPlaybackLatency, &left_latency_range);
    jack_port_get_latency_range(right_port, JackPlaybackLatency, &right_latency_range);

    //    if ((left_latency_range.min == 0) && (left_latency_range.max == 0) &&
    //        (right_latency_range.min == 0) && (right_latency_range.max == 0)) {
    //      reply = -2; // meaning Shairport Sync is not connected
    //    } else {
    reply = 0; // meaning jack is open and Shairport Sync is connected to it
               //    }
  }
  return reply;
}

int jack_client_open_if_needed(void) {
  pthread_mutex_lock(&client_mutex);
  if (client_is_open == 0) {
    jack_status_t status;
    client = jack_client_open(config.jack_client_name, JackNoStartServer, &status);
    if (client) {
      jack_set_process_callback(client, jack_stream_write_cb, 0);
      left_port = jack_port_register(client, config.jack_left_channel_name, JACK_DEFAULT_AUDIO_TYPE,
                                     JackPortIsOutput, 0);
      right_port = jack_port_register(client, config.jack_right_channel_name,
                                      JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
      sample_rate = jack_get_sample_rate(client);
      // debug(1, "jackaudio sample rate = %" PRId32 ".", sample_rate);
      if (sample_rate == 44100) {
        if (jack_set_latency_callback(client, default_jack_set_latency_callback, NULL) == 0) {
          if (jack_activate(client)) {
            debug(1, "jackaudio cannot activate client");
          } else {
            debug(2, "jackaudio client opened.");
            client_is_open = 1;
          }
        } else {
          debug(1, "jackaudio cannot set latency callback");
        }
      } else {
        inform(
            "jackaudio is running at the wrong speed (%d) for Shairport Sync, which must be 44100",
            sample_rate);
      }
    }
  }
  pthread_mutex_unlock(&client_mutex);
  return client_is_open;
}

void jack_close(void) {
  pthread_mutex_lock(&client_mutex);
  if (client_is_open) {
    if (jack_deactivate(client))
      debug(1, "Error deactivating jack client");
    if (jack_client_close(client))
      debug(1, "Error closing jack client");
    client_is_open = 0;
  }
  pthread_mutex_unlock(&client_mutex);
}

void jack_deinit() {
  jack_close();
  if (open_client_if_necessary_thread) {
    pthread_cancel(*open_client_if_necessary_thread);
    free((char *)open_client_if_necessary_thread);

   jack_ringbuffer_free(jackbuf);

  }
}

void *open_client_if_necessary_thread_function(void *arg) {
  int *interval = (int *)arg;
  while (*interval != 0) {
    if (client_is_open == 0) {
      debug(1, "Try to open the jack client");
      jack_client_open_if_needed();
    }
    sleep(*interval);
  }
  pthread_exit(NULL);
}

int jack_init(__attribute__((unused)) int argc, __attribute__((unused)) char **argv) {
  config.audio_backend_latency_offset = 0;
  config.audio_backend_buffer_desired_length = 0.500;
  config.audio_backend_buffer_interpolation_threshold_in_seconds =
      0.25; // below this, soxr interpolation will not occur -- it'll be basic interpolation
            // instead.
  config.jack_auto_client_open_interval = 1; // check every second

  // get settings from settings file first, allow them to be overridden by
  // command line options

  // do the "general" audio  options. Note, these options are in the "general" stanza!
  parse_general_audio_options();

  // other options would be picked up here...

  // now the specific options
  if (config.cfg != NULL) {
    const char *str;
    int value;
    /* Get the Client Name. */
    if (config_lookup_string(config.cfg, "jack.client_name", &str)) {
      config.jack_client_name = (char *)str;
    }
    /* Get the Left Channel Name. */
    if (config_lookup_string(config.cfg, "jack.left_channel_name", &str)) {
      config.jack_left_channel_name = (char *)str;
    }
    /* Get the Right Channel Name. */
    if (config_lookup_string(config.cfg, "jack.right_channel_name", &str)) {
      config.jack_right_channel_name = (char *)str;
    }

    /* See if we should attempt to connect to the jack server automatically, and, if so, how often
     * we should try. */
    if (config_lookup_int(config.cfg, "jack.auto_client_open_interval", &value)) {
      if ((value < 0) || (value > 300))
        debug(1,
              "Invalid jack auto_client_open_interval \"%sd\". It should be between 0 and 300, "
              "default is %d.",
              value, config.jack_auto_client_open_interval);
      else
        config.jack_auto_client_open_interval = value;
    }

    /* See if we should close the client at then end of a play session. */
    config_set_lookup_bool(config.cfg, "jack.auto_client_disconnect",
                           &config.jack_auto_client_disconnect);
  }

  if (config.jack_client_name == NULL)
    config.jack_client_name = strdup("Shairport Sync");
  if (config.jack_left_channel_name == NULL)
    config.jack_left_channel_name = strdup("left");
  if (config.jack_right_channel_name == NULL)
    config.jack_right_channel_name = strdup("right");

  jackbuf = jack_ringbuffer_create(buffer_size); // make the jack ringbuffer the same size as audio_lmb
  if (jackbuf == NULL)
    die("Can't allocate %d bytes for the JACK ringbuffer.", buffer_size);
  jack_ringbuffer_mlock(jackbuf);		 // lock buffer into memory so that it never gets paged out

  jack_set_error_function(default_jack_error_callback);
  jack_set_info_function(default_jack_info_callback);

/*
  // allocate space for the audio buffer
  audio_lmb = malloc(buffer_size);
  if (audio_lmb == NULL)
    die("Can't allocate %d bytes for jackaudio buffer.", buffer_size);
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + buffer_size;
  audio_occupancy = 0; // frames
*/

  client_is_open = 0;

  // now, if selected, start a thread to automatically open a client when there is a server.
  if (config.jack_auto_client_open_interval != 0) {
    open_client_if_necessary_thread = malloc(sizeof(pthread_t));
    if (open_client_if_necessary_thread == NULL) {
      debug(1, "Couldn't allocate space for jack server scanner thread");
      jack_client_open_if_needed();
    } else {
      pthread_create(open_client_if_necessary_thread, NULL,
                     open_client_if_necessary_thread_function,
                     &config.jack_auto_client_open_interval);
    }
  } else {
    jack_client_open_if_needed();
  }

  return 0;
}

void jack_start(__attribute__((unused)) int i_sample_rate,
                __attribute__((unused)) int i_sample_format) {
  // debug(1, "jack start");
  // see if the client is running. If not, try to open and initialise it

  if (jack_client_open_if_needed() == 0)
    debug(1, "cannot open a jack client for a play session");
}

int jack_delay(long *the_delay) {

  // without the mutex, we could get the time of what is the last transfer of data to a jack buffer,
  // but then a transfer could occur and we would get the buffer occupancy after another transfer
  // had occurred
  // so we could "lose" a full transfer (e.g. 1024 frames @ 44,100 fps ~ 23.2 milliseconds)

  pthread_mutex_lock(&buffer_mutex);
    int64_t time_now = get_absolute_time_in_fp();
    // this is the time back to the last time data
    // was transferred into a jack buffer
    int64_t delta = time_now - time_of_latest_transfer;
    // this is the buffer occupancy before any
    // subsequent transfer because transfer is blocked
    // by the mutex
    size_t audio_occupancy_now = jack_ringbuffer_read_space(jackbuf);
  pthread_mutex_unlock(&buffer_mutex);

  int64_t frames_processed_since_latest_latency_check = (delta * 44100) >> 32;
  // debug(1,"delta: %" PRId64 " frames.",frames_processed_since_latest_latency_check);
  jack_nframes_t base_latency = (latest_left_latency_range.min + latest_left_latency_range.max) / 2;
  if (base_latency == 0)
    base_latency = (latest_right_latency_range.min + latest_right_latency_range.max) / 2;
  *the_delay = base_latency + audio_occupancy_now - frames_processed_since_latest_latency_check;
  // debug(1,"reporting a delay of %d frames",*the_delay);
  return 0;
}

void jack_flush() {
  debug(1, "It is not possible to safely flush a lock-free ringbuffer. Asking the process callback to do it...");
  flush_please = 1;
/*
  //  debug(1,"jack flush");
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + buffer_size;
  audio_occupancy = 0; // frames
*/
}

void jack_stop(void) {
  // debug(1, "jack stop");
  if (config.jack_auto_client_disconnect)
    jack_close();
}
