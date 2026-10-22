Upload target relay reserve
---------------------------

The historical-block serving reserve for `-maxuploadtarget` now follows BTQ's
60-second block spacing. At the start of a daily cycle, the reserve is
11.52 GB (about 10.7 GiB), ten times the previous reserve. Nodes with lower
upload targets refuse blocks older than a week from the start of the
cycle, while still serving recent full blocks. BIP37 filtered-block requests
are also refused while the historical serving limit is reached. Peers with
`download` permission remain exempt from this serving restriction.
