Upload target relay reserve
---------------------------

The historical-block serving reserve for `-maxuploadtarget` now follows BTQ's
60-second block spacing. At the start of a daily cycle, the reserve is
11.52 GB (about 10.7 GiB), ten times the previous reserve. Nodes with lower
upload targets may stop serving blocks older than a week earlier in the
cycle, while still serving recent blocks. Peers with `download` permission
remain exempt from this historical-block serving restriction.
