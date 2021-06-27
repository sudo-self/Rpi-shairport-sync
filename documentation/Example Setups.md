Example Setups
===

Amazon Echo Devices
---
Since some Amazon Echo devices have either a 3.5mm input or can be used as a bluetooth speaker, Shairport Sync can be connected allowing you to Airplay to these speakers. Currently using the 3.5mm jack is the most reliable method - bluetooth connectivity can have an unreliable delay. Check first to see if your Echo device supports either of these. Volume control of the Echo device can be added via Home Assistant and a MQTT broker. Below goes through some example setups.
### _Raspberry Pi --> Echo 3.5mm Input_
* Set the Echo device to use the 3.5mm jack as an input.
* Via the Shairport Sync config, set the correct `output_device` and `mixer_control_name` for your configuration. Likely to be
`output_device = "hw:1";` and `mixer_control_name = "Headphone";`.
* At this point, you should be able to hear music via the Echo. Remember that so far, the volume of the Echo and Shairport Sync are _independent_. If you prefer, you can disable any volume control in Shairport Sync by setting `ignore_volume_control = "yes";`.
* Amazon Echo devices appear to add a bit of latency, currently this is thought to be 0.12 seconds. This can be countered by setting the following: `audio_backend_latency_offset_in_seconds = -0.12;`. This was measured _very_ crudely using Audacity so could be wrong. If anyone is able to determine the latency more accurately, please open an issue or pull request so these documents can be updated.
* You should now be able to use your Echo device in sync with other AirPlay devices!

### _Raspberry Pi --> Echo as Bluetooth Speaker (Docker Version)_
* 

### _Raspberry Pi --> Echo with Volume Control_
* Before continuing with volume control via Home Assistant, please note that currently only one way volume control is tested - Shairport Sync to the Echo device. This means if the volume of the Echo is changed (e.g. by voice control or by the physical controls on the device), this will not be reflected in the volume of Shairport Sync and will be overwritten the next time Shairport Sync's volume is changed.
* This setup requires Home Assistant and a MQTT broker. The basic idea is that Shairport Sync sends a MQTT message containing the new volume amount to the MQTT broker when the volume is changed, this is then picked up by Home Assistant (which is monitoring the MQTT broker). Home Assistant will then send a command to the Amazon Echo device to change the volume.
