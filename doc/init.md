Sample init scripts and service configuration for btqd
==========================================================

Sample scripts and configuration files for systemd, Upstart and OpenRC
can be found in the contrib/init folder.

    contrib/init/btqd.service:    systemd service unit configuration
    contrib/init/btqd.openrc:     OpenRC compatible SysV style init script
    contrib/init/btqd.openrcconf: OpenRC conf.d file
    contrib/init/btqd.conf:       Upstart service configuration file
    contrib/init/btqd.init:       CentOS compatible SysV style init script

Service User
---------------------------------

All three Linux startup configurations assume the existence of a "btq" user
and group.  They must be created before attempting to use these scripts.
The macOS configuration assumes btqd will be set up for the current user.

Configuration
---------------------------------

Running btqd as a daemon does not require any manual configuration. You may
set the `rpcauth` setting in the `btq.conf` configuration file to override
the default behaviour of using a special cookie for authentication.

This password does not have to be remembered or typed as it is mostly used
as a fixed token that btqd and client programs read from the configuration
file, however it is recommended that a strong and secure password be used
as this password is security critical to securing the wallet should the
wallet be enabled.

If btqd is run with the "-server" flag (set by default), and no rpcpassword is set,
it will use a special cookie file for authentication. The cookie is generated with random
content when the daemon starts, and deleted when it exits. Read access to this file
controls who can access it through RPC.

By default the cookie is stored in the data directory, but it's location can be overridden
with the option '-rpccookiefile'.

This allows for running btqd without having to do any manual configuration.

`conf`, `pid`, and `wallet` accept relative paths which are interpreted as
relative to the data directory. `wallet` *only* supports relative paths.

For an example configuration file that describes the configuration settings,
see `share/examples/btq.conf`.

Paths
---------------------------------

### Linux

All three configurations assume several paths that might need to be adjusted.

    Binary:              /usr/bin/btqd
    Configuration file:  /etc/btq/btq.conf
    Data directory:      /var/lib/btqd
    PID file:            /var/run/btqd/btqd.pid (OpenRC and Upstart) or
                         /run/btqd/btqd.pid (systemd)
    Lock file:           /var/lock/subsys/btqd (CentOS)

The PID directory (if applicable) and data directory should both be owned by the
btq user and group. It is advised for security reasons to make the
configuration file and data directory only readable by the btq user and
group. Access to btq-cli and other btqd rpc clients can then be
controlled by group membership.

NOTE: When using the systemd .service file, the creation of the aforementioned
directories and the setting of their permissions is automatically handled by
systemd. Directories are given a permission of 710, giving the btq group
access to files under it _if_ the files themselves give permission to the
btq group to do so. This does not allow
for the listing of files under the directory.

NOTE: It is not currently possible to override `datadir` in
`/etc/btq/btq.conf` with the current systemd, OpenRC, and Upstart init
files out-of-the-box. This is because the command line options specified in the
init files take precedence over the configurations in
`/etc/btq/btq.conf`. However, some init systems have their own
configuration mechanisms that would allow for overriding the command line
options specified in the init files (e.g. setting `BTQD_DATADIR` for
OpenRC).

### macOS

    Binary:              /usr/local/bin/btqd
    Configuration file:  ~/Library/Application Support/BTQ/btq.conf
    Data directory:      ~/Library/Application Support/BTQ
    Lock file:           ~/Library/Application Support/BTQ/.lock

Installing Service Configuration
-----------------------------------

### systemd

Installing this .service file consists of just copying it to
/usr/lib/systemd/system directory, followed by the command
`systemctl daemon-reload` in order to update running systemd configuration.

To test, run `systemctl start btqd` and to enable for system startup run
`systemctl enable btqd`

NOTE: When installing for systemd in Debian/Ubuntu the .service file needs to be copied to the /lib/systemd/system directory instead.

### OpenRC

Rename btqd.openrc to btqd and drop it in /etc/init.d.  Double
check ownership and permissions and make it executable.  Test it with
`/etc/init.d/btqd start` and configure it to run on startup with
`rc-update add btqd`

### Upstart (for Debian/Ubuntu based distributions)

Upstart is the default init system for Debian/Ubuntu versions older than 15.04. If you are using version 15.04 or newer and haven't manually configured upstart you should follow the systemd instructions instead.

Drop btqd.conf in /etc/init.  Test by running `service btqd start`
it will automatically start on reboot.

NOTE: This script is incompatible with CentOS 5 and Amazon Linux 2014 as they
use old versions of Upstart and do not supply the start-stop-daemon utility.

### CentOS

Copy btqd.init to /etc/init.d/btqd. Test by running `service btqd start`.

Using this script, you can adjust the path and flags to the btqd program by
setting the BTQD and FLAGS environment variables in the file
/etc/sysconfig/btqd. You can also use the DAEMONOPTS environment variable here.

### macOS

Copy org.btq.btqd.plist into ~/Library/LaunchAgents. Load the launch agent by
running `launchctl load ~/Library/LaunchAgents/org.btq.btqd.plist`.

This Launch Agent will cause btqd to start whenever the user logs in.

NOTE: This approach is intended for those wanting to run btqd as the current user.
You will need to modify org.btq.btqd.plist if you intend to use it as a
Launch Daemon with a dedicated btq user.

Auto-respawn
-----------------------------------

Auto respawning is currently only configured for Upstart and systemd.
Reasonable defaults have been chosen but YMMV.
