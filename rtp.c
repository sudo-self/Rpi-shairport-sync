/*
 * Apple RTP protocol handler. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * Copyright (c) Mike Brady 2014 -- 2019
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

#include "rtp.h"
#include "common.h"
#include "player.h"
#include "rtsp.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <math.h>
#include <memory.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#ifdef CONFIG_AIRPLAY_2
#include "ptp-utilities.h"
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <sodium.h>
#endif

struct Nvll {
  char *name;
  double value;
  struct Nvll *next;
};

typedef struct Nvll nvll;

uint64_t local_to_remote_time_jitter;
uint64_t local_to_remote_time_jitter_count;

typedef struct {
  int closed;
  int error_code;
  int sock_fd;
  char *buffer;
  char *toq;
  char *eoq;
  size_t buffer_max_size;
  size_t buffer_occupancy;
  pthread_mutex_t mutex;
  pthread_cond_t not_empty_cv;
  pthread_cond_t not_full_cv;
} buffered_tcp_desc;

void rtp_initialise(rtsp_conn_info *conn) {
  conn->rtp_time_of_last_resend_request_error_ns = 0;
  conn->rtp_running = 0;
  // initialise the timer mutex
  int rc = pthread_mutex_init(&conn->reference_time_mutex, NULL);
  if (rc)
    debug(1, "Error initialising reference_time_mutex.");
}

void rtp_terminate(rtsp_conn_info *conn) {
  conn->anchor_rtptime = 0;
  // destroy the timer mutex
  int rc = pthread_mutex_destroy(&conn->reference_time_mutex);
  if (rc)
    debug(1, "Error destroying reference_time_mutex variable.");
}

uint64_t local_to_remote_time_difference_now(rtsp_conn_info *conn) {
  // this is an attempt to compensate for clock drift since the last time ping that was used
  // so, if we have a non-zero clock drift, we will calculate the drift there would
  // be from the time of the last time ping
  uint64_t time_since_last_local_to_remote_time_difference_measurement =
      get_absolute_time_in_ns() - conn->local_to_remote_time_difference_measurement_time;

  uint64_t result = conn->local_to_remote_time_difference;
  if (conn->local_to_remote_time_gradient >= 1.0) {
    result = conn->local_to_remote_time_difference +
             (uint64_t)((conn->local_to_remote_time_gradient - 1.0) *
                        time_since_last_local_to_remote_time_difference_measurement);
  } else {
    result = conn->local_to_remote_time_difference -
             (uint64_t)((1.0 - conn->local_to_remote_time_gradient) *
                        time_since_last_local_to_remote_time_difference_measurement);
  }
  return result;
}

void rtp_audio_receiver_cleanup_handler(__attribute__((unused)) void *arg) {
  debug(3, "Audio Receiver Cleanup Done.");
}

void *rtp_audio_receiver(void *arg) {
  debug(3, "rtp_audio_receiver start");
  pthread_cleanup_push(rtp_audio_receiver_cleanup_handler, arg);
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;

  int32_t last_seqno = -1;
  uint8_t packet[2048], *pktp;

  uint64_t time_of_previous_packet_ns = 0;
  float longest_packet_time_interval_us = 0.0;

  // mean and variance calculations from "online_variance" algorithm at
  // https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Online_algorithm

  int32_t stat_n = 0;
  float stat_mean = 0.0;
  float stat_M2 = 0.0;

  int frame_count = 0;
  ssize_t nread;
  while (1) {
    nread = recv(conn->audio_socket, packet, sizeof(packet), 0);

    frame_count++;

    uint64_t local_time_now_ns = get_absolute_time_in_ns();
    if (time_of_previous_packet_ns) {
      float time_interval_us = (local_time_now_ns - time_of_previous_packet_ns) * 0.001;
      time_of_previous_packet_ns = local_time_now_ns;
      if (time_interval_us > longest_packet_time_interval_us)
        longest_packet_time_interval_us = time_interval_us;
      stat_n += 1;
      float stat_delta = time_interval_us - stat_mean;
      stat_mean += stat_delta / stat_n;
      stat_M2 += stat_delta * (time_interval_us - stat_mean);
      if (stat_n % 2500 == 0) {
        debug(2,
              "Packet reception interval stats: mean, standard deviation and max for the last "
              "2,500 packets in microseconds: %10.1f, %10.1f, %10.1f.",
              stat_mean, sqrtf(stat_M2 / (stat_n - 1)), longest_packet_time_interval_us);
        stat_n = 0;
        stat_mean = 0.0;
        stat_M2 = 0.0;
        time_of_previous_packet_ns = 0;
        longest_packet_time_interval_us = 0.0;
      }
    } else {
      time_of_previous_packet_ns = local_time_now_ns;
    }

    if (nread >= 0) {
      ssize_t plen = nread;
      uint8_t type = packet[1] & ~0x80;
      if (type == 0x60 || type == 0x56) { // audio data / resend
        pktp = packet;
        if (type == 0x56) {
          pktp += 4;
          plen -= 4;
        }
        seq_t seqno = ntohs(*(uint16_t *)(pktp + 2));
        // increment last_seqno and see if it's the same as the incoming seqno

        if (type == 0x60) { // regular audio data

          /*
          char obf[4096];
          char *obfp = obf;
          int obfc;
          for (obfc=0;obfc<plen;obfc++) {
            snprintf(obfp, 3, "%02X", pktp[obfc]);
            obfp+=2;
          };
          *obfp=0;
          debug(1,"Audio Packet Received: \"%s\"",obf);
          */

          if (last_seqno == -1)
            last_seqno = seqno;
          else {
            last_seqno = (last_seqno + 1) & 0xffff;
            // if (seqno != last_seqno)
            //  debug(3, "RTP: Packets out of sequence: expected: %d, got %d.", last_seqno, seqno);
            last_seqno = seqno; // reset warning...
          }
        } else {
          debug(3, "Audio Receiver -- Retransmitted Audio Data Packet %u received.", seqno);
        }

        uint32_t actual_timestamp = ntohl(*(uint32_t *)(pktp + 4));

        // uint32_t ssid = ntohl(*(uint32_t *)(pktp + 8));
        // debug(1, "Audio packet SSID: %08X,%u", ssid,ssid);

        // if (packet[1]&0x10)
        //	debug(1,"Audio packet Extension bit set.");

        pktp += 12;
        plen -= 12;

        // check if packet contains enough content to be reasonable
        if (plen >= 16) {
          if ((config.diagnostic_drop_packet_fraction == 0.0) ||
              (drand48() > config.diagnostic_drop_packet_fraction))
            player_put_packet(1, seqno, actual_timestamp, pktp, plen,
                              conn); // the '1' means is original format
          else
            debug(3, "Dropping audio packet %u to simulate a bad connection.", seqno);
          continue;
        }
        if (type == 0x56 && seqno == 0) {
          debug(2, "resend-related request packet received, ignoring.");
          continue;
        }
        debug(1, "Audio receiver -- Unknown RTP packet of type 0x%02X length %d seqno %d", type,
              nread, seqno);
      }
      warn("Audio receiver -- Unknown RTP packet of type 0x%02X length %d.", type, nread);
    } else {
      char em[1024];
      strerror_r(errno, em, sizeof(em));
      debug(1, "Error %d receiving an audio packet: \"%s\".", errno, em);
    }
  }

  /*
  debug(3, "Audio receiver -- Server RTP thread interrupted. terminating.");
  close(conn->audio_socket);
  */

  debug(1, "Audio receiver thread \"normal\" exit -- this can't happen. Hah!");
  pthread_cleanup_pop(0); // don't execute anything here.
  debug(2, "Audio receiver thread exit.");
  pthread_exit(NULL);
}

void rtp_control_handler_cleanup_handler(__attribute__((unused)) void *arg) {
  debug(2, "Control Receiver Cleanup Done.");
}

