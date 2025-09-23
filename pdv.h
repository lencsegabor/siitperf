/* Siitperf was originally an RFC 8219 SIIT (stateless NAT64) tester
 * written in C++ using DPDK in 2019.
 * RFC 4814 variable port number feature was added in 2020.
 * Extension for stateful tests was done in 2021.
 * Now it supports benchmarking of stateful NAT64 and stateful NAT44
 * gateways, but stateful NAT66 and stateful NAT46 are out of scope.
 *
 *  Copyright (C) 2019-2021 Gabor Lencse
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

#ifndef PDV_H_INCLUDED
#define PDV_H_INCLUDED

// the main class for PDV measurements, adds some features to class Throughput
class Pdv : public Throughput {
public:
  uint16_t frame_timeout;       // if 0, normal PDV measurement is done; if >0, then frames with higher delay then frame_timeout are considered as lost 

  Pdv() : Throughput() { }; // default constructor
  int readCmdLine(int argc, const char *argv[]);	// reads further one argument: frame_timeout
  virtual int senderPoolSize(int numDestNets, int varport);
  virtual int senderPoolSize(int numDestNets, int varport, int ip_varies);

  // perform pdv measurement
  void measure(uint16_t leftport, uint16_t rightport);
};

// functions to create PDV Frames (and their parts)
struct rte_mbuf *mkPdvFrame4(uint16_t length, rte_mempool *pkt_pool, const char *side,
                                const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                                const uint32_t *src_ip, const uint32_t *dst_ip, unsigned var_sport, unsigned var_dport);

struct rte_mbuf *mkFinalPdvFrame4(uint16_t length, rte_mempool *pkt_pool, const char *side,
                                const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                                const uint32_t *src_ip, const uint32_t *dst_ip, unsigned sport, unsigned dport);

void mkDataPdv(uint8_t *data, uint16_t length);

struct rte_mbuf *mkPdvFrame6(uint16_t length, rte_mempool *pkt_pool, const char *side,
                                const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                                const struct in6_addr *src_ip, const struct in6_addr *dst_ip, unsigned var_sport, unsigned var_dport);

class senderParametersPdv : public senderParameters {
public:
  uint64_t **send_ts;
  senderParametersPdv(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                          struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                          struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                          uint16_t num_dest_nets_, unsigned var_sport_, unsigned var_dport_,
			  uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_, uint64_t **send_ts_);
};

class rSenderParametersPdv : public rSenderParameters {
public:
  uint64_t **send_ts;
  rSenderParametersPdv(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                      struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                      struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                      uint16_t num_dest_nets_, unsigned var_sport_, unsigned var_dport_,
                      uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_, 
                      unsigned state_table_size_, atomicFourTuple *stateTable_, unsigned responder_ports_, uint64_t **send_ts_);
};

class receiverParametersPdv : public receiverParameters {
  public:
  uint64_t num_frames;	// number of all frames, needed for the rte_zmalloc call for allocating receive_ts
  uint16_t frame_timeout;
  uint64_t **receive_ts;
  receiverParametersPdv(uint64_t finish_receiving_, uint16_t eth_id_, const char *side_, 
                            uint64_t num_frames_, uint16_t frame_timeout_, uint64_t **receive_ts_);
};

class rReceiverParametersPdv : public rReceiverParameters {
  public:
  uint64_t num_frames;  // number of all frames, needed for the rte_zmalloc call for allocating receive_ts
  uint16_t frame_timeout;
  uint64_t **receive_ts;
  rReceiverParametersPdv(uint64_t finish_receiving_, uint16_t eth_id_, const char *side_, unsigned state_table_size_,
                        unsigned *valid_entries_, atomicFourTuple **stateTable_,
                        uint64_t num_frames_, uint16_t frame_timeout_, uint64_t **receive_ts_);
};

void evaluatePdv(uint64_t num_timestamps, uint64_t *send_ts, uint64_t *receive_ts, uint64_t hz, uint16_t frame_timeout, int penalty, const char *side);

#endif
