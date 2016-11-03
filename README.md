# KISS HTTPD Linux Daemon

This repository contains simple example of httpd daemon for Linux OS.
This repository also contains examples of starting scripts. 

## Requirements

To build example of the daemon you have to have following tools

* CMake
* GCC/CLang

## Build

To build example of daemon you have to type following commands:

    git clone https://github.com/jirihnidek/kiss-httpd
    cd daemon
    mkdir build
    cd build
    cmake -DCMAKE_INSTALL_PREFIX=/usr ../
    make
    sudo make install

## Usage

You can test running daemon from command line:

    ./bin/kiss-httpd

But running the app in this way is not running running daemon. Let
have a look at command line parameters and arguments

    Usage: ./bin/kiss-httpd [OPTIONS]

     Options:
      -h --help                 Print this help
      -c --conf_file filename   Read configuration from the file
      -t --test_conf filename   Test configuration file
      -l --log_file  filename   Write logs to the file
      -d --daemon               Daemonize this application
      -p --pid_file  filename   PID file used by daemonized app
      -f --file_html filename   HTML file

When you will run `./bin/kiss-httpd` with parameter `--daemon` or `-d`, then
it will become real UNIX daemon. But this is not the way, how UNIX daemons
are started nowdays. Some init scripts or service files are used for
this purpose.

When you use Linux distribution using systemd, then you can try start daemon using

    systemctl start simple-daemon
    systemctl status simple-daemon
    systemctl reload simple-daemon
    systemctl stop simple-daemon

> Note: The unit file `kiss-httpd.service` is copied to the directory
`/usr/lib/systemd/system` during installation using `make install` command.

When you use RedHat 4/5/6 or CentOS, then you can try to use init script:

    cp kiss-httpd.init /etc/rc.d/init.d/kiss-httpd

Then it should be possible to control daemon using:

    service kiss-httpd start
    service kiss-httpd status
    service kiss-httpd reload
    service kiss-httpd stop
