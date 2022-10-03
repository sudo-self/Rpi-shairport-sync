## Events
Shairport Sync can run programs when certain "events" occur. The events are:
1. Shairport Sync goes `active`,
2. Shairport Sync goes `inactive`,
3. Play starts,
4. Play stops,
5. Volume changes
6
### Active / Inactive Events
Shairport Sync is normally in the `inactive` state when no audio is being played.

When audio is sent to Shairport Sync, it will transition from `inactive` to `active`.

When the audio stops, Shairport Sync will start a timer. If audio restarts before the timer reaches the value of the `active_state_timeout` configuration setting (10 seconds by default), Shairport Sync will stay `active`. However, if no more audio is received before the timer reaches the `active_state_timeout` value, Shairport Sync will transition to `inactive`.

The overall effect of this is that Shairport Sync will go `active` when a track is played and will stay active in the interval between tracks, so long as the interval is less than the `active_state_timeout`. When the sequence of tracks ends. Shairport Sync will go `inactive`.

   1. Set the `run_this_before_entering_active_state` setting to the full path name to the program to run before Shairport Sync goes `active`.
   2. Set the `run_this_after_exiting_active_state` setting to the full path name to the program to run after Shairport Sync goes `inactive`.
   3. Set the `active_state_timeout` setting to the maximum amount of time to wait for play to resume before going `inactive`.

### Play Start / Play Stop
When audio starts, the `play begins` event occurs. When it stops, the `play ends` event occurs.
   1. Set the `run_this_before_play_begins` setting to the full path name to the program to run just before Shairport Sync starts playing.
   2. Set the `run_this_after_play_ends` setting to the full path name to the program to run after Shairport Sync stops playing.
  
**Note** Play events havbe been superceded by  Active/Inactive events -- they are less "noisy" and work better in AirPlay 2 operation.

### Volume Adjustment
Shairport Sync can run a program whenever the volume is set or changed. You specify the full path name to the program to run using the `general` group setting `run_this_when_volume_changes` but you should also add a space character to the end of the name. This is because when a volume event occurs, Shairport Sync will append the new volume to the text you have specified in `run_this_when_volume_changes` and will then try to execute it.

For example, if the program you wish to execute is `usr/local/vc` and the new volume is `-24.6`, then, if you have specified `run_this_when_volume_changes` to be `usr/local/vc ` (note the extra space at then end), then Shairport Sync will execute the command `usr/local/vc -24.6`. (Without the extra space at the end, it would look like this: `usr/local/vc24.6`.)

### Waiting for Completion

Set the `wait_for_completion` value to `"yes"` for Shairport Sync to wait until the respective commands have been completed before continuing.

### Program
The environment in which the program you specify will be very different to a login environment. In particular, the `PATH` variable will be different. This means that you can't assume that the system will look in the right directories for programs or documents. Theremore, it is _vital_ that you specify everything using a _full path name_.

You can specify a normal program to be executed or you can specify a script. Make sure the script file is marked as executable
and has the appropriate shebang `#!/bin/...` as the first line. And within the script, make sure all references to files are full path names also.
