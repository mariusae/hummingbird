hummingbird - no bullshit HTTP load generator.

Options are as follows:

    hb: [-c CONCURRENCY] [-b BUCKETS] [-n COUNT] [-p NUMPROCS] [-r INTERVAL] [HOST] [PORT]

The default host is `127.0.0.1`, and the default port is `80`.

* `-c` controls concurrency. This is the number of outstanding
  requests at a given time
  
* `-b` sets the "bucket spec".  This determines how we bucket the
  measurement histograms. Set to a comma-separated list of values in
  milliseconds. For example `1,10,100,250,500` will bucket requests
  into the given amount of milliseconds.
  
* `-n` controls the total number of requests to make. Left
  unspecified, `hb` never terminates.
  
* `-r` is the reporting interval in seconds.

* `-p` controls the number of processes to fork (for multiple event
  loops). The default value is `1`.

`hb` produces output like the following:

    $ hb localhost 8686
    # ts                errors  timeout <1      <10     <100    >=100
    1286169446          0       0       1251    19      3       0
    1286169447          0       0       1621    21      1       0
    1286169448          0       0       1995    23      0       0
    1286169449          0       0       2125    29      0       0
    1286169450          0       0       2388    29      0       0
    1286169451          0       0       2380    35      0       0
    1286169452          0       0       3069    34      0       0
    1286169453          0       0       3095    33      0       0
    1286169454          0       0       3160    33      0       0

The first column is the timestamp, and the subsequent columns are
according to the specified bucketing (controlled via `-b`). This
output format is handy for analysis with the standard Unix tools. The
banner is written to `stderr`, so only the data values are emitted to
`stdout`.

`hb` will also report the total rate to `stderr` as follows:

    rate: 1986/s

# TODO

* support for constant rate load generation
* persistent connections
* should be split into two programs? load generation & http requests?
* SIGINT prints report