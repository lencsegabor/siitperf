/* Siitperf was originally an RFC 8219 SIIT (stateless NAT64) tester
 * written in C++ using DPDK 16.11.9 (included in Debian 9) in 2019.
 * RFC 4814 variable port number feature was added in 2020.
 * Extension for stateful tests was done in 2021.
 * Now it supports benchmarking of stateful NAT64 and stateful NAT44
 * gateways, but stateful NAT66 and stateful NAT46 are out of scope.
 * Extension for multiple IP addresses was done in 2023.
 * Updated for DPDK 22.11.8 (included in Debian 12) in 2025.
 *
 *  Copyright (C) 2019-2025 Gabor Lencse
 *
 *  This file is part of siitperf.
 *
 *  Siitperf is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Siitperf is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with siitperf.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef THROUGHPUT_H_INCLUDED
#define THROUGHPUT_H_INCLUDED

// 4-tuple for stateful tests
struct fourTuple {
  uint32_t init_addr;	// Initiator's IPv4 address
  uint32_t resp_addr;	// Responder's IPv4 address
  uint16_t init_port;	// Initiator's port number
  uint16_t resp_port;	// Responder's port number
};

// atomic 4-tuple for to ensure consistent reading and writing of the state table of the Responder
typedef std::atomic<fourTuple> atomicFourTuple;

// port pair for unique port number or IP address (part) combinations using random permutation
struct fieldPair {
  uint16_t src;	// source port number or IP address (part)
  uint16_t dst;	// destination port number or IP address (part)
};

// a union facilitating the access of the portpair or IP address parts pair as a 32-bit number
union bits32 {
  fieldPair field;
  uint32_t data;
};
struct fT { // a reduced four tuple that contains only the changing part of the IP addresses
  uint16_t sip;
  uint16_t dip;
  uint16_t sport;
  uint16_t dport;
};

// a union facilitating the access of the IP address parts pair and port pair as a 64-bit number
union bits64 {
  fT ft;
  uint64_t data;
};

// function prepares unique random IP address or port number combinations by enumeration and then random permutation
void randomPermutation32(bits32 *array, uint16_t src_min, uint16_t src_max, uint16_t dst_min, uint16_t dst_max);

// function prepares unique random IP address and port number combinations by enumeration and then random permutation
void randomPermutation64(bits32 *array, uint16_t si_min, uint16_t si_max, uint16_t di_min, uint16_t di_max,
                         uint16_t sp_min, uint16_t sp_max, uint16_t dp_min, uint16_t dp_max);

// the main class for siitperf
// data members are used for storing parameters
// member functions are used for the most important functions
// but send() and receive() are NOT member functions, due to limitations of rte_eal_remote_launch()
class Throughput {
public:
  // parameters from the configuration file
  int ip_left_version; 	// Left Sender's foreground IP version
  int ip_right_version; // Right Sender's foreground IP version

  struct in6_addr ipv6_left_real; 	// Tester's left side IPv6 address 
  struct in6_addr ipv6_left_virtual; 	// IPv6 allusion for Tester's left side IPv4 address
  struct in6_addr ipv6_right_real; 	// Tester's right side IPv6 address 
  struct in6_addr ipv6_right_virtual; 	// IPv6 allusion for Tester's right side IPv4 address

  uint32_t ipv4_left_real; 	// Tester's left side IPv4 address
  uint32_t ipv4_left_virtual; 	// IPv4 allusion for Tester's left side IPv6 address
  uint32_t ipv4_right_real; 	// Tester's right side IPv4 address
  uint32_t ipv4_right_virtual; 	// IPv4 allusion for Tester's right side IPv6 address

  uint8_t mac_left_tester[6]; 	// Tester's left side MAC address
  uint8_t mac_right_tester[6]; 	// Tester's right side MAC address
  uint8_t mac_left_dut[6]; 	// DUT's left side MAC address
  uint8_t mac_right_dut[6]; 	// DUT's right side MAC address

  int forward, reverse;		// directions are active if non-zero
  int promisc;			// set promiscuous mode 
  uint16_t num_left_nets, num_right_nets; 	// number of destination networks

  int cpu_left_sender; 		// lcore for left side Sender
  int cpu_right_receiver; 	// lcore for right side Receiver
  int cpu_right_sender; 	// lcore for right side Sender
  int cpu_left_receiver; 	// lcore for left side Receiver

  uint8_t memory_channels; 	// Number of memory channnels (for the EAL init.)

  // encoding: 0: use fix ports as defined in RFC 2544, 1: increase, 2: decrease, 3: pseudorandom
  unsigned fwd_var_sport;       // control value for fixed or variable source port numbers
  unsigned fwd_var_dport;       // control value for fixed or variable destination port numbers
  unsigned fwd_varport;         // derived logical value: at least one port has to be changed?
  unsigned rev_var_sport;       // control value for fixed or variable source port numbers
  unsigned rev_var_dport;       // control value for fixed or variable destination port numbers
  unsigned rev_varport;         // derived logical value: at least one port has to be changed?

  uint16_t fwd_sport_min;       // minumum value for source port
  uint16_t fwd_sport_max;       // maximum value for source port
  uint16_t fwd_dport_min;       // minumum value for destination port
  uint16_t fwd_dport_max;       // maximum value for destination port

  uint16_t rev_sport_min;       // minumum value for source port
  uint16_t rev_sport_max;       // maximum value for source port
  uint16_t rev_dport_min;       // minumum value for destination port
  uint16_t rev_dport_max;       // maximum value for destination port

  // encoding: 0: use fix IP addresses as defined in RFC 2544, 1: increase, 2: decrease, 3: pseudorandom
  unsigned ip_left_varies;      // control value for fixed or variable left side IP address
  unsigned ip_right_varies;     // control value for fixed or variable left side IP address
  unsigned ip_varies;           // derived logical value: at least one IP address has to be changed?

  uint16_t ip_left_min;		// minumum value for the changing 16-bit of the left side IP address
  uint16_t ip_left_max;		// maximum value for the changing 16-bit of the left side IP address
  uint16_t ip_right_min;	// minumum value for the changing 16-bit of the right side IP address
  uint16_t ip_right_max;	// maxumum value for the changing 16-bit of the right side IP address
 
  uint16_t ipv4_left_offset;	// offset from the begining of the left side IPv4 address to its varying 16-bit
  uint16_t ipv4_right_offset;	// offset from the begining of the right side IPv4 address to its varying 16-bit
  uint16_t ipv6_left_offset;	// offset from the begining of the left side IPv6 address to its varying 16-bit
  uint16_t ipv6_right_offset;	// offset from the begining of the right side IPv6 address to its varying 16-bit

  // encoding: 0: stateless tests; 1,2: stateful, Responder is on the 1: right side, 2: left side
  unsigned stateful;            // control the type (stateless or stateful) of the DUT

  // encoding: 0: use a single 4-tuple taken from the very first preliminary frame (like fix port numbers)
  //           1: select a 4-tuple from the state table in increasing order (to save computing power)
  //           2: select a 4-tuple from the state table in decreasing order (to save computing power)
  //           3: select a 4-tuple from the state table in a pseudorandom way (to be RFC 4814 compliant)
  unsigned responder_tuples;     // how to select a 4-tuple for test frame generation

  // encoding: 
  //    0: no, use port numbers as specified by other parameters
  //  1,2: yes, sport is the low order, dport is the high order counter, their min and max values are honored
  //    1: port number combinations are enumerated in inreasing order
  //    2: port number combinations are enumerated in dereasing order
  //    3: unique random port number combinations are used, their min and max values are honored
  //       uniqueness is ensured by using pre-generated random permutation  
  unsigned enumerate_ports;

  // encoding:
  //    0: no, use IP addresses as specified by other parameters
  //  1,2: yes, source IP is the low order, destination IP is the high order counter, their min and max values are honored
  //    1: IP address combinations are enumerated in inreasing order
  //    2: IP address combinations are enumerated in dereasing order
  //    3: unique IP address combinations are used, their min and max values are honored
  //       uniqueness is ensured by using pre-generated random permutation
  unsigned enumerate_ips;


  // positional parameters from command line
  uint16_t ipv6_frame_size;	// size of the frames carrying IPv6 datagrams (including the 4 bytes of the FCS at the end) 
  uint16_t ipv4_frame_size; 	// redundant parameter, automatically set as ipv6_frame_size-20
  uint32_t frame_rate;		// number of frames per second
  uint16_t duration;		// test duration (in seconds, 1-3600)
  uint16_t global_timeout;	// global timeout (in milliseconds, 0-60000)
  uint32_t n, m;		// modulo and threshold for controlling background traffic proportion

  uint32_t pre_frames;		// "N": the number of test frames to send in the preliminary phase
  uint32_t eff_pre_frames;	// the number of the foregound preliminary frames
  uint32_t state_table_size;	// "M": the number of entries in the state table of the Tester
  uint32_t pre_rate;		// "R": the frame rate, at which the test frames are sent during the preliminary phase
  uint16_t pre_timeout;		// global timeout for the preliminary frames (in milliseconds, 0-2000)
  uint32_t pre_delay;		// "D": the delay caused by the preliminary phase (in milliseconds, 0-100000)

  //  further data members, set by init()
  rte_mempool *pkt_pool_left_sender, *pkt_pool_right_receiver;	// packet pools for the forward direction testing
  rte_mempool *pkt_pool_right_sender, *pkt_pool_left_receiver;	// packet pools for the reverse direction testing
  // note: the above packet pools are also used by the preliminary frame sending for the stateful tests
  uint64_t hz;			// number of clock cycles per second 
  uint64_t start_tsc;		// sending of the test frames will begin at this time
  uint64_t finish_receiving;	// receiving of the test frames will end at this time
  uint64_t frames_to_send;	// number of frames to send 

  uint64_t start_tsc_pre;	// sending of the preliminary frames will begin at this time
  uint64_t finish_receiving_pre; // receiving of the preliminary frames will end at this time

  atomicFourTuple *stateTable;	// pointer of the state table of the Responder
  unsigned valid_entries = 0;	// number of valid entries in the state table

  bits32 *uniquePortComb = 0; 	// array of pre-generated unique port number combinations (Enumerate-ports 3, but Enumerate-ips 0)
  bits32 *uniqueIpComb = 0; 	// array of pre-generated unique IP address combinations (Enumerate-ips 3, but Enumerate-ports 0)
  bits64 *uniqueFtComb = 0; 	// array of pre-generated unique four tuple combinations (Enumerate-ips 3, Enumerate-ports 3)


  // helper functions (see their description at their definition)
  int findKey(const char *line, const char *key);
  int readConfigFile(const char *filename);
  int readCmdLine(int argc, const char *argv[]);
  int init(const char *argv0, uint16_t leftport, uint16_t rightport);
  virtual int senderPoolSize(int numDestNets, int varport);
  virtual int senderPoolSize(int numDestNets, int varport, int ip_varies);
  void numaCheck(uint16_t port, const char *port_side, int cpu, const char *cpu_name);

  // perform throughput measurement
  void measure(uint16_t leftport, uint16_t rightport);

  Throughput();
};

// functions to create Test Frames (and their parts)
struct rte_mbuf *mkTestFrame4(uint16_t length, rte_mempool *pkt_pool, const char *side,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const uint32_t *src_ip, const uint32_t *dst_ip, unsigned var_sport, unsigned var_dport);
struct rte_mbuf *mkFinalTestFrame4(uint16_t length, rte_mempool *pkt_pool, const char *side,
                                   const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                                   const uint32_t *src_ip, const uint32_t *dst_ip, unsigned sport, unsigned dport);
void mkEthHeader(struct rte_ether_hdr *eth, const struct ether_addr *dst_mac, const struct ether_addr *src_mac, const uint16_t ether_type);
void mkIpv4Header(struct rte_ipv4_hdr *ip, uint16_t length, const uint32_t *src_ip, const uint32_t *dst_ip);
void mkUdpHeader(struct rte_udp_hdr *udp, uint16_t length, unsigned var_sport, unsigned var_dport); 
void mkData(uint8_t *data, uint16_t length);
struct rte_mbuf *mkTestFrame6(uint16_t length, rte_mempool *pkt_pool, const char *side,
                                const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                                const struct in6_addr *src_ip, const struct in6_addr *dst_ip, unsigned var_sport, unsigned var_dport);
void mkIpv6Header(struct rte_ipv6_hdr *ip, uint16_t length, const struct in6_addr *src_ip, const struct in6_addr *dst_ip);

// report the current TSC of the exeucting core
int report_tsc(void *par);

// check if the TSC of the given core is synchronized with the TSC of the main core
void check_tsc(int cpu, const char *cpu_name);

// send test frame: stateless version
int send(void *par);

// send test frame: stateless version using multiple source and/or destination IP address (does not support multiple networks)
int msend(void *par);

// send test frame: stateful version (Initiator/Sender) -- like send, plus supports port number enumeration (with a single dest. net.)
int isend(void *par);

// send test frame: stateful version (Initiator/Sender) -- like msend, plus supports IP address enumeration.
int imsend(void *par);

// send test frame: stateful version (Responder/Sender)
int rsend(void *par);

// receive and count test frames (stateless version and Initiator/Receiver, too)
int receive(void *par);

// rreceive, store 4-tuple and count test frames: stateful version (Responder/Receiver)
int rreceive(void *par);

// allocate NUMA local memory and pre-generate random permutation -- Executed by the core of Initiator/Sender!
int randomPermutationGenerator32(void *par);

// allocate NUMA local memory and pre-generate random permutation -- Executed by the core of Initiator/Sender!
int randomPermutationGenerator64(void *par);

// to store the parameters for randomPermutationGenerator32
class randomPermutationGeneratorParameters32 {
  public:
  bits32 **addr_of_arraypointer;	// pointer to the place, where the pointer is stored 
  uint16_t src_min, src_max; 	// source range
  uint16_t dst_min, dst_max;	// destination range
  char *type;			// just to display information (if "port number" or "IP address" combinations)
  uint64_t hz;			// just to be able to display the execution time
};

// to store the parameters for randomPermutationGenerator64
class randomPermutationGeneratorParameters64 {
  public:
  bits64 **addr_of_arraypointer;        	// pointer to the place, where the pointer is stored
  uint16_t si_min, si_max, di_min, di_max;  	// ranges for IP address parts
  uint16_t sp_min, sp_max, dp_min, dp_max;  	// ranges for port numbers
  uint64_t hz;                  // just to be able to display the execution time
};

// to store identical parameters for both senders
class senderCommonParameters {
  public:
  uint16_t ipv6_frame_size;     // size of the frames carrying IPv6 datagrams (including the 4 bytes of the FCS at the end)
  uint16_t ipv4_frame_size;     // redundant parameter, automatically set as ipv6_frame_size-20
  uint32_t frame_rate;          // number of frames per second
  uint16_t duration;            // test duration (in seconds, 1-3600)
  uint32_t n, m;         	// modulo and threshold for controlling background traffic proportion
  uint64_t hz;                  // number of clock cycles per second
  uint64_t start_tsc;           // sending of the test frames will begin at this time
//  uint64_t frames_to_send;      // number of frames to send

  senderCommonParameters();
  senderCommonParameters(uint16_t ipv6_frame_size_, uint16_t ipv4_frame_size_, uint32_t frame_rate_, uint16_t duration_,
                         uint32_t n_, uint32_t m_, uint64_t hz_, uint64_t start_tsc_);
};

// to store differing parameters for each sender
class senderParameters {
  public:
  class senderCommonParameters *cp;
  int ip_version;
  rte_mempool *pkt_pool;
  uint16_t eth_id;
  const char *side;
  struct ether_addr *dst_mac, *src_mac;
  uint32_t *src_ipv4, *dst_ipv4;
  struct in6_addr *src_ipv6, *dst_ipv6;
  struct in6_addr *src_bg, *dst_bg;
  uint16_t num_dest_nets;
  unsigned var_sport, var_dport;
  uint16_t sport_min, sport_max, dport_min, dport_max;

  senderParameters();
  senderParameters(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                   struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                   struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                   uint16_t num_dest_nets_, unsigned var_sport_, unsigned var_dport_,
	           uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_);
};


// to store differing parameters for each sender -num_dest_nets + par. for msend
class mSenderParameters {
  public:
  class senderCommonParameters *cp;
  int ip_version;
  rte_mempool *pkt_pool;
  uint16_t eth_id;
  const char *side;
  struct ether_addr *dst_mac, *src_mac;
  uint32_t *src_ipv4, *dst_ipv4;
  struct in6_addr *src_ipv6, *dst_ipv6;
  struct in6_addr *src_bg, *dst_bg;
  // excluded: uint16_t num_dest_nets;
  unsigned var_sip, var_dip;
  uint16_t sip_min, sip_max, dip_min, dip_max;
  uint16_t src_ipv4_offset, dst_ipv4_offset, src_ipv6_offset, dst_ipv6_offset;
  unsigned var_sport, var_dport;
  uint16_t sport_min, sport_max, dport_min, dport_max;

  mSenderParameters();
  mSenderParameters(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                   struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                   struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                   unsigned var_sip_, unsigned var_dip_,
                   uint16_t sip_min_, uint16_t sip_max_, uint16_t dip_min_, uint16_t dip_max,
                   uint16_t src_ipv4_offset_, uint16_t dst_ipv4_offset_, uint16_t src_ipv6_offset_, uint16_t dst_ipv6_offset,
                   unsigned var_sport_, unsigned var_dport_,
	           uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_);
};

// to store differing parameters for each sender + par. for isend
class iSenderParameters : public senderParameters {
  public:
  unsigned enumerate_ports;
  uint32_t pre_frames;
  bits32 *uniquePortComb;   // array for pre-generated unique port number combinations (Enumerate-ports: 3)

  iSenderParameters();
  iSenderParameters(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                   struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                   struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                   uint16_t num_dest_nets_, unsigned var_sport_, unsigned var_dport_,
                   uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_,
		   unsigned enumerate_ports_, uint32_t pre_frames_, bits32 *uniquePortComb_);
};


// to store differing parameters for each sender -num_dest_nets + par. for imsend
class imSenderParameters {
  public:
  class senderCommonParameters *cp;
  int ip_version;
  rte_mempool *pkt_pool;
  uint16_t eth_id;
  const char *side;
  struct ether_addr *dst_mac, *src_mac;
  uint32_t *src_ipv4, *dst_ipv4;
  struct in6_addr *src_ipv6, *dst_ipv6;
  struct in6_addr *src_bg, *dst_bg;
  // excluded: uint16_t num_dest_nets;
  unsigned var_sip, var_dip;
  uint16_t sip_min, sip_max, dip_min, dip_max;
  uint16_t src_ipv4_offset, dst_ipv4_offset, src_ipv6_offset, dst_ipv6_offset;
  unsigned var_sport, var_dport;
  uint16_t sport_min, sport_max, dport_min, dport_max;
  unsigned enumerate_ips;
  unsigned enumerate_ports;
  uint32_t pre_frames;
  bits32 *uniqueIpComb;   // array for pre-generated unique IP address (part) combinations (Enumerate-ips 3, but Enumerate-ports 0)
  bits64 *uniqueFtComb;   // array for pre-generated unique 4-tuple (part) combinations (Enumerate-ips 3, Enumerate-ports 3)

  imSenderParameters();
  imSenderParameters(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                   struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                   struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                   unsigned var_sip_, unsigned var_dip_,
                   uint16_t sip_min_, uint16_t sip_max_, uint16_t dip_min_, uint16_t dip_max,
                   uint16_t src_ipv4_offset_, uint16_t dst_ipv4_offset_, uint16_t src_ipv6_offset_, uint16_t dst_ipv6_offset,
                   unsigned var_sport_, unsigned var_dport_,
	           uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_,
		   unsigned enumerate_ips_, unsigned enumerate_ports_, uint32_t pre_frames_, 
		   bits32 *uniqueIpComb_, bits64 *uniqueFtComb_);
};

// to store differing parameters for each sender + par. for rsend
class rSenderParameters : public senderParameters {
  public:
  unsigned state_table_size;    // the number of entries in the state table
  atomicFourTuple *stateTable; 	// the 4-tuples are only read
  unsigned responder_tuples;     // how to select a 4-tuple for test frame generation

  rSenderParameters();
  rSenderParameters(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                   struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                   struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                   uint16_t num_dest_nets_, unsigned var_sport_, unsigned var_dport_,
                   uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_,
                   unsigned state_table_size_, atomicFourTuple *stateTable_, unsigned responder_tuples_);
};

// to store parameters for each receiver 
class receiverParameters {
  public:
  uint64_t finish_receiving;     // this one is common, but it was not worth dealing with it.
  uint16_t eth_id;
  const char *side;

  receiverParameters();
  receiverParameters(uint64_t finish_receiving_, uint16_t eth_id_, const char *side_);
};

// to store parameters for Responder's receiver
class rReceiverParameters : public receiverParameters {
  public:
  unsigned state_table_size;	// the number of possible entries in the state table
  unsigned *valid_entries;	// the number of valid entries in the state table (carries vale from prelim. to real test)
  atomicFourTuple **stateTable;	// allocate and set pointer, if preliminary test; exists with valid entries otherwise

  rReceiverParameters();
  rReceiverParameters(uint64_t finish_receiving_, uint16_t eth_id_, const char *side_,unsigned state_table_size_,
                      unsigned *valid_entries_, atomicFourTuple **stateTable_);
};

// to collect source and destionation IPv4 and IPv6 addresses
class ipQuad {
  public:
  uint32_t *src_ipv4, *dst_ipv4; 
  struct in6_addr *src_ipv6, *dst_ipv6;
  ipQuad(int vA, int vB, uint32_t *ipv4_A_real, uint32_t *ipv4_B_real, uint32_t *ipv4_A_virtual,  uint32_t *ipv4_B_virtual,
         struct in6_addr *ipv6_A_real, struct in6_addr *ipv6_B_real, struct in6_addr *ipv6_A_virtual, struct in6_addr *ipv6_B_virtual);
}; 

#endif
