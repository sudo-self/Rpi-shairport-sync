/*
 * libalsa output driver. This file is part of Shairport.
 * Copyright (c) Muffinman, Skaman 2013
 * Copyright (c) Mike Brady 2014 -- 2018
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

#define ALSA_PCM_NEW_HW_PARAMS_API

#include "audio.h"
#include "common.h"
#include <alsa/asoundlib.h>
#include <inttypes.h>
#include <math.h>
#include <memory.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

static void help(void);
static int init(int argc, char **argv);
static void deinit(void);
static void start(int i_sample_rate, int i_sample_format);
static int play(void *buf, int samples);
static void stop(void);
static void flush(void);
int delay(long *the_delay);
void do_mute(int request);
int get_rate_information(uint64_t *elapsed_time, uint64_t *frames_played);
void *alsa_buffer_monitor_thread_code(void *arg);

static void volume(double vol);
void do_volume(double vol);

static void parameters(audio_parameters *info);
static void mute(int do_mute);
static double set_volume;
static int output_method_signalled = 0;

audio_output audio_alsa = {
    .name = "alsa",
    .help = &help,
    .init = &init,
    .deinit = &deinit,
    .start = &start,
    .stop = &stop,
    .is_running = NULL,
    .flush = &flush,
    .delay = &delay,
    .play = &play,
    .rate_info = &get_rate_information,
    .mute = NULL,   // a function will be provided if it can, and is allowed to, do hardware mute
    .volume = NULL, // a function will be provided if it can do hardware volume
    .parameters = NULL}; // a function will be provided if it can do hardware volume

static pthread_mutex_t alsa_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_t alsa_buffer_monitor_thread;

// for tracking how long the output device has stalled
uint64_t stall_monitor_start_time;      // zero if not initialised / not started / zeroed by flush
long stall_monitor_frame_count;         // set to delay at start of time, incremented by any writes
uint64_t stall_monitor_error_threshold; // if the time is longer than this, it's an error

static snd_output_t *output = NULL;
static unsigned int desired_sample_rate;
static enum sps_format_t sample_format;
int frame_size; // in bytes for interleaved stereo

static snd_pcm_t *alsa_handle = NULL;
static snd_pcm_hw_params_t *alsa_params = NULL;
static snd_pcm_sw_params_t *alsa_swparams = NULL;
static snd_ctl_t *ctl = NULL;
static snd_ctl_elem_id_t *elem_id = NULL;
static snd_mixer_t *alsa_mix_handle = NULL;
static snd_mixer_elem_t *alsa_mix_elem = NULL;
static snd_mixer_selem_id_t *alsa_mix_sid = NULL;
static long alsa_mix_minv, alsa_mix_maxv;
static long alsa_mix_mindb, alsa_mix_maxdb;

static char *alsa_out_dev = "default";
static char *alsa_mix_dev = NULL;
static char *alsa_mix_ctrl = "Master";
static int alsa_mix_index = 0;
static int hardware_mixer = 0;
static int has_softvol = 0;

int keep_dac_busy = 0; // true if the dac must be kept busy with samples
int64_t dither_random_number_store = 0;

static int volume_set_request = 0;       // set when an external request is made to set the volume.
int mute_request_pending = 0;            //  set when an external request is made to mute or unmute.
int overriding_mute_state_requested = 0; // 1 = mute; 0 = unmute requested
int mixer_volume_setting_gives_mute =
    0;              // set when it is discovered that particular mixer volume setting causes a mute.
long alsa_mix_mute; // setting the volume to this value mutes output, if
                    // mixer_volume_setting_gives_mute is true
int volume_based_mute_is_active =
    0; // set when muting is being done by a setting the volume to a magic value

static snd_pcm_sframes_t (*alsa_pcm_write)(snd_pcm_t *, const void *,
                                           snd_pcm_uframes_t) = snd_pcm_writei;

// static int play_number;
// static int64_t accumulated_delay, accumulated_da_delay;
int alsa_characteristics_already_listed = 0;

static snd_pcm_uframes_t period_size_requested, buffer_size_requested;
static int set_period_size_request, set_buffer_size_request;

static uint64_t measurement_start_time;
static uint64_t frames_played_at_measurement_start_time;

static uint64_t measurement_time;
static uint64_t frames_played_at_measurement_time;

static uint64_t frames_sent_for_playing;
static uint64_t frame_index;
static int measurement_data_is_valid;

static void help(void) {
  printf("    -d output-device    set the output device [default*|...]\n"
         "    -m mixer-device     set the mixer device ['output-device'*|...]\n"
         "    -c mixer-control    set the mixer control [Master*|...]\n"
         "    -i mixer-index      set the mixer index [0*|...]\n"
         "    *) default option\n");
}

void set_alsa_out_dev(char *dev) { alsa_out_dev = dev; }

// assuming pthread cancellation is disabled
int open_mixer() {
  int response = 0;
  if (hardware_mixer) {
    debug(3, "Open Mixer");
    int ret = 0;
    snd_mixer_selem_id_alloca(&alsa_mix_sid);
    snd_mixer_selem_id_set_index(alsa_mix_sid, alsa_mix_index);
    snd_mixer_selem_id_set_name(alsa_mix_sid, alsa_mix_ctrl);

    if ((snd_mixer_open(&alsa_mix_handle, 0)) < 0) {
      debug(1, "Failed to open mixer");
      response = -1;
    } else {
      debug(3, "Mixer device name is \"%s\".", alsa_mix_dev);
      if ((snd_mixer_attach(alsa_mix_handle, alsa_mix_dev)) < 0) {
        debug(1, "Failed to attach mixer");
        response = -2;
      } else {
        if ((snd_mixer_selem_register(alsa_mix_handle, NULL, NULL)) < 0) {
          debug(1, "Failed to register mixer element");
          response = -3;
        } else {
          ret = snd_mixer_load(alsa_mix_handle);
          if (ret < 0) {
            debug(1, "Failed to load mixer element");
            response = -4;
          } else {
            debug(3, "Mixer Control name is \"%s\".", alsa_mix_ctrl);
            alsa_mix_elem = snd_mixer_find_selem(alsa_mix_handle, alsa_mix_sid);
            if (!alsa_mix_elem) {
              debug(1, "Failed to find mixer element");
              response = -5;
            } else {
              response = 1; // we found a hardware mixer and successfully opened it
            }
          }
        }
      }
    }
  }
  return response;
}

// assuming pthread cancellation is disabled
void close_mixer() {
  if (alsa_mix_handle) {
    snd_mixer_close(alsa_mix_handle);
    alsa_mix_handle = NULL;
  }
}

// assuming pthread cancellation is disabled
void do_snd_mixer_selem_set_playback_dB_all(snd_mixer_elem_t *mix_elem, double vol) {
  if (snd_mixer_selem_set_playback_dB_all(mix_elem, vol, 0) != 0) {
    debug(1, "Can't set playback volume accurately to %f dB.", vol);
    if (snd_mixer_selem_set_playback_dB_all(mix_elem, vol, -1) != 0)
      if (snd_mixer_selem_set_playback_dB_all(mix_elem, vol, 1) != 0)
        debug(1, "Could not set playback dB volume on the mixer.");
  }
}

void actual_close_alsa_device() {
  if (alsa_handle) {
    int derr;
    if ((derr = snd_pcm_hw_free(alsa_handle)))
      debug(1, "Error %d (\"%s\") freeing the output device hardware while closing it.", derr,
            snd_strerror(derr));

    if ((derr = snd_pcm_close(alsa_handle)))
      debug(1, "Error %d (\"%s\") closing the output device.", derr, snd_strerror(derr));
    alsa_handle = NULL;
  }
}

// assuming pthread cancellation is disabled
int actual_open_alsa_device(void) {
  // the alsa mutex is already acquired when this is called
  const snd_pcm_uframes_t minimal_buffer_headroom =
      352 * 2; // we accept this much headroom in the hardware buffer, but we'll
               // accept less
               /*
                 const snd_pcm_uframes_t requested_buffer_headroom =
                     minimal_buffer_headroom + 2048; // we ask for this much headroom in the
                                                     // hardware buffer, but we'll accept less
               */

  int ret, dir = 0;
  unsigned int my_sample_rate = desired_sample_rate;
  // snd_pcm_uframes_t frames = 441 * 10;
  snd_pcm_uframes_t actual_buffer_length;
  snd_pcm_access_t access;

  // ensure no calls are made to the alsa device enquiring about the buffer length if
  // synchronisation is disabled.
  if (config.no_sync != 0)
    audio_alsa.delay = NULL;

  // ensure no calls are made to the alsa device enquiring about the buffer length if
  // synchronisation is disabled.
  if (config.no_sync != 0)
    audio_alsa.delay = NULL;

  ret = snd_pcm_open(&alsa_handle, alsa_out_dev, SND_PCM_STREAM_PLAYBACK, 0);
  if (ret < 0)
    return (-10);

  snd_pcm_hw_params_alloca(&alsa_params);
  snd_pcm_sw_params_alloca(&alsa_swparams);

  ret = snd_pcm_hw_params_any(alsa_handle, alsa_params);
  if (ret < 0) {
    warn("audio_alsa: Broken configuration for device \"%s\": no configurations "
         "available",
         alsa_out_dev);
    return -11;
  }

  if ((config.no_mmap == 0) &&
      (snd_pcm_hw_params_set_access(alsa_handle, alsa_params, SND_PCM_ACCESS_MMAP_INTERLEAVED) >=
       0)) {
    if (output_method_signalled == 0) {
      debug(3, "Output written using MMAP");
      output_method_signalled = 1;
    }
    access = SND_PCM_ACCESS_MMAP_INTERLEAVED;
    alsa_pcm_write = snd_pcm_mmap_writei;
  } else {
    if (output_method_signalled == 0) {
      debug(3, "Output written with RW");
      output_method_signalled = 1;
    }
    access = SND_PCM_ACCESS_RW_INTERLEAVED;
    alsa_pcm_write = snd_pcm_writei;
  }

  ret = snd_pcm_hw_params_set_access(alsa_handle, alsa_params, access);
  if (ret < 0) {
    warn("audio_alsa: Access type not available for device \"%s\": %s", alsa_out_dev,
         snd_strerror(ret));
    return -12;
  }
  snd_pcm_format_t sf;
  switch (sample_format) {
  case SPS_FORMAT_S8:
    sf = SND_PCM_FORMAT_S8;
    frame_size = 2;
    break;
  case SPS_FORMAT_U8:
    sf = SND_PCM_FORMAT_U8;
    frame_size = 2;
    break;
  case SPS_FORMAT_S16:
    sf = SND_PCM_FORMAT_S16;
    frame_size = 4;
    break;
  case SPS_FORMAT_S24:
    sf = SND_PCM_FORMAT_S24;
    frame_size = 8;
    break;
  case SPS_FORMAT_S24_3LE:
    sf = SND_PCM_FORMAT_S24_3LE;
    frame_size = 6;
    break;
  case SPS_FORMAT_S24_3BE:
    sf = SND_PCM_FORMAT_S24_3BE;
    frame_size = 6;
    break;
  case SPS_FORMAT_S32:
    sf = SND_PCM_FORMAT_S32;
    frame_size = 8;
    break;
  default:
    sf = SND_PCM_FORMAT_S16; // this is just to quieten a compiler warning
    frame_size = 4;
    debug(1, "Unsupported output format at audio_alsa.c");
    return -1;
  }
  ret = snd_pcm_hw_params_set_format(alsa_handle, alsa_params, sf);
  if (ret < 0) {
    warn("audio_alsa: Sample format %d not available for device \"%s\": %s", sample_format,
         alsa_out_dev, snd_strerror(ret));
    return -2;
  }

  ret = snd_pcm_hw_params_set_channels(alsa_handle, alsa_params, 2);
  if (ret < 0) {
    warn("audio_alsa: Channels count (2) not available for device \"%s\": %s", alsa_out_dev,
         snd_strerror(ret));
    return -3;
  }

  ret = snd_pcm_hw_params_set_rate_near(alsa_handle, alsa_params, &my_sample_rate, &dir);
  if (ret < 0) {
    warn("audio_alsa: Rate %iHz not available for playback: %s", desired_sample_rate,
         snd_strerror(ret));
    return -4;
  }

  if (set_period_size_request != 0) {
    debug(1, "Attempting to set the period size");
    ret = snd_pcm_hw_params_set_period_size_near(alsa_handle, alsa_params, &period_size_requested,
                                                 &dir);
    if (ret < 0) {
      warn("audio_alsa: cannot set period size of %lu: %s", period_size_requested,
           snd_strerror(ret));
      return -5;
    } else {
      snd_pcm_uframes_t actual_period_size;
      snd_pcm_hw_params_get_period_size(alsa_params, &actual_period_size, &dir);
      if (actual_period_size != period_size_requested)
        inform("Actual period size set to a different value than requested. Requested: %lu, actual "
               "setting: %lu",
               period_size_requested, actual_period_size);
    }
  }

  if (set_buffer_size_request != 0) {
    debug(1, "Attempting to set the buffer size to %lu", buffer_size_requested);
    ret = snd_pcm_hw_params_set_buffer_size_near(alsa_handle, alsa_params, &buffer_size_requested);
    if (ret < 0) {
      warn("audio_alsa: cannot set buffer size of %lu: %s", buffer_size_requested,
           snd_strerror(ret));
      return -6;
    } else {
      snd_pcm_uframes_t actual_buffer_size;
      snd_pcm_hw_params_get_buffer_size(alsa_params, &actual_buffer_size);
      if (actual_buffer_size != buffer_size_requested)
        inform("Actual period size set to a different value than requested. Requested: %lu, actual "
               "setting: %lu",
               buffer_size_requested, actual_buffer_size);
    }
  }

  ret = snd_pcm_hw_params(alsa_handle, alsa_params);
  if (ret < 0) {
    warn("audio_alsa: Unable to set hw parameters for device \"%s\": %s.", alsa_out_dev,
         snd_strerror(ret));
    return -7;
  }

  // check parameters after attempting to set them…

  if (set_period_size_request != 0) {
    snd_pcm_uframes_t actual_period_size;
    snd_pcm_hw_params_get_period_size(alsa_params, &actual_period_size, &dir);
    if (actual_period_size != period_size_requested)
      inform("Actual period size set to a different value than requested. Requested: %lu, actual "
             "setting: %lu",
             period_size_requested, actual_period_size);
  }

  if (set_buffer_size_request != 0) {
    snd_pcm_uframes_t actual_buffer_size;
    snd_pcm_hw_params_get_buffer_size(alsa_params, &actual_buffer_size);
    if (actual_buffer_size != buffer_size_requested)
      inform("Actual period size set to a different value than requested. Requested: %lu, actual "
             "setting: %lu",
             buffer_size_requested, actual_buffer_size);
  }

  if (my_sample_rate != desired_sample_rate) {
    warn("Can't set the D/A converter to %d.", desired_sample_rate);
    return -8;
  }

  ret = snd_pcm_hw_params_get_buffer_size(alsa_params, &actual_buffer_length);
  if (ret < 0) {
    warn("audio_alsa: Unable to get hw buffer length for device \"%s\": %s.", alsa_out_dev,
         snd_strerror(ret));
    return -9;
  }

  ret = snd_pcm_sw_params_current(alsa_handle, alsa_swparams);
  if (ret < 0) {
    warn("audio_alsa: Unable to get current sw parameters for device \"%s\": %s.", alsa_out_dev,
         snd_strerror(ret));
    return -10;
  }

  ret = snd_pcm_sw_params_set_tstamp_mode(alsa_handle, alsa_swparams, SND_PCM_TSTAMP_ENABLE);
  if (ret < 0) {
    warn("audio_alsa: Can't enable timestamp mode of device: \"%s\": %s.", alsa_out_dev,
         snd_strerror(ret));
    return -11;
  }

  /* write the sw parameters */
  ret = snd_pcm_sw_params(alsa_handle, alsa_swparams);
  if (ret < 0) {
    warn("audio_alsa: Unable to set software parameters of device: \"%s\": %s.", alsa_out_dev,
         snd_strerror(ret));
    return -12;
  }

  if (actual_buffer_length < config.audio_backend_buffer_desired_length + minimal_buffer_headroom) {
    /*
    // the dac buffer is too small, so let's try to set it
    buffer_size =
        config.audio_backend_buffer_desired_length + requested_buffer_headroom;
    ret = snd_pcm_hw_params_set_buffer_size_near(alsa_handle, alsa_params,
                                                 &buffer_size);
    if (ret < 0)
      die("audio_alsa: Unable to set hw buffer size to %lu for device \"%s\": "
          "%s.",
          config.audio_backend_buffer_desired_length +
              requested_buffer_headroom,
          alsa_out_dev, snd_strerror(ret));
    if (config.audio_backend_buffer_desired_length + minimal_buffer_headroom >
        buffer_size) {
      die("audio_alsa: Can't set hw buffer size to %lu or more for device "
          "\"%s\". Requested size: %lu, granted size: %lu.",
          config.audio_backend_buffer_desired_length + minimal_buffer_headroom,
          alsa_out_dev, config.audio_backend_buffer_desired_length +
                            requested_buffer_headroom,
          buffer_size);
    }
    */
    debug(1, "The alsa buffer is smaller (%lu bytes) than the desired backend buffer "
             "length (%ld) you have chosen.",
          actual_buffer_length, config.audio_backend_buffer_desired_length);
  }

  if (alsa_characteristics_already_listed == 0) {
    alsa_characteristics_already_listed = 1;
    int log_level = 2; // the level at which debug information should be output
                       //    int rc;
    snd_pcm_access_t access_type;
    snd_pcm_format_t format_type;
    snd_pcm_subformat_t subformat_type;
    //    unsigned int val, val2;
    unsigned int uval, uval2;
    int sval;
    int dir;
    snd_pcm_uframes_t frames;

    debug(log_level, "PCM handle name = '%s'", snd_pcm_name(alsa_handle));

    //			ret = snd_pcm_hw_params_any(alsa_handle, alsa_params);
    //			if (ret < 0) {
    //				die("audio_alsa: Cannpot get configuration for device \"%s\": no
    // configurations
    //"
    //						"available",
    //						alsa_out_dev);
    //			}

    debug(log_level, "alsa device parameters:");

    snd_pcm_hw_params_get_access(alsa_params, &access_type);
    debug(log_level, "  access type = %s", snd_pcm_access_name(access_type));

    snd_pcm_hw_params_get_format(alsa_params, &format_type);
    debug(log_level, "  format = '%s' (%s)", snd_pcm_format_name(format_type),
          snd_pcm_format_description(format_type));

    snd_pcm_hw_params_get_subformat(alsa_params, &subformat_type);
    debug(log_level, "  subformat = '%s' (%s)", snd_pcm_subformat_name(subformat_type),
          snd_pcm_subformat_description(subformat_type));

    snd_pcm_hw_params_get_channels(alsa_params, &uval);
    debug(log_level, "  number of channels = %u", uval);

    sval = snd_pcm_hw_params_get_sbits(alsa_params);
    debug(log_level, "  number of significant bits = %d", sval);

    snd_pcm_hw_params_get_rate(alsa_params, &uval, &dir);
    switch (dir) {
    case -1:
      debug(log_level, "  rate = %u frames per second (<).", uval);
      break;
    case 0:
      debug(log_level, "  rate = %u frames per second (precisely).", uval);
      break;
    case 1:
      debug(log_level, "  rate = %u frames per second (>).", uval);
      break;
    }

    if (snd_pcm_hw_params_get_rate_numden(alsa_params, &uval, &uval2) == 0)
      debug(log_level, "  precise (rational) rate = %.3f frames per second (i.e. %u/%u).", uval,
            uval2, ((double)uval) / uval2);
    else
      debug(log_level, "  precise (rational) rate information unavailable.");

    snd_pcm_hw_params_get_period_time(alsa_params, &uval, &dir);
    switch (dir) {
    case -1:
      debug(log_level, "  period_time = %u us (<).", uval);
      break;
    case 0:
      debug(log_level, "  period_time = %u us (precisely).", uval);
      break;
    case 1:
      debug(log_level, "  period_time = %u us (>).", uval);
      break;
    }

    snd_pcm_hw_params_get_period_size(alsa_params, &frames, &dir);
    switch (dir) {
    case -1:
      debug(log_level, "  period_size = %lu frames (<).", frames);
      break;
    case 0:
      debug(log_level, "  period_size = %lu frames (precisely).", frames);
      break;
    case 1:
      debug(log_level, "  period_size = %lu frames (>).", frames);
      break;
    }

    snd_pcm_hw_params_get_buffer_time(alsa_params, &uval, &dir);
    switch (dir) {
    case -1:
      debug(log_level, "  buffer_time = %u us (<).", uval);
      break;
    case 0:
      debug(log_level, "  buffer_time = %u us (precisely).", uval);
      break;
    case 1:
      debug(log_level, "  buffer_time = %u us (>).", uval);
      break;
    }

    snd_pcm_hw_params_get_buffer_size(alsa_params, &frames);
    switch (dir) {
    case -1:
      debug(log_level, "  buffer_size = %lu frames (<).", frames);
      break;
    case 0:
      debug(log_level, "  buffer_size = %lu frames (precisely).", frames);
      break;
    case 1:
      debug(log_level, "  buffer_size = %lu frames (>).", frames);
      break;
    }

    snd_pcm_hw_params_get_periods(alsa_params, &uval, &dir);
    switch (dir) {
    case -1:
      debug(log_level, "  periods_per_buffer = %u (<).", uval);
      break;
    case 0:
      debug(log_level, "  periods_per_buffer = %u (precisely).", uval);
      break;
    case 1:
      debug(log_level, "  periods_per_buffer = %u (>).", uval);
      break;
    }
  }
  return 0;
}