void *rtp_control_receiver(void *arg) {
  debug(2, "rtp_control_receiver start");
  pthread_cleanup_push(rtp_control_handler_cleanup_handler, arg);
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;

  conn->anchor_rtptime = 0; // nothing valid received yet
  uint8_t packet[2048], *pktp;
  // struct timespec tn;
  uint64_t remote_time_of_sync;
  uint32_t sync_rtp_timestamp;
  ssize_t nread;
  while (1) {
    nread = recv(conn->control_socket, packet, sizeof(packet), 0);

    if (nread >= 0) {

      if ((config.diagnostic_drop_packet_fraction == 0.0) ||
          (drand48() > config.diagnostic_drop_packet_fraction)) {

        ssize_t plen = nread;
        if (packet[1] == 0xd4) {                       // sync data
                                                       /*
                                                            // the following stanza is for debugging only -- normally commented out.
                                                            {
                                                              char obf[4096];
                                                              char *obfp = obf;
                                                              int obfc;
                                                              for (obfc = 0; obfc < plen; obfc++) {
                                                                snprintf(obfp, 3, "%02X", packet[obfc]);
                                                                obfp += 2;
                                                              };
                                                              *obfp = 0;
                                             
                                             
                                                              // get raw timestamp information
                                                              // I think that a good way to understand these timestamps is that
                                                              // (1) the rtlt below is the timestamp of the frame that should be playing at the
                                                              // client-time specified in the packet if there was no delay
                                                              // and (2) that the rt below is the timestamp of the frame that should be playing
                                                              // at the client-time specified in the packet on this device taking account of
                                                              // the delay
                                                              // Thus, (3) the latency can be calculated by subtracting the second from the
                                                              // first.
                                                              // There must be more to it -- there something missing.
                                             
                                                              // In addition, it seems that if the value of the short represented by the second
                                                              // pair of bytes in the packet is 7
                                                              // then an extra time lag is expected to be added, presumably by
                                                              // the AirPort Express.
                                             
                                                              // Best guess is that this delay is 11,025 frames.
                                             
                                                              uint32_t rtlt = nctohl(&packet[4]); // raw timestamp less latency
                                                              uint32_t rt = nctohl(&packet[16]);  // raw timestamp
                                             
                                                              uint32_t fl = nctohs(&packet[2]); //
                                             
                                                              debug(1,"Sync Packet of %d bytes received: \"%s\", flags: %d, timestamps %u and %u,
                                                          giving a latency of %d frames.",plen,obf,fl,rt,rtlt,rt-rtlt);
                                                              //debug(1,"Monotonic timestamps are: %" PRId64 " and %" PRId64 "
                                                          respectively.",monotonic_timestamp(rt, conn),monotonic_timestamp(rtlt, conn));
                                                            }
                                                       */
          if (conn->local_to_remote_time_difference) { // need a time packet to be interchanged
                                                       // first...
            uint64_t ps, pn;

            ps = nctohl(&packet[8]);
            ps = ps * 1000000000; // this many nanoseconds from the whole seconds
            pn = nctohl(&packet[12]);
            pn = pn * 1000000000;
            pn = pn >> 32; // this many nanoseconds from the fractional part
            remote_time_of_sync = ps + pn;

            // debug(1,"Remote Sync Time: " PRIu64 "",remote_time_of_sync);

            sync_rtp_timestamp = nctohl(&packet[16]);
            uint32_t rtp_timestamp_less_latency = nctohl(&packet[4]);

            // debug(1,"Sync timestamp is %u.",ntohl(*((uint32_t *)&packet[16])));

            if (config.userSuppliedLatency) {
              if (config.userSuppliedLatency != conn->latency) {
                debug(1, "Using the user-supplied latency: %" PRIu32 ".",
                      config.userSuppliedLatency);
              }
              conn->latency = config.userSuppliedLatency;
            } else {

              // It seems that the second pair of bytes in the packet indicate whether a fixed
              // delay of 11,025 frames should be added -- iTunes set this field to 7 and
              // AirPlay sets it to 4.

              // However, on older versions of AirPlay, the 11,025 frames seem to be necessary too

              // The value of 11,025 (0.25 seconds) is a guess based on the "Audio-Latency"
              // parameter
              // returned by an AE.

              // Sigh, it would be nice to have a published protocol...

              uint16_t flags = nctohs(&packet[2]);
              uint32_t la = sync_rtp_timestamp - rtp_timestamp_less_latency; // note, this might
                                                                             // loop around in
                                                                             // modulo. Not sure if
                                                                             // you'll get an error!
              // debug(1, "Latency from the sync packet is %" PRIu32 " frames.", la);

              if ((flags == 7) || ((conn->AirPlayVersion > 0) && (conn->AirPlayVersion <= 353)) ||
                  ((conn->AirPlayVersion > 0) && (conn->AirPlayVersion >= 371))) {
                la += config.fixedLatencyOffset;
                // debug(1, "Latency offset by %" PRIu32" frames due to the source flags and version
                // giving a latency of %" PRIu32 " frames.", config.fixedLatencyOffset, la);
              }
              if ((conn->maximum_latency) && (conn->maximum_latency < la))
                la = conn->maximum_latency;
              if ((conn->minimum_latency) && (conn->minimum_latency > la))
                la = conn->minimum_latency;

              const uint32_t max_frames = ((3 * BUFFER_FRAMES * 352) / 4) - 11025;

              if (la > max_frames) {
                warn("An out-of-range latency request of %" PRIu32
                     " frames was ignored. Must be %" PRIu32
                     " frames or less (44,100 frames per second). "
                     "Latency remains at %" PRIu32 " frames.",
                     la, max_frames, conn->latency);
              } else {

                // here we have the latency but it does not yet account for the
                // audio_backend_latency_offset
                int32_t latency_offset =
                    (int32_t)(config.audio_backend_latency_offset * conn->input_rate);

                // debug(1,"latency offset is %" PRId32 ", input rate is %u", latency_offset,
                // conn->input_rate);
                int32_t adjusted_latency = latency_offset + (int32_t)la;
                if ((adjusted_latency < 0) ||
                    (adjusted_latency >
                     (int32_t)(conn->max_frames_per_packet *
                               (BUFFER_FRAMES - config.minimum_free_buffer_headroom))))
                  warn("audio_backend_latency_offset out of range -- ignored.");
                else
                  la = adjusted_latency;

                if (la != conn->latency) {
                  conn->latency = la;
                  debug(2,
                        "New latency: %" PRIu32 ", sync latency: %" PRIu32
                        ", minimum latency: %" PRIu32 ", maximum "
                        "latency: %" PRIu32 ", fixed offset: %" PRIu32
                        ", audio_backend_latency_offset: %f.",
                        conn->latency, sync_rtp_timestamp - rtp_timestamp_less_latency,
                        conn->minimum_latency, conn->maximum_latency, config.fixedLatencyOffset,
                        config.audio_backend_latency_offset);
                }
              }
            }

            // here, we apply the latency to the sync_rtp_timestamp

            sync_rtp_timestamp = sync_rtp_timestamp - conn->latency;

            debug_mutex_lock(&conn->reference_time_mutex, 1000, 0);

            if (conn->initial_reference_time == 0) {
              if (conn->packet_count_since_flush > 0) {
                conn->initial_reference_time = remote_time_of_sync;
                conn->initial_reference_timestamp = sync_rtp_timestamp;
              }
            } else {
              uint64_t remote_frame_time_interval =
                  conn->anchor_time -
                  conn->initial_reference_time; // here, this should never be zero
              if (remote_frame_time_interval) {
                conn->remote_frame_rate =
                    (1.0E9 * (conn->anchor_rtptime - conn->initial_reference_timestamp)) /
                    remote_frame_time_interval;
              } else {
                conn->remote_frame_rate = 0.0; // use as a flag.
              }
            }

            // this is for debugging
            uint64_t old_remote_reference_time = conn->anchor_time;
            uint32_t old_reference_timestamp = conn->anchor_rtptime;
            // int64_t old_latency_delayed_timestamp = conn->latency_delayed_timestamp;

            conn->anchor_time = remote_time_of_sync;
            // conn->reference_timestamp_time =
            //    remote_time_of_sync - local_to_remote_time_difference_now(conn);
            conn->anchor_rtptime = sync_rtp_timestamp;
            conn->latency_delayed_timestamp = rtp_timestamp_less_latency;
            debug_mutex_unlock(&conn->reference_time_mutex, 0);

            conn->reference_to_previous_time_difference =
                remote_time_of_sync - old_remote_reference_time;
            if (old_reference_timestamp == 0)
              conn->reference_to_previous_frame_difference = 0;
            else
              conn->reference_to_previous_frame_difference =
                  sync_rtp_timestamp - old_reference_timestamp;
          } else {
            debug(2, "Sync packet received before we got a timing packet back.");
          }
        } else if (packet[1] == 0xd6) { // resent audio data in the control path -- whaale only?
          pktp = packet + 4;
          plen -= 4;
          seq_t seqno = ntohs(*(uint16_t *)(pktp + 2));
          debug(3, "Control Receiver -- Retransmitted Audio Data Packet %u received.", seqno);

          uint32_t actual_timestamp = ntohl(*(uint32_t *)(pktp + 4));

          pktp += 12;
          plen -= 12;

          // check if packet contains enough content to be reasonable
          if (plen >= 16) {
            player_put_packet(1, seqno, actual_timestamp, pktp, plen,
                              conn); // the '1' means is original format
            continue;
          } else {
            debug(3, "Too-short retransmitted audio packet received in control port, ignored.");
          }
        } else
          debug(1, "Control Receiver -- Unknown RTP packet of type 0x%02X length %d, ignored.",
                packet[1], nread);
      } else {
        debug(3, "Control Receiver -- dropping a packet to simulate a bad network.");
      }
    } else {

      char em[1024];
      strerror_r(errno, em, sizeof(em));
      debug(1, "Control Receiver -- error %d receiving a packet: \"%s\".", errno, em);
    }
  }
  debug(1, "Control RTP thread \"normal\" exit -- this can't happen. Hah!");
  pthread_cleanup_pop(0); // don't execute anything here.
  debug(2, "Control RTP thread exit.");
  pthread_exit(NULL);
}

void rtp_timing_sender_cleanup_handler(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  debug(3, "Connection %d: Timing Sender Cleanup.", conn->connection_number);
}

void *rtp_timing_sender(void *arg) {
  debug(2, "rtp_timing_sender start");
  pthread_cleanup_push(rtp_timing_sender_cleanup_handler, arg);
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  struct timing_request {
    char leader;
    char type;
    uint16_t seqno;
    uint32_t filler;
    uint64_t origin, receive, transmit;
  };

  uint64_t request_number = 0;

  struct timing_request req; // *not* a standard RTCP NACK

  req.leader = 0x80;
  req.type = 0xd2; // Timing request
  req.filler = 0;
  req.seqno = htons(7);

  conn->time_ping_count = 0;
  while (1) {
    // debug(1,"Send a timing request");

    if (!conn->rtp_running)
      debug(1, "rtp_timing_sender called without active stream in RTSP conversation thread %d!",
            conn->connection_number);

    // debug(1, "Requesting ntp timestamp exchange.");

    req.filler = 0;
    req.origin = req.receive = req.transmit = 0;

    conn->departure_time = get_absolute_time_in_ns();
    socklen_t msgsize = sizeof(struct sockaddr_in);
#ifdef AF_INET6
    if (conn->rtp_client_timing_socket.SAFAMILY == AF_INET6) {
      msgsize = sizeof(struct sockaddr_in6);
    }
#endif
    if ((config.diagnostic_drop_packet_fraction == 0.0) ||
        (drand48() > config.diagnostic_drop_packet_fraction)) {
      if (sendto(conn->timing_socket, &req, sizeof(req), 0,
                 (struct sockaddr *)&conn->rtp_client_timing_socket, msgsize) == -1) {
        char em[1024];
        strerror_r(errno, em, sizeof(em));
        debug(1, "Error %d using send-to to the timing socket: \"%s\".", errno, em);
      }
    } else {
      debug(3, "Timing Sender Thread -- dropping outgoing packet to simulate bad network.");
    }

    request_number++;

    if (request_number <= 6)
      usleep(300000); // these are thread cancellation points
    else
      usleep(3000000);
  }
  debug(3, "rtp_timing_sender thread interrupted. This should never happen.");
  pthread_cleanup_pop(0); // don't execute anything here.
  pthread_exit(NULL);
}

void rtp_timing_receiver_cleanup_handler(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  debug(3, "Timing Receiver Cleanup.");
  // walk down the list of DACP / gradient pairs, if any
  nvll *gradients = config.gradients;
  if (conn->dacp_id)
    while ((gradients) && (strcasecmp((const char *)&conn->client_ip_string, gradients->name) != 0))
      gradients = gradients->next;

  // if gradients comes out of this non-null, it is pointing to the DACP and it's last-known
  // gradient
  if (gradients) {
    gradients->value = conn->local_to_remote_time_gradient;
    // debug(1,"Updating a drift of %.2f ppm for \"%s\".", (conn->local_to_remote_time_gradient
    // - 1.0)*1000000, gradients->name);
  } else {
    nvll *new_entry = (nvll *)malloc(sizeof(nvll));
    if (new_entry) {
      new_entry->name = strdup((const char *)&conn->client_ip_string);
      new_entry->value = conn->local_to_remote_time_gradient;
      new_entry->next = config.gradients;
      config.gradients = new_entry;
      // debug(1,"Setting a new drift of %.2f ppm for \"%s\".", (conn->local_to_remote_time_gradient
      // - 1.0)*1000000, new_entry->name);
    }
  }

  debug(3, "Cancel Timing Requester.");
  pthread_cancel(conn->timer_requester);
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  debug(3, "Join Timing Requester.");
  pthread_join(conn->timer_requester, NULL);
  debug(3, "Timing Receiver Cleanup Successful.");
  pthread_setcancelstate(oldState, NULL);
}

