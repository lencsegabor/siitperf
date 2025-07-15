siitperf
========

Siitperf is an RFC 8219, RFC 4814, and RFC 9693 compliant SIIT and stateful NAT64/NAT44 tester written in C++ using DPDK, and it can be used under the Linux operating system. Originally, it was only a SIIT tester (hence its name) and it worked with DPDK 16.11.9 (included in Debian 9). Then, it was extended to support stateful NAT64 and stateful NAT44 (also called NAPT) tests, too. Later, it was enabled to use pseudorandom IP addresses, which is currently implemented in siitperf-tp (see below) only. Finally, it was updated to work with DPDK 22.11.8 (included in Debian 12).

Introduction
------------

Siitperf implements the most important measurement procedures of RFC 8219, namely: Throughput, Frame Loss Rate, Latency, PDV (Packet Delay Variation). The measurement setup is shown below:

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

Where X and Y are in {4, 6}, and even though SIIT implies that X != Y, siitperf allows X==Y, thus siitperf can also be used as a classic RFC 2544 / RFC 5180 Tester for benchmarking IPv4 / IPv6 routers. Siitperf was designed for research purposes, and it is quite resilient an tunable: there are no "hard wired" constant values, rather it takes a lot of parameters either from its "siitperf.conf" configuration file, or from command line.

We note that testing with bidirectional traffic is a requirement, testing with unidirectional traffic is optional.

The package consists of three similar but different programs: siitperf-tp, siitperf-lat, siitperf-pdv. They serve the following purposes:
- siitperf-tp: Throughput and Frame Loss Rate measurements
- siitperf-lat: Latency measurements
- siitperf-pdv: PDV measurements plus special Throughput and Frame Loss Rate measurements checking timeout for every single frame individually

All three programs perform an elementary test (for 60 seconds, or whatever is set) and if iteration is needed, for example the binary search of the throughput tests, then the programs are executed several times by appropriate bash shell scripts. (The scripts are also provided, but they need to be tuned.)

Operation: SIIT Tests
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

All three programs use the same "siitperf.conf" file, and their command line parametes are also very similar. They use the following ones (in stateless tests):

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

We note that the specified frames size always interpreted as IPv6 frame size, even if pure IPv4 measurements are done (both sides are configured as IPv4 and there is no background traffic), and in this case the allowed range is 84-1538, to be able to use 64-1518 bytes long IPv4 frames.

The execution of the measurements are supported by the following scripts (they support only stateless tests):

__binary-rate-alg.sh__: Implements a binary search for throughput measurements using siitperf-tp. 

__frame-loss.sh-scan__: Performs frame loss rate measurements using siitperf-tp. The frames rates to be tested can be specified by the range and stepping. The frame sizes to be tested may also be listed.

__latency.sh__: Performs latency measurements using siitperf-lat.

__pdv.sh__: Performs PDV  measurements using siitperf-pdv.

__binary-rate-alg.sh-pdv__:  Implements a binary search for special throughput measurements using siitperf-pdv.

Warning: the scripts were written for personal use of the author of siitperf. They are included to be rather samples than ready to use scripts for other users. They should be read and understood before use.

## Operation: Stateful Tests

The test setup is demonstrated on an example of benchmarking a stateful NAT64 gateway, where IPv6 and IPv4 are assigned to the left and right side interfaces of the devices, respectively.

                  +--------------------------------------+
        2001:2::2 |Initiator                    Responder| 198.19.0.2
    +-------------|                Tester                |<------------+
    | IPv6 address|                         [state table]| IPv4 address|
    |             +--------------------------------------+             |
    |                                                                  |
    |             +--------------------------------------+             |
    |   2001:2::1 |                 DUT:                 | 198.19.0.1  |
    +------------>|        Stateful NAT64 gateway        |-------------+
      IPv6 address|     [connection tracking table]      | IPv4 address
                  +--------------------------------------+