int open_alsa_device(void) {
  int result;
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  result = actual_open_alsa_device();
  pthread_setcancelstate(oldState, NULL);
  return result;
}

static int init(int argc, char **argv) {
  // for debugging
  snd_output_stdio_attach(&output, stdout, 0);

  // debug(2,"audio_alsa init called.");
  int response = 0; // this will be what we return to the caller.
  const char *str;
  int value;
  // double dvalue;

  // set up default values first
  set_period_size_request = 0;
  set_buffer_size_request = 0;
  config.alsa_use_hardware_mute = 0; // don't use it by default

  config.audio_backend_latency_offset = 0;
  config.audio_backend_buffer_desired_length = 0.15;

  config.alsa_maximum_stall_time = 0.200; // 200 milliseconds -- if it takes longer, it's a problem

  // get settings from settings file first, allow them to be overridden by
  // command line options

  // do the "general" audio  options. Note, these options are in the "general" stanza!
  parse_general_audio_options();

  if (config.cfg != NULL) {
    double dvalue;

    /* Get the Output Device Name. */
    if (config_lookup_string(config.cfg, "alsa.output_device", &str)) {
      alsa_out_dev = (char *)str;
    }

    /* Get the Mixer Type setting. */

    if (config_lookup_string(config.cfg, "alsa.mixer_type", &str)) {
      inform("The alsa mixer_type setting is deprecated and has been ignored. "
             "FYI, using the \"mixer_control_name\" setting automatically "
             "chooses a hardware mixer.");
    }

    /* Get the Mixer Device Name. */
    if (config_lookup_string(config.cfg, "alsa.mixer_device", &str)) {
      alsa_mix_dev = (char *)str;
    }

    /* Get the Mixer Control Name. */
    if (config_lookup_string(config.cfg, "alsa.mixer_control_name", &str)) {
      alsa_mix_ctrl = (char *)str;
      hardware_mixer = 1;
    }

    /* Get the disable_synchronization setting. */
    if (config_lookup_string(config.cfg, "alsa.disable_synchronization", &str)) {
      if (strcasecmp(str, "no") == 0)
        config.no_sync = 0;
      else if (strcasecmp(str, "yes") == 0)
        config.no_sync = 1;
      else {
        warn("Invalid disable_synchronization option choice \"%s\". It should be \"yes\" or "
             "\"no\". It is set to \"no\".");
        config.no_sync = 0;
      }
    }

    /* Get the mute_using_playback_switch setting. */
    if (config_lookup_string(config.cfg, "alsa.mute_using_playback_switch", &str)) {
      inform("The alsa \"mute_using_playback_switch\" setting is deprecated. "
             "Please use the \"use_hardware_mute_if_available\" setting instead.");
      if (strcasecmp(str, "no") == 0)
        config.alsa_use_hardware_mute = 0;
      else if (strcasecmp(str, "yes") == 0)
        config.alsa_use_hardware_mute = 1;
      else {
        warn("Invalid mute_using_playback_switch option choice \"%s\". It should be \"yes\" or "
             "\"no\". It is set to \"no\".");
        config.alsa_use_hardware_mute = 0;
      }
    }

    /* Get the use_hardware_mute_if_available setting. */
    if (config_lookup_string(config.cfg, "alsa.use_hardware_mute_if_available", &str)) {
      if (strcasecmp(str, "no") == 0)
        config.alsa_use_hardware_mute = 0;
      else if (strcasecmp(str, "yes") == 0)
        config.alsa_use_hardware_mute = 1;
      else {
        warn("Invalid use_hardware_mute_if_available option choice \"%s\". It should be \"yes\" or "
             "\"no\". It is set to \"no\".");
        config.alsa_use_hardware_mute = 0;
      }
    }

    /* Get the output format, using the same names as aplay does*/
    if (config_lookup_string(config.cfg, "alsa.output_format", &str)) {
      if (strcasecmp(str, "S16") == 0)
        config.output_format = SPS_FORMAT_S16;
      else if (strcasecmp(str, "S24") == 0)
        config.output_format = SPS_FORMAT_S24;
      else if (strcasecmp(str, "S24_3LE") == 0)
        config.output_format = SPS_FORMAT_S24_3LE;
      else if (strcasecmp(str, "S24_3BE") == 0)
        config.output_format = SPS_FORMAT_S24_3BE;
      else if (strcasecmp(str, "S32") == 0)
        config.output_format = SPS_FORMAT_S32;
      else if (strcasecmp(str, "U8") == 0)
        config.output_format = SPS_FORMAT_U8;
      else if (strcasecmp(str, "S8") == 0)
        config.output_format = SPS_FORMAT_S8;
      else {
        warn("Invalid output format \"%s\". It should be \"U8\", \"S8\", \"S16\", \"S24\", "
             "\"S24_3LE\", \"S24_3BE\" or "
             "\"S32\". It is set to \"S16\".",
             str);
        config.output_format = SPS_FORMAT_S16;
      }
    }

    /* Get the output rate, which must be a multiple of 44,100*/
    if (config_lookup_int(config.cfg, "alsa.output_rate", &value)) {
      debug(1, "alsa output rate is %d frames per second", value);
      switch (value) {
      case 44100:
      case 88200:
      case 176400:
      case 352800:
        config.output_rate = value;
        break;
      default:
        warn("Invalid output rate \"%d\". It should be a multiple of 44,100 up to 352,800. It is "
             "set to 44,100",
             value);
        config.output_rate = 44100;
      }
    }

    /* Get the use_mmap_if_available setting. */
    if (config_lookup_string(config.cfg, "alsa.use_mmap_if_available", &str)) {
      if (strcasecmp(str, "no") == 0)
        config.no_mmap = 1;
      else if (strcasecmp(str, "yes") == 0)
        config.no_mmap = 0;
      else {
        warn("Invalid use_mmap_if_available option choice \"%s\". It should be \"yes\" or \"no\". "
             "It is set to \"yes\".");
        config.no_mmap = 0;
      }
    }
    /* Get the optional period size value */
    if (config_lookup_int(config.cfg, "alsa.period_size", &value)) {
      set_period_size_request = 1;
      debug(1, "Value read for period size is %d.", value);
      if (value < 0) {
        warn("Invalid alsa period size setting \"%d\". It "
             "must be greater than 0. No setting is made.",
             value);
        set_period_size_request = 0;
      } else {
        period_size_requested = value;
      }
    }

    /* Get the optional buffer size value */
    if (config_lookup_int(config.cfg, "alsa.buffer_size", &value)) {
      set_buffer_size_request = 1;
      debug(1, "Value read for buffer size is %d.", value);
      if (value < 0) {
        warn("Invalid alsa buffer size setting \"%d\". It "
             "must be greater than 0. No setting is made.",
             value);
        set_buffer_size_request = 0;
      } else {
        buffer_size_requested = value;
      }
    }

    /* Get the optional alsa_maximum_stall_time setting. */
    if (config_lookup_float(config.cfg, "alsa.maximum_stall_time", &dvalue)) {
      if (dvalue < 0.0) {
        warn("Invalid alsa maximum write time setting \"%f\". It "
             "must be greater than 0. Default is \"%f\". No setting is made.",
             dvalue, config.alsa_maximum_stall_time);
      } else {
        config.alsa_maximum_stall_time = dvalue;
      }
    }

    // Get the optional "Keep DAC Busy setting"
    config_set_lookup_bool(config.cfg, "alsa.disable_standby_mode", &keep_dac_busy);
    debug(1, "alsa: disable_standby_mode is %s.", keep_dac_busy ? "on" : "off");
  }

  optind = 1; // optind=0 is equivalent to optind=1 plus special behaviour
  argv--;     // so we shift the arguments to satisfy getopt()
  argc++;
  // some platforms apparently require optreset = 1; - which?
  int opt;
  while ((opt = getopt(argc, argv, "d:t:m:c:i:")) > 0) {
    switch (opt) {
    case 'd':
      alsa_out_dev = optarg;
      break;

    case 't':
      inform("The alsa backend -t option is deprecated and has been ignored. "
             "FYI, using the -c option automatically chooses a hardware "
             "mixer.");
      break;

    case 'm':
      alsa_mix_dev = optarg;
      break;
    case 'c':
      alsa_mix_ctrl = optarg;
      hardware_mixer = 1;
      break;
    case 'i':
      alsa_mix_index = strtol(optarg, NULL, 10);
      break;
    default:
      warn("Invalid audio option \"-%c\" specified -- ignored.", opt);
      help();
    }
  }

  if (optind < argc) {
    warn("Invalid audio argument: \"%s\" -- ignored", argv[optind]);
  }

  debug(1, "alsa: output device name is \"%s\".", alsa_out_dev);

  if (hardware_mixer) {
    int oldState;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable

    if (alsa_mix_dev == NULL)
      alsa_mix_dev = alsa_out_dev;

    // Now, start trying to initialise the alsa device with the settings obtained
    pthread_cleanup_debug_mutex_lock(&alsa_mutex, 1000, 1);
    if (open_mixer() == 1) {
      if (snd_mixer_selem_get_playback_volume_range(alsa_mix_elem, &alsa_mix_minv, &alsa_mix_maxv) <
          0)
        debug(1, "Can't read mixer's [linear] min and max volumes.");
      else {
        if (snd_mixer_selem_get_playback_dB_range(alsa_mix_elem, &alsa_mix_mindb,
                                                  &alsa_mix_maxdb) == 0) {

          audio_alsa.volume = &volume; // insert the volume function now we know it can do dB stuff
          audio_alsa.parameters = &parameters; // likewise the parameters stuff
          if (alsa_mix_mindb == SND_CTL_TLV_DB_GAIN_MUTE) {
            // For instance, the Raspberry Pi does this
            debug(1, "Lowest dB value is a mute");
            mixer_volume_setting_gives_mute = 1;
            alsa_mix_mute = SND_CTL_TLV_DB_GAIN_MUTE; // this may not be necessary -- it's always
            // going to be SND_CTL_TLV_DB_GAIN_MUTE, right?
            // debug(1, "Try minimum volume + 1 as lowest true attenuation value");
            if (snd_mixer_selem_ask_playback_vol_dB(alsa_mix_elem, alsa_mix_minv + 1,
                                                    &alsa_mix_mindb) != 0)
              debug(1, "Can't get dB value corresponding to a minimum volume + 1.");
          }
          debug(1, "Hardware mixer has dB volume from %f to %f.", (1.0 * alsa_mix_mindb) / 100.0,
                (1.0 * alsa_mix_maxdb) / 100.0);
        } else {
          // use the linear scale and do the db conversion ourselves
          warn("The hardware mixer specified -- \"%s\" -- does not have "
               "a dB volume scale.",
               alsa_mix_ctrl);

          if (snd_ctl_open(&ctl, alsa_mix_dev, 0) < 0) {
            warn("Cannot open control \"%s\"", alsa_mix_dev);
            response = -1;
          }
          if (snd_ctl_elem_id_malloc(&elem_id) < 0) {
            debug(1, "Cannot allocate memory for control \"%s\"", alsa_mix_dev);
            elem_id = NULL;
            response = -2;
          } else {
            snd_ctl_elem_id_set_interface(elem_id, SND_CTL_ELEM_IFACE_MIXER);
            snd_ctl_elem_id_set_name(elem_id, alsa_mix_ctrl);

            if (snd_ctl_get_dB_range(ctl, elem_id, &alsa_mix_mindb, &alsa_mix_maxdb) == 0) {
              debug(1, "alsa: hardware mixer \"%s\" selected, with dB volume from %f to %f.",
                    alsa_mix_ctrl, (1.0 * alsa_mix_mindb) / 100.0, (1.0 * alsa_mix_maxdb) / 100.0);
              has_softvol = 1;
              audio_alsa.volume =
                  &volume; // insert the volume function now we know it can do dB stuff
              audio_alsa.parameters = &parameters; // likewise the parameters stuff
            } else {
              debug(1, "Cannot get the dB range from the volume control \"%s\"", alsa_mix_ctrl);
            }
          }
          /*
          debug(1, "Min and max volumes are %d and
          %d.",alsa_mix_minv,alsa_mix_maxv);
          alsa_mix_maxdb = 0;
          if ((alsa_mix_maxv!=0) && (alsa_mix_minv!=0))
            alsa_mix_mindb =
          -20*100*(log10(alsa_mix_maxv*1.0)-log10(alsa_mix_minv*1.0));
          else if (alsa_mix_maxv!=0)
            alsa_mix_mindb = -20*100*log10(alsa_mix_maxv*1.0);
          audio_alsa.volume = &linear_volume; // insert the linear volume function
          audio_alsa.parameters = &parameters; // likewise the parameters stuff
          debug(1,"Max and min dB calculated are %d and
          %d.",alsa_mix_maxdb,alsa_mix_mindb);
          */
        }
      }
      if (((config.alsa_use_hardware_mute == 1) &&
           (snd_mixer_selem_has_playback_switch(alsa_mix_elem))) ||
          mixer_volume_setting_gives_mute) {
        audio_alsa.mute = &mute; // insert the mute function now we know it can do muting stuff
        // debug(1, "Has mixer and mute ability we will use.");
      } else {
        // debug(1, "Has mixer but not using hardware mute.");
      }
      close_mixer();
    }
    debug_mutex_unlock(&alsa_mutex, 3); // release the mutex

    pthread_cleanup_pop(0);
    pthread_setcancelstate(oldState, NULL);
  } else {
    debug(1, "alsa: no hardware mixer selected.");
  }
  alsa_mix_handle = NULL;

  // so, now, if the option to keep the DAC running has been selected, start a thread to monitor the
  // length of the queue
  // if the queue gets too short, stuff it with silence

  desired_sample_rate = config.output_rate;
  sample_format = config.output_format;

  if (response == 0) {
    // try opening the device.
    int ret = actual_open_alsa_device();
    if (ret == 0)
      actual_close_alsa_device();
    else
      die("Could not open the alsa device with the settings given.");
  }

  if (keep_dac_busy)
    pthread_create(&alsa_buffer_monitor_thread, NULL, &alsa_buffer_monitor_thread_code, NULL);

  return response;
}

