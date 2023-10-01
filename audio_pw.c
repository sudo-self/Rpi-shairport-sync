/*
 * Asynchronous PipeWire Backend. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2023
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

// This uses ideas from the tone generator sample code at:
// https://github.com/PipeWire/pipewire/blob/master/src/examples/audio-src.c
// Thanks to the Wim Taymans.

#include "audio.h"
#include "common.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

// note -- these are hacked and hardwired into this code.
#define DEFAULT_FORMAT SPA_AUDIO_FORMAT_S16_LE
#define DEFAULT_RATE 44100
#define DEFAULT_CHANNELS 2

// Four seconds buffer -- should be plenty
#define buffer_allocation 44100 * 4 * 2 * 2

static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

static char *audio_lmb, *audio_umb, *audio_toq, *audio_eoq;
static size_t audio_size = buffer_allocation;
static size_t audio_occupancy;

uint64_t starting_time;

struct timing_data {
  int pw_time_is_valid;     // set when the pw_time has been set
  struct pw_time time_info; // information about the last time a process callback occurred
  size_t frames;            // the number of frames sent at that time
};

// to avoid using a mutex, write the same data twice and check they are the same
// to ensure they are consistent. Make sure the first is written strictly before the second
// using __sync_synchronize();
struct timing_data timing_data_1, timing_data_2;

struct data {
  struct pw_thread_loop *loop;
  struct pw_stream *stream;
};

// the pipewire global data structure
struct data data = {
    0,
};

static void on_state_changed(__attribute__((unused)) void *userdata, enum pw_stream_state old,
                             enum pw_stream_state state,
                             __attribute__((unused)) const char *error) {
  // struct pw_data *pw = userdata;
  debug(3, "pw: stream state changed %s -> %s", pw_stream_state_as_string(old),
        pw_stream_state_as_string(state));
}

static void on_process(void *userdata) {

  struct data *data = userdata;
  int n_frames = 0;

  pthread_mutex_lock(&buffer_mutex);

  if (audio_occupancy > 0) {

    // get a buffer to see how big it can be
    struct pw_buffer *b = pw_stream_dequeue_buffer(data->stream);
    if (b == NULL) {
      pw_log_warn("out of buffers: %m");
      return;
    }
    struct spa_buffer *buf = b->buffer;
    uint8_t *dest = buf->datas[0].data;
    if (dest == NULL) // the first data block does not contain a data pointer
      return;

    int stride = sizeof(int16_t) * DEFAULT_CHANNELS;
    int max_possible_frames = SPA_MIN(b->requested, buf->datas[0].maxsize / stride);

    size_t bytes_we_can_transfer = max_possible_frames * stride;

    if (bytes_we_can_transfer > audio_occupancy)
      bytes_we_can_transfer = audio_occupancy;

    n_frames = bytes_we_can_transfer / stride;

    size_t bytes_to_end_of_buffer = (size_t)(audio_umb - audio_toq); // must be zero or positive

    if (bytes_we_can_transfer <= bytes_to_end_of_buffer) {
      // the bytes are all in a row in the audio buffer
      memcpy(dest, audio_toq, bytes_we_can_transfer);
      audio_toq += bytes_we_can_transfer;
    } else {
      // the bytes are in two places in the audio buffer
      size_t first_portion_to_write = audio_umb - audio_toq;
      if (first_portion_to_write != 0)
        memcpy(dest, audio_toq, first_portion_to_write);
      uint8_t *new_dest = dest + first_portion_to_write;
      memcpy(new_dest, audio_lmb, bytes_we_can_transfer - first_portion_to_write);
      audio_toq = audio_lmb + bytes_we_can_transfer - first_portion_to_write;
    }

    buf->datas[0].chunk->offset = 0;
    buf->datas[0].chunk->stride = stride;
    buf->datas[0].chunk->size = n_frames * stride;
    pw_stream_queue_buffer(data->stream, b);
    debug(3, "Queueing %d frames for output.", n_frames);

    audio_occupancy -= bytes_we_can_transfer;
  }
  pthread_mutex_unlock(&buffer_mutex);

  timing_data_1.frames = n_frames;
  if (pw_stream_get_time_n(data->stream, &timing_data_1.time_info, sizeof(struct timing_data)) == 0)
    timing_data_1.pw_time_is_valid = 1;
  else
    timing_data_1.pw_time_is_valid = 0;
  __sync_synchronize();
  memcpy((char *)&timing_data_2, (char *)&timing_data_1, sizeof(struct timing_data));
  __sync_synchronize();
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS, .process = on_process, .state_changed = on_state_changed};

static void deinit(void) {
  pw_thread_loop_stop(data.loop);
  pw_stream_destroy(data.stream);
  pw_thread_loop_destroy(data.loop);
  pw_deinit();
  free(audio_lmb); // deallocate that buffer
}

static int init(__attribute__((unused)) int argc, __attribute__((unused)) char **argv) {
  // set up default values first
  memset(&timing_data_1, 0, sizeof(struct timing_data));
  memset(&timing_data_2, 0, sizeof(struct timing_data));
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

  /* make a threaded loop. */
  data.loop = pw_thread_loop_new("shairport-sync", NULL);

  pw_thread_loop_lock(data.loop);

  pw_thread_loop_start(data.loop);

  props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                            PW_KEY_MEDIA_ROLE, "Music", PW_KEY_APP_NAME, "Shairport Sync", NULL);

  data.stream = pw_stream_new_simple(pw_thread_loop_get_loop(data.loop), "shairport-sync", props,
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
  return 0;
}

