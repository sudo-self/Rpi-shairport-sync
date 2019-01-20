#pragma once
void activity_monitor_start();
void activity_monitor_stop();
void activity_monitor_signify_activity(int active); // 0 means inactive, non-zero means active