static void deinit(void) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  // debug(2,"audio_alsa deinit called.");
  stop();
  if (keep_dac_busy) {
    debug(1, "Cancel buffer monitor thread.");
    pthread_cancel(alsa_buffer_monitor_thread);
    debug(1, "Join buffer monitor thread.");
    pthread_join(alsa_buffer_monitor_thread, NULL);
  }
  pthread_setcancelstate(oldState, NULL);
}

static void start(int i_sample_rate, int i_sample_format) {
  // debug(2,"audio_alsa start called.");
  if (i_sample_rate == 0)
    desired_sample_rate = 44100; // default
  else
    desired_sample_rate = i_sample_rate; // must be a variable

  if (i_sample_format == 0)
    sample_format = SPS_FORMAT_S16; // default
  else
    sample_format = i_sample_format;

  frame_index = 0;
  measurement_data_is_valid = 0;

  stall_monitor_start_time = 0;
  stall_monitor_frame_count = 0;

  stall_monitor_error_threshold =
      (uint64_t)1000000 * config.alsa_maximum_stall_time; // stall time max to microseconds;
  stall_monitor_error_threshold = (stall_monitor_error_threshold << 32) / 1000000; // now in fp form
}

// assuming pthread cancellation is disabled
int my_snd_pcm_delay(snd_pcm_t *pcm, snd_pcm_sframes_t *delayp) {
  int ret;
  snd_pcm_status_t *alsa_snd_pcm_status;
  snd_pcm_status_alloca(&alsa_snd_pcm_status);

  struct timespec tn;                // time now
  snd_htimestamp_t update_timestamp; // actually a struct timespec

  ret = snd_pcm_status(pcm, alsa_snd_pcm_status);
  if (ret) {
    *delayp = 0;
    return ret;
  }

  snd_pcm_state_t state = snd_pcm_status_get_state(alsa_snd_pcm_status);
  if (state != SND_PCM_STATE_RUNNING) {
    *delayp = 0;
    return -EIO; // might be a better code than this...
  }

  clock_gettime(CLOCK_MONOTONIC, &tn);
  snd_pcm_status_get_htstamp(alsa_snd_pcm_status, &update_timestamp);

  uint64_t t1 = tn.tv_sec * (uint64_t)1000000000 + tn.tv_nsec;
  uint64_t t2 = update_timestamp.tv_sec * (uint64_t)1000000000 + update_timestamp.tv_nsec;
  uint64_t delta = t1 - t2;

  uint64_t frames_played_since_last_interrupt =
      ((uint64_t)desired_sample_rate * delta) / 1000000000;
  snd_pcm_sframes_t frames_played_since_last_interrupt_sized = frames_played_since_last_interrupt;

  *delayp =
      snd_pcm_status_get_delay(alsa_snd_pcm_status) - frames_played_since_last_interrupt_sized;
  return 0;
}

