# Events
Shairport Sync can run programs when certain _events_ occur. The events are:
1. When Shairport Sync goes _active_ or _inactive,_
3. When audio play starts or stops,
5. When the volume is set or changed.


### Active / Inactive Events
Shairport Sync is normally in the `inactive` state when no audio is being played.

When audio is sent to Shairport Sync, it will transition from `inactive` to `active`.

When the audio stops, Shairport Sync will start a timer. If audio restarts before the timer reaches the value of the `active_state_timeout` setting (10 seconds by default), Shairport Sync will stay `active`. However, if no more audio is received before the timer reaches the `active_state_timeout` value, Shairport Sync will transition to `inactive`.

The overall effect of this is that Shairport Sync will go `active` when a track starts and stays active in the interval between tracks, so long as the interval is less than the `active_state_timeout`. When the sequence of tracks ends, Shairport Sync will go `inactive`.

* Set the `run_this_before_entering_active_state` setting to the full path name to the program to run before Shairport Sync goes `active`.
* Set the `run_this_after_exiting_active_state` setting to the full path name to the program to run after Shairport Sync goes `inactive`.
* Set the `active_state_timeout` setting to the maximum amount of time to wait for play to resume before going `inactive`.

### Play Start / Play Stop
**Note:** Play events have been superceded by  Active/Inactive events, which works better in AirPlay 2 operation.

When audio starts, the `play begins` event occurs. When it stops, the `play ends` event occurs.

* Set the `run_this_before_play_begins` setting to the full path name to the program to run just before Shairport Sync starts playing.
* Set the `run_this_after_play_ends` setting to the full path name to the program to run after Shairport Sync stops playing. 

### Volume Adjustment
Shairport Sync can run a program whenever the volume is set or changed.

* Set `run_this_when_volume_changes` to the full path name to the program to run using the `general` group setting `run_this_when_volume_changes` _but also_ add a space character to the end of the name. This is because when a volume event occurs, Shairport Sync will append the new volume to the text you have specified in `run_this_when_volume_changes` and will then try to execute it.

For example, to use the `echo` command to log the volume setting, let's say that you have found that the full path name for `echo` is `/usr/bin/echo`, then you would specify `run_this_when_volume_is_set` as follows:
```
run_this_when_volume_is_set = "/usr/bin/echo ";
```
(Note the extra space at the end of the path name.) Suppose the volume is set or changed to `-24.6`, then Shairport Sync will execute the command `/usr/bin/echo -24.6`. (Without that extra space, it would try to execute `/usr/bin/echo24.6`.)

### Waiting for Completion

Set the `wait_for_completion` value to `"yes"` for Shairport Sync to wait until the respective commands have been completed before continuing.

### Program
The environment in which the program you specify will be very different to a login environment. In particular, the `PATH` variable will be different. This means that you can't assume that the system will look in the right directories for programs or documents. Therefore, it is _vital_ that you specify everything using a _full path name_.

For example, to get the `logger` command to log `Active Start` when Shairport Sync goes active, set `run_this_before_entering_active_state` as follows:

```
run_this_before_entering_active_state = "/usr/bin/logger \"Active Start\"";
```

You can specify a program to be executed or you can specify a script. Make sure the script file is marked as executable
and has the appropriate shebang `#!/bin/...` as the first line. Within the script, make sure all references to files are full path names.