Supposing that the required number of connections is four million, and it is achieved by a source port number range of 1--40,000 and a destination port number range of 1--100, then "siitperf.conf" has the following content:

	IP-L-Vers 6 # Left Sender's IP version for foreground traffic
	IP-R-Vers 4 # Right Sender's IP version for foreground traffic
	
	IPv6-L-Real 2001:2:0:0::2
	IPv6-L-Virt :: # currently not used
	IPv6-R-Real :: # currently not used, but needed for background traffic, e.g., 2001:2:0:8000::2
	IPv6-R-Virt 64:ff9b::198.19.0.2
	IPv4-L-Real 0.0.0.0 # currently unused
	IPv4-L-Virt 0.0.0.0 # currently unused
	IPv4-R-Real 0.0.0.0 # currently unused
	IPv4-R-Virt 0.0.0.0 # currently unused
	
	MAC-L-Tester a0:36:9f:c5:fa:1c # StarBED p094 enp5s0f0
	MAC-R-Tester a0:36:9f:c5:fa:1e # StarBED p094 enp5s0f1
	MAC-L-DUT a0:36:9f:c5:e6:58 # StarBED p095 enp5s0f0
	MAC-R-DUT a0:36:9f:c5:e6:5a # StarBED p095 enp5s0f1
	
	Forward 1 # left to right direction is active
	Reverse 1 # right to left direction is active
	Promisc 0 # no promiscuous mode for receiving
	
	Num-L-Nets 1 # Port number enumeration requires it to be 1.
	Num-R-Nets 1 # Port number enumeration requires it to be 1.
	
	CPU-L-Send 2 # Left Sender runs on this core
	CPU-R-Recv 4 # Right Receiver runs on this core
	CPU-R-Send 6 # Right Sender runs on this core
	CPU-L-Recv 8 # Left Receiver runs on this core
	
	MEM-Channels 2 # Number of Memory Channels
	
	# parameters for RFC 4814 random port feature
	
	Fwd-var-sport 3 # Does source port vary? 0: fix, 1: increase, 2: decrease, 3: random
	Fwd-var-dport 3 # Does destination port vary? 0: fix, 1: increase, 2: decrease, 3: random
	Fwd-sport-min 1
	Fwd-sport-max 40000
	Fwd-dport-min 1
	Fwd-dport-max 100
	# The values of the below 6 parameters are redundant (four tuples come from the state table)
	Rev-var-sport 0
	Rev-var-dport 0
	Rev-sport-min 0
	Rev-sport-max 1
	Rev-dport-min 0
	Rev-dport-max 1
	
	# parameters for stateful tests
	
	Stateful 1 # Initiator is on the left side, Responder is on the right side
	Enumerate-ports 3 # 0: no, 1/2 yes in inc/dec order, 3 unique pseudorandom
	Responder-ports 3 # 0: a single fixed, 1/2: inc/dec order linear, 3: pseudorandom selection

The stateful extension uses the same command line parameters as before (please see them above), plus some further ones were added. The new parameters always appear at the same position (before the extra parameters of the latency and PDV tests).

	./build/siitperf-tp <IPv6 size> <rate> <duration> <global timeout> <n> <m> <N> <M> <R> <T> <D>
	./build/siitperf-lat <IPv6 size> <rate> <duration> <global timeout> <n> <m> <N> <M> <R> <T> <D> <delay> <ts>
	./build/siitperf-pdv <IPv6 size> <rate> <duration> <global timeout> <n> <m> <N> <M> <R> <T> <D> <fr. t_out>

The additional command line parameters are to be interpreted as follows:

__N__: the number of test frames to send in the preliminary phase (1 – 2^32-1)

__M__: the number of entries in the state table of the Tester (1 – 2^32-1)

__R__: the frame rate, at which the test frames are sent during the test phase 1 (in frames per second)

__T__: the global timeout for test phase 1 (in milliseconds, 1 – 2000)

__D__: the overall delay caused by test phase 1, including the gap before test phase 2 (in milliseconds, 1 – 100,000,000)

The execution of the stateful measurements are supported by the following scripts:

__binary-rate-alg.sh-R__: Implements a binary search for maximum connection establishment rate measurement using siitperf-tp.

