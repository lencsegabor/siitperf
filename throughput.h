/* Siitperf is an RFC 8219 SIIT (stateless NAT64) tester written in C++ using DPDK
 * Variable port feature is also added to comply with RFC 4814,
 * for more information: https://tools.ietf.org/html/rfc4814#section-4.5
 *
 *  Copyright (C) 2019-2020 Gabor Lencse
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

  uint8_t memory_channels; // Number of memory channnels (for the EAL init.)

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

  // positional parameters from command line
  uint16_t ipv6_frame_size;	// size of the frames carrying IPv6 datagrams (including the 4 bytes of the FCS at the end) 
  uint16_t ipv4_frame_size; 	// redundant parameter, automatically set as ipv6_frame_size-20
  uint32_t frame_rate;		// number of frames per second
  uint16_t duration;		// test duration (in seconds, 1-3600)
  uint16_t global_timeout;	// global timeout (in milliseconds, 0-60000)
  uint32_t n, m;		// modulo and threshold for controlling background traffic proportion

  // further data members, set by init()
  rte_mempool *pkt_pool_left_sender, *pkt_pool_right_receiver;	// packet pools for the forward direction testing
  rte_mempool *pkt_pool_right_sender, *pkt_pool_left_receiver;	// packet pools for the reverse direction testing
  uint64_t hz;			// number of clock cycles per second 
  uint64_t start_tsc;		// sending of the test frames will begin at this time
  uint64_t finish_receiving;	// receiving of the test frames will end at this time
  uint64_t frames_to_send;	// number of frames to send 

  // helper functions (see their description at their definition)
  int findKey(const char *line, const char *key);
  int readConfigFile(const char *filename);
  int readCmdLine(int argc, const char *argv[]);
  int init(const char *argv0, uint16_t leftport, uint16_t rightport);
  virtual int senderPoolSize(int numDestNets, int varport);
  void numaCheck(uint16_t port, const char *port_side, int cpu, const char *cpu_name);

  // perform throughput measurement
  void measure(uint16_t leftport, uint16_t rightport);

  Throughput();
};

// functions to create Test Frames (and their parts)
struct rte_mbuf *mkTestFrame4(uint16_t length, rte_mempool *pkt_pool, const char *side,
                                const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                                const uint32_t *src_ip, const uint32_t *dst_ip, unsigned var_sport, unsigned var_dport);
void mkEthHeader(struct ether_hdr *eth, const struct ether_addr *dst_mac, const struct ether_addr *src_mac, const uint16_t ether_type);
void mkIpv4Header(struct ipv4_hdr *ip, uint16_t length, const uint32_t *src_ip, const uint32_t *dst_ip);
void mkUdpHeader(struct udp_hdr *udp, uint16_t length, unsigned var_sport, unsigned var_dport); 
void mkData(uint8_t *data, uint16_t length);
struct rte_mbuf *mkTestFrame6(uint16_t length, rte_mempool *pkt_pool, const char *side,
                                const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                                const struct in6_addr *src_ip, const struct in6_addr *dst_ip, unsigned var_sport, unsigned var_dport);
void mkIpv6Header(struct ipv6_hdr *ip, uint16_t length, const struct in6_addr *src_ip, const struct in6_addr *dst_ip);

// report the current TSC of the exeucting core
int report_tsc(void *par);

// check if the TSC of the given core is synchronized with the TSC of the main core
void check_tsc(int cpu, const char *cpu_name);

// send test frame
int send(void *par);

// receive and count test frames
int receive(void *par);

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
  uint64_t frames_to_send;      // number of frames to send
  senderCommonParameters(uint16_t ipv6_frame_size_, uint16_t ipv4_frame_size_, uint32_t frame_rate_, uint16_t duration_,
                         uint32_t n_, uint32_t m_, uint64_t hz_, uint64_t start_tsc_);
};

// to store differing parameters for each sender
class senderParameters {
  public:
  class senderCommonParameters *cp;
  int ip_version;
  rte_mempool *pkt_pool;
  uint8_t eth_id;
  const char *side;
  struct ether_addr *dst_mac, *src_mac;
  uint32_t *src_ipv4, *dst_ipv4;
  struct in6_addr *src_ipv6, *dst_ipv6;
  struct in6_addr *src_bg, *dst_bg;
  uint16_t num_dest_nets;
  unsigned var_sport, var_dport;
  uint16_t sport_min, sport_max, dport_min, dport_max;

  senderParameters(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint8_t eth_id_, const char *side_,
                   struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                   struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                   uint16_t num_dest_nets_, unsigned var_sport_, unsigned var_dport_,
	           uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_);
};

// to store parameters for each receiver 
class receiverParameters {
  public:
  uint64_t finish_receiving;     // this one is common, but it was not worth dealing with it.
  uint8_t eth_id;
  const char *side;
  receiverParameters(uint64_t finish_receiving_, uint8_t eth_id_, const char *side_);
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
