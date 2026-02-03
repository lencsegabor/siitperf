/* Siitperf was originally an RFC 8219 SIIT (stateless NAT64) tester
 * written in C++ using DPDK 16.11.9 (included in Debian 9) in 2019.
 * RFC 4814 variable port number feature was added in 2020.
 * Extension for stateful tests was done in 2021.
 * Now it supports benchmarking of stateful NAT64 and stateful NAT44
 * gateways, but stateful NAT66 and stateful NAT46 are out of scope.
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

#ifndef LATENCY_H_INCLUDED
#define LATENCY_H_INCLUDED

// the main class for latency measurements, adds some features to class Throughput
class Latency : public Throughput {
public:
  uint16_t delay;               // time period while frames are sent, but no timestamps are used; then timestaps are used in the "duration-delay" length interval
  uint16_t num_timestamps;      // number of timestamps used, 1-50000 is accepted, RFC 8219 requires at least 500, RFC 2544 requires 1

  Latency() : Throughput() { }; // default constructor
  int readCmdLine(int argc, const char *argv[]);	// reads further two arguments
  virtual int senderPoolSize(int numDestNets, int varport);	// adds num_timestamps, too
  virtual int senderPoolSize(int numDestNets, int varport, int ip_varies);	// adds num_timestamps, too

  // perform latency measurement
  void measure(uint16_t leftport, uint16_t rightport);
};

// functions to create Latency Frames (and their parts)
struct rte_mbuf *mkLatencyFrame4(uint16_t length, rte_mempool *pkt_pool, const char *side,
                                const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                                const uint32_t *src_ip, const uint32_t *dst_ip, unsigned var_sport, unsigned var_dport, uint16_t id);
struct rte_mbuf *mkFinalLatencyFrame4(uint16_t length, rte_mempool *pkt_pool, const char *side,
                                const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                                const uint32_t *src_ip, const uint32_t *dst_ip, unsigned sport, unsigned dport, uint16_t id);
void mkDataLatency(uint8_t *data, uint16_t length, uint16_t latency_frame_id);
struct rte_mbuf *mkLatencyFrame6(uint16_t length, rte_mempool *pkt_pool, const char *side,
                                const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                                const struct in6_addr *src_ip, const struct in6_addr *dst_ip, unsigned var_sport, unsigned var_dport, uint16_t id);

class senderCommonParametersLatency : public senderCommonParameters {
public:
  uint16_t delay; 
  uint16_t num_timestamps;

  senderCommonParametersLatency();
  senderCommonParametersLatency(uint16_t ipv6_frame_size_, uint16_t ipv4_frame_size_, uint32_t frame_rate_, uint16_t duration_,
                                uint32_t n_, uint32_t m_, uint64_t hz_, uint64_t start_tsc_,
                                uint16_t delay_, uint16_t num_timestamps_);
};

class senderParametersLatency : public senderParameters {
public:
  uint64_t *send_ts;
  senderParametersLatency();
  senderParametersLatency(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                          struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                          struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                   	  uint16_t num_dest_nets_, unsigned var_sport_, unsigned var_dport_,
                          uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_, uint64_t *send_ts_);
};

/* It is not used
class iSenderParametersLatency : public iSenderParameters {
public:
  uint64_t *send_ts;

  iSenderParametersLatency(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                   struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                   struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                   uint16_t num_dest_nets_, unsigned var_sport_, unsigned var_dport_,
                   uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_,
                   unsigned enumerate_ports_, uint32_t pre_frames_, uint64_t *send_ts_);
};
*/

class rSenderParametersLatency : public rSenderParameters {
public:
  uint64_t *send_ts;

  rSenderParametersLatency();
  rSenderParametersLatency(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                   struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                   struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                   uint16_t num_dest_nets_, unsigned var_sport_, unsigned var_dport_,
                   uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_,
                   unsigned state_table_size_, atomicFourTuple *stateTable_, unsigned responder_ports_, uint64_t *send_ts_);
};

class receiverParametersLatency : public receiverParameters {
public:
  uint16_t num_timestamps;
  uint64_t *receive_ts;	// pointer to receive timestamps 

  receiverParametersLatency();
  receiverParametersLatency(uint64_t finish_receiving_, uint16_t eth_id_, const char *side_, uint16_t num_timestamps_, uint64_t *receive_ts_);
};

class rReceiverParametersLatency : public rReceiverParameters {
public:
  uint16_t num_timestamps;
  uint64_t *receive_ts; // pointer to receive timestamps

  rReceiverParametersLatency();
  rReceiverParametersLatency(uint64_t finish_receiving_, uint16_t eth_id_, const char *side_, unsigned state_table_size_,
                             unsigned *valid_entries_, atomicFourTuple **stateTable_, uint16_t num_timestamps_, uint64_t *receive_ts_);
};

void evaluateLatency(uint16_t num_timestamps, uint64_t *send_ts, uint64_t *receive_ts, uint64_t hz, int penalty, const char *side);

#endif