__binary-rate-alg.sh-sf__: Implements a binary search for throughput measurement using siitperf-tp. 

__cttc.sh__: Implements a connection tracking table capacity measurement using siitperf-tp. 

Warning: the scripts were written for personal use of the author of siitperf at the NICT StarBED environment. They are included to be rather samples than ready to use scripts for other users. They should be read and understood before use.

## Operation: Pseudorandom IP Addresses

The idea of pseudorandom IP addresses is a natural extension of the usage of RFC 4814 pseudorandom port numbers. Pseudorandom IP addresses can be used both in stateless and stateful tests. As the usage of pseudorandom IP addresses is another extension of siitperf, to keep the fixed IP addresses, the new parameters has to be set as follows:

	IP-L-var 0 # Does Left IP address vary? 0: fix, 1: increase, 2: decrease, 3: random
	IP-R-var 0 # Does Left IP address vary? 0: fix, 1: increase, 2: decrease, 3: random

The usage of pseudorandom IP addresses is is demonstrated by the examples below:

### IPv4 Packet Forwarding Tests with Multiple IP Addresses

Let us consider the below test setup for benchmarking IPv4 packet forwarding:

    198.18.0.2/16-198.18.255.254/16      198.19.0.2/16-198.19.255.254/16
               \  +--------------------------------------+  /
                \ |                                      | /
    +-------------|                Tester                |<------------+
    |             |                                      |             |
    |             +--------------------------------------+             |
    |                                                                  |
    |             +--------------------------------------+             |
    |             |                                      |             |
    +------------>|          DUT: IPv4 router            |-------------+
                / |                                      | \
               /  +--------------------------------------+  \
    198.18.0.1/16                                        198.19.0.1/16
    

To use pseudorandom IPv4 addresses from the above ranges, "siitperf.conf" should have the following relevant content:

	IP-L-Vers 4 # Left Sender's IP version for foreground traffic
	IP-R-Vers 4 # Right Sender's IP version for foreground traffic
	
	# parameters for tests with different IP addresses
	
	IP-L-var 3 # pseudorandom
	IP-R-var 3 # pseudorandom
	IP-L-min 2      # ".1" is for the DUT
	IP-L-max 65534  # ".255.255" is broadcast
	IP-R-min 2      # ".1" is for the DUT
	IP-R-max 65534  # ".255.255" is broadcast
	IPv4-L-offset 2 # last 16 bits
	IPv4-R-offset 2 # last 16 bits

As show by the above comments, a few IPv4 addresses cannot be used.

### IPv6 Packet Forwarding Tests with Multiple IP Addresses

Let us consider the below test setup for benchmarking IPv6 packet forwarding:

    2001:2::[0000-ffff]:2/64             2001:2:0:8000::[0000-ffff]:2/64
               \  +--------------------------------------+  /
                \ |                                      | /
    +-------------|                Tester                |<------------+
    |             |                                      |             |
    |             +--------------------------------------+             |
    |                                                                  |
    |             +--------------------------------------+             |
    |             |                                      |             |
    +------------>|          DUT: IPv6 router            |-------------+
                / |                                      | \
               /  +--------------------------------------+  \
       2001:2::1/64                                  2001:2:0:8000::1/64

To use pseudorandom IPv6 addresses from the above ranges, "siitperf.conf" should have the following relevant content:

	IP-L-Vers 6 # Left Sender's IP version for foreground traffic
	IP-R-Vers 6 # Right Sender's IP version for foreground traffic
	
	# parameters for tests with different IP addresses
	
	IP-L-var 3 # pseudorandom
	IP-R-var 3 # pseudorandom
	IP-L-min 0       # The full range
	IP-L-max 0xffff  # can be used. 
	IP-R-min 0       # The full range
	IP-R-max 0xffff  # can be used.
	IPv6-L-offset 12 # bits 96-111
	IPv6-R-offset 12 # bits 96-111

As show by the above comments, all IPv6 addresses can be used.

### Stateful NAT64 Tests with Multiple IP Addresses