void *rtp_timing_receiver(void *arg) {
  debug(3, "rtp_timing_receiver start");
  pthread_cleanup_push(rtp_timing_receiver_cleanup_handler, arg);
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;

  uint8_t packet[2048];
  ssize_t nread;
  pthread_create(&conn->timer_requester, NULL, &rtp_timing_sender, arg);
  //    struct timespec att;
  uint64_t distant_receive_time, distant_transmit_time, arrival_time, return_time;
  local_to_remote_time_jitter = 0;
  local_to_remote_time_jitter_count = 0;
  // uint64_t first_remote_time = 0;
  // uint64_t first_local_time = 0;

  uint64_t first_local_to_remote_time_difference = 0;

  conn->local_to_remote_time_gradient = 1.0; // initial value.
  // walk down the list of DACP / gradient pairs, if any
  nvll *gradients = config.gradients;
  while ((gradients) && (strcasecmp((const char *)&conn->client_ip_string, gradients->name) != 0))
    gradients = gradients->next;

  // if gradients comes out of this non-null, it is pointing to the IP and it's last-known gradient
  if (gradients) {
    conn->local_to_remote_time_gradient = gradients->value;
    // debug(1,"Using a stored drift of %.2f ppm for \"%s\".", (conn->local_to_remote_time_gradient
    // - 1.0)*1000000, gradients->name);
  }

  // calculate diffusion factor

  // at the end of the array of time pings, the diffusion factor
  // must be diffusion_expansion_factor
  // this, at each step, the diffusion multiplication constant must
  // be the nth root of diffusion_expansion_factor
  // where n is the number of elements in the array

  const double diffusion_expansion_factor = 10;
  double log_of_multiplier = log10(diffusion_expansion_factor) / time_ping_history;
  double multiplier = pow(10, log_of_multiplier);
  uint64_t dispersion_factor = (uint64_t)(multiplier * 100);
  // debug(1,"dispersion factor is %" PRIu64 ".", dispersion_factor);

  // uint64_t first_local_to_remote_time_difference_time;
  // uint64_t l2rtd = 0;
  int sequence_number = 0;

  // for getting mean and sd of return times
  int32_t stat_n = 0;
  double stat_mean = 0.0;
  double stat_M2 = 0.0;

  while (1) {
    nread = recv(conn->timing_socket, packet, sizeof(packet), 0);

    if (nread >= 0) {

      if ((config.diagnostic_drop_packet_fraction == 0.0) ||
          (drand48() > config.diagnostic_drop_packet_fraction)) {
        arrival_time = get_absolute_time_in_ns();

        // ssize_t plen = nread;
        // debug(1,"Packet Received on Timing Port.");
        if (packet[1] == 0xd3) { // timing reply

          return_time = arrival_time - conn->departure_time;
          debug(2, "clock synchronisation request: return time is %8.3f milliseconds.",
                0.000001 * return_time);

          if (return_time < 200000000) { // must be less than 0.2 seconds
            // distant_receive_time =
            // ((uint64_t)ntohl(*((uint32_t*)&packet[16])))<<32+ntohl(*((uint32_t*)&packet[20]));

            uint64_t ps, pn;

            ps = nctohl(&packet[16]);
            ps = ps * 1000000000; // this many nanoseconds from the whole seconds
            pn = nctohl(&packet[20]);
            pn = pn * 1000000000;
            pn = pn >> 32; // this many nanoseconds from the fractional part
            distant_receive_time = ps + pn;

            // distant_transmit_time =
            // ((uint64_t)ntohl(*((uint32_t*)&packet[24])))<<32+ntohl(*((uint32_t*)&packet[28]));

            ps = nctohl(&packet[24]);
            ps = ps * 1000000000; // this many nanoseconds from the whole seconds
            pn = nctohl(&packet[28]);
            pn = pn * 1000000000;
            pn = pn >> 32; // this many nanoseconds from the fractional part
            distant_transmit_time = ps + pn;

            uint64_t remote_processing_time = 0;

            if (distant_transmit_time >= distant_receive_time)
              remote_processing_time = distant_transmit_time - distant_receive_time;
            else {
              debug(1, "Yikes: distant_transmit_time is before distant_receive_time; remote "
                       "processing time set to zero.");
            }
            // debug(1,"Return trip time: %" PRIu64 " nS, remote processing time: %" PRIu64 "
            // nS.",return_time, remote_processing_time);

            if (remote_processing_time < return_time)
              return_time -= remote_processing_time;
            else
              debug(1, "Remote processing time greater than return time -- ignored.");

            int cc;
            // debug(1, "time ping history is %d entries.", time_ping_history);
            for (cc = time_ping_history - 1; cc > 0; cc--) {
              conn->time_pings[cc] = conn->time_pings[cc - 1];
              // if ((conn->time_ping_count) && (conn->time_ping_count < 10))
              //                conn->time_pings[cc].dispersion =
              //                  conn->time_pings[cc].dispersion * pow(2.14,
              //                  1.0/conn->time_ping_count);
              if (conn->time_pings[cc].dispersion > UINT64_MAX / dispersion_factor)
                debug(1, "dispersion factor is too large at %" PRIu64 ".");
              else
                conn->time_pings[cc].dispersion =
                    (conn->time_pings[cc].dispersion * dispersion_factor) /
                    100; // make the dispersions 'age' by this rational factor
            }
            // these are used for doing a least squares calculation to get the drift
            conn->time_pings[0].local_time = arrival_time;
            conn->time_pings[0].remote_time = distant_transmit_time + return_time / 2;
            conn->time_pings[0].sequence_number = sequence_number++;
            conn->time_pings[0].chosen = 0;
            conn->time_pings[0].dispersion = return_time;
            if (conn->time_ping_count < time_ping_history)
              conn->time_ping_count++;

            // here, calculate the mean and standard deviation of the return times

            // mean and variance calculations from "online_variance" algorithm at
            // https://en.wikipedia.org/wiki/Algorithms_for_calculating_variance#Online_algorithm

            stat_n += 1;
            double stat_delta = return_time - stat_mean;
            stat_mean += stat_delta / stat_n;
            stat_M2 += stat_delta * (return_time - stat_mean);
            // debug(1, "Timing packet return time stats: current, mean and standard deviation over
            // %d packets: %.1f, %.1f, %.1f (nanoseconds).",
            //        stat_n,return_time,stat_mean, sqrtf(stat_M2 / (stat_n - 1)));

            // here, pick the record with the least dispersion, and record that it's been chosen

            // uint64_t local_time_chosen = arrival_time;
            // uint64_t remote_time_chosen = distant_transmit_time;
            // now pick the timestamp with the lowest dispersion
            uint64_t rt = conn->time_pings[0].remote_time;
            uint64_t lt = conn->time_pings[0].local_time;
            uint64_t tld = conn->time_pings[0].dispersion;
            int chosen = 0;
            for (cc = 1; cc < conn->time_ping_count; cc++)
              if (conn->time_pings[cc].dispersion < tld) {
                chosen = cc;
                rt = conn->time_pings[cc].remote_time;
                lt = conn->time_pings[cc].local_time;
                tld = conn->time_pings[cc].dispersion;
                // local_time_chosen = conn->time_pings[cc].local_time;
                // remote_time_chosen = conn->time_pings[cc].remote_time;
              }
            // debug(1,"Record %d has the lowest dispersion with %0.2f us
            // dispersion.",chosen,1.0*((tld * 1000000) >> 32));
            conn->time_pings[chosen].chosen = 1; // record the fact that it has been used for timing

            conn->local_to_remote_time_difference =
                rt - lt; // make this the new local-to-remote-time-difference
            conn->local_to_remote_time_difference_measurement_time = lt; // done at this time.

            if (first_local_to_remote_time_difference == 0) {
              first_local_to_remote_time_difference = conn->local_to_remote_time_difference;
              // first_local_to_remote_time_difference_time = get_absolute_time_in_fp();
            }

            // here, let's try to use the timing pings that were selected because of their short
            // return times to
            // estimate a figure for drift between the local clock (x) and the remote clock (y)

            // if we plug in a local interval, we will get back what that is in remote time

            // calculate the line of best fit for relating the local time and the remote time
            // we will calculate the slope, which is the drift
            // see https://www.varsitytutors.com/hotmath/hotmath_help/topics/line-of-best-fit

            uint64_t y_bar = 0; // remote timestamp average
            uint64_t x_bar = 0; // local timestamp average
            int sample_count = 0;

            // approximate time in seconds to let the system settle down
            const int settling_time = 60;
            // number of points to have for calculating a valid drift
            const int sample_point_minimum = 8;
            for (cc = 0; cc < conn->time_ping_count; cc++)
              if ((conn->time_pings[cc].chosen) &&
                  (conn->time_pings[cc].sequence_number >
                   (settling_time / 3))) { // wait for a approximate settling time
                                           // have to scale them down so that the sum, possibly over
                                           // every term in the array, doesn't overflow
                y_bar += (conn->time_pings[cc].remote_time >> time_ping_history_power_of_two);
                x_bar += (conn->time_pings[cc].local_time >> time_ping_history_power_of_two);
                sample_count++;
              }
            conn->local_to_remote_time_gradient_sample_count = sample_count;
            if (sample_count > sample_point_minimum) {
              y_bar = y_bar / sample_count;
              x_bar = x_bar / sample_count;

              int64_t xid, yid;
              double mtl, mbl;
              mtl = 0;
              mbl = 0;
              for (cc = 0; cc < conn->time_ping_count; cc++)
                if ((conn->time_pings[cc].chosen) &&
                    (conn->time_pings[cc].sequence_number > (settling_time / 3))) {

                  uint64_t slt = conn->time_pings[cc].local_time >> time_ping_history_power_of_two;
                  if (slt > x_bar)
                    xid = slt - x_bar;
                  else
                    xid = -(x_bar - slt);

                  uint64_t srt = conn->time_pings[cc].remote_time >> time_ping_history_power_of_two;
                  if (srt > y_bar)
                    yid = srt - y_bar;
                  else
                    yid = -(y_bar - srt);

                  mtl = mtl + (1.0 * xid) * yid;
                  mbl = mbl + (1.0 * xid) * xid;
                }
              if (mbl)
                conn->local_to_remote_time_gradient = mtl / mbl;
              else {
                // conn->local_to_remote_time_gradient = 1.0;
                debug(1, "mbl is zero. Drift remains at %.2f ppm.",
                      (conn->local_to_remote_time_gradient - 1.0) * 1000000);
              }

              // scale the numbers back up
              uint64_t ybf = y_bar << time_ping_history_power_of_two;
              uint64_t xbf = x_bar << time_ping_history_power_of_two;

              conn->local_to_remote_time_difference =
                  ybf - xbf; // make this the new local-to-remote-time-difference
              conn->local_to_remote_time_difference_measurement_time = xbf;

            } else {
              debug(3, "not enough samples to estimate drift -- remaining at %.2f ppm.",
                    (conn->local_to_remote_time_gradient - 1.0) * 1000000);
              // conn->local_to_remote_time_gradient = 1.0;
            }
            // debug(1,"local to remote time gradient is %12.2f ppm, based on %d
            // samples.",conn->local_to_remote_time_gradient*1000000,sample_count);

          } else {
            debug(1,
                  "Time ping turnaround time: %" PRIu64
                  " ns -- it looks like a timing ping was lost.",
                  return_time);
          }
        } else {
          debug(1, "Timing port -- Unknown RTP packet of type 0x%02X length %d.", packet[1], nread);
        }
      } else {
        debug(3, "Timing Receiver Thread -- dropping incoming packet to simulate a bad network.");
      }
    } else {
      debug(1, "Timing receiver -- error receiving a packet.");
    }
  }

  debug(1, "Timing Receiver RTP thread \"normal\" exit -- this can't happen. Hah!");
  pthread_cleanup_pop(0); // don't execute anything here.
  debug(2, "Timing Receiver RTP thread exit.");
  pthread_exit(NULL);
}

void rtp_setup(SOCKADDR *local, SOCKADDR *remote, uint16_t cport, uint16_t tport,
               rtsp_conn_info *conn) {

  // this gets the local and remote ip numbers (and ports used for the TCD stuff)
  // we use the local stuff to specify the address we are coming from and
  // we use the remote stuff to specify where we're goint to

  if (conn->rtp_running)
    warn("rtp_setup has been called with al already-active stream -- ignored. Possible duplicate "
         "SETUP call?");
  else {

    debug(3, "rtp_setup: cport=%d tport=%d.", cport, tport);

    // print out what we know about the client
    void *client_addr = NULL, *self_addr = NULL;
    // int client_port, self_port;
    // char client_port_str[64];
    // char self_addr_str[64];

    conn->connection_ip_family =
        remote->SAFAMILY; // keep information about the kind of ip of the client

#ifdef AF_INET6
    if (conn->connection_ip_family == AF_INET6) {
      struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)remote;
      client_addr = &(sa6->sin6_addr);
      // client_port = ntohs(sa6->sin6_port);
      sa6 = (struct sockaddr_in6 *)local;
      self_addr = &(sa6->sin6_addr);
      // self_port = ntohs(sa6->sin6_port);
      conn->self_scope_id = sa6->sin6_scope_id;
    }
#endif
    if (conn->connection_ip_family == AF_INET) {
      struct sockaddr_in *sa4 = (struct sockaddr_in *)remote;
      client_addr = &(sa4->sin_addr);
      // client_port = ntohs(sa4->sin_port);
      sa4 = (struct sockaddr_in *)local;
      self_addr = &(sa4->sin_addr);
      // self_port = ntohs(sa4->sin_port);
    }

    inet_ntop(conn->connection_ip_family, client_addr, conn->client_ip_string,
              sizeof(conn->client_ip_string));
    inet_ntop(conn->connection_ip_family, self_addr, conn->self_ip_string,
              sizeof(conn->self_ip_string));

    debug(2, "Connection %d: SETUP -- Connection from %s to self at %s.", conn->connection_number,
          conn->client_ip_string, conn->self_ip_string);

    // set up a the record of the remote's control socket
    struct addrinfo hints;
    struct addrinfo *servinfo;

    memset(&conn->rtp_client_control_socket, 0, sizeof(conn->rtp_client_control_socket));
    memset(&hints, 0, sizeof hints);
    hints.ai_family = conn->connection_ip_family;
    hints.ai_socktype = SOCK_DGRAM;
    char portstr[20];
    snprintf(portstr, 20, "%d", cport);
    if (getaddrinfo(conn->client_ip_string, portstr, &hints, &servinfo) != 0)
      die("Can't get address of client's control port");

#ifdef AF_INET6
    if (servinfo->ai_family == AF_INET6) {
      memcpy(&conn->rtp_client_control_socket, servinfo->ai_addr, sizeof(struct sockaddr_in6));
      // ensure the scope id matches that of remote. this is needed for link-local addresses.
      struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&conn->rtp_client_control_socket;
      sa6->sin6_scope_id = conn->self_scope_id;
    } else
#endif
      memcpy(&conn->rtp_client_control_socket, servinfo->ai_addr, sizeof(struct sockaddr_in));
    freeaddrinfo(servinfo);

    // set up a the record of the remote's timing socket
    memset(&conn->rtp_client_timing_socket, 0, sizeof(conn->rtp_client_timing_socket));
    memset(&hints, 0, sizeof hints);
    hints.ai_family = conn->connection_ip_family;
    hints.ai_socktype = SOCK_DGRAM;
    snprintf(portstr, 20, "%d", tport);
    if (getaddrinfo(conn->client_ip_string, portstr, &hints, &servinfo) != 0)
      die("Can't get address of client's timing port");
#ifdef AF_INET6
    if (servinfo->ai_family == AF_INET6) {
      memcpy(&conn->rtp_client_timing_socket, servinfo->ai_addr, sizeof(struct sockaddr_in6));
      // ensure the scope id matches that of remote. this is needed for link-local addresses.
      struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&conn->rtp_client_timing_socket;
      sa6->sin6_scope_id = conn->self_scope_id;
    } else
#endif
      memcpy(&conn->rtp_client_timing_socket, servinfo->ai_addr, sizeof(struct sockaddr_in));
    freeaddrinfo(servinfo);

    // now, we open three sockets -- one for the audio stream, one for the timing and one for the
    // control
    conn->remote_control_port = cport;
    conn->remote_timing_port = tport;

    conn->local_control_port = bind_UDP_port(conn->connection_ip_family, conn->self_ip_string,
                                             conn->self_scope_id, &conn->control_socket);
    conn->local_timing_port = bind_UDP_port(conn->connection_ip_family, conn->self_ip_string,
                                            conn->self_scope_id, &conn->timing_socket);
    conn->local_audio_port = bind_UDP_port(conn->connection_ip_family, conn->self_ip_string,
                                           conn->self_scope_id, &conn->audio_socket);

    debug(3, "listening for audio, control and timing on ports %d, %d, %d.", conn->local_audio_port,
          conn->local_control_port, conn->local_timing_port);

    conn->anchor_rtptime = 0;

    conn->request_sent = 0;
    conn->rtp_running = 1;

#ifdef CONFIG_METADATA
    send_ssnc_metadata('clip', conn->client_ip_string, strlen(conn->client_ip_string), 1);
    send_ssnc_metadata('svip', conn->self_ip_string, strlen(conn->self_ip_string), 1);
#endif
  }
}