int delay(long *the_delay) {
  *the_delay = 0;
  // snd_pcm_sframes_t is a signed long -- hence the return of a "long"
  int reply = 0;
  // debug(3,"audio_alsa delay called.");
  if (alsa_handle == NULL) {
    reply = -ENODEV;
  } else {
    int oldState;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
    pthread_cleanup_debug_mutex_lock(&alsa_mutex, 10000, 0);
    int derr;
    snd_pcm_state_t dac_state = snd_pcm_state(alsa_handle);
    if (dac_state == SND_PCM_STATE_RUNNING) {

      reply = snd_pcm_delay(alsa_handle, the_delay);

      if (reply != 0) {
        debug(1, "Error %d in delay(): \"%s\". Delay reported is %d frames.", reply,
              snd_strerror(reply), *the_delay);
        derr = snd_pcm_recover(alsa_handle, reply, 1);
        if (derr < 0)
          warn("Error %d -- could not clear an error after attempting delay():  \"%s\".", derr,
               snd_strerror(derr));

        frame_index = 0;
        measurement_data_is_valid = 0;
      } else {
        if (*the_delay == 0) {
          // there's nothing in the pipeline, so we can't measure frame rate.
          frame_index = 0; // we'll be starting over...
          measurement_data_is_valid = 0;
        }
      }
    } else {
      reply = -EIO;    // shomething is wrong
      frame_index = 0; // we'll be starting over...
      measurement_data_is_valid = 0;

      if (dac_state == SND_PCM_STATE_PREPARED) {
        debug(2, "delay not available -- state is SND_PCM_STATE_PREPARED");
      } else {
        if (dac_state == SND_PCM_STATE_XRUN) {
          debug(2, "delay not available -- state is SND_PCM_STATE_XRUN");
        } else {

          debug(2, "Error -- ALSA delay(): bad state: %d.", dac_state);
        }
        if ((derr = snd_pcm_prepare(alsa_handle))) {
          debug(1, "Error preparing after delay error: \"%s\".", snd_strerror(derr));
          derr = snd_pcm_recover(alsa_handle, derr, 1);
          if (derr < 0)
            warn("Error %d -- could not clear an error after attempting to recover following a "
                 "delay():  \"%s\".",
                 derr, snd_strerror(derr));
        }
      }
    }
    debug_mutex_unlock(&alsa_mutex, 0);
    pthread_cleanup_pop(0);
    // here, occasionally pretend there's a problem with pcm_get_delay()
    // if ((random() % 100000) < 3) // keep it pretty rare
    //	reply = -EIO; // pretend something bad has happened
    pthread_setcancelstate(oldState, NULL);
  }
  if ((reply == 0) && (*the_delay != 0)) {
    uint64_t stall_monitor_time_now = get_absolute_time_in_fp();
    if ((stall_monitor_start_time != 0) && (stall_monitor_frame_count == *the_delay)) {
      // hasn't outputted anything since the last call to delay()
      uint64_t time_stalled = stall_monitor_time_now - stall_monitor_start_time;
      if (time_stalled > stall_monitor_error_threshold) {
        reply = sps_extra_errno_output_stalled;
      }
    } else {
      // is outputting stuff, so restart the monitoring here
      stall_monitor_start_time = stall_monitor_time_now;
      stall_monitor_frame_count = *the_delay;
    }
  } else {
    // if there is an error or the delay is zero (from which it is assumed there is no output)
    stall_monitor_start_time = 0;  // zero if not initialised / not started / zeroed by flush
    stall_monitor_frame_count = 0; // set to delay at start of time, incremented by any writes
  }
  return reply;
}

