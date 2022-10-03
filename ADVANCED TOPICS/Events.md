## Events
Shairport Sync can run programs when certain "events" occur. The events are:
1. When Shairport Sync become `active` or `inactive`. Shairport Sync is normally in the `inactive` state when no audio is being played. When audio is sent to Shairport Sync, it will transition from `inactive` to `active`. When the audio stops, Shairport Sync will start a timer. If audio restarts before the timer reaches the value of the `active_state_timeout` configuration setting (10 seconds by default), Shairport Sync will stay `active`. However, if no more audio is received before the timer reaches the `active_state_timeout` value, Shairport Sync will transition to `inactive`. The overall effect of this is that Shairport Sync will go `active` when a track is played and will stay active in the interval between tracks, so long as the interval is less than the `active_state_timeout`.
   1. Set the `run_this_before_entering_active_state` setting to the full path name to the program to run before Shairport Sync goes `active`.
   2. Set the `run_this_after_exiting_active_state` setting to the full path name to the program to run after Shairport Sync goes `inactive`.
   3. Set the `active_state_timeout` setting to the maximum amount of time to wait for play to resume before going `inactive`.
3. When audio starts and stops playing.
   1. Set the `run_this_before_play_begins` setting to the full path name to the program to run just before Shairport Sync starts playing.
   2. Set the `run_this_after_play_ends` setting to the full path name to the program to run after Shairport Sync stops playing.
5. When the volume is adjusted. Shairport Sync can run a program whenever the volume is set or changed. You specify the full path name to the program to run using the `general` group setting `run_this_when_volume_changes`. 

Set the `wait_for_completion` value to `"yes"` for Shairport Sync to wait until the respective commands have been completed before continuing.

Note that the full path to the programs must be specified, and script files will not be executed unless they are marked as executable
and have the appropriate shebang `#!/bin/...` as the first line. (This behaviour may be different from other Shairports.)

Shairport Sync can run a program whenever the volume is set or changed. You specify it using the `general` group setting `run_this_when_volume_changes`.
This is to facilitate situations where something has to be done when the volume changes, e.g. adjust an external mixer value. Set the `wait_for_completion` value to `"yes"` for Shairport Sync to wait until the command has been completed before continuing. Again, please note that the full path to the program must be specified, and script files will not be executed unless they are marked as executable and have the appropriate shebang `#!/bin/...` as the first line.
