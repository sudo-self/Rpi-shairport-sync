/*
 * Asynchronous PipeWire Backend. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2017-2023
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


#include "audio.h"
#include "common.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <math.h> // many not need this after development

#include <spa/param/audio/format-utils.h>
#include <pipewire/pipewire.h>

// note -- these are hacked and hardwired into this code.
#define DEFAULT_FORMAT SPA_AUDIO_FORMAT_S16_LE
#define DEFAULT_RATE 44100
#define DEFAULT_CHANNELS 2
#define DEFAULT_VOLUME 0.7

// Four seconds buffer -- should be plenty
#define buffer_allocation 44100 * 4 * 2 * 2

// static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

char *audio_lmb, *audio_umb, *audio_toq, *audio_eoq;
size_t audio_size = buffer_allocation;
size_t audio_occupancy;

#define M_PI_M2 (M_PI + M_PI)

uint64_t starting_time;

struct data {
  struct pw_thread_loop *loop;
  struct pw_stream *stream;

  double accumulator;
};

// the pipewire global data structure
struct data data = {
    0,
};

static void fill_le16(struct data *d, void *dest, int n_frames) {
  //float *dst = dest, val;
  float val;
  int16_t *dst = dest, le16val;
  int i, c;

  for (i = 0; i < n_frames; i++) {
    d->accumulator += M_PI_M2 * 440 / DEFAULT_RATE;
    if (d->accumulator >= M_PI_M2)
      d->accumulator -= M_PI_M2;

    val = sin(d->accumulator) * DEFAULT_VOLUME;
    le16val = INT16_MAX * val;
    for (c = 0; c < DEFAULT_CHANNELS; c++)
      *dst++ = le16val;
  }
}

/* our data processing function is in general:
 *
 *  struct pw_buffer *b;
 *  b = pw_stream_dequeue_buffer(stream);
 *
 *  .. generate stuff in the buffer ...
 *
 *  pw_stream_queue_buffer(stream, b);
 */
static void on_process(void *userdata) {
  int wait;
  do {
    uint64_t time_now = get_absolute_time_in_ns();
    int64_t elapsed_time = time_now - starting_time;
    double elapsed_time_seconds = fmod(elapsed_time * 0.000000001, 10.0);
    wait = (elapsed_time_seconds > 4.0) && (elapsed_time_seconds < 6.0);
    if (wait != 0) {
    //  debug(1, "wait...");
      usleep(1000);
    }
  } while (wait != 0);
  struct data *data = userdata;
  struct pw_buffer *b;
  struct spa_buffer *buf;
  int n_frames, stride;
  uint8_t *p;

  if ((b = pw_stream_dequeue_buffer(data->stream)) == NULL) {
    pw_log_warn("out of buffers: %m");
    return;
  }
  
  struct pw_time time_info;
  memset(&time_info, 0, sizeof(time_info));

  int response = pw_stream_get_time_n(data->stream, &time_info, sizeof(time_info));	
  if (response == 0) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t diff = SPA_TIMESPEC_TO_NSEC(&ts) - time_info.now;
    int64_t elapsed = (time_info.rate.denom * diff) / (time_info.rate.num * SPA_NSEC_PER_SEC);
    debug(1, "rate.num: %" PRId64 ", rate.denom: %" PRId64 ", diff: %" PRId64 "ns, %" PRId64 " frames, delay: %" PRId64 ", queued: %" PRId64 ", buffered: %" PRId64 ".", time_info.rate.num, time_info.rate.denom, diff, elapsed, time_info.delay, time_info.queued, time_info.buffered);  
  } else {
    debug(1, "can't get time info: %d.", response);
  }

  buf = b->buffer;
  if ((p = buf->datas[0].data) == NULL)
    return;

  stride = sizeof(int16_t) * DEFAULT_CHANNELS;
  n_frames = SPA_MIN(b->requested, buf->datas[0].maxsize / stride);

  fill_le16(data, p, n_frames);

  buf->datas[0].chunk->offset = 0;
  buf->datas[0].chunk->stride = stride;
  buf->datas[0].chunk->size = n_frames * stride;
  pw_stream_queue_buffer(data->stream, b);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
};