int get_rate_information(uint64_t *elapsed_time, uint64_t *frames_played) {
  int response = 0; // zero means okay
  if (measurement_data_is_valid) {
    *elapsed_time = measurement_time - measurement_start_time;
    *frames_played = frames_played_at_measurement_time - frames_played_at_measurement_start_time;
  } else {
    *elapsed_time = 0;
    *frames_played = 0;
    response = -1;
  }
  return response;
}

static int play(void *buf, int samples) {
  // debug(3,"audio_alsa play called.");
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  int ret = 0;
  if (alsa_handle == NULL) {

    pthread_cleanup_debug_mutex_lock(&alsa_mutex, 10000, 1);
    ret = actual_open_alsa_device();
    if (ret == 0) {
      if (audio_alsa.volume)
        do_volume(set_volume);
      if (audio_alsa.mute)
        do_mute(0);
    }

    debug_mutex_unlock(&alsa_mutex, 3);
    pthread_cleanup_pop(0); // release the mutex
  }
  if (ret == 0) {
    pthread_cleanup_debug_mutex_lock(&alsa_mutex, 10000, 0);
    //    snd_pcm_sframes_t current_delay = 0;
    int err, err2;
    if ((snd_pcm_state(alsa_handle) == SND_PCM_STATE_XRUN) ||
        (snd_pcm_state(alsa_handle) == SND_PCM_STATE_OPEN) ||
        (snd_pcm_state(alsa_handle) == SND_PCM_STATE_SETUP)) {
      if ((err = snd_pcm_prepare(alsa_handle))) {
        debug(1, "Error preparing after underrun: \"%s\".", snd_strerror(err));
        err = snd_pcm_recover(alsa_handle, err, 1);
        if (err < 0)
          warn("Error %d -- could not clear an error after detecting underrun in play():  \"%s\".",
               err, snd_strerror(err));
      }
      frame_index = 0; // we'll be starting over
      measurement_data_is_valid = 0;
    } else if ((snd_pcm_state(alsa_handle) == SND_PCM_STATE_PREPARED) ||
               (snd_pcm_state(alsa_handle) == SND_PCM_STATE_RUNNING)) {
      if (buf == NULL)
        debug(1, "NULL buffer passed to pcm_writei -- skipping it");
      if (samples == 0)
        debug(1, "empty buffer being passed to pcm_writei -- skipping it");
      if ((samples != 0) && (buf != NULL)) {
        // debug(3, "write %d frames.", samples);

        err = alsa_pcm_write(alsa_handle, buf, samples);

        stall_monitor_frame_count += samples;

        if (err < 0) {
          frame_index = 0;
          measurement_data_is_valid = 0;
          debug(1, "Error %d writing %d samples in play(): \"%s\".", err, samples,
                snd_strerror(err));
          err = snd_pcm_recover(alsa_handle, err, 1);
          if (err < 0)
            warn("Error %d -- could not clear an error after attempting to write %d samples in "
                 "play():  \"%s\".",
                 err, samples, snd_strerror(err));
        }

        if (frame_index == 0) {
          frames_sent_for_playing = samples;
        } else {
          frames_sent_for_playing += samples;
        }
        const uint64_t start_measurement_from_this_frame =
            (2 * 44100) / 352; // two seconds of frames…
        frame_index++;
        if ((frame_index == start_measurement_from_this_frame) || (frame_index % 32 == 0)) {
          long fl = 0;
          err2 = snd_pcm_delay(alsa_handle, &fl);
          if (err2 != 0) {
            debug(1, "Error %d in delay in play(): \"%s\". Delay reported is %d frames.", err2,
                  snd_strerror(err2), fl);
            err2 = snd_pcm_recover(alsa_handle, err2, 1);
            if (err2 < 0)
              warn("Error %d -- could not clear an error after checking delay in play():  \"%s\".",
                   err2, snd_strerror(err2));
            frame_index = 0;
            measurement_data_is_valid = 0;
          } else {
            if (fl == 0) {
              // there's nothing in the pipeline, so we can't measure frame rate.
              frame_index = 0; // we'll be starting over...
              measurement_data_is_valid = 0;
            }
          }

          measurement_time = get_absolute_time_in_fp();
          frames_played_at_measurement_time = frames_sent_for_playing - fl;
          if (frame_index == start_measurement_from_this_frame) {
            // debug(1, "Start frame counting");
            frames_played_at_measurement_start_time = frames_played_at_measurement_time;
            measurement_start_time = measurement_time;
            measurement_data_is_valid = 1;
          }
        }
      }
    } else {
      debug(1, "Error -- ALSA device in incorrect state (%d) for play.",
            snd_pcm_state(alsa_handle));
      if ((err = snd_pcm_prepare(alsa_handle))) {
        debug(1, "Error preparing after play error: \"%s\".", snd_strerror(err));
        err2 = snd_pcm_recover(alsa_handle, err, 1);
        if (err2 < 0)
          warn("Error %d -- could not clear an error after reporting ALSA device in incorrect "
               "state for play:  \"%s\".",
               err2, snd_strerror(err2));
      }
      frame_index = 0;
      measurement_data_is_valid = 0;
    }
    debug_mutex_unlock(&alsa_mutex, 0);
    pthread_cleanup_pop(0); // release the mutex
  }
  pthread_setcancelstate(oldState, NULL);
  return ret;
}