Let us consider the below test setup for benchmarking stateful NAT64 gateways:

    2001:2::[0000-ffff]:2/64           198.19.0.0/15 - 198.19.255.254/15
               \  +--------------------------------------+  /
      IPv6      \ |Initiator                    Responder| /
    +-------------|                Tester                |<------------+
    | addresses   |                         [state table]| public IPv4 |
    |             +--------------------------------------+             |
    |                                                                  |
    |             +--------------------------------------+             |
    | 2001:2::1/64|                 DUT:                 | public IPv4 |
    +------------>|        Stateful NAT64 gateway        |-------------+
     IPv6 address |     [connection tracking table]      | \
                  +--------------------------------------+  \
                                       198.18.0.1/15 - 198.18.255.255/15

Suppose that the required number of connections is four million, and they are achieved by 4,000 IPv6 addresses and 1,000 IPv4 addresses on the left side port and on the right side port of the Tester, respectively. (The tester has no influence on, how many public IPv4 addresses are used in the "WAN" side port of the stateful NAT64 gateway. Maybe only one: 198.18.0.1/15, but a /16 range is possible. Please be aware of the /15 mask.)

Then "siitperf.conf" should have the following relevant content:

	IP-L-Vers 6 # Left Sender's IP version for foreground traffic
	IP-R-Vers 4 # Right Sender's IP version for foreground traffic
	
	IPv6-L-Real 2001:2:0:0::2
	IPv6-L-Virt :: # currently not used
	IPv6-R-Real :: # currently not used, but needed for background traffic, e.g., 2001:2:0:8000::2
	IPv6-R-Virt 64:ff9b::198.19.0.2
	IPv4-L-Real 0.0.0.0 # currently unused
	IPv4-L-Virt 0.0.0.0 # currently unused
	IPv4-R-Real 0.0.0.0 # currently unused
	IPv4-R-Virt 0.0.0.0 # currently unused
	
	# parameters for tests with different IP addresses
	
	IP-L-var 3 # pseudorandom
	IP-R-var 3 # pseudorandom
	IP-L-min 0       # The full range
	IP-L-max 3999    # can be used. 
	IP-R-min 2       # 0 is valid, but 
	IP-R-max 1001    # ".255.255" is broadcast.
	IPv6-L-offset 12 # bits 96-111
	IPv6-R-offset 14 # bits 112-127 (the two last octets of IPv6-R-Virt!)
	IPv4-L-offset 2  # currently unused
	IPv4-R-offset 2  # currently unused
	
	# parameters for stateful tests
	
	Stateful 1 # : stateless, 1/2 stateful with initiator on the left/right and responder on the right/left
	Enumerate-ports 0 # 0: no, 1/2 yes in inc/dec order, 3 unique pseudorandom
	Enumerate-ips 3 # 0: no, 1/2 yes in inc/dec order, 3 unique pseudorandom
	Responder-tuples 3 # 0: a single fixed, 1/2: inc/dec order linear, 3: pseudorandom selection

In the current example, the 4M connections are generated solely by the IP addresses, and fixed port numbers are used. However, they can be combined, too.

A much more detailed documentation is provided in the paper about the extension of siitperf to use pseudorandom IP addresses.

Hardware and Software Requirements
----------------------------------

As siitperf uses DPDK (Intel Data Plane Development Kit), a requirement is to use DPDK compatible hardware as Tester.
The list of supported NICs is available from: https://core.dpdk.org/supported/ 

As for time measurements, siitperf relies on the rte_rdtsc() DPDK function, which executes the RDTSC instuction of Intel CPUs. RDTSC is also implemented by several AMD CPUs. To produce reliable results, the CPU needs to support constant TSC. (In Intel terminology, the CPU needs to have the "constant_tsc" flag.)

Closing with latest commit 165cb7f on September 6, 2023, we used it under Debian 9.9 -- 9.13, and our DPDK version was: 16.11.9-1+deb9u1 -- 16.11.11-1+deb9u2.
Since July 15, 2025, it works under Debian 12.11 with DPDK 22.11.8-1~deb12u1.

