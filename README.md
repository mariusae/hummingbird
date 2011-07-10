hummingbird - no bullshit HTTP load generator.

Options are as follows:

    hb: [-c CONCURRENCY] [-b BUCKETS] [-n COUNT] [-p NUMPROCS] [-r RPC] [-i INTERVAL] [HOST] [PORT]

The default host is `127.0.0.1`, and the default port is `80`.

* `-c` controls concurrency. This is the number of outstanding
  requests at a given time
  
* `-b` sets the "bucket spec".  This determines how we bucket the
  measurement histograms. Set to a comma-separated list of values in
  milliseconds. For example `1,10,100,250,500` will bucket requests
  into the given amount of milliseconds.
  
* `-n` controls the total number of requests to make. Left
  unspecified, `hb` never terminates.
  
* `-r` specifies the number of requests per connection (default is no limit)

* `-p` controls the number of processes to fork (for multiple event
  loops). The default value is `1`.
  
* `-i` specifies the reporting interval in seconds

`hb` produces output like the following:

	$ hb -n100000 -c20 localhost 8080
	# params: c=20 p=1 n=100000 r=-1
	# ts        errors   timeout  closes     <1        <10       <100     >=100    hz
	1310334247  0        0        220        22393     93        0        0        22351
	1310334248  0        0        220        22637     30        0        0        22689
	1310334249  0        0        226        22566     37        0        0        22625
	1310334250  0        0        230        22439     51        0        0        22490
	1310334250  0        0        115        9752      21        0        0        22727
	# total		100019
	# errors	0
	# timeouts	0
	# closes	1011
	# <1		99787
	# <10		232
	# <100		0
	# >=100		0
	# hz		22542

The first column is the timestamp, and the subsequent columns are
according to the specified bucketing (controlled via `-b`). This
output format is handy for analysis with the standard Unix tools. The
banner is written to `stderr`, so only the data values are emitted to
`stdout`.

# TODO

* support for constant rate load generation
* should be split into two programs? load generation & http requests?