static void flush(void) {
  // debug(2,"audio_alsa flush called.");
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  pthread_cleanup_debug_mutex_lock(&alsa_mutex, 10000, 1);
  int derr;
  do_mute(1);

  if (alsa_handle) {
    stall_monitor_start_time = 0;
    if (keep_dac_busy == 0) {
      if ((derr = snd_pcm_drop(alsa_handle)))
        debug(1, "Error %d (\"%s\") dropping output device.", derr, snd_strerror(derr));
      if ((derr = snd_pcm_hw_free(alsa_handle)))
        debug(1, "Error %d (\"%s\") freeing the output device hardware.", derr, snd_strerror(derr));

      // flush also closes the device
      if ((derr = snd_pcm_close(alsa_handle)))
        debug(1, "Error %d (\"%s\") closing the output device.", derr, snd_strerror(derr));
      alsa_handle = NULL;
    }
    frame_index = 0;
    measurement_data_is_valid = 0;
  }
  debug_mutex_unlock(&alsa_mutex, 3);
  pthread_cleanup_pop(0); // release the mutex
  pthread_setcancelstate(oldState, NULL);
}

static void stop(void) {
  // debug(2,"audio_alsa stop called.");
  // when we want to stop, we want the alsa device
  // to be closed immediately -- we may even be killing the thread, so we
  // don't wish to wait
  // so we should flush first
  flush(); // flush will also close the device
           // close_alsa_device();
}