Siitperf uses a separate core for sending and receiving in each directions, thus it requires 4 CPU cores for bidirectional traffic besides the main core used for executing the main program. The 4 cores should be reserved at boot time to avoid interference with other programs. We used the following line in "/etc/default/grub":

	GRUB_CMDLINE_LINUX_DEFAULT="quiet isolcpus=2,4,6,8 hugepagesz=1G hugepages=32"

As it can be also seen, we used 1GB hugepages. If your CPU does not have the "pdpe1gb" flag, but it has the "pse" flag, then 2MB hugepages will still do. (We have also sortly tested it.) Our relevant setting for 1GB hugepages in the "/etc/fstab" file was:

	nodev   /mnt/huge_1GB hugetlbfs pagesize=1GB 0 0

Of course, the "/mnt/huge_1GB" mount point must exist as a directory.

Further Information
-------------------

For further information on the design, implementation and initial performance estimation of siitperf, please read our (open access) papers:

G. Lencse, "Design and Implementation of a Software Tester for Benchmarking Stateless NAT64 Gateways", _IEICE Transactions on Communications_, vol. E104-B, no.2, pp. 128-140. February 1, 2021. DOI: 10.1587/transcom.2019EBN0010, available: http://doi.org/10.1587/transcom.2019EBN0010 

The above paper documents the original version of siitperf, which used fixed port numbers. Its extension to support pseudorandom port numbers is documented in the following (open access) paper:

G. Lencse, "Adding RFC 4814 Random Port Feature to Siitperf: Design, Implementation and Performance Estimation", _International Journal of Advances in Telecommunications, Electrotechnics, Signals and Systems_, vol 9, no 3, pp. 18-26, 2020, DOI: 10.11601/ijates.v9i3.291, available: https://doi.org/10.11601/ijates.v9i3.291

The testing of the accuracy of siitperf is documented in the following (open access) paper:

G. Lencse, "Checking the Accuracy of Siitperf", *Infocommunications Journal*, vol. 13, no. 2, pp. 2-9, June 2021, DOI: 10.36244/ICJ.2021.2.1, available https://doi.org/10.36244/ICJ.2021.2.1

And its extension to support stateful NAT64/NAT44 measurements is documented in the following (open access) paper:

G. Lencse, "Design and Implementation of a Software Tester for Benchmarking Stateful NATxy Gateways: Theory and Practice of Extending Siitperf for Stateful Tests", Computer Communications, vol. 172, no. 1, pp. 75-88, August 1, 2022, DOI: 10.1016/j.comcom.2022.05.028, available: https://doi.org/10.1016/j.comcom.2022.05.028

Regarding the methodology for benchmarking stateful NAT64/NAT44 gateways, please see our Internet Draft:

G. Lencse, K. Shima, "Benchmarking Methodology for Stateful NATxy Gateways using RFC 4814 Pseudorandom Port Numbers", Internet Draft, IETF BMWG, https://datatracker.ietf.org/doc/html/draft-ietf-bmwg-benchmarking-stateful

The above methodology has been validated by performing benchmarking measurements with three radically different stateful NAT64 implementations (Jool, tayga+iptables, OpenBSD PF). Our experiments were documented (including configuration scripts) in the following (open access) paper:

G. Lencse, K. Shima, K. Cho, "Benchmarking methodology for stateful NAT64 gateways", _Computer Communications_, vol. 210, October 2023, pp. 256-272, DOI: 10.1016/j.comcom.2023.08.009, available: https://doi.org/10.1016/j.comcom.2023.08.009

Its extension to support pseudorandom IP addresses both it stateless and stateful tests has been documented in the following (open access) paper:

G. Lencse, "Making Stateless and Stateful Network Performance Measurements Unbiased", _Computer Communications_, vol. 225, September 2024, pp. 141-155 DOI: 10.1016/j.comcom.2024.05.018, available: https://doi.org/10.1016/j.comcom.2024.05.018

Any feedbacks (including questions, feature requests, comments, suggestions, etc.) are welcomed by the author.

Gabor LENCSE

e-mail: lencse-at-hit-dot-bme-dot-hu. s/-at-/@/, s/-dot-/./g
