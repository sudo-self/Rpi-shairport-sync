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

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

// note -- these are hacked and hardwired into this code.
#define DEFAULT_FORMAT SPA_AUDIO_FORMAT_S16_LE
#define DEFAULT_RATE 44100
#define DEFAULT_CHANNELS 2
#define DEFAULT_VOLUME 0.7

// Four seconds buffer -- should be plenty
#define buffer_allocation 44100 * 4 * 2 * 2

static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

static char *audio_lmb, *audio_umb, *audio_toq, *audio_eoq;
static size_t audio_size = buffer_allocation;
static size_t audio_occupancy;

uint64_t starting_time;

struct timing_data {
  int pw_time_is_valid; //set when the pw_time has been set
  struct pw_time time_info; // information about the last time a process callback occurred
  size_t frames; // the number of frames sent at that time
};

// to avoid using a mutex, write the same data twice and check they are the same
// to ensure they are consistent. Make sure the first is written strictly before the second
// using __sync_synchronize();
struct timing_data timing_data_1, timing_data_2;

struct data {
  struct pw_thread_loop *loop;
  struct pw_stream *stream;

  double accumulator;
};

// the pipewire global data structure
struct data data = {
    0,
};

static int fill(void *dest, int max_frames, int stride) {
  size_t bytes_we_can_transfer = max_frames * stride;
  pthread_mutex_lock(&buffer_mutex);
  if (bytes_we_can_transfer > audio_occupancy)
    bytes_we_can_transfer = audio_occupancy;
  pthread_mutex_unlock(&buffer_mutex);
  if (bytes_we_can_transfer > 0) {
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
    // lock
    pthread_mutex_lock(&buffer_mutex);
    audio_occupancy -= bytes_we_can_transfer;
    pthread_mutex_unlock(&buffer_mutex);
    // unlock
  }
  return bytes_we_can_transfer / stride; // back to numbers of frames
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
  struct data *data = userdata;
  
  struct pw_time time_info;
  memset(&time_info, 0, sizeof(time_info));
  
  struct pw_buffer *b;
  struct spa_buffer *buf;
  int max_possible_frames, n_frames, stride;
  uint8_t *p;

  if ((b = pw_stream_dequeue_buffer(data->stream)) == NULL) {
    pw_log_warn("out of buffers: %m");
    return;
  }

  buf = b->buffer;
  if ((p = buf->datas[0].data) == NULL) // the first data block does not contain a data pointer
    return;

  stride = sizeof(int16_t) * DEFAULT_CHANNELS;
  max_possible_frames = SPA_MIN(b->requested, buf->datas[0].maxsize / stride);
  
  do {
    n_frames = fill(p, max_possible_frames, stride);
    if (n_frames == 0) {
      usleep(1000);
    }
  } while(n_frames == 0);

  buf->datas[0].chunk->offset = 0;
  buf->datas[0].chunk->stride = stride;
  buf->datas[0].chunk->size = n_frames * stride;
  debug(3, "Queueing %d frames for output.", n_frames);
  
  if (pw_stream_get_time_n(data->stream, &timing_data_1.time_info, sizeof(time_info)) == 0)
    timing_data_1.pw_time_is_valid = 1;
  else
    timing_data_1.pw_time_is_valid = 0;
  __sync_synchronize();
  memcpy((char *)&timing_data_2, (char *)&timing_data_1, sizeof(struct timing_data));
  __sync_synchronize();
  pw_stream_queue_buffer(data->stream, b);

}


static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .process = on_process,
};

static int init(__attribute__((unused)) int argc, __attribute__((unused)) char **argv) {
  debug(1, "pw_init");
  // set up default values first
  memset(&timing_data_1,0,sizeof(struct timing_data));
  memset(&timing_data_2,0,sizeof(struct timing_data));
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
  data.loop = pw_thread_loop_new("tone-generator", NULL);

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
  debug(1, "pw_deinit");
  pw_thread_loop_stop(data.loop);
  pw_stream_destroy(data.stream);
  pw_thread_loop_destroy(data.loop);
  pw_deinit();
  free(audio_lmb); // deallocate that buffer
  debug(1, "pa_deinit done");
}

