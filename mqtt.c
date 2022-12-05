#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

#include "common.h"
#include "player.h"
#include "rtsp.h"

#include "rtp.h"

#include "dacp.h"
#include "metadata_hub.h"
#include "mqtt.h"
#include <mosquitto.h>

// this holds the mosquitto client
struct mosquitto *global_mosq = NULL;
char *topic = NULL;
int connected = 0;

// mosquitto logging
void _cb_log(__attribute__((unused)) struct mosquitto *mosq, __attribute__((unused)) void *userdata,
             int level, const char *str) {
  switch (level) {
  case MOSQ_LOG_DEBUG:
    debug(3, str);
    break;
  case MOSQ_LOG_INFO:
    debug(3, str);
    break;
  case MOSQ_LOG_NOTICE:
    debug(3, str);
    break;
  case MOSQ_LOG_WARNING:
    inform(str);
    break;
  case MOSQ_LOG_ERR: {
    die("MQTT: Error: %s\n", str);
  }
  }
}

// mosquitto message handler
void on_message(__attribute__((unused)) struct mosquitto *mosq,
                __attribute__((unused)) void *userdata, const struct mosquitto_message *msg) {

  // null-terminate the payload
  char payload[msg->payloadlen + 1];
  memcpy(payload, msg->payload, msg->payloadlen);
  payload[msg->payloadlen] = 0;

  debug(2, "[MQTT]: received Message on topic %s: %s\n", msg->topic, payload);

  // All recognized commands
  char *commands[] = {"command",    "beginff",       "beginrew",   "mutetoggle", "nextitem",
                      "previtem",   "pause",         "playpause",  "play",       "stop",
                      "playresume", "shuffle_songs", "volumedown", "volumeup",   NULL};

  int it = 0;

  // send command if it's a valid one
  while (commands[it] != NULL) {
    if ((size_t)msg->payloadlen >= strlen(commands[it]) &&
        strncmp(msg->payload, commands[it], strlen(commands[it])) == 0) {
      debug(2, "[MQTT]: DACP Command: %s\n", commands[it]);
      send_simple_dacp_command(commands[it]);
      break;
    }
    it++;
  }
}

void on_disconnect(__attribute__((unused)) struct mosquitto *mosq,
                   __attribute__((unused)) void *userdata, __attribute__((unused)) int rc) {
  connected = 0;
  debug(2, "[MQTT]: disconnected");
}

void on_connect(struct mosquitto *mosq, __attribute__((unused)) void *userdata,
                __attribute__((unused)) int rc) {
  connected = 1;
  debug(2, "[MQTT]: connected");

  // subscribe if requested
  if (config.mqtt_enable_remote) {
    char remotetopic[strlen(config.mqtt_topic) + 8];
    snprintf(remotetopic, strlen(config.mqtt_topic) + 8, "%s/remote", config.mqtt_topic);
    mosquitto_subscribe(mosq, NULL, remotetopic, 0);
  }
}

// helper function to publish under a topic and automatically append the main topic
void mqtt_publish(char *topic, char *data_in, uint32_t length_in) {
  char *data = data_in;
  uint32_t length = length_in;

  if ((length == 0) && (config.mqtt_empty_payload_substitute != NULL)) {
    length = strlen(config.mqtt_empty_payload_substitute);
    data = config.mqtt_empty_payload_substitute;
  }

  char fulltopic[strlen(config.mqtt_topic) + strlen(topic) + 3];
  snprintf(fulltopic, strlen(config.mqtt_topic) + strlen(topic) + 2, "%s/%s", config.mqtt_topic,
           topic);
  debug(2, "[MQTT]: publishing under %s", fulltopic);

  int rc;
  if ((rc = mosquitto_publish(global_mosq, NULL, fulltopic, length, data, 0, 0)) !=
      MOSQ_ERR_SUCCESS) {
    switch (rc) {
    case MOSQ_ERR_NO_CONN:
      debug(1, "[MQTT]: Publish failed: not connected to broker");
      break;
    default:
      debug(1, "[MQTT]: Publish failed: unknown error");
      break;
    }
  }
}