static void start(__attribute__((unused)) int sample_rate,
                  __attribute__((unused)) int sample_format) {}

static int play(__attribute__((unused)) void *buf, int samples,
                __attribute__((unused)) int sample_type, __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime) {
  // copy the samples into the queue
  debug(3, "play %u samples; %u bytes already in the buffer.", samples, audio_occupancy);
  size_t bytes_to_transfer = samples * 2 * 2;
  pthread_mutex_lock(&buffer_mutex);
  size_t bytes_available = audio_size - audio_occupancy;
  if (bytes_available < bytes_to_transfer)
    bytes_to_transfer = bytes_available;
  if (bytes_to_transfer > 0) {
    size_t space_to_end_of_buffer = audio_umb - audio_eoq;
    if (space_to_end_of_buffer >= bytes_to_transfer) {
      memcpy(audio_eoq, buf, bytes_to_transfer);
      audio_eoq += bytes_to_transfer;
    } else {
      memcpy(audio_eoq, buf, space_to_end_of_buffer);
      buf += space_to_end_of_buffer;
      memcpy(audio_lmb, buf, bytes_to_transfer - space_to_end_of_buffer);
      audio_eoq = audio_lmb + bytes_to_transfer - space_to_end_of_buffer;
    }
    audio_occupancy += bytes_to_transfer;
  }
  pthread_mutex_unlock(&buffer_mutex);
  return 0;
}

int delay(long *the_delay) {
  long result = 0;
  int reply = 0;
  // find out what's already in the PipeWire system and when
  struct timing_data timing_data;
  int loop_count = 1;
  do {
    memcpy(&timing_data, (char *)&timing_data_1, sizeof(struct timing_data));
    __sync_synchronize();
    if (memcmp(&timing_data, (char *)&timing_data_2, sizeof(struct timing_data)) != 0) {
      usleep(2); // microseconds
      loop_count++;
      __sync_synchronize();
    }
  } while ((memcmp(&timing_data, (char *)&timing_data_2, sizeof(struct timing_data)) != 0) &&
           (loop_count < 10));
  long total_delay_now_frames_long = 0;
  if ((loop_count < 10) && (timing_data.pw_time_is_valid != 0)) {
    struct timespec time_now;
    clock_gettime(CLOCK_MONOTONIC, &time_now);
    int64_t interval_from_process_time_to_now =
        SPA_TIMESPEC_TO_NSEC(&time_now) - timing_data.time_info.now;
    int64_t delay_in_ns = timing_data.time_info.delay + timing_data.time_info.buffered;
    delay_in_ns = delay_in_ns * 1000000000;
    delay_in_ns = delay_in_ns * timing_data.time_info.rate.num;
    delay_in_ns = delay_in_ns / timing_data.time_info.rate.denom;

    int64_t total_delay_now_ns = delay_in_ns - interval_from_process_time_to_now;
    int64_t total_delay_now_frames = (total_delay_now_ns * 44100) / 1000000000 + timing_data.frames;
    total_delay_now_frames_long = total_delay_now_frames;
    debug(3, "total delay in frames: %ld.", total_delay_now_frames_long);

    if (timing_data.time_info.queued != 0) {
      debug(1, "buffers queued: %d", timing_data.time_info.queued);
    }
    /*
        debug(3,
              "interval_from_process_time_to_now: %" PRId64 " ns, "
              "delay_in_ns: %" PRId64 ", queued: %" PRId64 ", buffered: %" PRId64 ".",
              // delay_timing_data.time_info.rate.num, delay_timing_data.time_info.rate.denom,
              interval_from_process_time_to_now, delay_in_ns,
              timing_data.time_info.queued, timing_data.time_info.buffered);
    */

  } else {
    debug(1, "can't get time info.");
  }

  pthread_mutex_lock(&buffer_mutex);
  result = total_delay_now_frames_long + audio_occupancy / (2 * 2);
  pthread_mutex_unlock(&buffer_mutex);
  *the_delay = result;
  return reply;
}

static void flush(void) {
  pthread_mutex_lock(&buffer_mutex);
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;
  pthread_mutex_unlock(&buffer_mutex);
}

static void stop(void) {
  pthread_mutex_lock(&buffer_mutex);
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;
  pthread_mutex_unlock(&buffer_mutex);
}

audio_output audio_pw = {.name = "pw",
                         .help = NULL,
                         .init = &init,
                         .deinit = &deinit,
                         .prepare = NULL,
                         .start = &start,
                         .stop = &stop,
                         .is_running = NULL,
                         .flush = &flush,
                         .delay = &delay,
                         .stats = NULL,
                         .play = &play,
                         .volume = NULL,
                         .parameters = NULL,
                         .mute = NULL};