void reset_ntp_anchor_info(rtsp_conn_info *conn) {
  debug_mutex_lock(&conn->reference_time_mutex, 1000, 1);
  conn->anchor_rtptime = 0;
  conn->anchor_time = 0;
  debug_mutex_unlock(&conn->reference_time_mutex, 3);
}

int have_ntp_timestamp_timing_information(rtsp_conn_info *conn) {
  if (conn->anchor_rtptime == 0)
    return 0;
  else
    return 1;
}

// set this to zero to use the rates supplied by the sources, which might not always be completely
// right...
const int use_nominal_rate = 0; // specify whether to use the nominal input rate, usually 44100 fps

int sanitised_source_rate_information(uint32_t *frames, uint64_t *time, rtsp_conn_info *conn) {
  int result = 1;
  uint32_t fs = conn->input_rate;
  *frames = fs;       // default value to return
  *time = 1000000000; // default value to return
  if ((conn->initial_reference_time) && (conn->initial_reference_timestamp)) {
    //    uint32_t local_frames = conn->reference_timestamp - conn->initial_reference_timestamp;
    uint32_t local_frames =
        modulo_32_offset(conn->initial_reference_timestamp, conn->anchor_rtptime);
    uint64_t local_time = conn->anchor_time - conn->initial_reference_time;
    if ((local_frames == 0) || (local_time == 0) || (use_nominal_rate)) {
      result = 1;
    } else {
      double calculated_frame_rate = conn->input_rate;
      if (local_time)
        calculated_frame_rate = (1.0E9 * local_frames) / local_time;
      else
        debug(1, "sanitised_source_rate_information: local_time is zero");
      if ((local_time == 0) || ((calculated_frame_rate / conn->input_rate) > 1.002) ||
          ((calculated_frame_rate / conn->input_rate) < 0.998)) {
        debug(3, "input frame rate out of bounds at %.2f fps.", calculated_frame_rate);
        result = 1;
      } else {
        *frames = local_frames;
        *time = local_time;
        result = 0;
      }
    }
  }
  return result;
}

void set_ntp_anchor_info(rtsp_conn_info *conn, uint32_t rtptime, uint64_t networktime) {
  conn->anchor_remote_info_is_valid = 1;
  conn->anchor_rtptime = rtptime;
  conn->anchor_time = networktime;
}

// the timestamp is a timestamp calculated at the input rate
// the reference timestamps are denominated in terms of the input rate

int frame_to_ntp_local_time(uint32_t timestamp, uint64_t *time, rtsp_conn_info *conn) {
  debug_mutex_lock(&conn->reference_time_mutex, 1000, 0);
  int result = 0;
  uint64_t time_difference;
  uint32_t frame_difference;
  result = sanitised_source_rate_information(&frame_difference, &time_difference, conn);

  uint64_t remote_time_of_timestamp;
  int32_t timestamp_interval = timestamp - conn->anchor_rtptime;
  int64_t timestamp_interval_time = timestamp_interval;
  timestamp_interval_time = timestamp_interval_time * time_difference;
  timestamp_interval_time =
      timestamp_interval_time / frame_difference; // this is the nominal time, based on the
                                                  // fps specified between current and
                                                  // previous sync frame.
  remote_time_of_timestamp =
      conn->anchor_time + timestamp_interval_time; // based on the reference timestamp time
                                                   // plus the time interval calculated based
                                                   // on the specified fps.
  *time = remote_time_of_timestamp - local_to_remote_time_difference_now(conn);
  debug_mutex_unlock(&conn->reference_time_mutex, 0);
  return result;
}

int local_ntp_time_to_frame(uint64_t time, uint32_t *frame, rtsp_conn_info *conn) {
  debug_mutex_lock(&conn->reference_time_mutex, 1000, 0);
  int result = 0;
  uint64_t time_difference;
  uint32_t frame_difference;
  result = sanitised_source_rate_information(&frame_difference, &time_difference, conn);
  // first, get from [local] time to remote time.
  uint64_t remote_time = time + local_to_remote_time_difference_now(conn);
  // next, get the remote time interval from the remote_time to the reference time
  // here, we calculate the time interval, in terms of remote time
  int64_t offset = remote_time - conn->anchor_time;
  // now, convert the remote time interval into frames using the frame rate we have observed or
  // which has been nominated
  int64_t frame_interval = 0;
  frame_interval = (offset * frame_difference) / time_difference;
  int32_t frame_interval_32 = frame_interval;
  uint32_t new_frame = conn->anchor_rtptime + frame_interval_32;
  *frame = new_frame;
  debug_mutex_unlock(&conn->reference_time_mutex, 0);
  return result;
}

void rtp_request_resend(seq_t first, uint32_t count, rtsp_conn_info *conn) {
  // debug(1, "rtp_request_resend of %u packets from sequence number %u.", count, first);
  if (conn->rtp_running) {
    // if (!request_sent) {
    // debug(2, "requesting resend of %d packets starting at %u.", count, first);
    //  request_sent = 1;
    //}

    char req[8]; // *not* a standard RTCP NACK
    req[0] = 0x80;
#ifdef CONFIG_AIRPLAY_2
    if (conn->airplay_type == ap_2) {
      if (conn->ap2_remote_control_socket_addr_length == 0) {
        debug(2, "No remote socket -- skipping the resend");
        return; // hack
      }
      req[1] = 0xD5; // Airplay 2 'resend'
    } else {
#endif
      req[1] = (char)0x55 | (char)0x80; // Apple 'resend'
#ifdef CONFIG_AIRPLAY_2
    }
#endif
    *(unsigned short *)(req + 2) = htons(1);     // our sequence number
    *(unsigned short *)(req + 4) = htons(first); // missed seqnum
    *(unsigned short *)(req + 6) = htons(count); // count

    uint64_t time_of_sending_ns = get_absolute_time_in_ns();
    uint64_t resend_error_backoff_time = 300000000; // 0.3 seconds
    if ((conn->rtp_time_of_last_resend_request_error_ns == 0) ||
        ((time_of_sending_ns - conn->rtp_time_of_last_resend_request_error_ns) >
         resend_error_backoff_time)) {
      if ((config.diagnostic_drop_packet_fraction == 0.0) ||
          (drand48() > config.diagnostic_drop_packet_fraction)) {
        // put a time limit on the sendto

        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        int response;
#ifdef CONFIG_AIRPLAY_2
        if (conn->airplay_type == ap_2) {
          if (setsockopt(conn->ap2_control_socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,
                         sizeof(timeout)) < 0)
            debug(1, "Can't set timeout on resend request socket.");
          response = sendto(conn->ap2_control_socket, req, sizeof(req), 0,
                            (struct sockaddr *)&conn->ap2_remote_control_socket_addr,
                            conn->ap2_remote_control_socket_addr_length);
        } else {
#endif
          if (setsockopt(conn->control_socket, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout,
                         sizeof(timeout)) < 0)
            debug(1, "Can't set timeout on resend request socket.");
          socklen_t msgsize = sizeof(struct sockaddr_in);
#ifdef AF_INET6
          if (conn->rtp_client_control_socket.SAFAMILY == AF_INET6) {
            msgsize = sizeof(struct sockaddr_in6);
          }
#endif
          response = sendto(conn->control_socket, req, sizeof(req), 0,
                            (struct sockaddr *)&conn->rtp_client_control_socket, msgsize);

#ifdef CONFIG_AIRPLAY_2
        }
#endif
        if (response == -1) {
          char em[1024];
          strerror_r(errno, em, sizeof(em));
          debug(2, "Error %d using sendto to request a resend: \"%s\".", errno, em);
          conn->rtp_time_of_last_resend_request_error_ns = time_of_sending_ns;
        } else {
          conn->rtp_time_of_last_resend_request_error_ns = 0;
        }

      } else {
        debug(3, "Dropping resend request packet to simulate a bad network. Backing off for 0.3 "
                 "second.");
        conn->rtp_time_of_last_resend_request_error_ns = time_of_sending_ns;
      }
    } else {
      debug(1,
            "Suppressing a resend request due to a resend sendto error in the last 0.3 seconds.");
    }
  } else {
    // if (!request_sent) {
    debug(2, "rtp_request_resend called without active stream!");
    //  request_sent = 1;
    //}
  }
}

#ifdef CONFIG_AIRPLAY_2

void set_ptp_anchor_info(rtsp_conn_info *conn, uint64_t clock_id, uint32_t rtptime,
                         uint64_t networktime) {
  if (conn->anchor_clock != clock_id)
    debug(1, "Connection %d: Set Anchor Clock: %" PRIx64 ".", conn->connection_number, clock_id);
  conn->anchor_remote_info_is_valid = 1;
  conn->anchor_rtptime = rtptime;
  conn->anchor_time = networktime;
  conn->anchor_clock = clock_id;
}

void reset_ptp_anchor_info(rtsp_conn_info *conn) {
  debug(2, "Connection %d: Clear anchor information.", conn->connection_number);
  conn->last_anchor_info_is_valid = 0;
  conn->anchor_remote_info_is_valid = 0;
}

int get_ptp_anchor_local_time_info(rtsp_conn_info *conn, uint32_t *anchorRTP,
                                   uint64_t *anchorLocalTime) {

  uint64_t actual_clock_id, actual_offset;
  int response = ptp_get_clock_info(&actual_clock_id, &actual_offset);

  if (response == clock_ok) {
    if (conn->anchor_remote_info_is_valid !=
        0) { // i.e. if we have anchor clock ID and anchor time / rtptime
      if (actual_clock_id == conn->anchor_clock) {
        conn->last_anchor_clock_offset = actual_offset;
        conn->last_anchor_time_of_update = get_absolute_time_in_ns();
        conn->last_anchor_info_is_valid = 1;
        if (anchorRTP != NULL)
          *anchorRTP = conn->anchor_rtptime;
        if (anchorLocalTime != NULL)
          *anchorLocalTime = conn->anchor_time - conn->last_anchor_clock_offset;
      } else {
        if (conn->last_anchor_info_is_valid != 0) {
          int64_t time_since_last_update =
              get_absolute_time_in_ns() - conn->last_anchor_time_of_update;
          if (time_since_last_update > 5000000000) {
            debug(1, "change master clock to %" PRIx64 ".", actual_clock_id);
            uint64_t new_anchor_time = conn->anchor_time;
            new_anchor_time = new_anchor_time - conn->last_anchor_clock_offset; // to local
            new_anchor_time = new_anchor_time + actual_offset;                  // to the next clock
            conn->anchor_time = new_anchor_time;
            conn->anchor_clock = actual_clock_id;
            if (anchorRTP != NULL)
              *anchorRTP = conn->anchor_rtptime;
            if (anchorLocalTime != NULL)
              *anchorLocalTime = conn->anchor_time - actual_offset;
          } else {
            if (anchorRTP != NULL)
              *anchorRTP = conn->anchor_rtptime;
            if (anchorLocalTime != NULL)
              *anchorLocalTime = conn->anchor_time - conn->last_anchor_clock_offset;
          }
        } else {
          response = clock_not_valid; // no current clock information and no previous clock info
        }
      }
    } else {
      response = clock_no_anchor_info; // no anchor information
    }
  }

  // here, check and update the clock status
  if ((clock_status_t)response != conn->clock_status) {
    switch (response) {
    case clock_ok:
      debug(1, "Connection %d: NQPTP new master clock % " PRIx64 ".", conn->connection_number,
            actual_clock_id);
      break;
    case clock_service_unavailable:
      debug(1, "Connection %d: NQPTP clock is not available.", conn->connection_number);
      warn("Can't access the NQPTP clock. Is NQPTP running?");
      break;
    case clock_access_error:
      debug(1, "Connection %d: Error accessing the NQPTP clock interface.",
            conn->connection_number);
      break;
    case clock_data_unavailable:
      debug(1, "Connection %d: Can not access NQPTP clock information.", conn->connection_number);
      break;
    case clock_no_master:
      debug(1, "Connection %d: No NQPTP master clock.", conn->connection_number);
      break;
    case clock_no_anchor_info:
      debug(1, "Connection %d: No Clock Anchor.", conn->connection_number);
      break;
    case clock_version_mismatch:
      debug(1, "Connection %d: NQPTP clock interface mismatch.", conn->connection_number);
      warn("This version of Shairport Sync is not compatible with the installed version of NQPTP. "
           "Please update.");
      break;
    case clock_not_synchronised:
      debug(1, "Connection %d: NQPTP clock is not synchronised.", conn->connection_number);
      break;
    case clock_not_valid:
      debug(1, "Connection %d: NQPTP clock information is not valid.", conn->connection_number);
      break;
    default:
      debug(3, "Connection %d: NQPTP clock reports an unrecognised status: %u.",
            conn->connection_number, response);
      break;
    }
    conn->clock_status = response;
  }

  if ((response != clock_ok) && (conn->last_anchor_info_is_valid != 0)) {
    if (anchorRTP != NULL)
      *anchorRTP = conn->anchor_rtptime;
    if (anchorLocalTime != NULL)
      *anchorLocalTime = conn->anchor_time - conn->last_anchor_clock_offset;
    response = clock_ok;
  }
  return response;
}