// handler for incoming metadata
void mqtt_process_metadata(uint32_t type, uint32_t code, char *data, uint32_t length) {
  if (global_mosq == NULL || connected != 1) {
    debug(3, "[MQTT]: Client not connected, skipping metadata handling");
    return;
  }
  if (config.mqtt_publish_raw) {
    uint32_t val;
    char topic[] = "____/____";

    val = htonl(type);
    memcpy(topic, &val, 4);
    val = htonl(code);
    memcpy(topic + 5, &val, 4);
    mqtt_publish(topic, data, length);
  }
  if (config.mqtt_publish_parsed) {
    if (type == 'core') {
      int32_t r;
      uint64_t trackid;
      char trackidstring[32];

      switch (code) {
      case 'asar':
        mqtt_publish("artist", data, length);
        break;
      case 'asal':
        mqtt_publish("album", data, length);
        break;
      case 'asfm':
        mqtt_publish("format", data, length);
        break;
      case 'asgn':
        mqtt_publish("genre", data, length);
        break;
      case 'minm':
        mqtt_publish("title", data, length);
        break;
      case 'mper':
        trackid = *(uint64_t *)(data);
        r = snprintf(trackidstring, sizeof(trackidstring), "%" PRIX64 "", trackid);
        mqtt_publish("track_id", trackidstring, r);
      }
    } else if (type == 'ssnc') {
      switch (code) {
      case 'abeg':
        mqtt_publish("active_start", data, length);
        break;
      case 'acre':
        mqtt_publish("active_remote_id", data, length);
        break;
      case 'aend':
        mqtt_publish("active_end", data, length);
        break;
      case 'asal':
        mqtt_publish("songalbum", data, length);
        break;
      case 'asdk':
        mqtt_publish("songdatakind", data,
                     length); // 0 seem to be a timed item, 1 an untimed stream
        break;
      case 'clip':
        mqtt_publish("client_ip", data, length);
        break;
      case 'cdid':
        mqtt_publish("client_device_id", data, length);
        break;
      case 'cmac':
        mqtt_publish("client_mac_address", data, length);
        break;
      case 'cmod':
        mqtt_publish("client_model", data, length);
        break;
      case 'daid':
        mqtt_publish("dacp_id", data, length);
        break;
      case 'phbt':
        mqtt_publish("frame_position_and_time", data, length);
        break;
      case 'phb0':
        mqtt_publish("first_frame_position_and_time", data, length);
        break;
      case 'ofmt':
        mqtt_publish("output_format", data, length);
        break;
      case 'ofps':
        mqtt_publish("output_frame_rate", data, length);
        break;
      case 'pbeg':
        mqtt_publish("play_start", data, length);
        break;
      case 'pend':
        mqtt_publish("play_end", data, length);
        break;
      case 'pfls':
        mqtt_publish("play_flush", data, length);
        break;
      case 'PICT':
        if (config.mqtt_publish_cover) {
          mqtt_publish("cover", data, length);
        }
        break;
      case 'prsm':
        mqtt_publish("play_resume", data, length);
        break;
      case 'pvol':
        mqtt_publish("volume", data, length);
        break;
      case 'snam':
        mqtt_publish("client_name", data, length);
        break;
      case 'styp':
        mqtt_publish("stream_type", data, length);
        break;
      case 'svip':
        mqtt_publish("server_ip", data, length);
        break;
      case 'svna':
        mqtt_publish("service_name", data, length);
        break;
      }
    }
  }

  return;
}

int initialise_mqtt() {
  debug(1, "Initialising MQTT");
  if (config.mqtt_hostname == NULL) {
    debug(1, "[MQTT]: Not initialized, as the hostname is not set");
    return 0;
  }
  int keepalive = 60;
  mosquitto_lib_init();
  if (!(global_mosq = mosquitto_new(config.service_name, true, NULL))) {
    die("[MQTT]: FATAL: Could not create mosquitto object! %d\n", global_mosq);
  }

  if (config.mqtt_cafile != NULL || config.mqtt_capath != NULL || config.mqtt_certfile != NULL ||
      config.mqtt_keyfile != NULL) {
    if (mosquitto_tls_set(global_mosq, config.mqtt_cafile, config.mqtt_capath, config.mqtt_certfile,
                          config.mqtt_keyfile, NULL) != MOSQ_ERR_SUCCESS) {
      die("[MQTT]: TLS Setup failed");
    }
  }

  if (config.mqtt_username != NULL || config.mqtt_password != NULL) {
    if (mosquitto_username_pw_set(global_mosq, config.mqtt_username, config.mqtt_password) !=
        MOSQ_ERR_SUCCESS) {
      die("[MQTT]: Username/Password set failed");
    }
  }
  mosquitto_log_callback_set(global_mosq, _cb_log);

  if (config.mqtt_enable_remote) {
    mosquitto_message_callback_set(global_mosq, on_message);
  }

  mosquitto_disconnect_callback_set(global_mosq, on_disconnect);
  mosquitto_connect_callback_set(global_mosq, on_connect);
  if (mosquitto_connect(global_mosq, config.mqtt_hostname, config.mqtt_port, keepalive)) {
    inform("[MQTT]: Could not establish a mqtt connection");
  }
  if (mosquitto_loop_start(global_mosq) != MOSQ_ERR_SUCCESS) {
    inform("[MQTT]: Could start MQTT Main loop");
  }

  return 0;
}
