/*
 * Asynchronous Pipewire Backend. This file is part of Shairport Sync.
 * Copyright (c) Shairport Sync 2021--2022
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

#include <pipewire/pipewire.h>
#include <pipewire/stream.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/layout.h>
#include <spa/utils/result.h>

#include <math.h>

#define PW_TIMEOUT_S 5
#define SECONDS_TO_NANOSECONDS 1000000000L
#define PW_TIMEOUT_NS (PW_TIMEOUT_S * SECONDS_TO_NANOSECONDS)

struct pw_data {
  struct pw_thread_loop *mainloop;
  struct pw_context *context;

  struct pw_core *core;
  struct spa_hook core_listener;

  struct pw_registry *registry;
  struct spa_hook registry_listener;

  struct pw_stream *stream;
  struct spa_hook stream_listener;

  struct pw_buffer *pw_buffer;

  struct pw_properties *props;
  int sync;

  enum spa_audio_format format;
  uint32_t rate;
  uint32_t channels;
  uint32_t stride;
  uint32_t latency;

} data;

static void on_core_info(__attribute__((unused)) void *userdata, const struct pw_core_info *info) {
  debug(1, "pw: remote %" PRIu32 " is named \"%s\"", info->id, info->name);
}

static void on_core_error(__attribute__((unused)) void *userdata, uint32_t id, int seq, int res,
                          const char *message) {
  warn("pw: remote error: id=%" PRIu32 " seq:%d res:%d (%s): %s", id, seq, res, spa_strerror(res),
       message);
}

static const struct pw_core_events core_events = {
    PW_VERSION_CORE_EVENTS,
    .info = on_core_info,
    .error = on_core_error,
};

static void registry_event_global(__attribute__((unused)) void *userdata, uint32_t id,
                                  __attribute__((unused)) uint32_t permissions, const char *type,
                                  __attribute__((unused)) uint32_t version,
                                  const struct spa_dict *props) {
  const struct spa_dict_item *item;
  const char *name, *media_class;

  if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
    name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);

    if (!name || !media_class)
      return;

    debug(1, "pw: registry: id=%" PRIu32 " type=%s name=\"%s\" media_class=\"%s\"", id, type, name,
          media_class);

    spa_dict_for_each(item, props) { debug(1, "pw: \t\t%s = \"%s\"", item->key, item->value); }
  }
}

static void registry_event_global_remove(__attribute__((unused)) void *userdata, uint32_t id) {
  debug(1, "pw: registry: remove id=%" PRIu32 "", id);
}

static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = registry_event_global,
    .global_remove = registry_event_global_remove,
};

static void on_state_changed(void *userdata, enum pw_stream_state old, enum pw_stream_state state,
                             const char *error) {
  struct pw_data *pipewire = userdata;

  debug(1, "pw: stream state changed %s -> %s", pw_stream_state_as_string(old),
        pw_stream_state_as_string(state));

  if (state == PW_STREAM_STATE_STREAMING)
    debug(1, "pw: stream node %" PRIu32 "", pw_stream_get_node_id(pipewire->stream));

  if (state == PW_STREAM_STATE_ERROR)
    debug(1, "pw: stream node %" PRIu32 " error: %s", pw_stream_get_node_id(pipewire->stream),
          error);

  pw_thread_loop_signal(pipewire->mainloop, 0);
}

static void on_process(void *userdata) {
  struct pw_data *pipewire = userdata;

  pw_thread_loop_signal(pipewire->mainloop, 0);
}

static void on_drained(void *userdata) {
  struct pw_data *pipewire = userdata;

  pw_stream_set_active(pipewire->stream, false);

  pw_thread_loop_signal(pipewire->mainloop, 0);
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = on_state_changed,
    .process = on_process,
    .drained = on_drained,
};

static void deinit() {
  pw_thread_loop_stop(data.mainloop);

  if (data.stream) {
    pw_stream_destroy(data.stream);
    data.stream = NULL;
  }

  if (data.registry) {
    pw_proxy_destroy((struct pw_proxy *)data.registry);
    data.registry = NULL;
  }

  if (data.core) {
    pw_core_disconnect(data.core);
    data.core = NULL;
  }

  if (data.context) {
    pw_context_destroy(data.context);
    data.context = NULL;
  }

  if (data.mainloop) {
    pw_thread_loop_destroy(data.mainloop);
    data.mainloop = NULL;
  }

  if (data.props) {
    pw_properties_free(data.props);
    data.props = NULL;
  }

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
}

static int init(__attribute__((unused)) int argc, __attribute__((unused)) char **argv) {

  struct pw_loop *loop;
  struct pw_properties *props;

  // set up default values first
  config.audio_backend_buffer_desired_length = 0.35;
  config.audio_backend_buffer_interpolation_threshold_in_seconds = 0.02;
  config.audio_backend_latency_offset = 0;

  pw_init(NULL, NULL);

  debug(1, "pw: compiled with libpipewire %s", pw_get_headers_version());
  debug(1, "pw: linked with libpipewire: %s", pw_get_library_version());

  data.props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                                 PW_KEY_MEDIA_ROLE, "Music", PW_KEY_APP_NAME, "shairport-sync",
                                 PW_KEY_NODE_NAME, "shairport-sync", NULL);

  if (!data.props) {
    deinit();
    die("pw: pw_properties_new() failed: %m");
  }

  data.mainloop = pw_thread_loop_new("pipewire", NULL);
  if (!data.mainloop) {
    deinit();
    die("pw: pw_thread_loop_new_full() failed: %m");
  }

  props = pw_properties_new(PW_KEY_CONFIG_NAME, "client-rt.conf", NULL);
  if (!props) {
    deinit();
    die("pw: pw_properties_new() failed: %m");
  }

  loop = pw_thread_loop_get_loop(data.mainloop);

  data.context = pw_context_new(loop, props, 0);
  if (!data.context) {
    deinit();
    die("pw: pw_context_new() failed: %m");
  }

  props = pw_properties_new(PW_KEY_REMOTE_NAME, NULL, NULL);
  if (!props) {
    deinit();
    die("pw: pw_properties_new() failed: %m");
  }

  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
  pw_thread_loop_lock(data.mainloop);

  if (pw_thread_loop_start(data.mainloop) != 0) {
    deinit();
    die("pw: pw_thread_loop_start() failed: %m");
  }

  data.core = pw_context_connect(data.context, props, 0);
  if (!data.core) {
    deinit();
    die("pw: pw_context_connect() failed: %m");
  }

  pw_core_add_listener(data.core, &data.core_listener, &core_events, &data);

  data.registry = pw_core_get_registry(data.core, PW_VERSION_REGISTRY, 0);
  if (!data.registry) {
    deinit();
    die("pw: pw_core_get_registry() failed: %m");
  }

  pw_registry_add_listener(data.registry, &data.registry_listener, &registry_events, &data);

  data.sync = pw_core_sync(data.core, 0, data.sync);

  pw_thread_loop_unlock(data.mainloop);
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

  return 0;
}

static enum spa_audio_format sps_format_to_spa_format(sps_format_t sps_format) {
  switch (sps_format) {
  case SPS_FORMAT_S8:
    return SPA_AUDIO_FORMAT_S8;
  case SPS_FORMAT_U8:
    return SPA_AUDIO_FORMAT_U8;
  case SPS_FORMAT_S16:
    return SPA_AUDIO_FORMAT_S16;
  case SPS_FORMAT_S16_LE:
    return SPA_AUDIO_FORMAT_S16_LE;
  case SPS_FORMAT_S16_BE:
    return SPA_AUDIO_FORMAT_S16_BE;
  case SPS_FORMAT_S24:
    return SPA_AUDIO_FORMAT_S24_32;
  case SPS_FORMAT_S24_LE:
    return SPA_AUDIO_FORMAT_S24_32_LE;
  case SPS_FORMAT_S24_BE:
    return SPA_AUDIO_FORMAT_S24_32_BE;
  case SPS_FORMAT_S24_3LE:
    return SPA_AUDIO_FORMAT_S24_LE;
  case SPS_FORMAT_S24_3BE:
    return SPA_AUDIO_FORMAT_S24_BE;
  case SPS_FORMAT_S32:
    return SPA_AUDIO_FORMAT_S32;
  case SPS_FORMAT_S32_LE:
    return SPA_AUDIO_FORMAT_S32_LE;
  case SPS_FORMAT_S32_BE:
    return SPA_AUDIO_FORMAT_S32_BE;

  case SPS_FORMAT_UNKNOWN:
  case SPS_FORMAT_AUTO:
  case SPS_FORMAT_INVALID:
  default:
    return SPA_AUDIO_FORMAT_S16;
  }
}

static int spa_format_samplesize(enum spa_audio_format audio_format) {
  switch (audio_format) {
  case SPA_AUDIO_FORMAT_S8:
  case SPA_AUDIO_FORMAT_U8:
    return 1;
  case SPA_AUDIO_FORMAT_S16:
    return 2;
  case SPA_AUDIO_FORMAT_S24:
    return 3;
  case SPA_AUDIO_FORMAT_S24_32:
  case SPA_AUDIO_FORMAT_S32:
    return 4;
  default:
    die("pw: unhandled spa_audio_format: %d", audio_format);
    return -1;
  }
}

static const char *spa_format_to_str(enum spa_audio_format audio_format) {
  switch (audio_format) {
  case SPA_AUDIO_FORMAT_U8:
    return "u8";
  case SPA_AUDIO_FORMAT_S8:
    return "s8";
  case SPA_AUDIO_FORMAT_S16:
    return "s16";
  case SPA_AUDIO_FORMAT_S24:
  case SPA_AUDIO_FORMAT_S24_32:
    return "s24";
  case SPA_AUDIO_FORMAT_S32:
    return "s32";
  default:
    die("pw: unhandled spa_audio_format: %d", audio_format);
    return "(invalid)";
  }
}

static void start(int sample_rate, int sample_format) {

  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

  pw_thread_loop_lock(data.mainloop);

  const struct spa_pod *params[1];
  uint8_t buffer[1024];
  struct spa_pod_builder pod_builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
  struct spa_audio_info_raw info;
  uint32_t nom;
  int ret;

  data.format = sps_format_to_spa_format(sample_format);
  data.rate = sample_rate;
  data.channels = 2;
  data.stride = spa_format_samplesize(data.format) * data.channels;
  data.latency = 20000; // looks like microseconds

  nom = nearbyint((data.latency * data.rate) / 1000000.0);

  pw_properties_setf(data.props, PW_KEY_NODE_LATENCY, "%u/%u", nom, data.rate); // looks like samples in the data.latency period

  debug(1, "pw: rate: %d", data.rate);
  debug(1, "pw: channgels: %d", data.channels);
  debug(1, "pw: format: %s", spa_format_to_str(data.format));
  debug(1, "pw: samplesize: %d", spa_format_samplesize(data.format));
  debug(1, "pw: stride: %d", data.stride);
  if (data.rate != 0)
    debug(1, "pw: latency: %d samples (%.3fs)", nom, (double)nom / data.rate);

  info = SPA_AUDIO_INFO_RAW_INIT(.flags = SPA_AUDIO_FLAG_NONE, .format = data.format,
                                 .rate = data.rate, .channels = data.channels);

  params[0] = spa_format_audio_raw_build(&pod_builder, SPA_PARAM_EnumFormat, &info);

  data.stream = pw_stream_new(data.core, "shairport-sync", data.props);

  if (!data.stream) {
    deinit();
    die("pw: pw_stream_new() failed: %m");
  }

  debug(1, "pw: connecting stream: target_id=%" PRIu32 "", PW_ID_ANY);

  pw_stream_add_listener(data.stream, &data.stream_listener, &stream_events, &data);

  ret = pw_stream_connect(
      data.stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
      PW_STREAM_FLAG_INACTIVE | PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS, params, 1);

  if (ret < 0) {
    deinit();
    die("pw: pw_stream_connect() failed: %s", spa_strerror(ret));
  }

  const struct pw_properties *props;
  void *pstate;
  const char *key, *val;

  if ((props = pw_stream_get_properties(data.stream)) != NULL) {
    debug(1, "pw: stream properties:");
    pstate = NULL;
    while ((key = pw_properties_iterate(props, &pstate)) != NULL &&
           (val = pw_properties_get(props, key)) != NULL) {
      debug(1, "pw: \t%s = \"%s\"", key, val);
    }
  }

  while (1) {
    enum pw_stream_state stream_state = pw_stream_get_state(data.stream, NULL);
    if (stream_state == PW_STREAM_STATE_PAUSED)
      break;

    struct timespec abstime;

    pw_thread_loop_get_time(data.mainloop, &abstime, PW_TIMEOUT_NS);

    ret = pw_thread_loop_timed_wait_full(data.mainloop, &abstime);
    if (ret == -ETIMEDOUT) {
      deinit();
      die("pw: pw_thread_loop_timed_wait_full timed out: %s", strerror(ret));
    }
  }

  pw_thread_loop_unlock(data.mainloop);

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
}

static void stop() {
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
  pw_thread_loop_lock(data.mainloop);

  pw_stream_flush(data.stream, true);

  pw_thread_loop_unlock(data.mainloop);
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
}

static void flush() {
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
  pw_thread_loop_lock(data.mainloop);

  pw_stream_flush(data.stream, false);

  pw_thread_loop_unlock(data.mainloop);
  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
}

static int play(void *buf, int samples, __attribute__((unused)) int sample_type,
                __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime) {
  struct pw_buffer *pw_buffer = NULL;
  struct spa_buffer *spa_buffer;
  struct spa_data *spa_data;
  int ret;

  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
  pw_thread_loop_lock(data.mainloop);

  if (pw_stream_get_state(data.stream, NULL) == PW_STREAM_STATE_PAUSED)
    pw_stream_set_active(data.stream, true);

  while (pw_buffer == NULL) {
    pw_buffer = pw_stream_dequeue_buffer(data.stream);
    if (pw_buffer)
      break;

    struct timespec abstime;

    pw_thread_loop_get_time(data.mainloop, &abstime, PW_TIMEOUT_NS);

    ret = pw_thread_loop_timed_wait_full(data.mainloop, &abstime);
    if (ret == -ETIMEDOUT) {
      pw_thread_loop_unlock(data.mainloop);
      pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
      return ret;
    }
  }

  spa_buffer = pw_buffer->buffer;
  spa_data = &spa_buffer->datas[0];

  size_t bytes_to_copy = samples * data.stride;

  debug(3, "pw: bytes_to_copy: %d", bytes_to_copy);

  if (spa_data->maxsize < bytes_to_copy)
    bytes_to_copy = spa_data->maxsize;

  debug(3, "pw: spa_data->maxsize: %d", spa_data->maxsize);

  memcpy(spa_data->data, buf, bytes_to_copy);

  spa_data->chunk->offset = 0;
  spa_data->chunk->stride = data.stride;
  spa_data->chunk->size = bytes_to_copy;

  pw_stream_queue_buffer(data.stream, pw_buffer);

  pw_thread_loop_unlock(data.mainloop);

  pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);

  return 0;
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
                         .delay = NULL,
                         .stats = NULL,
                         .play = &play,
                         .volume = NULL,
                         .parameters = NULL,
                         .mute = NULL};