int have_ptp_timing_information(rtsp_conn_info *conn) {
  if (get_ptp_anchor_local_time_info(conn, NULL, NULL) == clock_ok)
    return 1;
  else
    return 0;
}

int frame_to_ptp_local_time(uint32_t timestamp, uint64_t *time, rtsp_conn_info *conn) {
  int result = -1;
  uint32_t anchor_rtptime;
  uint64_t anchor_local_time;
  if (get_ptp_anchor_local_time_info(conn, &anchor_rtptime, &anchor_local_time) == clock_ok) {
    int32_t frame_difference = timestamp - anchor_rtptime;
    int64_t time_difference = frame_difference;
    time_difference = time_difference * 1000000000;
    time_difference = time_difference / conn->input_rate;
    uint64_t ltime = anchor_local_time + time_difference;
    *time = ltime;
    result = 0;
  } else {
    debug(3, "frame_to_local_time can't get anchor local time information");
  }
  return result;
}

int local_ptp_time_to_frame(uint64_t time, uint32_t *frame, rtsp_conn_info *conn) {
  int result = -1;
  uint32_t anchor_rtptime;
  uint64_t anchor_local_time;
  if (get_ptp_anchor_local_time_info(conn, &anchor_rtptime, &anchor_local_time) == clock_ok) {
    int64_t time_difference = time - anchor_local_time;
    int64_t frame_difference = time_difference;
    frame_difference = frame_difference * conn->input_rate; // but this is by 10^9
    frame_difference = frame_difference / 1000000000;
    int32_t fd32 = frame_difference;
    uint32_t lframe = anchor_rtptime + fd32;
    *frame = lframe;
    result = 0;
  } else {
    debug(3, "local_time_to_frame can't get anchor local time information");
  }
  return result;
}

void rtp_event_receiver_cleanup_handler(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  debug(2, "Connection %d: AP2 Event Receiver Cleanup.", conn->connection_number);
}

void *rtp_event_receiver(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  debug(2, "Connection %d: AP2 Event Receiver started", conn->connection_number);
  pthread_cleanup_push(rtp_event_receiver_cleanup_handler, arg);

  listen(conn->event_socket, 5);

  uint8_t packet[4096];
  ssize_t nread;
  SOCKADDR remote_addr;
  memset(&remote_addr, 0, sizeof(remote_addr));
  socklen_t addr_size = sizeof(remote_addr);

  int fd = accept(conn->event_socket, (struct sockaddr *)&remote_addr, &addr_size);
  pthread_cleanup_push(socket_cleanup, (void *)fd);
  int finished = 0;
  do {
    nread = recv(fd, packet, sizeof(packet), 0);

    if (nread < 0) {
      char errorstring[1024];
      strerror_r(errno, (char *)errorstring, sizeof(errorstring));
      debug(1,
            "Connection %d: error in ap2 rtp_event_receiver %d: \"%s\". Could not recv a packet.",
            conn->connection_number, errno, errorstring);
      // if ((config.diagnostic_drop_packet_fraction == 0.0) ||
      //     (drand48() > config.diagnostic_drop_packet_fraction)) {
    } else if (nread > 0) {

      // ssize_t plen = nread;
      debug(1, "Packet Received on Event Port.");
      if (packet[1] == 0xD7) {
        debug(1,
              "Connection %d: AP2 Event Receiver -- Time Announce RTP packet of type 0x%02X length "
              "%d received.",
              conn->connection_number, packet[1], nread);
      } else {
        debug(1,
              "Connection %d: AP2 Event Receiver -- Unknown RTP packet of type 0x%02X length %d "
              "received.",
              conn->connection_number, packet[1], nread);
      }
      // } else {
      //   debug(3, "Event Receiver Thread -- dropping incoming packet to simulate a bad network.");
      // }
    } else {
      finished = 1;
    }
  } while (finished == 0);
  pthread_cleanup_pop(1); // close the socket
  pthread_cleanup_pop(1); // do the cleanup
  debug(2, "Connection %d: AP2 Event Receiver RTP thread \"normal\" exit.",
        conn->connection_number);
  pthread_exit(NULL);
}

void rtp_ap2_control_handler_cleanup_handler(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  debug(2, "Connection %d: AP2 Control Receiver Cleanup.", conn->connection_number);
  close(conn->ap2_control_socket);
  conn->ap2_control_socket = 0;
  conn->ap2_remote_control_socket_addr_length =
      0; // indicates to the control receiver thread that the socket address need to be
         // recreated (needed for resend requests in the realtime mode)
}

int32_t decipher_player_put_packet(uint8_t *ciphered_audio_alt, ssize_t nread,
                                   rtsp_conn_info *conn) {

  // this deciphers the packet -- it doesn't decode it from ALAC
  uint16_t sequence_number = 0;

  // if the packet is too small, don't go ahead.
  // it must contain an uint16_t sequence number and eight bytes of AAD followed by the
  // ciphertext and then followed by an eight-byte nonce. Thus it must be greater than 18
  if (nread > 18) {

    memcpy(&sequence_number, ciphered_audio_alt, sizeof(uint16_t));
    sequence_number = ntohs(sequence_number);

    uint32_t timestamp;
    memcpy(&timestamp, ciphered_audio_alt + sizeof(uint16_t), sizeof(uint32_t));
    timestamp = ntohl(timestamp);

    /*
    uint32_t ssrc;
    memcpy(&ssrc, packet+8, sizeof(uint32_t));
    ssrc = ntohl(ssrc);
    */

    // debug(1, "Realtime Audio Receiver Packet received. Version: %u, Padding: %u, Extension:
    // %u, Csrc Count: %u, Marker: %u, Payload Type: %u, Sequence Number: %u, Timestamp: %u,
    // SSRC: %u.", version, padding, extension, csrc_count, marker, payload_type,
    // sequence_number, timestamp, ssrc);

    unsigned char nonce[12];
    memset(nonce, 0, sizeof(nonce));
    memcpy(nonce + 4, ciphered_audio_alt + nread - 8,
           8); // front-pad the 8-byte nonce received to get the 12-byte nonce expected

    // https://libsodium.gitbook.io/doc/secret-key_cryptography/aead/chacha20-poly1305/ietf_chacha20-poly1305_construction
    // Note: the eight-byte nonce must be front-padded out to 12 bytes.

    unsigned char m[4096];
    unsigned long long new_payload_length = 0;
    int response = crypto_aead_chacha20poly1305_ietf_decrypt(
        m,                   // m
        &new_payload_length, // mlen_p
        NULL,                // nsec,
        ciphered_audio_alt +
            10,                 // the ciphertext starts 10 bytes in and is followed by the MAC tag,
        nread - (8 + 10),       // clen -- the last 8 bytes are the nonce
        ciphered_audio_alt + 2, // authenticated additional data
        8,                      // authenticated additional data length
        nonce,
        conn->session_key); // *k
    if (response != 0) {
      debug(1, "Error decrypting an audio packet.");
    }
    // now pass it in to the regular processing chain

    unsigned long long max_int = INT_MAX; // put in the right format
    if (new_payload_length > max_int)
      debug(1, "Madly long payload length!");
    int plen = new_payload_length; //
    player_put_packet(1, sequence_number, timestamp, m, plen,
                      conn); // the '1' means is original format
    return sequence_number;
  } else {
    debug(1, "packet was too small -- ignored");
    return -1;
  }
}

void *rtp_ap2_control_receiver(void *arg) {
  pthread_cleanup_push(rtp_ap2_control_handler_cleanup_handler, arg);
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  uint8_t packet[4096];
  ssize_t nread;
  int keep_going = 1;
  while (keep_going) {
    SOCKADDR from_sock_addr;
    socklen_t from_sock_addr_length = sizeof(SOCKADDR);
    memset(&from_sock_addr, 0, sizeof(SOCKADDR));

    nread = recvfrom(conn->ap2_control_socket, packet, sizeof(packet), 0,
                     (struct sockaddr *)&from_sock_addr, &from_sock_addr_length);

    if (nread > 0) {
      // debug(1,"rtp_ap2_control_receiver coded: %u, %u", packet[0], packet[1]);

      if ((config.diagnostic_drop_packet_fraction == 0.0) ||
          (drand48() > config.diagnostic_drop_packet_fraction)) {
        // store the from_sock_addr if we haven't already done so
        // v remember to zero this when you're finished!
        if (conn->ap2_remote_control_socket_addr_length == 0) {
          memcpy(&conn->ap2_remote_control_socket_addr, &from_sock_addr, from_sock_addr_length);
          conn->ap2_remote_control_socket_addr_length = from_sock_addr_length;
        }
        switch (packet[1]) {
        case 215: // code 215, effectively an anchoring announcement
        {
          // struct timespec tnr;
          // clock_gettime(CLOCK_REALTIME, &tnr);
          // uint64_t local_realtime_now = timespec_to_ns(&tnr);

          /*
                    char obf[4096];
                    char *obfp = obf;
                    int obfc;
                    for (obfc=0;obfc<nread;obfc++) {
                      snprintf(obfp, 3, "%02X", packet[obfc]);
                      obfp+=2;
                    };
                    *obfp=0;
                    debug(1,"AP2 Timing Control Received: \"%s\"",obf);
          */

          uint64_t remote_packet_time_ns = nctoh64(packet + 8);
          uint64_t clock_id = nctoh64(packet + 20);

          // debug(1, "we have clock_id: %" PRIx64 ".", clock_id);
          // debug(1,"remote_packet_time_ns: %" PRIx64 ", local_realtime_now_ns: %" PRIx64 ".",
          // remote_packet_time_ns, local_realtime_now);
          uint32_t frame_1 =
              nctohl(packet + 4); // this seems to be the frame with latency of 77165 included
          uint32_t frame_2 = nctohl(packet + 16); // this seems to be the frame the time refers to
          // this just updates the anchor information contained in the packet
          // the frame and its remote time
          // add in the audio_backend_latency_offset;
          int32_t notified_latency = frame_2 - frame_1;
          int32_t added_latency = (int32_t)(config.audio_backend_latency_offset * conn->input_rate);
          // the actual latency is the notified latency plus the fixed latency + the added latency

          if (added_latency < (-(notified_latency + 11035)))
            debug(1, "the audio_backend_latency_offset is causing a negative latency!");

          /*
          debug_mutex_lock(&conn->reference_time_mutex, 1000, 0);
          conn->remote_reference_timestamp_time = remote_packet_time_ns;
          conn->reference_timestamp =
              frame_1 - 11035 - added_latency; // add the latency in to the anchortime
          debug_mutex_unlock(&conn->reference_time_mutex, 0);
          */
          // this is now only used for calculating when to ask for resends
          conn->latency = notified_latency + 11035 + added_latency;
          // debug(1,"conn->latency is %d.", conn->latency);
          set_ptp_anchor_info(conn, clock_id, frame_1 - 11035 - added_latency,
                              remote_packet_time_ns);

        } break;
        case 0xd6:
          // six bytes in is the sequence number at the start of the encrypted audio packet
          // returns the sequence number but we're not really interested
          decipher_player_put_packet(packet + 6, nread - 6, conn);
          break;
        default: {
          char *packet_in_hex_cstring =
              debug_malloc_hex_cstring(packet, nread); // remember to free this afterwards
          debug(1,
                "AP2 Control Receiver Packet of first byte 0x%02X, type 0x%02X length %d received: "
                "\"%s\".",
                packet[0], packet[1], nread, packet_in_hex_cstring);
          free(packet_in_hex_cstring);
        } break;
        }
      } else {
        debug(1, "AP2 Control Receiver -- dropping a packet.");
      }
    } else if (nread == 0) {
      debug(1, "AP2 Control Receiver -- connection closed.");
      keep_going = 0;
    } else {
      debug(1, "AP2 Control Receiver -- error %d receiving a packet.", errno);
    }
  }
  debug(1, "AP2 Control RTP thread \"normal\" exit -- this can't happen. Hah!");
  pthread_cleanup_pop(1);
  debug(1, "AP2 Control RTP thread exit.");
  pthread_exit(NULL);
}

