siitperf
========

Siitperf is an RFC 8219 compliant SIIT (stateless NAT64) tester written in C++ using DPDK, and it can be used under the Linux operating system.

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

Where X and Y are in {4, 6}, and even though SIIT implies that X != Y, siitperf allows X==Y, thus siitperf can also be used as a classic RFC 2544 / RFC 5180 Tester. Siitperf was designed for research purposes, and it is quite resilient and tuneable, no constant values are "wired in", rather it takes a lot of parameters either from its "siitperf.conf" configuration file, or from command line.

We note that testing with bidirectional traffic is a requirement, testing with unidirectional traffic is optional.

The package consists of three similar but different programs: siitperf-tp, siitperf-lat, siitperf-pdv. They serve the following purposes:
- siitperf-tp: Througput and Frame Loss Rate measurements
- siitperf-lat: Latency measurements
- siitperf-pdv: PDV measurements plus special Througput and Frame Loss Rate measurements checking timeout for every single frame

All three programs perform an elementary test (for 60 seconds, or whatever is set) and if iteration is needed, for example the binary search of the throughput tests, then the programs are executed several times by appropriate bash shell scripts. (The scripts are also provided.)



Operation
---------

The settings for the "siitperf.conf" configuration file demonstrated on an example of benchmarking a stateless NAT64 gateway, where IPv6 and IPv4 are assigned to the left and right side interfaces of the devices, respectively, and EAM (Explicit Address Mapping) is used. Test and traffic setup is shown below. Note that addresses in parenthesis are NOT assigned to the interfaces, they are written there for the convenience of the reader only.

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


              Explicit Address Mapping Table of the DUT:

              +----------------+----------------------+
              | IPv4 Prefix    | IPv6 Prefix          |
              +----------------+----------------------+
              | 198.18.0.0/24  | 2001:2::/120         |
              | 198.19.0.0/24  | 2001:2:0:1000::/120  |
              +----------------+----------------------+

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

	# parameters for RFC 4814 random port feature

	Fwd-var-sport 3 # Does source port vary? 0: fix, 1: increase, 2: decrease, 3: random
	Fwd-var-dport 3 # Does destination port vary? 0: fix, 1: increase, 2: decrease, 3: random
	Fwd-sport-min 1024
	Fwd-sport-max 65535
	Fwd-dport-min 1
	Fwd-dport-max 49151
	Rev-var-sport 3 # Does source port vary? 0: fix, 1: increase, 2: decrease, 3: random
	Rev-var-dport 3 # Does destination port vary? 0: fix, 1: increase, 2: decrease, 3: random
	Rev-sport-min 1024
	Rev-sport-max 65535
	Rev-dport-min 1
	Rev-dport-max 49151
	

All three programs use the same "siitperf.conf" file, and their command line parametes are also very similar. They use the following ones:

	./build/siitperf-tp <IPv6 size> <rate> <duration> <global timeout> <n> <m>
	./build/siitperf-lat <IPv6 size> <rate> <duration> <global timeout> <n> <m> <delay> <timestamps>
	./build/siitperf-pdv <IPv6 size> <rate> <duration> <global timeout> <n> <m> <frame timeout>

The command line parameters are to be interpreted as follows:

__IPv6 size__: IPv6 frame size (in bytes, 84-1518), IPv4 frames are automatically 20 bytes shorter

__rate__: frame rate (in frames per second)

__duration__: duration of testing (in seconds, 1-3600)

__global timeout__: global timeout (in milliseconds), the tester stops receiving, when this global timeout elapsed after sending finished

__n__ and __m__: they are two relative prime numbers for specifying the proportion of foreground and background traffic: m packets form every n packets belong to the foreground traffic and the rest (n-m) packets belong to the background traffic.

Besides the parameters above, which are common for all three tester programs, siitperf-lat uses two further ones:

__delay__: delay before the first frame with timestamp is sent (in seconds, 0-3600)

__timestamps__: number of frames with timestamp (1-50,000)

And siitperf-pdv uses the following one:

__frame timeout__: frame timeout (in milliseconds). If the value of this parameter is 0, then proper PDV measurement is done. If the value of this parameter is higher than zero, then no PDV measurement is done, rather a special throughput (or frame loss rate) measurement is performed, where the tester checks this timeout for each frame individually: if the measured delay of a frame is longer than the timeout, then the frame is reclassified as lost. 

We note that the specified frames size always interpreted as IPv6 frame size, even if pure IPv4 measurements are done (both sides are configured as IPv4 and there is no backround traffic), and in this case the allowed range is 84-1538, to be able to use 64-1518 bytes long IPv4 frames.

The execution of the measurements are supported by the following scripts:

__binary-rate-alg.sh__: Implements a binary search for througput measurements using siitperf-tp.

__frame-loss.sh-scan__: Performs frame loss rate measurements using siitperf-tp. The frames rates to be tested can be specified by the range and stepping. The frame sizes to be tested may also be listed.

__latency.sh__: Performs latency measurements using siitperf-lat.

__pdv.sh__: Performs PDV  measurements using siitperf-pdv.

__binary-rate-alg.sh-pdv__:  Implements a binary search for special througput measurements using siitperf-pdv.

Warning: the scripts were written for personal use of the author of siitperf at the NICT StarBED environment. They are included to be rather samples than ready to use scripts for other users. They should be read and understood before use.

Hardware and Software Requirements
----------------------------------

As siitperf uses DPDK (Intel Data Plane Development Kit), a requirement is to use DPDK compatible hardware as Tester.
The list of supported NICs is available from: https://core.dpdk.org/supported/ 

As for time measurements, siitperf relies on the rte_rdtsc() DPDK function, which executes the RDTSC instuction of Intel CPUs. RDTSC is also implemented by several AMD CPUs. To produce reliable results, the CPU needs to support constant TSC. (In Intel terminology, the CPU needs to have the "constant_tsc" flag.)

Although siitperf is likely to run on most Linux disributions, we have tested it under Debian 9.9 only, and our DPDK version was: 16.11.9-1+deb9u1.

Siitperf uses a separate core for sending and receiving in each directions, thus it requires 4 CPU cores for bidirectional traffic besides the main core used for executing the main program. The 4 cores should be reserved at boot time to avoid interference with other programs. We used the following line in "/etc/default/grub":

	GRUB_CMDLINE_LINUX_DEFAULT="quiet isolcpus=2,4,6,8 hugepagesz=1G hugepages=32"

As it can be also seen, we used 1GB hugepages. If your CPU does not have the "pdpe1gb" flag, but it has the "pse" flag, then 2MB hugepages will still do. (We have also sortly tested it.) Our relevant setting for 1GB hugepages in the "/etc/fstab" file was:

	nodev   /mnt/huge_1GB hugetlbfs pagesize=1GB 0 0

Further Information
-------------------

For further information on the design, implementation and initial peformance estimation of siitperf, please read our (open access) paper:
G. Lencse, "Design and Implementation of a Software Tester for Benchmarking Stateless NAT64 Gateways", _IEICE Transactions on Communications_, DOI: 10.1587/transcom.2019EBN0010, avilable: http://doi.org/10.1587/transcom.2019EBN0010 

Any feedbacks (including questions, feature requests, comments, suggestions, etc.) are welcomed by the author.

Gabor LENCSE

e-mail: lencse-at-hit-dot-bme-dot-hu. s/-at-/@/, s/-dot-/./g