static int init(__attribute__((unused)) int argc, __attribute__((unused)) char **argv) {
  debug(1, "pw_init");
  // set up default values first
  config.audio_backend_buffer_desired_length = 0.35;
  config.audio_backend_buffer_interpolation_threshold_in_seconds =
      0.02; // below this, soxr interpolation will not occur -- it'll be basic interpolation
            // instead.

  config.audio_backend_latency_offset = 0;

  // get settings from settings file

  // do the "general" audio  options. Note, these options are in the "general" stanza!
  parse_general_audio_options();

/*
  // now any PipeWire-specific options
  if (config.cfg != NULL) {
    const char *str;
  }
*/
  // finished collecting settings

  // allocate space for the audio buffer
  audio_lmb = malloc(audio_size);
  if (audio_lmb == NULL)
    die("Can't allocate %d bytes for pulseaudio buffer.", audio_size);
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;

  const struct spa_pod *params[1];
  uint8_t buffer[1024];
  struct pw_properties *props;
  struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

  int largc = 0;
  pw_init(&largc, NULL);

  /* make a main loop. If you already have another main loop, you can add
   * the fd of this pipewire mainloop to it. */
  data.loop =  pw_thread_loop_new("tone-generator", NULL);

  pw_thread_loop_lock(data.loop);
    
  pw_thread_loop_start(data.loop);

  /* Create a simple stream, the simple stream manages the core and remote
   * objects for you if you don't need to deal with them.
   *
   * If you plan to autoconnect your stream, you need to provide at least
   * media, category and role properties.
   *
   * Pass your events and a user_data pointer as the last arguments. This
   * will inform you about the stream state. The most important event
   * you need to listen to is the process event where you need to produce
   * the data.
   */
  
  props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                            PW_KEY_MEDIA_ROLE, "Music", NULL);
  if (argc > 1)
    /* Set stream target if given on command line */
    pw_properties_set(props, PW_KEY_TARGET_OBJECT, argv[1]);
  data.stream = pw_stream_new_simple(pw_thread_loop_get_loop(data.loop), "audio-src-tg", props,
                                     &stream_events, &data);

  /* Make one parameter with the supported formats. The SPA_PARAM_EnumFormat
   * id means that this is a format enumeration (of 1 value). */
  params[0] = spa_format_audio_raw_build(&b, SPA_PARAM_EnumFormat,
                                         &SPA_AUDIO_INFO_RAW_INIT(.format = SPA_AUDIO_FORMAT_S16_LE,
                                                                  .channels = DEFAULT_CHANNELS,
                                                                  .rate = DEFAULT_RATE));

  /* Now connect this stream. We ask that our process function is
   * called in a realtime thread. */
  pw_stream_connect(data.stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                    PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                        PW_STREAM_FLAG_RT_PROCESS,
                    params, 1);

  pw_thread_loop_unlock(data.loop);
  debug(1, "pa_init done");
  return 0;
}

static void deinit(void) {
  debug (1, "pw_deinit");
  pw_thread_loop_stop(data.loop);
  pw_stream_destroy(data.stream);
  pw_thread_loop_destroy(data.loop);
  pw_deinit();
  debug(1, "pa_deinit done");
}

static int play(__attribute__((unused)) void *buf, int samples, __attribute__((unused)) int sample_type,
                __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime) {
  debug(1,"pw_play of %d samples.",samples);
  /*
  // copy the samples into the queue
  check_pa_stream_status(stream, "audio_pw play.");
  size_t bytes_to_transfer = samples * 2 * 2;
  size_t space_to_end_of_buffer = audio_umb - audio_eoq;
  if (space_to_end_of_buffer >= bytes_to_transfer) {
    memcpy(audio_eoq, buf, bytes_to_transfer);
    audio_occupancy += bytes_to_transfer;
    pthread_mutex_lock(&buffer_mutex);
    audio_eoq += bytes_to_transfer;
    pthread_mutex_unlock(&buffer_mutex);
  } else {
    memcpy(audio_eoq, buf, space_to_end_of_buffer);
    buf += space_to_end_of_buffer;
    memcpy(audio_lmb, buf, bytes_to_transfer - space_to_end_of_buffer);
    pthread_mutex_lock(&buffer_mutex);
    audio_occupancy += bytes_to_transfer;
    pthread_mutex_unlock(&buffer_mutex);
    audio_eoq = audio_lmb + bytes_to_transfer - space_to_end_of_buffer;
  }
  // maybe goose it if it's stopped?
  */
  return 0;
}

/*
int pa_delay(long *the_delay) {
  check_pa_stream_status(stream, "audio_pa delay.");
  // debug(1,"pa_delay");
  long result = 0;
  int reply = 0;
  pa_usec_t latency;
  int negative;
  pa_threaded_mainloop_lock(mainloop);
  int gl = pa_stream_get_latency(stream, &latency, &negative);
  pa_threaded_mainloop_unlock(mainloop);
  if (gl == PA_ERR_NODATA) {
    // debug(1, "No latency data yet.");
    reply = -ENODEV;
  } else if (gl != 0) {
    // debug(1,"Error %d getting latency.",gl);
    reply = -EIO;
  } else {
    result = (audio_occupancy / (2 * 2)) + (latency * 44100) / 1000000;
    reply = 0;
  }
  *the_delay = result;
  return reply;
}
*/

void flush(void) {
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;
}

static void stop(void) {
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;
}

audio_output audio_pw = {.name = "pw",
                         .help = NULL,
                         .init = &init,
                         .deinit = &deinit,
                         .prepare = NULL,
                         .start = NULL,
                         .stop = &stop,
                         .is_running = NULL,
                         .flush = &flush,
                         .delay = NULL,
                         .stats = NULL,
                         .play = &play,
                         .volume = NULL,
                         .parameters = NULL,
                         .mute = NULL};