void rtp_realtime_audio_cleanup_handler(__attribute__((unused)) void *arg) {
  debug(2, "Realtime Audio Receiver Cleanup Start.");
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  close(conn->realtime_audio_socket);
  conn->realtime_audio_socket = 0;
  debug(2, "Realtime Audio Receiver Cleanup Done.");
}

void *rtp_realtime_audio_receiver(void *arg) {
  pthread_cleanup_push(rtp_realtime_audio_cleanup_handler, arg);
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  uint8_t packet[4096];
  int32_t last_seqno = -1;
  ssize_t nread;
  while (1) {
    nread = recv(conn->realtime_audio_socket, packet, sizeof(packet), 0);

    if (nread > 36) { // 36 is the 12-byte header and and 24-byte footer
      if ((config.diagnostic_drop_packet_fraction == 0.0) ||
          (drand48() > config.diagnostic_drop_packet_fraction)) {

        /*
                char *packet_in_hex_cstring =
                    debug_malloc_hex_cstring(packet, nread); // remember to free this afterwards
                debug(1, "Audio Receiver Packet of type 0x%02X length %d received: \"%s\".",
                packet[1], nread, packet_in_hex_cstring);
                free(packet_in_hex_cstring);
        */

        /*
        // debug(1, "Realtime Audio Receiver Packet of type 0x%02X length %d received.", packet[1],
        nread);
        // now get hold of its various bits and pieces
        uint8_t version = (packet[0] & 0b11000000) >> 6;
        uint8_t padding = (packet[0] & 0b00100000) >> 5;
        uint8_t extension = (packet[0] & 0b00010000) >> 4;
        uint8_t csrc_count = packet[0] & 0b00001111;
        uint8_t marker = (packet[1] & 0b1000000) >> 7;
        uint8_t payload_type = packet[1] & 0b01111111;
        */

        int32_t seqno = decipher_player_put_packet(packet + 2, nread - 2, conn);
        if (seqno >= 0) {
          if (last_seqno == -1) {
            last_seqno = seqno;
          } else {
            last_seqno = (last_seqno + 1) & 0xffff;
            // if (seqno != last_seqno)
            //  debug(3, "RTP: Packets out of sequence: expected: %d, got %d.", last_seqno, seqno);
            last_seqno = seqno; // reset warning...
          }
        } else {
          debug(1, "Realtime Audio Receiver -- bad packet dropped.");
        }
      } else {
        debug(3, "Realtime Audio Receiver -- dropping a packet.");
      }
    } else {
      debug(1, "Realtime Audio Receiver -- error receiving a packet.");
    }
  }
  pthread_cleanup_pop(0); // don't execute anything here.
  pthread_exit(NULL);
}

ssize_t buffered_read(buffered_tcp_desc *descriptor, void *buf, size_t count) {
  ssize_t response;
  // usleep(1500000);
  if (pthread_mutex_lock(&descriptor->mutex) != 0)
    debug(1, "problem with mutex");
  pthread_cleanup_push(mutex_unlock, (void *)&descriptor->mutex);
  if ((descriptor->closed == 0)) {
    while ((descriptor->buffer_occupancy == 0) && (descriptor->error_code == 0)) {
      if (pthread_cond_wait(&descriptor->not_empty_cv, &descriptor->mutex))
        debug(1, "Error waiting for buffered read");
    }
  }
  if (descriptor->buffer_occupancy != 0) {
    ssize_t bytes_to_move = count;

    if (descriptor->buffer_occupancy < count)
      bytes_to_move = descriptor->buffer_occupancy;

    ssize_t top_gap = descriptor->buffer + descriptor->buffer_max_size - descriptor->toq;
    if (top_gap < bytes_to_move)
      bytes_to_move = top_gap;

    memcpy(buf, descriptor->toq, bytes_to_move);
    descriptor->toq += bytes_to_move;
    if (descriptor->toq == descriptor->buffer + descriptor->buffer_max_size)
      descriptor->toq = descriptor->buffer;
    descriptor->buffer_occupancy -= bytes_to_move;
    response = bytes_to_move;
    if (pthread_cond_signal(&descriptor->not_full_cv))
      debug(1, "Error signalling");
  } else if (descriptor->error_code) {
    errno = descriptor->error_code;
    response = -1;
  } else if (descriptor->closed != 0) {
    response = 0;
  }

  pthread_cleanup_pop(1); // release the mutex
  return response;
}

#define STANDARD_PACKET_SIZE 4096

void buffered_tcp_reader_cleanup_handler(__attribute__((unused)) void *arg) {
  debug(2, "Buffered TCP Reader Thread Exit via Cleanup.");
}

void *buffered_tcp_reader(void *arg) {
  pthread_cleanup_push(buffered_tcp_reader_cleanup_handler, NULL);
  buffered_tcp_desc *descriptor = (buffered_tcp_desc *)arg;

  listen(descriptor->sock_fd, 5);
  ssize_t nread;
  SOCKADDR remote_addr;
  memset(&remote_addr, 0, sizeof(remote_addr));
  socklen_t addr_size = sizeof(remote_addr);
  int finished = 0;
  int fd = accept(descriptor->sock_fd, (struct sockaddr *)&remote_addr, &addr_size);
  pthread_cleanup_push(socket_cleanup, (void *)fd);

  do {
    if (pthread_mutex_lock(&descriptor->mutex) != 0)
      debug(1, "problem with mutex");
    pthread_cleanup_push(mutex_unlock, (void *)&descriptor->mutex);
    while ((descriptor->buffer_occupancy == descriptor->buffer_max_size) ||
           (descriptor->error_code != 0) || (descriptor->closed != 0)) {
      if (pthread_cond_wait(&descriptor->not_full_cv, &descriptor->mutex))
        debug(1, "Error waiting for buffered read");
    }
    pthread_cleanup_pop(1); // release the mutex

    // now we know it is not full, so go ahead and try to read some more into it

    // wrap
    if ((size_t)(descriptor->eoq - descriptor->buffer) == descriptor->buffer_max_size)
      descriptor->eoq = descriptor->buffer;

    // figure out how much to ask for
    size_t bytes_to_request = STANDARD_PACKET_SIZE;
    size_t free_space = descriptor->buffer_max_size - descriptor->buffer_occupancy;
    if (bytes_to_request > free_space)
      bytes_to_request = free_space; // don't ask for more than will fit

    size_t gap_to_end_of_buffer =
        descriptor->buffer + descriptor->buffer_max_size - descriptor->eoq;
    if (gap_to_end_of_buffer < bytes_to_request)
      bytes_to_request =
          gap_to_end_of_buffer; // only ask for what will fill to the top of the buffer

    // do the read
    // debug(1, "Request buffered read  of up to %d bytes.", bytes_to_request);
    nread = recv(fd, descriptor->eoq, bytes_to_request, 0);
    // debug(1, "Received %d bytes for a buffer size of %d bytes.",nread,
    // descriptor->buffer_occupancy + nread);
    if (pthread_mutex_lock(&descriptor->mutex) != 0)
      debug(1, "problem with not empty mutex");
    pthread_cleanup_push(mutex_unlock, (void *)&descriptor->mutex);
    if (nread < 0) {
      char errorstring[1024];
      strerror_r(errno, (char *)errorstring, sizeof(errorstring));
      debug(1, "error in buffered_read %d: \"%s\". Could not recv a packet.", errno, errorstring);
      descriptor->error_code = errno;
    } else if (nread == 0) {
      descriptor->closed = 1;
    } else if (nread > 0) {
      descriptor->eoq += nread;
      descriptor->buffer_occupancy += nread;
    } else {
      debug(1, "buffered audio port closed!");
    }
    // signal if we got data or an error or the file closed
    if (pthread_cond_signal(&descriptor->not_empty_cv))
      debug(1, "Error signalling");
    pthread_cleanup_pop(1); // release the mutex
  } while (finished == 0);

  debug(1, "Buffered TCP Reader Thread Exit \"Normal\" Exit Begin.");
  pthread_cleanup_pop(1); // close the socket
  pthread_cleanup_pop(1); // cleanup
  debug(1, "Buffered TCP Reader Thread Exit \"Normal\" Exit -- Shouldn't happen!.");
  pthread_exit(NULL);
}

void avcodec_alloc_context3_cleanup_handler(void *arg) {
  debug(3, "avcodec_alloc_context3_cleanup_handler");
  AVCodecContext *codec_context = arg;
  av_free(codec_context);
}

void avcodec_open2_cleanup_handler(void *arg) {
  debug(3, "avcodec_open2_cleanup_handler");
  AVCodecContext *codec_context = arg;
  avcodec_close(codec_context);
}

void av_parser_init_cleanup_handler(void *arg) {
  debug(3, "av_parser_init_cleanup_handler");
  AVCodecParserContext *codec_parser_context = arg;
  av_parser_close(codec_parser_context);
}

void swr_alloc_cleanup_handler(void *arg) {
  debug(3, "swr_alloc_cleanup_handler");
  SwrContext **swr = arg;
  swr_free(swr);
}

void av_packet_alloc_cleanup_handler(void *arg) {
  debug(3, "av_packet_alloc_cleanup_handler");
  AVPacket **pkt = arg;
  av_packet_free(pkt);
}

// this will read a block of the size specified to the buffer
// and will return either with the block or on error
ssize_t lread_sized_block(buffered_tcp_desc *descriptor, void *buf, size_t count) {
  ssize_t response, nread;
  size_t inbuf = 0; // bytes already in the buffer
  int keep_trying = 1;

  do {
    nread = buffered_read(descriptor, buf + inbuf, count - inbuf);
    if (nread == 0) {
      // a blocking read that returns zero means eof -- implies connection closed
      debug(3, "read_sized_block connection closed.");
      keep_trying = 0;
    } else if (nread < 0) {
      if (errno == EAGAIN) {
        debug(1, "read_sized_block getting Error 11 -- EAGAIN from a blocking read!");
      }
      if ((errno != ECONNRESET) && (errno != EAGAIN) && (errno != EINTR)) {
        char errorstring[1024];
        strerror_r(errno, (char *)errorstring, sizeof(errorstring));
        debug(1, "read_sized_block read error %d: \"%s\".", errno, (char *)errorstring);
        keep_trying = 0;
      }
    } else {
      inbuf += (size_t)nread;
    }
  } while ((keep_trying != 0) && (inbuf < count));
  if (nread <= 0)
    response = nread;
  else
    response = inbuf;
  return response;
}

// From
// https://stackoverflow.com/questions/18862715/how-to-generate-the-aac-adts-elementary-stream-with-android-mediacodec
// with thanks!
/**
 *  Add ADTS header at the beginning of each and every AAC packet.
 *  This is needed as MediaCodec encoder generates a packet of raw
 *  AAC data.
 *
 *  Note the packetLen must count in the ADTS header itself.
 **/
void addADTStoPacket(uint8_t *packet, int packetLen) {
  int profile = 2; // AAC LC
                   // 39=MediaCodecInfo.CodecProfileLevel.AACObjectELD;
  int freqIdx = 4; // 44.1KHz
  int chanCfg = 2; // CPE

  // fill in ADTS data
  packet[0] = 0xFF;
  packet[1] = 0xF9;
  packet[2] = ((profile - 1) << 6) + (freqIdx << 2) + (chanCfg >> 2);
  packet[3] = ((chanCfg & 3) << 6) + (packetLen >> 11);
  packet[4] = (packetLen & 0x7FF) >> 3;
  packet[5] = ((packetLen & 7) << 5) + 0x1F;
  packet[6] = 0xFC;
}

void rtp_buffered_audio_cleanup_handler(__attribute__((unused)) void *arg) {
  debug(2, "Buffered Audio Receiver Cleanup Start.");
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  close(conn->buffered_audio_socket);
  conn->buffered_audio_socket = 0;
  debug(2, "Buffered Audio Receiver Cleanup Done.");
}

