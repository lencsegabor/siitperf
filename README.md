siitperf
========

Siitperf is an RFC 8219 SIIT (stateless NAT64) tester written in C++ using DPDK

Introduction
------------

Siitperf implements the most important measurement procedures of RFC 8219, namely Througput, Frame Loss Rate, Latency, PDV (Packet Delay Variation). Measurement setup is shown below:

                       +--------------------+
                       |                    |
              +--------|IPvX   Tester   IPvY|<---------+
              |        |                    |          |
              |        +--------------------+          |
              |                                        |
              |        +--------------------+          |
              |        |                    |          |
              +------->|IPvX     DUT    IPvY|----------+
                       |                    |
                       +--------------------+

Where X and Y are in {4, 6}, and even though SIIT implies that X <> Y, siitperf allows X=Y, thus siitperf can also be used as a classic RFC 2544 / RFC 5180 Tester. Siitperf was designed for research purposes, and it is quite resilient and tuneable, no constant values are "wired in", rather it takes a lot of parameters either from its "siitperf.conf" configuration file, or from command line.

The package consists of three similar but different programs: siitperf-tp, siitperf-lat, siitperf-pdv. They serve the following purposes:
- siitperf-tp: Througput and Frame Loss Rate measurements
- siitperf-lat: Latency measurements
- siitperf-pdv: PDV measurements plus special Througput and Frame Loss Rate measurements checking timeout for every single frame

All three programs perform an elementary test (for 60 seconds, or whatever is set) and if iteration is needed, for example the binary search of the throughput tests, then the programs are executed several times by appropriate bash shell scripts. (The scripts are also provided.)