static void start(__attribute__((unused)) int sample_rate,
                  __attribute__((unused)) int sample_format) {
}

static int play(__attribute__((unused)) void *buf, int samples,
                __attribute__((unused)) int sample_type, __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime) {  
  // copy the samples into the queue
  size_t bytes_to_transfer = samples * 2 * 2;
  size_t space_to_end_of_buffer = audio_umb - audio_eoq;
  if (space_to_end_of_buffer >= bytes_to_transfer) {
    memcpy(audio_eoq, buf, bytes_to_transfer);
    pthread_mutex_lock(&buffer_mutex);
    audio_occupancy += bytes_to_transfer;
    pthread_mutex_unlock(&buffer_mutex);
    audio_eoq += bytes_to_transfer;
  } else {
    memcpy(audio_eoq, buf, space_to_end_of_buffer);
    buf += space_to_end_of_buffer;
    memcpy(audio_lmb, buf, bytes_to_transfer - space_to_end_of_buffer);
    pthread_mutex_lock(&buffer_mutex);
    audio_occupancy += bytes_to_transfer;
    pthread_mutex_unlock(&buffer_mutex);
    audio_eoq = audio_lmb + bytes_to_transfer - space_to_end_of_buffer;
  }
  debug(3, "play %d samples; %d bytes in buffer.", samples, audio_occupancy);
  return 0;
}

int delay(long *the_delay) {
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
  } while ((memcmp(&timing_data, (char *)&timing_data_2, sizeof(struct timing_data)) != 0) && (loop_count < 10));
  long total_delay_now_frames_long = 0;
  if ((loop_count < 10) && (timing_data.pw_time_is_valid != 0)) {
      struct timespec time_now;
    clock_gettime(CLOCK_MONOTONIC, &time_now);
    int64_t interval_from_process_time_to_now = SPA_TIMESPEC_TO_NSEC(&time_now) - timing_data.time_info.now;
    int64_t delay_in_ns = timing_data.time_info.delay + timing_data.time_info.buffered;
    delay_in_ns = delay_in_ns * 1000000000;
    delay_in_ns = delay_in_ns * timing_data.time_info.rate.num;
    delay_in_ns = delay_in_ns / timing_data.time_info.rate.denom;
    
    int64_t total_delay_now_ns = delay_in_ns - interval_from_process_time_to_now;
    int64_t total_delay_now_frames = (total_delay_now_ns * 44100)/1000000000 + timing_data.frames;
    total_delay_now_frames_long = total_delay_now_frames;
    debug(3, "total delay in frames: % " PRId64 ", %ld.", total_delay_now_frames, total_delay_now_frames_long);

    debug(3,
          "interval_from_process_time_to_now: %" PRId64 " ns, "
          "delay_in_ns: %" PRId64 ", queued: %" PRId64 ", buffered: %" PRId64 ".",
          // delay_timing_data.time_info.rate.num, delay_timing_data.time_info.rate.denom, 
          interval_from_process_time_to_now, delay_in_ns,
          timing_data.time_info.queued, timing_data.time_info.buffered);


  } else {
    debug(1, "can't get time info.");
  }

  long result = 0;
  int reply = 0;
  pthread_mutex_lock(&buffer_mutex);
  result = total_delay_now_frames_long + audio_occupancy / (2 * 2);
  pthread_mutex_unlock(&buffer_mutex);
  *the_delay = result;
  return reply;
}


static void flush(void) {
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  pthread_mutex_lock(&buffer_mutex);
  audio_occupancy = 0;
  pthread_mutex_unlock(&buffer_mutex);
}

static void stop(void) {
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  pthread_mutex_lock(&buffer_mutex);
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
