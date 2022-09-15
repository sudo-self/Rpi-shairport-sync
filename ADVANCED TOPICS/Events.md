## Events
Shairport Sync can run programs just before it starts to play an audio stream and just after it finishes.
You specify them using the `sessioncontrol` group settings `run_this_before_play_begins` and `run_this_after_play_ends`.
This is to facilitate situations where something has to be done before and after playing, e.g. switching on an amplifier beforehand
and switching it off afterwards.
Set the `wait_for_completion` value to `"yes"` for Shairport Sync to wait until the respective commands have been completed before continuing.

Note that the full path to the programs must be specified, and script files will not be executed unless they are marked as executable
and have the appropriate shebang `#!/bin/...` as the first line. (This behaviour may be different from other Shairports.)

Shairport Sync can run a program whenever the volume is set or changed. You specify it using the `general` group setting `run_this_when_volume_changes`.
This is to facilitate situations where something has to be done when the volume changes, e.g. adjust an external mixer value. Set the `wait_for_completion` value to `"yes"` for Shairport Sync to wait until the command has been completed before continuing. Again, please note that the full path to the program must be specified, and script files will not be executed unless they are marked as executable and have the appropriate shebang `#!/bin/...` as the first line.
