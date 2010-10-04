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