static void parameters(audio_parameters *info) {
  info->minimum_volume_dB = alsa_mix_mindb;
  info->maximum_volume_dB = alsa_mix_maxdb;
}

void do_volume(double vol) { // caller is assumed to have the alsa_mutex when using this function
  debug(3, "Setting volume db to %f.", vol);
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  set_volume = vol;
  if (volume_set_request && (open_mixer() == 1)) {
    if (has_softvol) {
      if (ctl && elem_id) {
        snd_ctl_elem_value_t *value;
        long raw;

        if (snd_ctl_convert_from_dB(ctl, elem_id, vol, &raw, 0) < 0)
          debug(1, "Failed converting dB gain to raw volume value for the "
                   "software volume control.");

        snd_ctl_elem_value_alloca(&value);
        snd_ctl_elem_value_set_id(value, elem_id);
        snd_ctl_elem_value_set_integer(value, 0, raw);
        snd_ctl_elem_value_set_integer(value, 1, raw);
        if (snd_ctl_elem_write(ctl, value) < 0)
          debug(1, "Failed to set playback dB volume for the software volume "
                   "control.");
      }
    } else {
      if (volume_based_mute_is_active == 0) {
        // debug(1,"Set alsa volume.");
        do_snd_mixer_selem_set_playback_dB_all(alsa_mix_elem, vol);
      } else {
        debug(2, "Not setting volume because volume-based mute is active");
      }
    }
    volume_set_request = 0; // any external request that has been made is now satisfied
    close_mixer();
  }
  pthread_setcancelstate(oldState, NULL);
}

