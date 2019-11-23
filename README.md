siitperf
========

Siitperf is an RFC 8219 SIIT (stateless NAT64) tester written in C++ using DPDK

Introduction
------------

Siitperf implements the most important measurement procedures of RFC 8219, namely Througput, Frame Loss Rate, Latency, PDV (Packet Delay Variation). Measurement setup is shown below:

                       +--------------------+
                       |                    |
              +--------|IPvX   Tester   IPvY|<--------+
              |        |                    |         |
              |        +--------------------+         |
              |                                       |
              |        +--------------------+         |
              |        |                    |         |
              +------->|IPvX     DUT    IPvY|---------+
                       |                    |
                       +--------------------+

Where X and Y are in {4, 6}, and even though SIIT implies that X <> Y, siitperf allows X=Y, thus siitperf can also be used as a classic RFC 2544 / RFC 5180 Tester. Siitperf was designed for research purposes, and it is quite resilient and tuneable, no constant values are "wired in", rather it takes a lot of parameters either from its "siitperf.conf" configuration file, or from command line.

The package consists of three similar but different programs: siitperf-tp, siitperf-lat, siitperf-pdv. They serve the following purposes:
- siitperf-tp: Througput and Frame Loss Rate measurements
- siitperf-lat: Latency measurements
- siitperf-pdv: PDV measurements plus special Througput and Frame Loss Rate measurements checking timeout for every single frame

All three programs perform an elementary test (for 60 seconds, or whatever is set) and if iteration is needed, for example the binary search of the throughput tests, then the programs are executed several times by appropriate bash shell scripts. (The scripts are also provided.)



Operation
---------

The settings for the "siitperf.conf" configuration file demonstrated on and example of benchmarking a stateless NAT64 gateway, where IPv6 and IPv4 are on the left and right side, respectively, and EAM (Explicit Address Mapping) is used. Test and traffic setup is shown below. Note that addresses i parenthesis are NOT assigned to the interfaces, they are written there for the convenience of the reader only.

                                               2001:2:0:8000::2/64
         2001:2::2/64  +--------------------+  198.19.0.2/24
         (198.18.0.2)  |                    |  (2001:2:0:1000::2)
              +--------|IPv6   Tester   IPv4|<--------+
              |        |                    |         |
              |        +--------------------+         |
              |                                       |
              |        +--------------------+         |
              |        |                    |         |
              +------->|IPv6     DUT    IPv4|---------+
         2001:2::1/64  |                    |  198.19.0.1/24
         (198.18.0.1)  +--------------------+  (2001:2:0:1000::1)
                                               2001:2:0:8000::1/64

Then "siitperf.conf" has the following content:
	
	IP-L-Vers 6 # Left Sender's IP version for foreground traffic
	IP-R-Vers 4 # Right Sender's IP version for foreground traffic
	
	IPv6-L-Real 2001:2:0:0::2
	IPv6-L-Virt :: # currently not used
	IPv6-R-Real 2001:2:0:8000::2
	IPv6-R-Virt 2001:2:0:1000::2
	IPv4-L-Real 0.0.0.0 # currently unused
	IPv4-L-Virt 198.18.0.2
	IPv4-R-Real 198.19.0.2
	IPv4-R-Virt 0.0.0.0 # currently unused
	
	MAC-L-Tester a0:36:9f:c5:fa:1c # StarBED p094 enp5s0f0
	MAC-R-Tester a0:36:9f:c5:fa:1e # StarBED p094 enp5s0f1
	MAC-L-DUT a0:36:9f:c5:e6:58 # StarBED p095 enp5s0f0
	MAC-R-DUT a0:36:9f:c5:e6:5a # StarBED p095 enp5s0f1
	
	Forward 1 # left to right direction is active
	Reverse 1 # right to left direction is active
	Promisc 0 # no promiscuous mode for receiving
	
	Num-L-Nets 1 # Use only a single src. and dst. address pair
	Num-R-Nets 1 # Max. 256 destination networks are supported
	
	CPU-L-Send 2 # Left Sender runs on this core
	CPU-R-Recv 4 # Right Receiver runs on this core
	CPU-R-Send 6 # Right Sender runs on this core
	CPU-L-Recv 8 # Left Receiver runs on this core
	
	MEM-Channels 2 # Number of Memory Channels
	

All three programs use the same "siitperf.conf" file, and their command line parametes are also very similar. They use the following ones:

	./build/siitperf-tp <IPv6 size> <rate> <duration> <global timeout> <n> <m>
	./build/siitperf-lat <IPv6 size> <rate> <duration> <global timeout> <n> <m> <delay> <timestamps>
	./build/siitperf-pdv <IPv6 size> <rate> <duration> <global timeout> <n> <m> <frame timeout>

The command line parameters are to be interpreted as follows:

__IPv6 size__: IPv6 frame size (in bytes, 84-1518), IPv4 frames are automatically 20 bytes shorter

__rate__: frame rate (in frames per second)

__duration__: duration of testing (in seconds, 1-3600)

__global timeout__: global timeout (in milliseconds), the tester stops receiving, when this global timeout elapsed after sending finished

__n__ and __m__: they are two relative prime numbers for specifying the proportion of foreground and background traffic (see below).
Traffic proportion is expressed by two relative prime numbers n and m, where m packets form every n packets belong to the foreground traffic and the rest (n-m) packets belong to the background traffi.

Besides the parameters above, which are common for all tester programs, siitperf-lat uses two further ones:

__delay__: delay before the first frame with timestamp is sent (in seconds, 0-3600)

__timestamps__: number of frames with timestamp (1-50,000)

And siitperf-pdv uses the following one:

__frame timeout__: frame timeout (in milliseconds). If the value of this parameter is 0, then proper PDV measurement is done. If the value of this parameter is higher than zero, then no PDV measurement is done, rather a special throughput (or frame loss rate) measurement is performed, where the tester checks this timeout for each frame individually: if the measured delay of a frame is longer than the timeout, then the frame is reclassified as lost. 

We note that the specified frames size always interpreted as IPv6 frame sizes, even if pure IPv4 measurements are done (both sides are configured as IPv4 and there is no backround traffic), and in this case the allowed range is 84-1538, to be able to use 84-1518 bytes long IPv4 frames.

The execution of the measurements are supported by the following scripts:

__binary-rate-alg.sh__: Implements a binary search for througput measurements using siitperf-tp.

__frame-loss.sh-scan__: Performs frame loss rate measurements using siitperf-tp. The frames rates to be tested can be specified by the range and stepping. The frame sizes to be tested may also be listed.

__latency.sh__: Performs latency measurements using siitperf-lat.

__pdv.sh__: Performs PDV  measurements using siitperf-pdv.

__binary-rate-alg.sh-pdv__:  Implements a binary search for special througput measurements using siitperf-pdv.

Warning: the scripts were written for personal use of the author of siitperf at the NICT StarBED environment. They are included to be rather samples than ready to use scripts for other users. They should be read and understood before use.