void *rtp_buffered_audio_processor(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  pthread_cleanup_push(rtp_buffered_audio_cleanup_handler, arg);

  pthread_t *buffered_reader_thread = malloc(sizeof(pthread_t));
  if (buffered_reader_thread == NULL)
    debug(1, "cannot allocate a buffered_reader_thread!");
  memset(buffered_reader_thread, 0, sizeof(pthread_t));
  pthread_cleanup_push(malloc_cleanup, buffered_reader_thread);

  buffered_tcp_desc *buffered_audio = malloc(sizeof(buffered_tcp_desc));
  if (buffered_audio == NULL)
    debug(1, "cannot allocate a buffered_tcp_desc!");
  // initialise the descriptor
  memset(buffered_audio, 0, sizeof(buffered_tcp_desc));
  pthread_cleanup_push(malloc_cleanup, buffered_audio);

  if (pthread_mutex_init(&buffered_audio->mutex, NULL))
    debug(1, "Connection %d: error %d initialising buffered_audio mutex.", conn->connection_number,
          errno);
  pthread_cleanup_push(mutex_cleanup, &buffered_audio->mutex);

  if (pthread_cond_init(&buffered_audio->not_empty_cv, NULL))
    die("Connection %d: error %d initialising not_empty cv.", conn->connection_number, errno);
  pthread_cleanup_push(cv_cleanup, &buffered_audio->not_empty_cv);

  if (pthread_cond_init(&buffered_audio->not_full_cv, NULL))
    die("Connection %d: error %d initialising not_full cv.", conn->connection_number, errno);
  pthread_cleanup_push(cv_cleanup, &buffered_audio->not_full_cv);

  // initialise the buffer data structure
  buffered_audio->buffer_max_size = conn->ap2_audio_buffer_size;
  buffered_audio->buffer = malloc(conn->ap2_audio_buffer_size);
  if (buffered_audio->buffer == NULL)
    debug(1, "cannot allocate an audio buffer of %u bytes!", buffered_audio->buffer_max_size);
  pthread_cleanup_push(malloc_cleanup, buffered_audio->buffer);

  // pthread_mutex_lock(&conn->buffered_audio_mutex);
  buffered_audio->toq = buffered_audio->buffer;
  buffered_audio->eoq = buffered_audio->buffer;

  buffered_audio->sock_fd = conn->buffered_audio_socket;

  pthread_create(buffered_reader_thread, NULL, &buffered_tcp_reader, buffered_audio);
  pthread_cleanup_push(thread_cleanup, buffered_reader_thread);

  // ideas and some code from https://rodic.fr/blog/libavcodec-tutorial-decode-audio-file/
  // with thanks

  // initialize all muxers, demuxers and protocols for libavformat
  // (does nothing if called twice during the course of one program execution)
  // not needed in ffmpeg 4.0 and later...
  // av_register_all();

  AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
  if (codec == NULL) {
    debug(1, "Can't find an AAC decoder!");
  }

  AVCodecContext *codec_context = avcodec_alloc_context3(codec);
  if (codec_context == NULL) {
    debug(1, "Could not allocate audio codec context!");
  }
  // push a deallocator -- av_free(codec_context)
  pthread_cleanup_push(avcodec_alloc_context3_cleanup_handler, codec_context);

  if (avcodec_open2(codec_context, codec, NULL) < 0) {
    debug(1, "Could not open a codec into the audio codec context");
  }
  // push a closer -- avcodec_close(codec_context);
  pthread_cleanup_push(avcodec_open2_cleanup_handler, codec_context);

  AVCodecParserContext *codec_parser_context = av_parser_init(codec->id);
  if (codec_parser_context == NULL) {
    debug(1, "Can't initialise a parser context!");
  }
  // push a closer -- av_parser_close(codec_parser_context);
  pthread_cleanup_push(av_parser_init_cleanup_handler, codec_parser_context);

  AVPacket *pkt = av_packet_alloc();
  if (pkt == NULL) {
    debug(1, "Can't allocate an AV packet");
  }
  // push a deallocator -- av_packet_free(pkt);
  pthread_cleanup_push(av_packet_alloc_cleanup_handler, &pkt);

  AVFrame *decoded_frame = NULL;
  int dst_linesize;
  int dst_bufsize;

  // Prepare software resampler to convert floating point (?) to S16
  SwrContext *swr = swr_alloc();
  if (swr == NULL) {
    debug(1, "can not allocate a swr context");
  }
  // push a deallocator -- av_packet_free(pkt);
  pthread_cleanup_push(swr_alloc_cleanup_handler, &swr);

  av_opt_set_int(swr, "in_channel_layout", AV_CH_LAYOUT_STEREO, 0);
  av_opt_set_int(swr, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
  av_opt_set_int(swr, "in_sample_rate", 44100, 0);
  av_opt_set_int(swr, "out_sample_rate", 44100, 0);
  av_opt_set_sample_fmt(swr, "in_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);
  av_opt_set_sample_fmt(swr, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);
  swr_init(swr);

  uint8_t packet[16 * 1024];
  unsigned char m[16 * 1024]; // leave the first 7 bytes blank to make room for the ADTS
  uint8_t *pcm_audio = NULL;  // the S16 output
  unsigned char *data_to_process;
  ssize_t data_remaining;
  uint32_t seq_no, last_block_flushed_seq_no; // audio packet number
  uint32_t previous_seq_no = 0;
  int new_buffer_needed;

  uint32_t last_block_flushed_rtp_time;

  ssize_t nread;

  int finished = 0;
  int pcm_buffer_size =
      (1024 + 352) * 8; // This seems to be right. 8 is for 2 * 32-bit samples per frame
  uint8_t pcm_buffer[pcm_buffer_size];

  int pcm_buffer_occupancy = 0;
  int pcm_buffer_read_point = 0; // offset to where the next buffer should come from
  uint32_t pcm_buffer_read_point_rtptime = 0;

  uint64_t blocks_read = 0;
  int flush_requested = 0;
  int streaming_has_started = 0;
  do {
    int flush_newly_requested = 0;
    // are we in in flush mode, or just about to leave it?
    debug_mutex_lock(&conn->flush_mutex, 10000, 1); // 10ms is a long time to wait!
    uint32_t flushUntilSeq = conn->ap2_flush_sequence_number;
    uint32_t flushUntilTS = conn->ap2_flush_rtp_timestamp;
    // if we are in flush mode
    if (conn->ap2_flush_requested) {
      if (flush_requested == 0) {
        // here, a flush has been newly requested
        debug(1,
              "flush requested. flushUntilSeq = %u, flushUntilTS = %u, seq_no: %u, "
              "pcm_buffer_read_point_rtptime = "
              "%u, buffer_size: %u.",
              flushUntilSeq, flushUntilTS, seq_no, pcm_buffer_read_point_rtptime,
              buffered_audio->buffer_occupancy);
        last_block_flushed_rtp_time = pcm_buffer_read_point_rtptime;
        last_block_flushed_seq_no = seq_no;
        flush_newly_requested = 1;
      }
      // if we have arrived at the nominated flush block
      // (it must be the case that the block is in the pcm buffer)
      if ((blocks_read != 0) && (seq_no == flushUntilSeq)) {
        // we may need to flush the pcm_buffer up to flushUntilTS
        int32_t flush_difference = flushUntilTS - pcm_buffer_read_point_rtptime;
        if (flush_difference < 0) {
          debug(1, "flushUntilTS is before the flushUntilSeq's start TS");
        } else if (flush_difference > pcm_buffer_occupancy) {
          debug(1,
                "flushUntilTS is after the flushUntilSeq's last TS. flushUntilTS = %u, "
                "pcm_buffer_read_point_rtptime = %u, pcm_buffer_occupancy = %u.",
                flushUntilTS, pcm_buffer_read_point_rtptime, pcm_buffer_occupancy);
        } else {
          // move the buffer contents from flush_difference to pcm_buffer_occupancy
          // to the front of the pcm_buffer
          memcpy(pcm_buffer, pcm_buffer + flush_difference * 4,
                 pcm_buffer_occupancy - flush_difference * 4);
          pcm_buffer_occupancy = pcm_buffer_occupancy - flush_difference * 4;
          pcm_buffer_read_point = 0;
          debug(1, "set pcm_buffer_read_point_rtptime from %u to %u.",
                pcm_buffer_read_point_rtptime, flushUntilTS);
          pcm_buffer_read_point_rtptime = flushUntilTS;
        }
        conn->ap2_flush_requested = 0;
      } else {
        if (seq_no > flushUntilSeq) {
          debug(1,
                "we have overshot the flush ending block. flushUntilSeq is %u, this block is: %u.",
                flushUntilSeq, seq_no);
          conn->ap2_flush_requested = 0; // but conn->ap2_play_enabled will remain at zero
        }
      }
    }
    flush_requested = conn->ap2_flush_requested;

    // this isn't always the case, i.e. that play is re-enabled after a flush
    // if (flush_requested)
    //  conn->ap2_play_enabled = 0;
    // play is disabled until an anchor time is sent and play is restarted

    int play_enabled = conn->ap2_play_enabled;
    debug_mutex_unlock(&conn->flush_mutex, 3);

    // do this outside the flush mutex
    if (flush_newly_requested) {
      player_full_flush(conn);
      streaming_has_started = 0;
      pcm_buffer_occupancy = 0;
      pcm_buffer_read_point = 0;
    }

    // now, if a flush is not requested, we can do the normal stuff
    if (flush_requested == 0) {
      // is there space in the player thread's buffer system?
      unsigned int player_buffer_size, player_buffer_occupancy;
      get_audio_buffer_size_and_occupancy(&player_buffer_size, &player_buffer_occupancy, conn);
      // debug(1,"player buffer size and occupancy: %u and %u", player_buffer_size,
      // player_buffer_occupancy);
      double reqested_lead_time = 0.050; //
      if (player_buffer_occupancy >
          ((reqested_lead_time + 0.1) * 44100.0 / 352)) { // must be greater than the lead time.
        // if there is enough stuff in the player's buffer, sleep for a while and try again
        // debug(1,"sleep for 20 ms");
        usleep(20000); // wait for a while
      } else {
        if ((pcm_buffer_occupancy - pcm_buffer_read_point) >= (352 * 4)) {
          new_buffer_needed = 0;
          // send a frame to the player if allowed
          // it it's way too late, it means that a new anchor time is needed

          /*
                    uint32_t at_rtp = conn->reference_timestamp;
                    at_rtp =
                        at_rtp - (44100 * 10); // allow it to start a few seconds late, but not
             madly late int rtp_diff = pcm_buffer_read_point_rtptime - at_rtp;
          */

          if ((play_enabled) && (have_ptp_timing_information(conn) != 0)) {
            uint64_t buffer_should_be_time;
            if (frame_to_local_time(pcm_buffer_read_point_rtptime, &buffer_should_be_time, conn) ==
                0) {
              int64_t lead_time = buffer_should_be_time - get_absolute_time_in_ns();
              // debug(1,"lead time in buffered_audio is %f milliseconds.", lead_time * 0.000001);
              if ((lead_time >= (int64_t)(reqested_lead_time * 1000000000)) ||
                  (streaming_has_started == 1)) {
                if (streaming_has_started == 0)
                  debug(2, "Connection %d: buffered audio lead time is %f seconds.", conn->connection_number,
                        0.000000001 * lead_time);
                streaming_has_started = 1;
                player_put_packet(0, 0, pcm_buffer_read_point_rtptime,
                                  pcm_buffer + pcm_buffer_read_point, 352, conn);
              }

              pcm_buffer_read_point_rtptime += 352;
              pcm_buffer_read_point += 352 * 4;
            }
            // usleep(2000); // let other stuff happens
          } else {
            usleep(20000); // wait before asking if play is enabled again
          }
        } else {
          new_buffer_needed = 1;
          if (pcm_buffer_read_point != 0) {
            // debug(1,"pcm_buffer_read_point (frames): %u, pcm_buffer_occupancy (frames): %u",
            // pcm_buffer_read_point/4, pcm_buffer_occupancy/4);
            // if there is anything to move down to the front of the buffer, do it now;
            if ((pcm_buffer_occupancy - pcm_buffer_read_point) > 0) {
              // move the remaining frames down to the start of the buffer
              // debug(1,"move the remaining frames down to the start of the pcm_buffer");
              memcpy(pcm_buffer, pcm_buffer + pcm_buffer_read_point,
                     pcm_buffer_occupancy - pcm_buffer_read_point);
              pcm_buffer_occupancy = pcm_buffer_occupancy - pcm_buffer_read_point;
            } else {
              // debug(1,"nothing to move to the front of the buffer");
              pcm_buffer_occupancy = 0;
            }
            pcm_buffer_read_point = 0;
          }
        }
      }
    }
    if ((flush_requested) || (new_buffer_needed)) {

      // debug(1,"pcm_buffer_read_point (frames): %u, pcm_buffer_occupancy (frames): %u",
      // pcm_buffer_read_point/4, pcm_buffer_occupancy/4);
      // ok, so here we know we need material from the sender
      // do we will get in a packet of audio
      uint16_t data_len;
      // here we read from the buffer that our thread has been reading
      nread = lread_sized_block(buffered_audio, &data_len, sizeof(data_len));
      if (nread < 0) {
        char errorstring[1024];
        strerror_r(errno, (char *)errorstring, sizeof(errorstring));
        debug(1, "error in rtp_buffered_audio_processor %d: \"%s\". Could not recv a data_len .",
              errno, errorstring);
        // if ((config.diagnostic_drop_packet_fraction == 0.0) ||
        //     (drand48() > config.diagnostic_drop_packet_fraction)) {
      }
      data_len = ntohs(data_len);
      // debug(1,"buffered audio packet of size %u detected.", data_len - 2);
      nread = lread_sized_block(buffered_audio, packet, data_len - 2);
      // debug(1, "buffered audio packet of size %u received.", nread);
      if (nread < 0) {
        char errorstring[1024];
        strerror_r(errno, (char *)errorstring, sizeof(errorstring));
        debug(1, "error in rtp_buffered_audio_processor %d: \"%s\". Could not recv a data packet.",
              errno, errorstring);
        // if ((config.diagnostic_drop_packet_fraction == 0.0) ||
        //     (drand48() > config.diagnostic_drop_packet_fraction)) {
      } else if (nread > 0) {
        blocks_read++; // note, this doesn't mean they are valid audio blocks
        // debug(1, "Realtime Audio Receiver Packet of length %d received.", nread);
        // now get hold of its various bits and pieces
        /*
        uint8_t version = (packet[0] & 0b11000000) >> 6;
        uint8_t padding = (packet[0] & 0b00100000) >> 5;
        uint8_t extension = (packet[0] & 0b00010000) >> 4;
        uint8_t csrc_count = packet[0] & 0b00001111;
        */
        seq_no = packet[1] * (1 << 16) + packet[2] * (1 << 8) + packet[3];
        uint32_t timestamp = nctohl(&packet[4]);
        // uint32_t ssrc = nctohl(&packet[8]);
        // uint8_t marker = 0;
        // uint8_t payload_type = 0;

        if ((blocks_read != 0) && (seq_no != previous_seq_no + 1)) {
          if (previous_seq_no != 0)
            debug(1, "block discontinuity: from sequence number %u to sequence number %u.",
                  previous_seq_no, seq_no);
          if (pcm_buffer_occupancy != 0) {
            debug(1,
                  "rtptime discontinuity! Existing pcm buffer contents with timestamp "
                  "%u, seq_no %u to new block with timestamp %u, seq_no %u",
                  pcm_buffer_read_point_rtptime, previous_seq_no, timestamp, seq_no);
            pcm_buffer_occupancy = 0;
          }
          // if still playing, this is a problem -- need to reset the player
          if (play_enabled)
            player_full_flush(conn);
        }

        previous_seq_no = seq_no;

        // at this point, we can check if we can to flush this packet -- we won't have
        // to decipher it first
        // debug(1,"seq_no %u, timestamp %u", seq_no, timestamp);

        if ((flush_requested) && (seq_no < flushUntilSeq)) {
          last_block_flushed_seq_no = seq_no;
          last_block_flushed_rtp_time = timestamp;
          // debug(1, "flushing block %u, rtptime %u up to block %u, rtptime %u.", seq_no,
          // timestamp, flushUntilSeq, flushUntilTS);
        }

        if (((flush_requested) && (seq_no == flushUntilSeq)) || (new_buffer_needed)) {

          if ((flush_requested) && (seq_no == flushUntilSeq)) {
            int32_t timestamp_difference = timestamp - flushUntilTS;
            debug(1,
                  "end of flush reached. flushUntilSeq = %u, flushUntilTS = %u, last block flushed "
                  "seq_no = %u, last block flushed rtptime = %u, next block seq_no = %u, next "
                  "block timestamp = "
                  "%u, timestamp difference: %d, buffer_size: %u.",
                  flushUntilSeq, flushUntilTS, last_block_flushed_seq_no,
                  last_block_flushed_rtp_time, seq_no, timestamp, timestamp_difference,
                  buffered_audio->buffer_occupancy);
            debug(2,
                  "pcm_buffer_read_point_rtptime = %u, pcm_buffer_occupancy = %u, "
                  "pcm_buffer_read_point = %u",
                  pcm_buffer_read_point_rtptime, pcm_buffer_occupancy, pcm_buffer_read_point);
          }

          // if we are here because of a flush request, it must be the case that
          // flushing the pcm buffer wasn't enough, as the request would have been turned off by now
          // so we better indicate that the pcm buffer is empty and its contents invalid
          if (flush_requested) {
            pcm_buffer_occupancy = 0;
          }

          // decode the block and add it to or put it in the pcm buffer

          if (pcm_buffer_occupancy == 0) {
            // they should match and the read point should be zero
            if ((blocks_read != 0) && (pcm_buffer_read_point_rtptime != timestamp)) {
              debug(2, "set pcm_buffer_read_point_rtptime from %u to %u.",
                    pcm_buffer_read_point_rtptime, timestamp);
              pcm_buffer_read_point_rtptime = timestamp;
              pcm_buffer_read_point = 0;
            }
          }

          unsigned char nonce[12];
          memset(nonce, 0, sizeof(nonce));
          memcpy(nonce + 4, packet + nread - 8,
                 8); // front-pad the 8-byte nonce received to get the 12-byte nonce expected

          // https://libsodium.gitbook.io/doc/secret-key_cryptography/aead/chacha20-poly1305/ietf_chacha20-poly1305_construction
          // Note: the eight-byte nonce must be front-padded out to 12 bytes.
          unsigned long long new_payload_length = 0;
          int response = crypto_aead_chacha20poly1305_ietf_decrypt(
              m + 7,               // m
              &new_payload_length, // mlen_p
              NULL,                // nsec,
              packet + 12,      // the ciphertext starts 12 bytes in and is followed by the MAC tag,
              nread - (8 + 12), // clen -- the last 8 bytes are the nonce
              packet + 4,       // authenticated additional data
              8,                // authenticated additional data length
              nonce,
              conn->session_key); // *k
          if (response != 0) {
            debug(1, "Error decrypting audio packet %u -- packet length %d.", seq_no, nread);
          } else {
            // now pass it in to the regular processing chain

            unsigned long long max_int = INT_MAX; // put in the right format
            if (new_payload_length > max_int)
              debug(1, "Madly long payload length!");
            int payload_length = new_payload_length; // change from long long to int
            int aac_packet_length = payload_length + 7;

            // now, fill in the 7-byte ADTS information, which seems to be needed by the decoder
            // we made room for it in the front of the buffer

            addADTStoPacket(m, aac_packet_length);

            // now we are ready to send this to the decoder

            data_to_process = m;
            data_remaining = aac_packet_length;
            int ret = 0;
            // there can be more than one av packet (? terminology) in a block
            while (data_remaining > 0) {
              if (decoded_frame == NULL) {
                decoded_frame = av_frame_alloc();
                if (decoded_frame == NULL)
                  debug(1, "could not allocate av_frame");
              } else {
                ret = av_parser_parse2(codec_parser_context, codec_context, &pkt->data, &pkt->size,
                                       data_to_process, data_remaining, AV_NOPTS_VALUE,
                                       AV_NOPTS_VALUE, 0);
                if (ret < 0) {
                  debug(1, "error while parsing deciphered audio packet.");
                } else {
                  data_to_process += ret;
                  data_remaining -= ret;
                  // debug(1, "frame found");
                  // now pass each packet to be decoded
                  if (pkt->size) {
                    ret = avcodec_send_packet(codec_context, pkt);
                    if (ret < 0) {
                      debug(1, "error sending  packet to decoder");
                    } else {
                      while (ret >= 0) {
                        ret = avcodec_receive_frame(codec_context, decoded_frame);
                        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                          break;
                        else if (ret < 0) {
                          debug(1, "error %d during decoding", ret);
                        } else {
                          av_samples_alloc(&pcm_audio, &dst_linesize, codec_context->channels,
                                           decoded_frame->nb_samples, AV_SAMPLE_FMT_S16, 1);
                          // remember to free pcm_audio
                          ret = swr_convert(swr, &pcm_audio, decoded_frame->nb_samples,
                                            (const uint8_t **)decoded_frame->extended_data,
                                            decoded_frame->nb_samples);
                          dst_bufsize = av_samples_get_buffer_size(
                              &dst_linesize, codec_context->channels, ret, AV_SAMPLE_FMT_S16, 1);
                          // debug(1,"generated %d bytes of PCM", dst_bufsize);
                          // copy the PCM audio into the PCM buffer.
                          // make sure it's big enough first
                          if ((pcm_buffer_size - pcm_buffer_occupancy) < dst_bufsize) {
                            debug(1,
                                  "pcm_buffer_read_point (frames): %u, pcm_buffer_occupancy "
                                  "(frames): %u",
                                  pcm_buffer_read_point / 4, pcm_buffer_occupancy / 4);
                            pcm_buffer_size = dst_bufsize + pcm_buffer_occupancy;
                            debug(1, "fatal error! pcm buffer too small at %d bytes.",
                                  pcm_buffer_size);
                          } else {
                            memcpy(pcm_buffer + pcm_buffer_occupancy, pcm_audio, dst_bufsize);
                            pcm_buffer_occupancy += dst_bufsize;
                          }
                          // debug(1,"decoded %d samples", decoded_frame->nb_samples);
                          // memcpy(sampleBuffer,outputBuffer16,dst_bufsize);
                          av_freep(&pcm_audio);
                        }
                      }
                    }
                  }
                }
                if (decoded_frame == NULL)
                  debug(1, "decoded_frame is NULL");
                if (decoded_frame != NULL)
                  av_frame_free(&decoded_frame);
              }
            }

            // revert the state of cancellability
          }
        }
      } else {
        // nread is 0 -- the port has been closed
        // finished = 1;
        debug(1, "buffered audio port closed!");
        finished = 1;
      }
    }

  } while (finished == 0);
  debug(1, "Buffered Audio Receiver RTP thread \"normal\" exit.");
  pthread_cleanup_pop(1); // deallocate the swr
  pthread_cleanup_pop(1); // deallocate the av_packet
  pthread_cleanup_pop(1); // av_parser_init_cleanup_handler
  pthread_cleanup_pop(1); // avcodec_open2_cleanup_handler
  pthread_cleanup_pop(1); // avcodec_alloc_context3_cleanup_handler
  pthread_cleanup_pop(1); // thread creation
  pthread_cleanup_pop(1); // buffer malloc
  pthread_cleanup_pop(1); // not_full_cv
  pthread_cleanup_pop(1); // not_empty_cv
  pthread_cleanup_pop(1); // mutex
  pthread_cleanup_pop(1); // descriptor malloc
  pthread_cleanup_pop(1); // pthread_t malloc
  pthread_cleanup_pop(1); // do the cleanup.
  pthread_exit(NULL);
}

int frame_to_local_time(uint32_t timestamp, uint64_t *time, rtsp_conn_info *conn) {
  if (conn->timing_type == ts_ptp)
    return frame_to_ptp_local_time(timestamp, time, conn);
  else
    return frame_to_ntp_local_time(timestamp, time, conn);
}

int local_time_to_frame(uint64_t time, uint32_t *frame, rtsp_conn_info *conn) {
  if (conn->timing_type == ts_ptp)
    return local_ptp_time_to_frame(time, frame, conn);
  else
    return local_ntp_time_to_frame(time, frame, conn);
}

void reset_anchor_info(rtsp_conn_info *conn) {
  if (conn->timing_type == ts_ptp)
    reset_ptp_anchor_info(conn);
  else
    reset_ntp_anchor_info(conn);
}

int have_timestamp_timing_information(rtsp_conn_info *conn) {
  if (conn->timing_type == ts_ptp)
    return have_ptp_timing_information(conn);
  else
    return have_ntp_timestamp_timing_information(conn);
}

#else

int frame_to_local_time(uint32_t timestamp, uint64_t *time, rtsp_conn_info *conn) {
  return frame_to_ntp_local_time(timestamp, time, conn);
}

int local_time_to_frame(uint64_t time, uint32_t *frame, rtsp_conn_info *conn) {
  return local_ntp_time_to_frame(time, frame, conn);
}

void reset_anchor_info(rtsp_conn_info *conn) { reset_ntp_anchor_info(conn); }

int have_timestamp_timing_information(rtsp_conn_info *conn) {
  return have_ntp_timestamp_timing_information(conn);
}
#endif