void volume(double vol) {
  pthread_cleanup_debug_mutex_lock(&alsa_mutex, 1000, 1);
  volume_set_request = 1; // an external request has been made to set the volume
  do_volume(vol);
  debug_mutex_unlock(&alsa_mutex, 3);
  pthread_cleanup_pop(0); // release the mutex
}

/*
static void linear_volume(double vol) {
  debug(2, "Setting linear volume to %f.", vol);
  set_volume = vol;
  if (hardware_mixer && alsa_mix_handle) {
    double linear_volume = pow(10, vol);
    // debug(1,"Linear volume is %f.",linear_volume);
    long int_vol = alsa_mix_minv + (alsa_mix_maxv - alsa_mix_minv) * linear_volume;
    // debug(1,"Setting volume to %ld, for volume input of %f.",int_vol,vol);
    if (alsa_mix_handle) {
      if (snd_mixer_selem_set_playback_volume_all(alsa_mix_elem, int_vol) != 0)
        die("Failed to set playback volume");

    }
  }
}
*/

static void mute(int mute_state_requested) {
  // debug(1,"External Mute Request: %d",mute_state_requested);
  pthread_cleanup_debug_mutex_lock(&alsa_mutex, 10000, 1);
  mute_request_pending = 1;
  overriding_mute_state_requested = mute_state_requested;
  do_mute(mute_state_requested);
  debug_mutex_unlock(&alsa_mutex, 3);
  pthread_cleanup_pop(0); // release the mutex
}

void do_mute(int mute_state_requested) {
  debug(3, "Setting mute to %d.", mute_state_requested);
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable

  // if a mute is requested now, then
  // 	if an external mute request is in place, leave everything muted
  //  otherwise, if an external mute request is pending, action it
  //  otherwise, action the do_mute request

  int local_mute_state_requested =
      overriding_mute_state_requested; // go with whatever was asked by the external "mute" call

  // The mute state requested will be actioned unless mute_request_pending is set
  // If it is set, then that the pending request will be actioned.
  // If the hardware isn't there, or we are not allowed to use it, nothing will be done
  // The caller must have the alsa mutex

  if (config.alsa_use_hardware_mute == 1) {
    if (mute_request_pending == 0)
      local_mute_state_requested = mute_state_requested;
    if (open_mixer() == 1) {
      if (local_mute_state_requested) {
        // debug(1,"Playback Switch mute actually done");
        if (snd_mixer_selem_has_playback_switch(alsa_mix_elem))
          snd_mixer_selem_set_playback_switch_all(alsa_mix_elem, 0);
        else {
          // debug(1,"Activating volume-based mute.");
          volume_based_mute_is_active = 1;
          do_snd_mixer_selem_set_playback_dB_all(alsa_mix_elem, alsa_mix_mute);
        }
      } else if (overriding_mute_state_requested == 0) {
        // debug(1,"Playback Switch unmute actually done");
        if (snd_mixer_selem_has_playback_switch(alsa_mix_elem))
          snd_mixer_selem_set_playback_switch_all(alsa_mix_elem, 1);
        else {
          // debug(1,"Deactivating volume-based mute.");
          volume_based_mute_is_active = 0;
          do_snd_mixer_selem_set_playback_dB_all(alsa_mix_elem, set_volume);
        }
      }
      close_mixer();
    }
  }
  mute_request_pending = 0;
  pthread_setcancelstate(oldState, NULL);
}

void alsa_buffer_monitor_thread_cleanup_function(__attribute__((unused)) void *arg) {
  debug(1, "alsa: alsa_buffer_monitor_thread_cleanup_function called.");
}

void *alsa_buffer_monitor_thread_code(void *arg) {
  // debug(1,"alsa: alsa_buffer_monitor_thread_code called.");
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  int ret = 0;
  if (alsa_handle == NULL) {

    pthread_cleanup_debug_mutex_lock(&alsa_mutex, 10000, 1);
    ret = actual_open_alsa_device();
    if (ret == 0) {
      if (audio_alsa.volume)
        do_volume(set_volume);
      if (audio_alsa.mute)
        do_mute(0);
    }

    debug_mutex_unlock(&alsa_mutex, 3);
    pthread_cleanup_pop(0); // release the mutex
  }
  pthread_cleanup_push(alsa_buffer_monitor_thread_cleanup_function, arg);
  pthread_setcancelstate(oldState, NULL);

  // The thinking is, if the device has a hardware mixer, then
  // (if no source transformation is happening), Shairport Sync
  // will deliver fill-in silences and the audio material without adding dither.
  // So don't insert dither into the silences sent to keep the DAC busy.

  int use_dither = 0;
  if (hardware_mixer == 0)
    use_dither = 1;

  debug(1, "alsa: dither will %sbe added to inter-session silence.", use_dither ? "" : "not ");

  int sleep_time_ms = 80;
  int frames_of_silence = desired_sample_rate * sleep_time_ms / 1000;
  size_t size_of_silence_buffer = frames_of_silence * frame_size;
  // debug(1,"Silence buffer length: %u bytes.",size_of_silence_buffer);
  void *silence = malloc(size_of_silence_buffer);
  if (silence == NULL) {
    debug(1, "alsa: failed to allocate memory for a silent frame  buffer, thus "
             "disable_standby_mode is \"off\".");
  } else {
    long buffer_size;
    int reply;
    while (1) {
      if (buffer_size < (((int)desired_sample_rate * sleep_time_ms) / (1000))) {
        dither_random_number_store = generate_zero_frames(
            silence, frames_of_silence, config.output_format, use_dither, // i.e. with dither
            dither_random_number_store);
        play(silence, frames_of_silence);
      }
      usleep((sleep_time_ms * 1000) / 2);
      reply = delay(&buffer_size);
      if (reply != 0)
        debug(1, "error %d during buffer monitoring of delay", reply);
      pthread_testcancel();
    }
  }
  pthread_cleanup_pop(1);
  pthread_exit(NULL);
}
