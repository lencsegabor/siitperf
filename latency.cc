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

#include "defines.h"
#include "includes.h"
#include "throughput.h"
#include "latency.h"

// the understanding of this code requires the knowledge of throughput.c
// only a few functions are redefined or added here

// after reading the parameters for throughput measurement, further two parameters are read 
int Latency::readCmdLine(int argc, const char *argv[]) {
  int numThroughputPars;	// number of parameters read by Throughput::readCmdLine

  if ( Throughput::readCmdLine(argc-2,argv) < 0 )
    return -1;

  if ( !stateful )
    numThroughputPars=6;	// stateless throughput test uses 6 parameters 
  else
    numThroughputPars=11;	// stateful throughput test uses 11 parameters 

  if ( sscanf(argv[numThroughputPars+1], "%hu", &delay) != 1 || delay > 3600 ) {
    std::cerr << "Input Error: Delay before timestamps must be between 0 and 3600." << std::endl;
    return -1;
  }
  if ( duration <= delay ) {
    std::cerr << "Input Error: Test duration MUST be longer than the delay before timestamps." << std::endl;
    return -1;
  }
  if ( sscanf(argv[numThroughputPars+2], "%hu", &num_timestamps) != 1 || num_timestamps < 1 || num_timestamps > 50000 ) {
    std::cerr << "Input Error: Number of timestamps must be between 1 and 50000." << std::endl;
    return -1;
  }
  if ( (duration-delay)*frame_rate < num_timestamps ) {
    std::cerr << "Input Error: There are not enough test frames in the (duration-delay) interval to carry so many timestamps." << std::endl;
    return -1;
  }
  return 0;
}

int Latency::senderPoolSize(int num_dest_nets, int varport) {
  return Throughput::senderPoolSize(num_dest_nets,varport)+num_timestamps; // frames with timestamps are also pre-generated
}

// creates a special IPv4 Test Frame tagged for latency measurement using several helper functions
// BEHAVIOR: it sets exatly, what it is told to set :-)
struct rte_mbuf *mkFinalLatencyFrame4(uint16_t length, rte_mempool *pkt_pool, const char *side,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const uint32_t *src_ip, const uint32_t *dst_ip, unsigned sport, unsigned dport, int id) {
  struct rte_mbuf *pkt_mbuf=rte_pktmbuf_alloc(pkt_pool); // message buffer for the Latency Frame
  if ( !pkt_mbuf )
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Latency Frame! \n", side);
  length -=  RTE_ETHER_CRC_LEN; // exclude CRC from the frame length
  pkt_mbuf->pkt_len = pkt_mbuf->data_len = length; // set the length in both places
  uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbuf, uint8_t *); // Access the Test Frame in the message buffer
  rte_ether_hdr *eth_hdr = reinterpret_cast<struct rte_ether_hdr *>(pkt); // Ethernet header
  rte_ipv4_hdr *ip_hdr = reinterpret_cast<rte_ipv4_hdr *>(pkt+sizeof(rte_ether_hdr)); // IPv4 header
  rte_udp_hdr *udp_hd = reinterpret_cast<rte_udp_hdr *>(pkt+sizeof(rte_ether_hdr)+sizeof(rte_ipv4_hdr)); // UDP header
  uint8_t *udp_data = reinterpret_cast<uint8_t*>(pkt+sizeof(rte_ether_hdr)+sizeof(rte_ipv4_hdr)+sizeof(rte_udp_hdr)); // UDP data

  mkEthHeader(eth_hdr, dst_mac, src_mac, 0x0800);       // contains an IPv4 packet
  int ip_length = length - sizeof(rte_ether_hdr);
  mkIpv4Header(ip_hdr, ip_length, src_ip, dst_ip);      // Does not set IPv4 header checksum
  int udp_length = ip_length - sizeof(rte_ipv4_hdr);        // No IP Options are used
  mkUdpHeader(udp_hd, udp_length, sport, dport);
  int data_legth = udp_length - sizeof(rte_udp_hdr);
  mkDataLatency(udp_data, data_legth, id);
  udp_hd->dgram_cksum = rte_ipv4_udptcp_cksum( ip_hdr, udp_hd ); // UDP checksum is calculated and set
  ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);
  return pkt_mbuf;
}

// creates a special IPv4 Test Frame tagged for latency measurement using mkFinalLatencyFrame4
// BEHAVIOR: if port number is 0, it is set according to RFC 2544 Test Frame format, otherwise it is set to 0, to be set later.
struct rte_mbuf *mkLatencyFrame4(uint16_t length, rte_mempool *pkt_pool, const char *side,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const uint32_t *src_ip, const uint32_t *dst_ip, unsigned var_sport, unsigned var_dport, int id) {
  // sport/dport are set to 0, if they will change, otherwise follow RFC 2544 Test Frame format
  struct rte_mbuf *pkt_mbuf=mkFinalLatencyFrame4(length,pkt_pool,side,dst_mac,src_mac,src_ip,dst_ip,var_sport ? 0 : 0xC020,var_dport ? 0 : 0x0007,id);
  // The above function terminated the Tester if it could not allocate memory, thus no error handling is needed here. :-)
  return pkt_mbuf;
}

// creates a special IPv6 Test Frame tagged for latency measurement using several helper functions
// BEHAVIOR: it sets exatly, what it is told to set :-)
struct rte_mbuf *mkFinalLatencyFrame6(uint16_t length, rte_mempool *pkt_pool, const char *side,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const struct in6_addr *src_ip, const struct in6_addr *dst_ip, unsigned sport, unsigned dport, uint16_t id) {
  struct rte_mbuf *pkt_mbuf=rte_pktmbuf_alloc(pkt_pool); // message buffer for the Latency Frame
  if ( !pkt_mbuf )
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Latency Frame! \n", side);
  length -=  RTE_ETHER_CRC_LEN; // exclude CRC from the frame length
  pkt_mbuf->pkt_len = pkt_mbuf->data_len = length; // set the length in both places
  uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbuf, uint8_t *); // Access the Test Frame in the message buffer
  rte_ether_hdr *eth_hdr = reinterpret_cast<struct rte_ether_hdr *>(pkt); // Ethernet header
  rte_ipv6_hdr *ip_hdr = reinterpret_cast<rte_ipv6_hdr *>(pkt+sizeof(rte_ether_hdr)); // IPv6 header
  rte_udp_hdr *udp_hd = reinterpret_cast<rte_udp_hdr *>(pkt+sizeof(rte_ether_hdr)+sizeof(rte_ipv6_hdr)); // UDP header
  uint8_t *udp_data = reinterpret_cast<uint8_t*>(pkt+sizeof(rte_ether_hdr)+sizeof(rte_ipv6_hdr)+sizeof(rte_udp_hdr)); // UDP data

  mkEthHeader(eth_hdr, dst_mac, src_mac, 0x86DD); // contains an IPv6 packet
  int ip_length = length - sizeof(rte_ether_hdr);
  mkIpv6Header(ip_hdr, ip_length, src_ip, dst_ip);
  int udp_length = ip_length - sizeof(rte_ipv6_hdr); // No IP Options are used
  mkUdpHeader(udp_hd, udp_length, sport, dport);
  int data_legth = udp_length - sizeof(rte_udp_hdr);
  mkDataLatency(udp_data, data_legth, id);
  udp_hd->dgram_cksum = rte_ipv6_udptcp_cksum( ip_hdr, udp_hd ); // UDP checksum is calculated and set
  return pkt_mbuf;
}

// creates a special IPv6 Test Frame tagged for latency measurement using mkFinalLatencyFrame6
// BEHAVIOR: if port number is 0, it is set according to RFC 2544 Test Frame format, otherwise it is set to 0, to be set later.
struct rte_mbuf *mkLatencyFrame6(uint16_t length, rte_mempool *pkt_pool, const char *side,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const struct in6_addr *src_ip, const struct in6_addr *dst_ip, unsigned var_sport, unsigned var_dport, uint16_t id) {
  // sport/dport are set to 0, if they will change, otherwise follow RFC 2544 Test Frame format
  struct rte_mbuf *pkt_mbuf=mkFinalLatencyFrame6(length,pkt_pool,side,dst_mac,src_mac,src_ip,dst_ip,var_sport ? 0 : 0xC020,var_dport ? 0 : 0x0007,id);
  // The above function terminated the Tester if it could not allocate memory, thus no error handling is needed here. :-)
  return pkt_mbuf;
}

// fills the data field of the Latency Frame
void mkDataLatency(uint8_t *data, uint16_t length, uint16_t latency_frame_id) {
  unsigned i;
  uint8_t identify[8]= { 'I', 'd', 'e', 'n', 't', 'i', 'f', 'y' };      // Identificion of the Latency Frames
  uint64_t *id=(uint64_t *) identify;
  *(uint64_t *)data = *id;
  data += 8;
  length -= 8;
  *(uint16_t *)data = latency_frame_id;
  data += 2;
  length -=2;
  for ( i=0; i<length; i++ )
    data[i] = i % 256;
}

// sends Test Frames for latency measurements including "num_timestamps" number of Latency frames
int sendLatency(void *par) {
  // collecting input parameters:
  class senderParametersLatency *p = (class senderParametersLatency *)par;
  class senderCommonParametersLatency *cp = (class senderCommonParametersLatency *) p->cp;

  // parameters directly correspond to the data members of class Throughput
  uint16_t ipv6_frame_size = cp->ipv6_frame_size;
  uint16_t ipv4_frame_size = cp->ipv4_frame_size;
  uint32_t frame_rate = cp->frame_rate;
  uint16_t duration = cp->duration;
  uint32_t n = cp->n;
  uint32_t m = cp->m;
  uint64_t hz = cp->hz;
  uint64_t start_tsc = cp->start_tsc;
  // parameters directly correspond to the data members of class Latency
  uint16_t delay = cp->delay;
  uint16_t num_timestamps = cp->num_timestamps;

  // parameters which are different for the Left sender and the Right sender
  int ip_version = p->ip_version;
  rte_mempool *pkt_pool = p->pkt_pool;
  uint16_t eth_id = p->eth_id;
  const char *side = p->side;
  struct ether_addr *dst_mac = p->dst_mac;
  struct ether_addr *src_mac = p->src_mac;
  uint16_t num_dest_nets = p->num_dest_nets;
  uint32_t *src_ipv4 = p->src_ipv4;
  uint32_t *dst_ipv4 = p->dst_ipv4;
  struct in6_addr *src_ipv6 = p->src_ipv6;
  struct in6_addr *dst_ipv6 = p->dst_ipv6;
  struct in6_addr *src_bg= p->src_bg;
  struct in6_addr *dst_bg = p->dst_bg;
  unsigned var_sport = p->var_sport;
  unsigned var_dport = p->var_dport;
  unsigned varport = var_sport || var_dport; // derived logical value: at least one port has to be changed?
  uint16_t sport_min = p->sport_min;
  uint16_t sport_max = p->sport_max;
  uint16_t dport_min = p->dport_min;
  uint16_t dport_max = p->dport_max;
  uint64_t *send_ts = p->send_ts;

  // further local variables
  uint64_t frames_to_send = duration * frame_rate;      // Each active sender sends this number of frames
  uint64_t sent_frames=0; // counts the number of sent frames
  double elapsed_seconds; // for checking the elapsed seconds during sending
  int latency_test_time = duration-delay;	// lenght of the time interval, while latency frames are sent
  uint64_t frames_to_send_during_latency_test = latency_test_time * frame_rate; // precalcalculated value to speed up calculation in the loop

  if ( !varport ) {
    // optimized code for using hard coded fix port numbers as defined in RFC 2544 https://tools.ietf.org/html/rfc2544#appendix-C.2.6.4
    if ( num_dest_nets== 1 ) {
      // optimized code for single detination network: always the same foreground or background frame is sent, 
      // except latency frames, which are stored in an array
      struct rte_mbuf *fg_pkt_mbuf, *bg_pkt_mbuf; // message buffers for fg. and bg. Test Frames
      // create foreground Test Frame
      if ( ip_version == 4 )
        fg_pkt_mbuf = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, dst_ipv4, 0, 0);
      else  // IPv6
        fg_pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, 0, 0);
  
      // create backround Test Frame (always IPv6)
      bg_pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg, 0, 0);
  
      // create Latency Test Frames (may be foreground frames and background frames as well)
      struct rte_mbuf ** latency_frames = new struct rte_mbuf *[num_timestamps];
      if ( !latency_frames )
        rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for latency frame pointers!\n");
  
      uint64_t start_latency_frame = delay*frame_rate; // the ordinal number of the very first latency frame
      for ( int i=0; i<num_timestamps; i++ )
        if ( (start_latency_frame+i*frame_rate*latency_test_time/num_timestamps) % n  < m ) {
          if ( ip_version == 4 )  // foreground frame, may be IPv4 or IPv6
            latency_frames[i] = mkLatencyFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, dst_ipv4, 0, 0, i);
          else  // IPv6
            latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, 0, 0, i);
        } else {
          // background frame, must be IPv6
          latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg, 0, 0, i);
        }
   
      // naive sender version: it is simple and fast
      int latency_timestamp_no=0; // counter for the latency frames from 0 to num_timestamps-1
      uint64_t send_next_latency_frame = start_latency_frame; // at what frame count to send the next latency frame 
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); // Beware: an "empty" loop, and further three will come!
        if ( unlikely( sent_frames == send_next_latency_frame ) ) {
          // a latency frame is to be sent
          while ( !rte_eth_tx_burst(eth_id, 0, &latency_frames[latency_timestamp_no], 1) ); // send latency frame
          send_ts[latency_timestamp_no++]=rte_rdtsc();
          send_next_latency_frame = start_latency_frame + latency_timestamp_no*frames_to_send_during_latency_test/num_timestamps; 
        } else {
          // normal test frame is to be sent
          if ( sent_frames % n  < m )
            while ( !rte_eth_tx_burst(eth_id, 0, &fg_pkt_mbuf, 1) ); // send foreground frame
          else
             while ( !rte_eth_tx_burst(eth_id, 0, &bg_pkt_mbuf, 1) ); // send background frame
        }
      } // this is the end of the sending cycle
    } // end of optimized code for single flow
    else {
      // optimized code for multiple destination networks: foreground and background frames are generated for each network and pointers are stored in arrays
      // latency frames are also pre-generated and stored in an array
      // assertion: num_dest_nets <= 256
      struct rte_mbuf *fg_pkt_mbuf[256], *bg_pkt_mbuf[256]; // message buffers for fg. and bg. Test Frames
      uint32_t curr_dst_ipv4;   // IPv4 destination address, which will be changed
      in6_addr curr_dst_ipv6;   // foreground IPv6 destination address, which will be changed
      in6_addr curr_dst_bg;     // backround IPv6 destination address, which will be changed
      int i;                    // cycle variable for grenerating different destination network addresses
      if ( ip_version == 4 )
        curr_dst_ipv4 = *dst_ipv4;
      else // IPv6
        curr_dst_ipv6 = *dst_ipv6;
      curr_dst_bg = *dst_bg;
  
      for ( i=0; i<num_dest_nets; i++ ) {
        // create foreground Test Frame
        if ( ip_version == 4 ) {
          ((uint8_t *)&curr_dst_ipv4)[2] = (uint8_t) i; // bits 16 to 23 of the IPv4 address are rewritten, like in 198.18.x.2
          fg_pkt_mbuf[i] = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, &curr_dst_ipv4, 0, 0);
        }
        else { // IPv6
          ((uint8_t *)&curr_dst_ipv6)[7] = (uint8_t) i; // bits 56 to 63 of the IPv6 address are rewritten, like in 2001:2:0:00xx::1
          fg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, &curr_dst_ipv6, 0, 0);
        }
        // create backround Test Frame (always IPv6)
        ((uint8_t *)&curr_dst_bg)[7] = (uint8_t) i; // see comment above
        bg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, &curr_dst_bg, 0, 0);
      }
  
      // random number infrastructure is taken from: https://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution
      // MT64 is used because of https://medium.com/@odarbelaeze/how-competitive-are-c-standard-random-number-generators-f3de98d973f0
      // thread_local is used on the basis of https://stackoverflow.com/questions/40655814/is-mersenne-twister-thread-safe-for-cpp
      thread_local std::random_device rd;  //Will be used to obtain a seed for the random number engine
      thread_local std::mt19937_64 gen(rd()); //Standard 64-bit mersenne_twister_engine seeded with rd()
      std::uniform_int_distribution<int> uni_dis(0, num_dest_nets-1);     // uniform distribution in [0, num_dest_nets-1]
  
      // create Latency Test Frames (may be foreground frames and background frames as well)
      struct rte_mbuf ** latency_frames = new struct rte_mbuf *[num_timestamps];
      if ( !latency_frames )
        rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for latency frame pointers!\n");
  
      uint64_t start_latency_frame = delay*frame_rate; // the ordinal number of the very first latency frame
      for ( int i=0; i<num_timestamps; i++ )
        if ( (start_latency_frame+i*frame_rate*latency_test_time/num_timestamps) % n  < m ) {
          if ( ip_version == 4 ) { 
            // random IPv4 destination network
            ((uint8_t *)&curr_dst_ipv4)[2] = (uint8_t) uni_dis(gen); // bits 16 to 23 of the IPv4 address are rewritten, like in 198.18.x.2
            latency_frames[i] = mkLatencyFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, &curr_dst_ipv4, 0, 0, i);
          } else {
            // random IPv6 destination network
            ((uint8_t *)&curr_dst_ipv6)[7] = (uint8_t) i; // bits 56 to 63 of the IPv6 address are rewritten, like in 2001:2:0:00xx::1
            latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, &curr_dst_ipv6, 0, 0, i);
          }
        } else {
          // background frame, must be IPv6, choose random network
          ((uint8_t *)&curr_dst_bg)[7] = (uint8_t) i; // see comment above
          latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, &curr_dst_bg, 0, 0, i);
        }
  
      // naive sender version: it is simple and fast
      int latency_timestamp_no=0; // counter for the latency frames from 0 to num_timestamps-1
      uint64_t send_next_latency_frame = start_latency_frame; // at what frame count to send the next latency frame
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        int index = uni_dis(gen); // index of the pre-generated frame (it will not be used, when a latency frame is sent)
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); // Beware: an "empty" loop, and further two will come!
        if ( unlikely( sent_frames == send_next_latency_frame ) ) {
          // a latency frame is to be sent
          while ( !rte_eth_tx_burst(eth_id, 0, &latency_frames[latency_timestamp_no], 1) ); // send latency frame
          send_ts[latency_timestamp_no++]=rte_rdtsc();
          send_next_latency_frame = start_latency_frame + latency_timestamp_no*frames_to_send_during_latency_test/num_timestamps;
        } else {
          // normal test frame is to be sent
          if ( sent_frames % n  < m )
            while ( !rte_eth_tx_burst(eth_id, 0, &fg_pkt_mbuf[index], 1) ); // send foreground frame
          else
             while ( !rte_eth_tx_burst(eth_id, 0, &bg_pkt_mbuf[index], 1) ); // send background frame
        }
      } // this is the end of the sending cycle
    } // end of optimized code for multiple destination networks
  } // end of optimized code for fixed port numbers
  else {
    // implementation of varying port numbers recommended by RFC 4814 https://tools.ietf.org/html/rfc4814#section-4.5
    // RFC 4814 requires pseudorandom port numbers, increasing and decreasing ones are our additional, non-stantard solutions
    if ( num_dest_nets== 1 ) {
      // optimized code for single detination network: always one of the same N pre-prepared foreground or background frames is updated and sent,
      // except latency frames, which are stored in an array and updated only once, thus no N copies are necessary
      // source and/or destination port number(s) and UDP checksum are updated
      // as for foreground or background frames, N size arrays are used to resolve the write after send problem
      int i; // cycle variable for the above mentioned purpose: takes {0..N-1} values
      struct rte_mbuf *fg_pkt_mbuf[N], *bg_pkt_mbuf[N], *pkt_mbuf; // message buffers for fg. and bg. Test Frames
      uint8_t *pkt; // working pointer to the current frame (in the message buffer)
      uint8_t *fg_udp_sport[N], *fg_udp_dport[N], *fg_udp_chksum[N], *bg_udp_sport[N], *bg_udp_dport[N], *bg_udp_chksum[N]; // pointers to the given fields
      uint8_t *lat_udp_sport[num_timestamps], *lat_udp_dport[num_timestamps], *lat_udp_chksum[num_timestamps]; // pointers to the given fields
      uint16_t *udp_sport, *udp_dport, *udp_chksum; // working pointers to the given fields
      uint16_t fg_udp_chksum_start, bg_udp_chksum_start;  // starting values (uncomplemented checksums taken from the original frames)
      uint32_t chksum; // temporary variable for shecksum calculation
      uint16_t sport, dport; // values of source and destination port numbers -- to be preserved, when increase or decrease is done
      uint16_t sp, dp; // values of source and destination port numbers -- temporary values

      for ( i=0; i<N; i++ ) {
        // create foreground Test Frame
        if ( ip_version == 4 ) {
          fg_pkt_mbuf[i] = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, dst_ipv4, var_sport, var_dport);
          pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
          fg_udp_sport[i] = pkt + 34;
          fg_udp_dport[i] = pkt + 36;
          fg_udp_chksum[i] = pkt + 40;
        } else { // IPv6
          fg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, var_sport, var_dport);
          pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
          fg_udp_sport[i] = pkt + 54;
          fg_udp_dport[i] = pkt + 56;
          fg_udp_chksum[i] = pkt + 60;
	}
	fg_udp_chksum_start = ~*(uint16_t *)fg_udp_chksum[i]; // save the uncomplemented checksum value (same for all values of "i")
    
        // create backround Test Frame (always IPv6)
        bg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg, var_sport, var_dport);
        pkt = rte_pktmbuf_mtod(bg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
        bg_udp_sport[i] = pkt + 54;
        bg_udp_dport[i] = pkt  + 56;
        bg_udp_chksum[i] = pkt + 60;
        bg_udp_chksum_start = ~*(uint16_t *)bg_udp_chksum[i]; // save the uncomplemented checksum value (same for all values of "i")
      } 
      // create Latency Test Frames (may be foreground frames and background frames as well)
      struct rte_mbuf ** latency_frames = new struct rte_mbuf *[num_timestamps];
      if ( !latency_frames )
        rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for latency frame pointers!\n");
  
      uint64_t start_latency_frame = delay*frame_rate; // the ordinal number of the very first latency frame
      for ( int i=0; i<num_timestamps; i++ )
        if ( (start_latency_frame+i*frame_rate*latency_test_time/num_timestamps) % n  < m ) {
	  // foreground frame, may be IPv4 or IPv6
          if ( ip_version == 4 ) {
            latency_frames[i] = mkLatencyFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, dst_ipv4, var_sport, var_dport, i);
            pkt = rte_pktmbuf_mtod(latency_frames[i], uint8_t *); // Access the Test Frame in the message buffer
            lat_udp_sport[i] = pkt + 34;
            lat_udp_dport[i] = pkt + 36;
            lat_udp_chksum[i] = pkt + 40;
          } else { // IPv6
            latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, var_sport, var_dport, i);
            pkt = rte_pktmbuf_mtod(latency_frames[i], uint8_t *); // Access the Test Frame in the message buffer
            lat_udp_sport[i] = pkt + 54;
            lat_udp_dport[i] = pkt + 56;
            lat_udp_chksum[i] = pkt + 60;
	  }
        } else {
          // background frame, must be IPv6
          latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg, var_sport, var_dport, i);
          pkt = rte_pktmbuf_mtod(latency_frames[i], uint8_t *); // Access the Test Frame in the message buffer
          lat_udp_sport[i] = pkt + 54;
          lat_udp_dport[i] = pkt + 56;
          lat_udp_chksum[i] = pkt + 60;
        }
  
      // set the starting values of port numbers, if they are increased or decreased
      if ( var_sport == 1 )
        sport = sport_min;
      if ( var_sport == 2 )
        sport = sport_max;
      if ( var_dport == 1 )
        dport = dport_min;
      if ( var_dport == 2 )
        dport = dport_max;

      // prepare random number infrastructure
      thread_local std::random_device rd;  // Will be used to obtain a seed for the random number engines
      thread_local std::mt19937_64 gen_sport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      thread_local std::mt19937_64 gen_dport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      std::uniform_int_distribution<int> uni_dis_sport(sport_min, sport_max);   // uniform distribution in [sport_min, sport_max]
      std::uniform_int_distribution<int> uni_dis_dport(dport_min, dport_max);   // uniform distribution in [dport_min, dport_max]
 
      // naive sender version: it is simple and fast
      i=0; // increase maunally after each sending of a normal Test Frame 
      int latency_timestamp_no=0; // counter for the latency frames from 0 to num_timestamps-1
      uint64_t send_next_latency_frame = start_latency_frame; // at what frame count to send the next latency frame 
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        if ( unlikely( sent_frames == send_next_latency_frame ) ) {
          // a latency frame is to be sent
          chksum = ~*(uint16_t *)lat_udp_chksum[latency_timestamp_no]; // take the value of the uncomplemented checksum
          udp_sport = (uint16_t *)lat_udp_sport[latency_timestamp_no];
          udp_dport = (uint16_t *)lat_udp_dport[latency_timestamp_no];
          udp_chksum = (uint16_t *)lat_udp_chksum[latency_timestamp_no];
          pkt_mbuf = latency_frames[latency_timestamp_no];
        } else {
          // normal test frame is to be sent
          if ( sent_frames % n  < m ) {
            // foreground frame is to be sent
            chksum = fg_udp_chksum_start;
            udp_sport = (uint16_t *)fg_udp_sport[i];
            udp_dport = (uint16_t *)fg_udp_dport[i];
            udp_chksum = (uint16_t *)fg_udp_chksum[i];
            pkt_mbuf = fg_pkt_mbuf[i];
          } else {
            // background frame is to be sent
            chksum = bg_udp_chksum_start;
            udp_sport = (uint16_t *)bg_udp_sport[i];
            udp_dport = (uint16_t *)bg_udp_dport[i];
            udp_chksum = (uint16_t *)bg_udp_chksum[i];
            pkt_mbuf = bg_pkt_mbuf[i];
          }
        }
        // from here, we need to handle the frame identified by the temprary variables
        if ( var_sport ) {
          // sport is varying
          switch ( var_sport ) {
            case 1:                     // increasing port numbers
              if ( (sp=sport++) == sport_max )
                sport = sport_min;
              break;
            case 2:                     // decreasing port numbers
              if ( (sp=sport--) == sport_min )
                sport = sport_max;
              break;
            case 3:                     // pseudorandom port numbers
              sp = uni_dis_sport(gen_sport);
          }
          chksum += *udp_sport = htons(sp);     // set source port and add to checksum -- corrected
        }
        if ( var_dport ) {
          // dport is varying
          switch ( var_dport ) {
            case 1:                     // increasing port numbers
              if ( (dp=dport++) == dport_max )
                dport = dport_min;
              break;
            case 2:                     // decreasing port numbers
              if ( (dp=dport--) == dport_min )
                dport = dport_max;
              break;
            case 3:                     // pseudorandom port numbers
              dp = uni_dis_dport(gen_dport);
          }
          chksum += *udp_dport = htons(dp);     // set destination port add to checksum -- corrected
        }
        chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);     // calculate 16-bit one's complement sum
        chksum = (~chksum) & 0xffff;                                    // make one's complement
        if (chksum == 0)                                                // checksum should not be 0 (0 means, no checksum is used)
          chksum = 0xffff;
        *udp_chksum = (uint16_t) chksum;            // set checksum in the frame
        // finally, when its time is here, send the frame
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate );    // Beware: an "empty" loop, as well as in the next line
        while ( !rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1) );           // send out the frame
        if ( unlikely( sent_frames == send_next_latency_frame ) ) {
          // the sent frame was a Latency Frame
          send_ts[latency_timestamp_no++]=rte_rdtsc(); // store its sending timestamp
          send_next_latency_frame = start_latency_frame + latency_timestamp_no*frames_to_send_during_latency_test/num_timestamps; 
	} else {
          // the sent frame was a normal Test Frame
          i = (i+1) % N;
        }
      } // this is the end of the sending cycle
    } // end of optimized code for single destination network
    else {
      // optimized code for multiple destination networks: foreground and background frames are generated for each network and pointers are stored in arrays
      // latency frames are also pre-generated and stored in an array
      // as for foreground or background frames, N size arrays are used to resolve the write after send problem
      // source and/or destination port number(s) and UDP checksum are updated in the actually used copy before sending
      // assertion: num_dest_nets <= 256
      int j; // cycle variable to index the N size array: takes {0..N-1} values
      struct rte_mbuf *fg_pkt_mbuf[256][N], *bg_pkt_mbuf[256][N],  *pkt_mbuf; // message buffers for fg. and bg. Test Frames
      uint8_t *pkt; // working pointer to the current frame (in the message buffer)
      uint8_t *fg_udp_sport[256][N], *fg_udp_dport[256][N], *fg_udp_chksum[256][N]; // pointers to the given fields of the pre-prepared Test Frames
      uint8_t *bg_udp_sport[256][N], *bg_udp_dport[256][N], *bg_udp_chksum[256][N]; // pointers to the given fields of the pre-prepared Test Frames
      uint8_t *lat_udp_sport[num_timestamps], *lat_udp_dport[num_timestamps], *lat_udp_chksum[num_timestamps]; // pointers to the given fields
      uint16_t *udp_sport, *udp_dport, *udp_chksum; // working pointers to the given fields
      uint16_t fg_udp_chksum_start[256], bg_udp_chksum_start[256];  // starting values (uncomplemented checksums taken from the original frames)
      uint32_t chksum; // temporary variable for shecksum calculation
      uint16_t sport, dport; // values of source and destination port numbers -- to be preserved, when increase or decrease is done
      uint16_t sp, dp; // values of source and destination port numbers -- temporary values
      uint32_t curr_dst_ipv4;   // IPv4 destination address, which will be changed
      in6_addr curr_dst_ipv6;   // foreground IPv6 destination address, which will be changed
      in6_addr curr_dst_bg;     // backround IPv6 destination address, which will be changed
      int i;                    // cycle variable for grenerating different destination network addresses
      if ( ip_version == 4 )
        curr_dst_ipv4 = *dst_ipv4;
      else // IPv6
        curr_dst_ipv6 = *dst_ipv6;
      curr_dst_bg = *dst_bg;
  
      for ( j=0; j<N; j++ ) {
        for ( i=0; i<num_dest_nets; i++ ) {
          // create foreground Test Frame
          if ( ip_version == 4 ) {
            ((uint8_t *)&curr_dst_ipv4)[2] = (uint8_t) i; // bits 16 to 23 of the IPv4 address are rewritten, like in 198.18.x.2
            fg_pkt_mbuf[i][j] = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, &curr_dst_ipv4, var_sport, var_dport);
            pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i][j], uint8_t *); // Access the Test Frame in the message buffer
            fg_udp_sport[i][j] = pkt + 34;
            fg_udp_dport[i][j] = pkt + 36;
            fg_udp_chksum[i][j] = pkt + 40;
          }
          else { // IPv6
            ((uint8_t *)&curr_dst_ipv6)[7] = (uint8_t) i; // bits 56 to 63 of the IPv6 address are rewritten, like in 2001:2:0:00xx::1
            fg_pkt_mbuf[i][j] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, &curr_dst_ipv6, var_sport, var_dport);
            pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i][j], uint8_t *); // Access the Test Frame in the message buffer
            fg_udp_sport[i][j] = pkt + 54;
            fg_udp_dport[i][j] = pkt + 56;
            fg_udp_chksum[i][j] = pkt + 60;
          }
          fg_udp_chksum_start[i] = ~*(uint16_t *)fg_udp_chksum[i][j]; // save the uncomplemented checksum value (same for all values of "j")
          // create backround Test Frame (always IPv6)
          ((uint8_t *)&curr_dst_bg)[7] = (uint8_t) i; // see comment above
          bg_pkt_mbuf[i][j] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, &curr_dst_bg, var_sport, var_dport);
          pkt = rte_pktmbuf_mtod(bg_pkt_mbuf[i][j], uint8_t *); // Access the Test Frame in the message buffer
          bg_udp_sport[i][j] = pkt + 54;
          bg_udp_dport[i][j] = pkt  + 56;
          bg_udp_chksum[i][j] = pkt + 60;
          bg_udp_chksum_start[i] = ~*(uint16_t *)bg_udp_chksum[i][j]; // save the uncomplemented checksum value (same for all values of "j")
        }
      }
  
      // random number infrastructure is taken from: https://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution
      // MT64 is used because of https://medium.com/@odarbelaeze/how-competitive-are-c-standard-random-number-generators-f3de98d973f0
      // thread_local is used on the basis of https://stackoverflow.com/questions/40655814/is-mersenne-twister-thread-safe-for-cpp
      thread_local std::random_device rd;  //Will be used to obtain a seed for the random number engine
      thread_local std::mt19937_64 gen_net(rd()); //Standard 64-bit mersenne_twister_engine seeded with rd()
      thread_local std::mt19937_64 gen_sport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      thread_local std::mt19937_64 gen_dport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      std::uniform_int_distribution<int> uni_dis_net(0, num_dest_nets-1);     // uniform distribution in [0, num_dest_nets-1]
      std::uniform_int_distribution<int> uni_dis_sport(sport_min, sport_max);   // uniform distribution in [sport_min, sport_max]
      std::uniform_int_distribution<int> uni_dis_dport(dport_min, dport_max);   // uniform distribution in [dport_min, dport_max]
 
      // create Latency Test Frames (may be foreground frames and background frames as well)
      struct rte_mbuf ** latency_frames = new struct rte_mbuf *[num_timestamps];
      if ( !latency_frames )
        rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for latency frame pointers!\n");
  
      uint64_t start_latency_frame = delay*frame_rate; // the ordinal number of the very first latency frame
      for ( int i=0; i<num_timestamps; i++ )
        if ( (start_latency_frame+i*frame_rate*latency_test_time/num_timestamps) % n  < m ) {
          // foreground frame
          if ( ip_version == 4 ) { 
            // random IPv4 destination network
            ((uint8_t *)&curr_dst_ipv4)[2] = (uint8_t) uni_dis_net(gen_net); // bits 16 to 23 of the IPv4 address are rewritten, like in 198.18.x.2
            latency_frames[i] = mkLatencyFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, &curr_dst_ipv4, var_sport, var_dport, i);
            pkt = rte_pktmbuf_mtod(latency_frames[i], uint8_t *); // Access the Test Frame in the message buffer
            lat_udp_sport[i] = pkt + 34;
            lat_udp_dport[i] = pkt + 36;
            lat_udp_chksum[i] = pkt + 40;
          } else {
            // random IPv6 destination network
            ((uint8_t *)&curr_dst_ipv6)[7] = (uint8_t) i; // bits 56 to 63 of the IPv6 address are rewritten, like in 2001:2:0:00xx::1
            latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, &curr_dst_ipv6, var_sport, var_dport, i);
            pkt = rte_pktmbuf_mtod(latency_frames[i], uint8_t *); // Access the Test Frame in the message buffer
            lat_udp_sport[i] = pkt + 54;
            lat_udp_dport[i] = pkt + 56;
            lat_udp_chksum[i] = pkt + 60;
          }
        } else {
          // background frame, must be IPv6, choose random network
          ((uint8_t *)&curr_dst_bg)[7] = (uint8_t) i; // see comment above
          latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, &curr_dst_bg, var_sport, var_dport, i);
          pkt = rte_pktmbuf_mtod(latency_frames[i], uint8_t *); // Access the Test Frame in the message buffer
          lat_udp_sport[i] = pkt + 54;
          lat_udp_dport[i] = pkt + 56;
          lat_udp_chksum[i] = pkt + 60;
        }
  
      // set the starting values of port numbers, if they are increased or decreased
      if ( var_sport == 1 )
        sport = sport_min;
      if ( var_sport == 2 )
        sport = sport_max;
      if ( var_dport == 1 )
        dport = dport_min;
      if ( var_dport == 2 )
        dport = dport_max;

      // naive sender version: it is simple and fast
      i=0; // increase maunally after each sending of a normal Test Frame
      int latency_timestamp_no=0; // counter for the latency frames from 0 to num_timestamps-1
      uint64_t send_next_latency_frame = start_latency_frame; // at what frame count to send the next latency frame
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        if ( unlikely( sent_frames == send_next_latency_frame ) ) {
          // a latency frame is to be sent
          chksum = ~*(uint16_t *)lat_udp_chksum[latency_timestamp_no]; // take the value of the uncomplemented checksum
          udp_sport = (uint16_t *)lat_udp_sport[latency_timestamp_no];
          udp_dport = (uint16_t *)lat_udp_dport[latency_timestamp_no];
          udp_chksum = (uint16_t *)lat_udp_chksum[latency_timestamp_no];
          pkt_mbuf = latency_frames[latency_timestamp_no];
        } else {
          // normal test frame is to be sent
          int index = uni_dis_net(gen_net); // index of the pre-generated frame 
          if ( sent_frames % n  < m ) {
            // foreground frame is to be sent
            chksum = fg_udp_chksum_start[index];
            udp_sport = (uint16_t *)fg_udp_sport[index][i];
            udp_dport = (uint16_t *)fg_udp_dport[index][i];
            udp_chksum = (uint16_t *)fg_udp_chksum[index][i];
            pkt_mbuf = fg_pkt_mbuf[index][i];
          } else {
            // background frame is to be sent
            chksum = bg_udp_chksum_start[index];
            udp_sport = (uint16_t *)bg_udp_sport[index][i];
            udp_dport = (uint16_t *)bg_udp_dport[index][i];
            udp_chksum = (uint16_t *)bg_udp_chksum[index][i];
            pkt_mbuf = bg_pkt_mbuf[index][i];
          }
        }
        // from here, we need to handle the frame identified by the temprary variables
        if ( var_sport ) {
          // sport is varying
          switch ( var_sport ) {
            case 1:                     // increasing port numbers
              if ( (sp=sport++) == sport_max )
                sport = sport_min;
              break;
            case 2:                     // decreasing port numbers
              if ( (sp=sport--) == sport_min )
                sport = sport_max;
              break;
            case 3:                     // pseudorandom port numbers
              sp = uni_dis_sport(gen_sport);
          }
          chksum += *udp_sport = htons(sp);     // set source port and add to checksum -- corrected
        }
        if ( var_dport ) {
          // dport is varying
          switch ( var_dport ) {
            case 1:                     // increasing port numbers
              if ( (dp=dport++) == dport_max )
                dport = dport_min;
              break;
            case 2:                     // decreasing port numbers
              if ( (dp=dport--) == dport_min )
                dport = dport_max;
              break;
            case 3:                     // pseudorandom port numbers
              dp = uni_dis_dport(gen_dport);
          }
          chksum += *udp_dport = htons(dp);     // set destination port add to checksum -- corrected
        }
        chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);     // calculate 16-bit one's complement sum
        chksum = (~chksum) & 0xffff;                                    // make one's complement
        if (chksum == 0)                                                // checksum should not be 0 (0 means, no checksum is used)
          chksum = 0xffff;
        *udp_chksum = (uint16_t) chksum;            // set checksum in the frame
        // finally, when its time is here, send the frame
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate );    // Beware: an "empty" loop, as well as in the next line
        while ( !rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1) );           // send out the frame
        if ( unlikely( sent_frames == send_next_latency_frame ) ) {
          // the sent frame was a Latency Frame
          send_ts[latency_timestamp_no++]=rte_rdtsc(); // store its sending timestamp
          send_next_latency_frame = start_latency_frame + latency_timestamp_no*frames_to_send_during_latency_test/num_timestamps;
        } else {
          // the sent frame was a normal Test Frame
          i = (i+1) % N;
        }
      } // this is the end of the sending cycle
    } // end of optimized code for multiple destination networks
  } // end of implementation of varying port numbers

  // Now, we check the time
  elapsed_seconds = (double)(rte_rdtsc()-start_tsc)/hz;
  printf("Info: %s sender's sending took %3.10lf seconds.\n", side, elapsed_seconds);
  if ( elapsed_seconds > duration*TOLERANCE )
    printf("Warning: %s sending exceeded the %3.10lf seconds limit, the test is invalid.\n", side, duration*TOLERANCE);
  else
    printf("%s frames sent: %lu\n", side, sent_frames);
  return 0;
}


// Responder/Sender: sends Test Frames for latency measurements including "num_timestamps" number of Latency frames
int rsendLatency(void *par) {
  // collecting input parameters:
  class rSenderParametersLatency *p = (class rSenderParametersLatency *)par;
  class senderCommonParametersLatency *cp = (class senderCommonParametersLatency *) p->cp;

  // parameters directly correspond to the data members of class Throughput
  uint16_t ipv6_frame_size = cp->ipv6_frame_size;
  uint16_t ipv4_frame_size = cp->ipv4_frame_size;
  uint32_t frame_rate = cp->frame_rate;
  uint16_t duration = cp->duration;
  uint32_t n = cp->n;
  uint32_t m = cp->m;
  uint64_t hz = cp->hz;
  uint64_t start_tsc = cp->start_tsc;
  // parameters directly correspond to the data members of class Latency
  uint16_t delay = cp->delay;
  uint16_t num_timestamps = cp->num_timestamps;

  // parameters which are different for the Left sender and the Right sender
  int ip_version = p->ip_version;
  rte_mempool *pkt_pool = p->pkt_pool;
  uint16_t eth_id = p->eth_id;
  const char *side = p->side;
  struct ether_addr *dst_mac = p->dst_mac;
  struct ether_addr *src_mac = p->src_mac;
  uint16_t num_dest_nets = p->num_dest_nets;
  uint32_t *src_ipv4 = p->src_ipv4;
  uint32_t *dst_ipv4 = p->dst_ipv4;
  struct in6_addr *src_ipv6 = p->src_ipv6;
  struct in6_addr *dst_ipv6 = p->dst_ipv6;
  struct in6_addr *src_bg= p->src_bg;
  struct in6_addr *dst_bg = p->dst_bg;
  unsigned var_sport = p->var_sport;
  unsigned var_dport = p->var_dport;
  unsigned varport = var_sport || var_dport; // derived logical value: at least one port has to be changed?
  uint16_t sport_min = p->sport_min;
  uint16_t sport_max = p->sport_max;
  uint16_t dport_min = p->dport_min;
  uint16_t dport_max = p->dport_max;
  uint64_t *send_ts = p->send_ts;

  // parameters directly correspond to the data members of class rSenderParameters
  unsigned state_table_size = p->state_table_size;
  atomicFourTuple *stateTable = p->stateTable;
  unsigned responder_tuples = p->responder_tuples;


  // further local variables
  uint64_t frames_to_send = duration * frame_rate;      // Each active sender sends this number of frames
  uint64_t sent_frames=0; // counts the number of sent frames
  double elapsed_seconds; // for checking the elapsed seconds during sending
  int latency_test_time = duration-delay;	// lenght of the time interval, while latency frames are sent
  uint64_t frames_to_send_during_latency_test = latency_test_time * frame_rate; // precalcalculated value to speed up calculation in the loop

  unsigned index;       // current state table index for reading a 4-tuple (used when 'responder-ports' is 1 or 2)
  fourTuple ft;         // 4-tuple is read from the state table into this
  bool fg_frame;        // the current frame belongs to the foreground traffic: will be handled in a stateful way (if it is IPv4)
  if ( !responder_tuples ) {
    // optimized code for using a single 4-tuple taken from the very first preliminary frame (as foreground traffic)
    // ( similar to using hard coded fix port numbers as defined in RFC 2544 https://tools.ietf.org/html/rfc2544#appendix-C.2.6.4 )
    ft = stateTable[0];   // read only once
    uint16_t resp_port = ntohs(ft.resp_port); // our functions expect port numbers in host byte order
    uint16_t init_port = ntohs(ft.init_port); // our functions expect port numbers in host byte order

    if ( num_dest_nets== 1 ) {
      // optimized code for single detination network: always the same foreground or background frame is sent, 
      // except latency frames, which are stored in an array
      struct rte_mbuf *fg_pkt_mbuf, *bg_pkt_mbuf; // message buffers for fg. and bg. Test Frames
      // create foreground Test Frame
      if ( ip_version == 4 )
        fg_pkt_mbuf = mkFinalTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, &ft.resp_addr, &ft.init_addr, resp_port, init_port);
      else  // IPv6 -- stateful operation is not yet supported!
        fg_pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, 0, 0);
  
      // create backround Test Frame (always IPv6)
      bg_pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg, 0, 0);
  
      // create Latency Test Frames (may be foreground frames and background frames as well)
      struct rte_mbuf ** latency_frames = new struct rte_mbuf *[num_timestamps];
      if ( !latency_frames )
        rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for latency frame pointers!\n");
  
      uint64_t start_latency_frame = delay*frame_rate; // the ordinal number of the very first latency frame
      for ( int i=0; i<num_timestamps; i++ )
        if ( (start_latency_frame+i*frame_rate*latency_test_time/num_timestamps) % n  < m ) {
          if ( ip_version == 4 )  // foreground frame, may be IPv4 or IPv6
            latency_frames[i] = mkFinalLatencyFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, &ft.resp_addr, &ft.init_addr, resp_port, init_port, i);
          else  // IPv6 -- stateful operation is not yet supported!
            latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, 0, 0, i);
        } else {
          // background frame, must be IPv6
          latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg, 0, 0, i);
        }
   
      // naive sender version: it is simple and fast
      int latency_timestamp_no=0; // counter for the latency frames from 0 to num_timestamps-1
      uint64_t send_next_latency_frame = start_latency_frame; // at what frame count to send the next latency frame 
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); // Beware: an "empty" loop, and further three will come!
        if ( unlikely( sent_frames == send_next_latency_frame ) ) {
          // a latency frame is to be sent
          while ( !rte_eth_tx_burst(eth_id, 0, &latency_frames[latency_timestamp_no], 1) ); // send latency frame
          send_ts[latency_timestamp_no++]=rte_rdtsc();
          send_next_latency_frame = start_latency_frame + latency_timestamp_no*frames_to_send_during_latency_test/num_timestamps; 
        } else {
          // normal test frame is to be sent
          if ( sent_frames % n  < m )
            while ( !rte_eth_tx_burst(eth_id, 0, &fg_pkt_mbuf, 1) ); // send foreground frame
          else
             while ( !rte_eth_tx_burst(eth_id, 0, &bg_pkt_mbuf, 1) ); // send background frame
        }
      } // this is the end of the sending cycle
    } // end of optimized code for single flow
    else {
      // optimized code for multiple destination networks  -- only regarding background traffic!
      // always the same foreground frame is sent!
      // background frames are generated for each network and pointers are stored in an array
      // latency frames are also pre-generated and stored in an array
      // assertion: num_dest_nets <= 256
      struct rte_mbuf *fg_pkt_mbuf, *bg_pkt_mbuf[256]; // message buffers for fg. and bg. Test Frames
      in6_addr curr_dst_bg;     // backround IPv6 destination address, which will be changed
      int i;                    // cycle variable for grenerating different destination network addresses

      curr_dst_bg = *dst_bg;
  
      // create foreground Test Frame
      if ( ip_version == 4 ) {
        fg_pkt_mbuf = mkFinalTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, &ft.resp_addr, &ft.init_addr, resp_port, init_port);
      }
      else { // IPv6 -- stateful operation is not yet supported!
        fg_pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, 0, 0);
      }
      for ( i=0; i<num_dest_nets; i++ ) {
        // create backround Test Frame (always IPv6)
        ((uint8_t *)&curr_dst_bg)[7] = (uint8_t) i; // see comment above
        bg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, &curr_dst_bg, 0, 0);
      }
  
      // random number infrastructure is taken from: https://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution
      // MT64 is used because of https://medium.com/@odarbelaeze/how-competitive-are-c-standard-random-number-generators-f3de98d973f0
      // thread_local is used on the basis of https://stackoverflow.com/questions/40655814/is-mersenne-twister-thread-safe-for-cpp
      thread_local std::random_device rd;  //Will be used to obtain a seed for the random number engine
      thread_local std::mt19937_64 gen(rd()); //Standard 64-bit mersenne_twister_engine seeded with rd()
      std::uniform_int_distribution<int> uni_dis_net(0, num_dest_nets-1);     // uniform distribution in [0, num_dest_nets-1]
  
      // create Latency Test Frames (may be foreground frames and background frames as well)
      struct rte_mbuf ** latency_frames = new struct rte_mbuf *[num_timestamps];
      if ( !latency_frames )
        rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for latency frame pointers!\n");
  
      uint64_t start_latency_frame = delay*frame_rate; // the ordinal number of the very first latency frame
      for ( int i=0; i<num_timestamps; i++ )
        if ( (start_latency_frame+i*frame_rate*latency_test_time/num_timestamps) % n  < m ) {
          if ( ip_version == 4 ) { 
            latency_frames[i] = mkFinalLatencyFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, &ft.resp_addr, &ft.init_addr, resp_port, init_port, i);

          } else {	// IPv6 -- stateful operation is not yet supported!
            latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, 0, 0, i);
          }
        } else {
          // background frame, must be IPv6, choose random network
          ((uint8_t *)&curr_dst_bg)[7] = (uint8_t) i; // see comment above
          latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, &curr_dst_bg, 0, 0, i);
        }
  
      // naive sender version: it is simple and fast
      int latency_timestamp_no=0; // counter for the latency frames from 0 to num_timestamps-1
      uint64_t send_next_latency_frame = start_latency_frame; // at what frame count to send the next latency frame
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); // Beware: an "empty" loop, and further two will come!
        if ( unlikely( sent_frames == send_next_latency_frame ) ) {
          // a latency frame is to be sent
          while ( !rte_eth_tx_burst(eth_id, 0, &latency_frames[latency_timestamp_no], 1) ); // send latency frame
          send_ts[latency_timestamp_no++]=rte_rdtsc();
          send_next_latency_frame = start_latency_frame + latency_timestamp_no*frames_to_send_during_latency_test/num_timestamps;
        } else {
          // normal test frame is to be sent
          if ( sent_frames % n  < m )
            while ( !rte_eth_tx_burst(eth_id, 0, &fg_pkt_mbuf, 1) ); // send foreground frame
          else {
             int net_index = uni_dis_net(gen); // index of the pre-generated background frame 
             while ( !rte_eth_tx_burst(eth_id, 0, &bg_pkt_mbuf[net_index], 1) ); // send background frame
          }
        }
      } // this is the end of the sending cycle
    } // end of optimized code for multiple destination networks
  } // end of optimized code for fixed port numbers
  else {
    // responder-port values of 1, 2 and 3 are handled together here
    // 1: index for reading four tuple is increased from 0 to state_table_size-1
    // 2: index for reading four tuple is decreases from state_table_size-1 to 0
    // 3: index for reading four tuple is pseudorandom in the range of [0, state_table_size-1]
    // 3 is believed to be the best implementation of RFC 4814 pseudorandom port numbers for stateful tests,
    // increasing and decreasing ones are our additional, non-stantard, computationally cheaper solutions
    if ( responder_tuples == 1 )
      index = 0;
    else if ( responder_tuples == 2 )
      index = state_table_size-1;
    uint32_t ipv4_zero = 0;     // IPv4 address 0.0.0.0 used as a placeholder for UDP checksum calculation (value will be set later)
    if ( num_dest_nets== 1 ) {
      // optimized code for single detination network: always one of the same N pre-prepared foreground or background frames is updated and sent,
      // except latency frames, which are stored in an array and updated only once, thus no N copies are necessary
      // IPv4 addresses, source and destination port number(s) and UDP checksum are updated in the actually used copy.
      // as for foreground or background frames, N size arrays are used to resolve the write after send problem
      int i; // cycle variable for the above mentioned purpose: takes {0..N-1} values
      struct rte_mbuf *fg_pkt_mbuf[N], *bg_pkt_mbuf[N], *pkt_mbuf; // message buffers for fg. and bg. Test Frames
      uint8_t *pkt; // working pointer to the current frame (in the message buffer)
      uint8_t *fg_udp_sport[N], *fg_udp_dport[N], *fg_udp_chksum[N], *bg_udp_sport[N], *bg_udp_dport[N], *bg_udp_chksum[N]; // pointers to the given fields
      uint8_t *fg_rte_ipv4_hdr[N], *fg_ipv4_chksum[N], *fg_ipv4_src[N], *fg_ipv4_dst[N]; // further ones for stateful tests
      rte_ipv4_hdr *rte_ipv4_hdr_start; // used for IPv4 header checksum calculation
      uint8_t *lat_udp_sport[num_timestamps], *lat_udp_dport[num_timestamps], *lat_udp_chksum[num_timestamps]; // pointers to the given fields
      uint8_t *lat_rte_ipv4_hdr[num_timestamps], *lat_ipv4_chksum[num_timestamps], *lat_ipv4_src[num_timestamps], *lat_ipv4_dst[num_timestamps]; // further ones for stateful tests
      rte_ipv4_hdr *lat_rte_ipv4_hdr_start; // used for IPv4 header checksum calculation
      uint16_t *udp_sport, *udp_dport, *udp_chksum, *ipv4_chksum; // working pointers to the given fields
      uint32_t *ipv4_src, *ipv4_dst; // further ones for stateful tests
      uint16_t fg_udp_chksum_start, bg_udp_chksum_start;  // starting values (uncomplemented checksums taken from the original frames)
      uint32_t chksum; // temporary variable for shecksum calculation
      uint16_t sport, dport; // values of source and destination port numbers -- to be preserved, when increase or decrease is done
      uint16_t sp, dp; // values of source and destination port numbers -- temporary values

      for ( i=0; i<N; i++ ) {
        // create foreground Test Frame
        if ( ip_version == 4 ) {
          // All IPv4 addresses and port numbers are set to 0.
          fg_pkt_mbuf[i] = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, &ipv4_zero, &ipv4_zero, 1, 1);
          pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
          fg_rte_ipv4_hdr[i] = pkt + 14;
          fg_ipv4_chksum[i] = pkt + 24;
          fg_ipv4_src[i] = pkt + 26;
          fg_ipv4_dst[i] = pkt + 30;
          fg_udp_sport[i] = pkt + 34;
          fg_udp_dport[i] = pkt + 36;
          fg_udp_chksum[i] = pkt + 40;
        } else { // IPv6 -- stateful operation is not yet supported!
          fg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, var_sport, var_dport);
          pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
          fg_udp_sport[i] = pkt + 54;
          fg_udp_dport[i] = pkt + 56;
          fg_udp_chksum[i] = pkt + 60;
	}
	fg_udp_chksum_start = ~*(uint16_t *)fg_udp_chksum[i]; // save the uncomplemented checksum value (same for all values of "i")
    
        // create backround Test Frame (always IPv6)
        bg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg, var_sport, var_dport);
        pkt = rte_pktmbuf_mtod(bg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
        bg_udp_sport[i] = pkt + 54;
        bg_udp_dport[i] = pkt  + 56;
        bg_udp_chksum[i] = pkt + 60;
        bg_udp_chksum_start = ~*(uint16_t *)bg_udp_chksum[i]; // save the uncomplemented checksum value (same for all values of "i")
      } 
      // create Latency Test Frames (may be foreground frames and background frames as well)
      struct rte_mbuf ** latency_frames = new struct rte_mbuf *[num_timestamps];
      if ( !latency_frames )
        rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for latency frame pointers!\n");
  
      uint64_t start_latency_frame = delay*frame_rate; // the ordinal number of the very first latency frame
      for ( int i=0; i<num_timestamps; i++ )
        if ( (start_latency_frame+i*frame_rate*latency_test_time/num_timestamps) % n  < m ) {
	  // foreground frame, may be IPv4 or IPv6
          if ( ip_version == 4 ) {
            latency_frames[i] = mkLatencyFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, &ipv4_zero, &ipv4_zero, 1, 1, i);
            pkt = rte_pktmbuf_mtod(latency_frames[i], uint8_t *); // Access the Test Frame in the message buffer
            lat_rte_ipv4_hdr[i] = pkt + 14;
            lat_ipv4_chksum[i] = pkt + 24;
            lat_ipv4_src[i] = pkt + 26;
            lat_ipv4_dst[i] = pkt + 30;
            lat_udp_sport[i] = pkt + 34;
            lat_udp_dport[i] = pkt + 36;
            lat_udp_chksum[i] = pkt + 40;
          } else { // IPv6 -- stateful operation is not yet supported!
            latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, var_sport, var_dport, i);
            pkt = rte_pktmbuf_mtod(latency_frames[i], uint8_t *); // Access the Test Frame in the message buffer
            lat_udp_sport[i] = pkt + 54;
            lat_udp_dport[i] = pkt + 56;
            lat_udp_chksum[i] = pkt + 60;
	  }
        } else {
          // background frame, must be IPv6
          latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg, var_sport, var_dport, i);
          pkt = rte_pktmbuf_mtod(latency_frames[i], uint8_t *); // Access the Test Frame in the message buffer
          lat_udp_sport[i] = pkt + 54;
          lat_udp_dport[i] = pkt + 56;
          lat_udp_chksum[i] = pkt + 60;
        }
  
      // set the starting values of port numbers, if they are increased or decreased
      if ( var_sport == 1 )
        sport = sport_min;
      if ( var_sport == 2 )
        sport = sport_max;
      if ( var_dport == 1 )
        dport = dport_min;
      if ( var_dport == 2 )
        dport = dport_max;

      // prepare random number infrastructure
      thread_local std::random_device rd;  // Will be used to obtain a seed for the random number engines
      thread_local std::mt19937_64 gen_sport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      thread_local std::mt19937_64 gen_dport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      thread_local std::mt19937_64 gen_index(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      std::uniform_int_distribution<int> uni_dis_sport(sport_min, sport_max);   // uniform distribution in [sport_min, sport_max]
      std::uniform_int_distribution<int> uni_dis_dport(dport_min, dport_max);   // uniform distribution in [dport_min, dport_max]
      std::uniform_int_distribution<unsigned> uni_dis_index(0, state_table_size-1); // uniform distribution in [0, state_table_size-1]
 
      // naive sender version: it is simple and fast
      i=0; // increase maunally after each sending of a normal Test Frame 
      int latency_timestamp_no=0; // counter for the latency frames from 0 to num_timestamps-1
      uint64_t send_next_latency_frame = start_latency_frame; // at what frame count to send the next latency frame 
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        if ( unlikely( sent_frames == send_next_latency_frame ) ) {
          // a latency frame is to be sent
          chksum = ~*(uint16_t *)lat_udp_chksum[latency_timestamp_no]; // take the value of the uncomplemented checksum
          udp_sport = (uint16_t *)lat_udp_sport[latency_timestamp_no];
          udp_dport = (uint16_t *)lat_udp_dport[latency_timestamp_no];
          udp_chksum = (uint16_t *)lat_udp_chksum[latency_timestamp_no];
          rte_ipv4_hdr_start = (rte_ipv4_hdr *)lat_rte_ipv4_hdr[latency_timestamp_no];
          ipv4_chksum = (uint16_t *)lat_ipv4_chksum[latency_timestamp_no];
          ipv4_src = (uint32_t *)lat_ipv4_src[latency_timestamp_no];
          ipv4_dst = (uint32_t *)lat_ipv4_dst[latency_timestamp_no];
          pkt_mbuf = latency_frames[latency_timestamp_no];
	  if ( sent_frames % n  < m ) 
	    fg_frame = 1;
	  else
	    fg_frame = 0;
        } else {
          // normal test frame is to be sent
          if ( sent_frames % n  < m ) {
            // foreground frame is to be sent
            chksum = fg_udp_chksum_start;
            udp_sport = (uint16_t *)fg_udp_sport[i];
            udp_dport = (uint16_t *)fg_udp_dport[i];
            udp_chksum = (uint16_t *)fg_udp_chksum[i];
            rte_ipv4_hdr_start = (rte_ipv4_hdr *)fg_rte_ipv4_hdr[i];
            ipv4_chksum = (uint16_t *)fg_ipv4_chksum[i];
            ipv4_src = (uint32_t *)fg_ipv4_src[i];        // this is rubbish if IP version is 6
            ipv4_dst = (uint32_t *)fg_ipv4_dst[i];        // this is rubbish if IP version is 6
            pkt_mbuf = fg_pkt_mbuf[i];
            fg_frame = 1;
          } else {
            // background frame is to be sent
            chksum = bg_udp_chksum_start;
            udp_sport = (uint16_t *)bg_udp_sport[i];
            udp_dport = (uint16_t *)bg_udp_dport[i];
            udp_chksum = (uint16_t *)bg_udp_chksum[i];
            pkt_mbuf = bg_pkt_mbuf[i];
            fg_frame = 0;
          }
        }
        // from here, we need to handle the frame identified by the temprary variables
        if ( ip_version == 4 && fg_frame ) {
          // this frame is handled in a stateful way
          switch ( responder_tuples ) {                  // here, it is surely not 0
            case 1:
              ft=stateTable[index++];
              index = index % state_table_size;
              break;
            case 2:
              ft=stateTable[index];
              if ( unlikely ( !index ) )
                index=state_table_size-1;
              else
                index--;
              break;
            case 3:
              ft=stateTable[uni_dis_index(gen_index)];
              break;
          }
          // now we set the IPv4 addresses and port numbers in the currently used template
          // without using conversion from host byte order to network byte order
          *ipv4_src = ft.resp_addr;
          *ipv4_dst = ft.init_addr;
          *udp_sport = ft.resp_port;
          *udp_dport = ft.init_port;
          // calculate checksum....
          chksum += rte_raw_cksum(&ft,12);
          chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   // reduce to 32 bits
          chksum = (~chksum) & 0xffff;                                  // make one's complement
          if (chksum == 0)                                              // checksum should not be 0 (0 means, no checksum is used)
            chksum = 0xffff;
          *udp_chksum = (uint16_t) chksum;              // set checksum in the frame
          *ipv4_chksum = 0;                             // IPv4 header checksum is set to 0
          *ipv4_chksum = rte_ipv4_cksum(rte_ipv4_hdr_start);        // IPv4 header checksum is set now
          // this is the end of handling the frame in a stateful way
        } else {
          // this frame is handled in the old way
          if ( var_sport ) {
            // sport is varying
            switch ( var_sport ) {
              case 1:                     // increasing port numbers
                if ( (sp=sport++) == sport_max )
                  sport = sport_min;
                break;
              case 2:                     // decreasing port numbers
                if ( (sp=sport--) == sport_min )
                  sport = sport_max;
                break;
              case 3:                     // pseudorandom port numbers
                sp = uni_dis_sport(gen_sport);
            }
            chksum += *udp_sport = htons(sp);     // set source port and add to checksum -- corrected
          }
          if ( var_dport ) {
            // dport is varying
            switch ( var_dport ) {
              case 1:                     // increasing port numbers
                if ( (dp=dport++) == dport_max )
                  dport = dport_min;
                break;
              case 2:                     // decreasing port numbers
                if ( (dp=dport--) == dport_min )
                  dport = dport_max;
                break;
              case 3:                     // pseudorandom port numbers
                dp = uni_dis_dport(gen_dport);
            }
            chksum += *udp_dport = htons(dp);     // set destination port add to checksum -- corrected
          }
          chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);     // calculate 16-bit one's complement sum
          chksum = (~chksum) & 0xffff;                                    // make one's complement
          if (chksum == 0)                                                // checksum should not be 0 (0 means, no checksum is used)
            chksum = 0xffff;
          *udp_chksum = (uint16_t) chksum;            // set checksum in the frame
          // this is the end of handling the frame in the old way
        }

        // finally, when its time is here, send the frame
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate );    // Beware: an "empty" loop, as well as in the next line
        while ( !rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1) );           // send out the frame
        if ( unlikely( sent_frames == send_next_latency_frame ) ) {
          // the sent frame was a Latency Frame
          send_ts[latency_timestamp_no++]=rte_rdtsc(); // store its sending timestamp
          send_next_latency_frame = start_latency_frame + latency_timestamp_no*frames_to_send_during_latency_test/num_timestamps; 
	} else {
          // the sent frame was a normal Test Frame
          i = (i+1) % N;
        }
      } // this is the end of the sending cycle
    } // end of optimized code for single destination network
    else {
      // optimized code for multiple destination networks: foreground and background frames are generated for each network and pointers are stored in arrays
      // latency frames are also pre-generated and stored in an array
      // as for foreground or background frames, N size arrays are used to resolve the write after send problem
      // source and/or destination port number(s) and UDP checksum are updated in the actually used copy before sending
      // assertion: num_dest_nets <= 256
      int j; // cycle variable to index the N size array: takes {0..N-1} values
      struct rte_mbuf *fg_pkt_mbuf[N], *bg_pkt_mbuf[256][N], *pkt_mbuf; // message buffers for fg. and bg. Test Frames
      uint8_t *pkt; // working pointer to the current frame (in the message buffer)
      uint8_t *fg_udp_sport[N], *fg_udp_dport[N], *fg_udp_chksum[N]; // pointers to the given fields of the pre-prepared Test Frames
      uint8_t *bg_udp_sport[256][N], *bg_udp_dport[256][N], *bg_udp_chksum[256][N]; // pointers to the given fields of the pre-prepared Test Frames
      uint8_t *fg_rte_ipv4_hdr[N], *fg_ipv4_chksum[N], *fg_ipv4_src[N], *fg_ipv4_dst[N]; // further ones for stateful tests, but not per dest. networks!
      rte_ipv4_hdr *rte_ipv4_hdr_start; // used for IPv4 header checksum calculation
      uint8_t *lat_udp_sport[num_timestamps], *lat_udp_dport[num_timestamps], *lat_udp_chksum[num_timestamps]; // pointers to the given fields
      uint8_t *lat_rte_ipv4_hdr[num_timestamps], *lat_ipv4_chksum[num_timestamps], *lat_ipv4_src[num_timestamps], *lat_ipv4_dst[num_timestamps]; // further ones for stateful tests
      rte_ipv4_hdr *lat_rte_ipv4_hdr_start; // used for IPv4 header checksum calculation
      uint16_t *udp_sport, *udp_dport, *udp_chksum, *ipv4_chksum; // working pointers to the given fields
      uint32_t *ipv4_src, *ipv4_dst; // further ones for stateful tests
      uint16_t fg_udp_chksum_start, bg_udp_chksum_start[256];  // starting values (uncomplemented checksums taken from the original frames)
      uint32_t chksum; // temporary variable for shecksum calculation
      uint16_t sport, dport; // values of source and destination port numbers -- to be preserved, when increase or decrease is done
      uint16_t sp, dp; // values of source and destination port numbers -- temporary values
      in6_addr curr_dst_bg;     // backround IPv6 destination address, which will be changed
      int i;                    // cycle variable for grenerating different destination network addresses
  
      curr_dst_bg = *dst_bg;
  
      for ( j=0; j<N; j++ ) {
        // create foreground Test Frame
        if ( ip_version == 4 ) {
          fg_pkt_mbuf[j] = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, &ipv4_zero, &ipv4_zero, 1, 1);
          pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[j], uint8_t *); // Access the Test Frame in the message buffer
          fg_rte_ipv4_hdr[j] = pkt + 14;
          fg_ipv4_chksum[j] = pkt + 24;
          fg_ipv4_src[j] = pkt + 26;
          fg_ipv4_dst[j] = pkt + 30;
          fg_udp_sport[j] = pkt + 34;
          fg_udp_dport[j] = pkt + 36;
          fg_udp_chksum[j] = pkt + 40;
        }
        else { // IPv6 -- stateful operation is not yet supported!
          fg_pkt_mbuf[j] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, var_sport, var_dport);
          pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[j], uint8_t *); // Access the Test Frame in the message buffer
          fg_udp_sport[j] = pkt + 54;
          fg_udp_dport[j] = pkt + 56;
          fg_udp_chksum[j] = pkt + 60;
        }
        fg_udp_chksum_start = ~*(uint16_t *)fg_udp_chksum[j]; // save the uncomplemented checksum value (same for all values of "j")
        // create backround Test Frame (always IPv6)
        for ( i=0; i<num_dest_nets; i++ ) {
          ((uint8_t *)&curr_dst_bg)[7] = (uint8_t) i; // see comment above
          bg_pkt_mbuf[i][j] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, &curr_dst_bg, var_sport, var_dport);
          pkt = rte_pktmbuf_mtod(bg_pkt_mbuf[i][j], uint8_t *); // Access the Test Frame in the message buffer
          bg_udp_sport[i][j] = pkt + 54;
          bg_udp_dport[i][j] = pkt  + 56;
          bg_udp_chksum[i][j] = pkt + 60;
          bg_udp_chksum_start[i] = ~*(uint16_t *)bg_udp_chksum[i][j]; // save the uncomplemented checksum value (same for all values of "j")
        }
      }
  
      // random number infrastructure is taken from: https://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution
      // MT64 is used because of https://medium.com/@odarbelaeze/how-competitive-are-c-standard-random-number-generators-f3de98d973f0
      // thread_local is used on the basis of https://stackoverflow.com/questions/40655814/is-mersenne-twister-thread-safe-for-cpp
      thread_local std::random_device rd;  //Will be used to obtain a seed for the random number engine
      thread_local std::mt19937_64 gen_net(rd()); //Standard 64-bit mersenne_twister_engine seeded with rd()
      thread_local std::mt19937_64 gen_sport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      thread_local std::mt19937_64 gen_dport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      thread_local std::mt19937_64 gen_index(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      std::uniform_int_distribution<int> uni_dis_net(0, num_dest_nets-1);     // uniform distribution in [0, num_dest_nets-1]
      std::uniform_int_distribution<int> uni_dis_sport(sport_min, sport_max);   // uniform distribution in [sport_min, sport_max]
      std::uniform_int_distribution<int> uni_dis_dport(dport_min, dport_max);   // uniform distribution in [dport_min, dport_max]
      std::uniform_int_distribution<unsigned> uni_dis_index(0, state_table_size-1); // uniform distribution in [0, state_table_size-1]
 
      // create Latency Test Frames (may be foreground frames and background frames as well)
      struct rte_mbuf ** latency_frames = new struct rte_mbuf *[num_timestamps];
      if ( !latency_frames )
        rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for latency frame pointers!\n");
  
      uint64_t start_latency_frame = delay*frame_rate; // the ordinal number of the very first latency frame
      for ( int i=0; i<num_timestamps; i++ )
        if ( (start_latency_frame+i*frame_rate*latency_test_time/num_timestamps) % n  < m ) {
          // foreground frame
          if ( ip_version == 4 ) { 
            latency_frames[i] = mkLatencyFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, &ipv4_zero, &ipv4_zero, 1, 1, i);
            pkt = rte_pktmbuf_mtod(latency_frames[i], uint8_t *); // Access the Test Frame in the message buffer
            lat_rte_ipv4_hdr[i] = pkt + 14;
            lat_ipv4_chksum[i] = pkt + 24;
            lat_ipv4_src[i] = pkt + 26;
            lat_ipv4_dst[i] = pkt + 30;
            lat_udp_sport[i] = pkt + 34;
            lat_udp_dport[i] = pkt + 36;
            lat_udp_chksum[i] = pkt + 40;
          } else { // IPv6 -- stateful operation is not yet supported!
            latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, var_sport, var_dport, i);
            pkt = rte_pktmbuf_mtod(latency_frames[i], uint8_t *); // Access the Test Frame in the message buffer
            lat_udp_sport[i] = pkt + 54;
            lat_udp_dport[i] = pkt + 56;
            lat_udp_chksum[i] = pkt + 60;
          }
        } else {
          // background frame, must be IPv6, choose random network
          ((uint8_t *)&curr_dst_bg)[7] = (uint8_t) i; // see comment above
          latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, &curr_dst_bg, var_sport, var_dport, i);
          pkt = rte_pktmbuf_mtod(latency_frames[i], uint8_t *); // Access the Test Frame in the message buffer
          lat_udp_sport[i] = pkt + 54;
          lat_udp_dport[i] = pkt + 56;
          lat_udp_chksum[i] = pkt + 60;
        }
  
      // set the starting values of port numbers, if they are increased or decreased
      if ( var_sport == 1 )
        sport = sport_min;
      if ( var_sport == 2 )
        sport = sport_max;
      if ( var_dport == 1 )
        dport = dport_min;
      if ( var_dport == 2 )
        dport = dport_max;

      // naive sender version: it is simple and fast
      i=0; // increase maunally after each sending of a normal Test Frame
      int latency_timestamp_no=0; // counter for the latency frames from 0 to num_timestamps-1
      uint64_t send_next_latency_frame = start_latency_frame; // at what frame count to send the next latency frame
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        if ( unlikely( sent_frames == send_next_latency_frame ) ) {
          // a latency frame is to be sent
          chksum = ~*(uint16_t *)lat_udp_chksum[latency_timestamp_no]; // take the value of the uncomplemented checksum
          udp_sport = (uint16_t *)lat_udp_sport[latency_timestamp_no];
          udp_dport = (uint16_t *)lat_udp_dport[latency_timestamp_no];
          udp_chksum = (uint16_t *)lat_udp_chksum[latency_timestamp_no];
          rte_ipv4_hdr_start = (rte_ipv4_hdr *)lat_rte_ipv4_hdr[latency_timestamp_no];
          ipv4_chksum = (uint16_t *)lat_ipv4_chksum[latency_timestamp_no];
          ipv4_src = (uint32_t *)lat_ipv4_src[latency_timestamp_no];
          ipv4_dst = (uint32_t *)lat_ipv4_dst[latency_timestamp_no];
          pkt_mbuf = latency_frames[latency_timestamp_no];
          if ( sent_frames % n  < m )
            fg_frame = 1;
          else
            fg_frame = 0;
        } else {
          // normal test frame is to be sent
          if ( sent_frames % n  < m ) {
            // foreground frame is to be sent
            chksum = fg_udp_chksum_start;
            udp_sport = (uint16_t *)fg_udp_sport[i];
            udp_dport = (uint16_t *)fg_udp_dport[i];
            udp_chksum = (uint16_t *)fg_udp_chksum[i];
            rte_ipv4_hdr_start = (rte_ipv4_hdr *)fg_rte_ipv4_hdr[i];
            ipv4_chksum = (uint16_t *)fg_ipv4_chksum[i];
            ipv4_src = (uint32_t *)fg_ipv4_src[i];        // this is rubbish if IP version is 6
            ipv4_dst = (uint32_t *)fg_ipv4_dst[i];        // this is rubbish if IP version is 6
            pkt_mbuf = fg_pkt_mbuf[i];
            fg_frame = 1;
          } else {
            // background frame is to be sent
            int net_index = uni_dis_net(gen_net); // index of the pre-generated frame 
            chksum = bg_udp_chksum_start[net_index];
            udp_sport = (uint16_t *)bg_udp_sport[net_index][i];
            udp_dport = (uint16_t *)bg_udp_dport[net_index][i];
            udp_chksum = (uint16_t *)bg_udp_chksum[net_index][i];
            pkt_mbuf = bg_pkt_mbuf[net_index][i];
            fg_frame = 0;
          }
        }
        // from here, we need to handle the frame identified by the temprary variables
        if ( ip_version == 4 && fg_frame ) {
          // this frame is handled in a stateful way
          switch ( responder_tuples ) {                  // here, it is surely not 0
            case 1:
              ft=stateTable[index++];
              index = index % state_table_size;
              break;
            case 2:
              ft=stateTable[index];
              if ( unlikely ( !index ) )
                index=state_table_size-1;
              else
                index--;
              break;
            case 3:
              ft=stateTable[uni_dis_index(gen_index)];
              break;
          }
          // now we set the IPv4 addresses and port numbers in the currently used template
          // without using conversion from host byte order to network byte order
          *ipv4_src = ft.resp_addr;
          *ipv4_dst = ft.init_addr;
          *udp_sport = ft.resp_port;
          *udp_dport = ft.init_port;
          // calculate checksum....
          chksum += rte_raw_cksum(&ft,12);
          chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   // reduce to 32 bits
          chksum = (~chksum) & 0xffff;                                  // make one's complement
          if (chksum == 0)                                              // checksum should not be 0 (0 means, no checksum is used)
            chksum = 0xffff;
          *udp_chksum = (uint16_t) chksum;              // set checksum in the frame
          *ipv4_chksum = 0;                             // IPv4 header checksum is set to 0
          *ipv4_chksum = rte_ipv4_cksum(rte_ipv4_hdr_start);        // IPv4 header checksum is set now
          // this is the end of handling the frame in a stateful way
        } else {
          // this frame is handled in the old way
          if ( var_sport ) {
            // sport is varying
            switch ( var_sport ) {
              case 1:                     // increasing port numbers
                if ( (sp=sport++) == sport_max )
                  sport = sport_min;
                break;
              case 2:                     // decreasing port numbers
                if ( (sp=sport--) == sport_min )
                  sport = sport_max;
                break;
              case 3:                     // pseudorandom port numbers
                sp = uni_dis_sport(gen_sport);
            }
            chksum += *udp_sport = htons(sp);     // set source port and add to checksum -- corrected
          }
          if ( var_dport ) {
            // dport is varying
            switch ( var_dport ) {
              case 1:                     // increasing port numbers
                if ( (dp=dport++) == dport_max )
                  dport = dport_min;
                break;
              case 2:                     // decreasing port numbers
                if ( (dp=dport--) == dport_min )
                  dport = dport_max;
                break;
              case 3:                     // pseudorandom port numbers
                dp = uni_dis_dport(gen_dport);
            }
            chksum += *udp_dport = htons(dp);     // set destination port add to checksum -- corrected
          }
          chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);     // calculate 16-bit one's complement sum
          chksum = (~chksum) & 0xffff;                                    // make one's complement
          if (chksum == 0)                                                // checksum should not be 0 (0 means, no checksum is used)
            chksum = 0xffff;
          *udp_chksum = (uint16_t) chksum;            // set checksum in the frame
          // this is the end of handling the frame in the old way
        }

        // finally, when its time is here, send the frame
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate );    // Beware: an "empty" loop, as well as in the next line
        while ( !rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1) );           // send out the frame
        if ( unlikely( sent_frames == send_next_latency_frame ) ) {
          // the sent frame was a Latency Frame
          send_ts[latency_timestamp_no++]=rte_rdtsc(); // store its sending timestamp
          send_next_latency_frame = start_latency_frame + latency_timestamp_no*frames_to_send_during_latency_test/num_timestamps;
        } else {
          // the sent frame was a normal Test Frame
          i = (i+1) % N;
        }
      } // this is the end of the sending cycle
    } // end of optimized code for multiple destination networks
  } // end of implementation of varying port numbers

  // Now, we check the time
  elapsed_seconds = (double)(rte_rdtsc()-start_tsc)/hz;
  printf("Info: %s sender's sending took %3.10lf seconds.\n", side, elapsed_seconds);
  if ( elapsed_seconds > duration*TOLERANCE )
    rte_exit(EXIT_FAILURE, "%s sending exceeded the %3.10lf seconds limit, the test is invalid.\n", side, duration*TOLERANCE);
  printf("%s frames sent: %lu\n", side, sent_frames);
  return 0;
}

// receives Test Frames for latency measurements including "num_timestamps" number of Latency frames
// Offsets from the start of the Ethernet Frame:
// EtherType: 6+6=12
// IPv6 Next header: 14+6=20, UDP Data for IPv6: 14+40+8=62
// IPv4 Protolcol: 14+9=23, UDP Data for IPv4: 14+20+8=42
int receiveLatency(void *par) {
  // collecting input parameters:
  class receiverParametersLatency *p = (class receiverParametersLatency *)par;
  uint64_t finish_receiving = p->finish_receiving;
  uint16_t eth_id = p->eth_id;
  const char *side = p->side;
  uint16_t num_timestamps =  p->num_timestamps;
  uint64_t *receive_ts = p->receive_ts; 

  // further local variables
  int frames, i;
  struct rte_mbuf *pkt_mbufs[MAX_PKT_BURST]; // pointers for the mbufs of received frames
  uint16_t ipv4=htons(0x0800); // EtherType for IPv4 in Network Byte Order
  uint16_t ipv6=htons(0x86DD); // EtherType for IPv6 in Network Byte Order
  uint8_t identify[8]= { 'I', 'D', 'E', 'N', 'T', 'I', 'F', 'Y' };      // Identificion of the Test Frames
  uint64_t *id=(uint64_t *) identify;
  uint8_t identify_latency[8]= { 'I', 'd', 'e', 'n', 't', 'i', 'f', 'y' };      // Identificion of the Latency Frames
  uint64_t *id_lat=(uint64_t *) identify_latency;
  uint64_t received=0;  // number of received frames

  while ( rte_rdtsc() < finish_receiving ){
    frames = rte_eth_rx_burst(eth_id, 0, pkt_mbufs, MAX_PKT_BURST);
    for (i=0; i < frames; i++){
      uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbufs[i], uint8_t *); // Access the Test Frame in the message buffer
      // check EtherType at offset 12: IPv6, IPv4, or anything else
      if ( *(uint16_t *)&pkt[12]==ipv6 ) { /* IPv6 */
        /* check if IPv6 Next Header is UDP, and the first 8 bytes of UDP data is 'IDENTIFY' */
        if ( likely( pkt[20]==17 && *(uint64_t *)&pkt[62]==*id ) )
          received++; // normal Test Frame
        else if ( pkt[20]==17 && *(uint64_t *)&pkt[62]==*id_lat ) {
          // Latency Frame
          uint64_t timestamp = rte_rdtsc(); // get a timestamp ASAP
          int latency_frame_id = *(uint16_t *)&pkt[70]; 
          if ( latency_frame_id < 0 || latency_frame_id >= num_timestamps )
            rte_exit(EXIT_FAILURE, "Error: IPv6 Latency Frame with invalid frame ID %i was received in the %s direction!\n",latency_frame_id,side); // to avoid segmentation fault
          receive_ts[latency_frame_id] = timestamp;
          received++; // Latency Frame is also counted as Test Frame
        }
      } else if ( *(uint16_t *)&pkt[12]==ipv4 ) { /* IPv4 */
        if ( likely( pkt[23]==17 && *(uint64_t *)&pkt[42]==*id ) )
           received++; // normal Test Frame
        else if ( pkt[23]==17 && *(uint64_t *)&pkt[42]==*id_lat ) {
          // Latency Frame
          uint64_t timestamp = rte_rdtsc(); // get a timestamp ASAP
          int latency_frame_id = *(uint16_t *)&pkt[50];
          if ( latency_frame_id < 0 || latency_frame_id >= num_timestamps )
            rte_exit(EXIT_FAILURE, "Error: IPv4 Latency Frame with invalid frame ID %i was received in the %s direction!\n",latency_frame_id,side); // to avoid segmentation fault
          receive_ts[latency_frame_id] = timestamp;
          received++; // Latency Frame is also counted as Test Frame
        }
      }
      rte_pktmbuf_free(pkt_mbufs[i]);
    }
  }
  printf("%s frames received: %lu\n", side, received);
  return received;
}


// Responder/Receiver: receives Test Frames for latency measurements including "num_timestamps" number of Latency frames
// Offsets from the start of the Ethernet Frame:
// EtherType: 6+6=12
// IPv6 Next header: 14+6=20, UDP Data for IPv6: 14+40+8=62
// IPv4 Protolcol: 14+9=23, UDP Data for IPv4: 14+20+8=42
int rreceiveLatency(void *par) {
  // collecting input parameters:
  class rReceiverParametersLatency *p = (class rReceiverParametersLatency *)par;
  uint64_t finish_receiving = p->finish_receiving;
  uint16_t eth_id = p->eth_id;
  const char *side = p->side;
  unsigned state_table_size = p->state_table_size;
  unsigned *valid_entries = p->valid_entries;
  atomicFourTuple **stateTable = p->stateTable;
  uint16_t num_timestamps =  p->num_timestamps;
  uint64_t *receive_ts = p->receive_ts; 

  atomicFourTuple *stTbl;               // state table
  unsigned index = 0;                   // state table index: first write will happen to this position
  fourTuple four_tuple;                 // 4-tuple for collecting IPv4 addresses and port numbers

  // further local variables
  int frames, i;
  struct rte_mbuf *pkt_mbufs[MAX_PKT_BURST]; // pointers for the mbufs of received frames
  uint16_t ipv4=htons(0x0800); // EtherType for IPv4 in Network Byte Order
  uint16_t ipv6=htons(0x86DD); // EtherType for IPv6 in Network Byte Order
  uint8_t identify[8]= { 'I', 'D', 'E', 'N', 'T', 'I', 'F', 'Y' };      // Identificion of the Test Frames
  uint64_t *id=(uint64_t *) identify;
  uint8_t identify_latency[8]= { 'I', 'd', 'e', 'n', 't', 'i', 'f', 'y' };      // Identificion of the Latency Frames
  uint64_t *id_lat=(uint64_t *) identify_latency;
  uint64_t fg_received=0, bg_received=0;        // number of received (fg, bg) frames (counted separetely)

  if ( !*valid_entries ) {
    // This is preliminary phase: state table is allocated from the memory of this NUMA node
    stTbl = (atomicFourTuple *) rte_malloc("Responder/Receiver's state table", (sizeof(atomicFourTuple))*state_table_size, 128);
    if ( !stTbl )
      rte_exit(EXIT_FAILURE, "Error: Responder/Receiver can't allocate memory for state table!\n");
    *stateTable = stTbl;                // return the address of the state table
  } else {
    // A real test is performed now: we use the previously allocated state table
    stTbl = *stateTable;
  }

  // frames are received and their four tuples are recorded, timestamps of latency frames are also recorded
  while ( rte_rdtsc() < finish_receiving ){
    frames = rte_eth_rx_burst(eth_id, 0, pkt_mbufs, MAX_PKT_BURST);
    for (i=0; i < frames; i++){
      uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbufs[i], uint8_t *); // Access the Test Frame in the message buffer
      // check EtherType at offset 12: IPv6, IPv4, or anything else
      if ( *(uint16_t *)&pkt[12]==ipv6 ) { /* IPv6 */
        /* check if IPv6 Next Header is UDP, and the first 8 bytes of UDP data is 'IDENTIFY' */
        if ( likely( pkt[20]==17 && *(uint64_t *)&pkt[62]==*id ) )
          bg_received++; 	// it is considered a background Test frame: we do not deal with it any more
        else if ( pkt[20]==17 && *(uint64_t *)&pkt[62]==*id_lat ) {
          // Latency Frame
          uint64_t timestamp = rte_rdtsc(); // get a timestamp ASAP
          int latency_frame_id = *(uint16_t *)&pkt[70]; 
          if ( latency_frame_id < 0 || latency_frame_id >= num_timestamps )
            rte_exit(EXIT_FAILURE, "Error XXX 1: Latency Frame with invalid frame ID was received!\n"); // to avoid segmentation fault
          receive_ts[latency_frame_id] = timestamp;
          bg_received++; // backgroud Latency Frame is also counted as Test Frame
        }
      } else if ( *(uint16_t *)&pkt[12]==ipv4 ) { /* IPv4 */
        if ( likely( pkt[23]==17 && *(uint64_t *)&pkt[42]==*id ) ) {
          fg_received++; 	// it is considered a foregroud Test frame: we must learn its 4-tuple
          // copy IPv4 fields to the four_tuple -- without using conversion from network byte order to host byte order
          four_tuple.init_addr = *(uint32_t *)&pkt[26];         // 14+12: source IPv4 address
          four_tuple.resp_addr = *(uint32_t *)&pkt[30];         // 14+16: destination IPv4 address
          four_tuple.init_port = *(uint16_t *)&pkt[34];         // 14+20: source UDP port
          four_tuple.resp_port = *(uint16_t *)&pkt[36];         // 14+22: destination UDP port
          stTbl[index] = four_tuple;                            // atomic write
          index = ++index % state_table_size;                   // maintain write pointer
        }
        else if ( pkt[23]==17 && *(uint64_t *)&pkt[42]==*id_lat ) {
          // foreground Latency Frame
          uint64_t timestamp = rte_rdtsc(); // get a timestamp ASAP
          int latency_frame_id = *(uint16_t *)&pkt[50];
          if ( latency_frame_id < 0 || latency_frame_id >= num_timestamps )
            rte_exit(EXIT_FAILURE, "Error XXX 2: Latency Frame with invalid frame ID was received!\n"); // to avoid segmentation fault
          receive_ts[latency_frame_id] = timestamp;
          fg_received++; // Latency Frame is also counted as Test Frame
          // copy IPv4 fields to the four_tuple -- without using conversion from network byte order to host byte order
          four_tuple.init_addr = *(uint32_t *)&pkt[26];         // 14+12: source IPv4 address
          four_tuple.resp_addr = *(uint32_t *)&pkt[30];         // 14+16: destination IPv4 address
          four_tuple.init_port = *(uint16_t *)&pkt[34];         // 14+20: source UDP port
          four_tuple.resp_port = *(uint16_t *)&pkt[36];         // 14+22: destination UDP port
          stTbl[index] = four_tuple;                            // atomic write
          index = ++index % state_table_size;                   // maintain write pointer
        }
      }
      rte_pktmbuf_free(pkt_mbufs[i]);
    }
  }
  printf("%s frames received: %lu\n", side, fg_received+bg_received);
  if ( !*valid_entries ) {
    // This one was a preliminary test, the number of valid entries should be reported
    *valid_entries = ( fg_received < state_table_size ? fg_received : state_table_size );
  }
  return fg_received+bg_received;
}

// performs latency measurement
void Latency::measure(uint16_t leftport, uint16_t rightport) {
  uint64_t *left_send_ts, *right_send_ts, *left_receive_ts, *right_receive_ts; // pointers for timestamp arrays

  // create the dynamic arrays for timestamps depending on which directions are active.
  if ( forward ) {      // Left to right direction is active
    // create dynamic arrays for timestamps
    left_send_ts = new uint64_t[num_timestamps];
    right_receive_ts = new uint64_t[num_timestamps];
    if ( !left_send_ts || !right_receive_ts )
      rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for timestamps!\n");
    // fill with 0 (will be used to chek, if frame with timestamp was received)
    memset(right_receive_ts, 0, num_timestamps*sizeof(uint64_t));
  }
  if ( reverse ) {      // Right to Left direction is active
    // create dynamic arrays for timestamps
    right_send_ts = new uint64_t[num_timestamps];
    left_receive_ts = new uint64_t[num_timestamps];
    if ( !right_send_ts || !left_receive_ts )
      rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for timestamps!\n");
    // fill with 0 (will be used to chek, if frame with timestamp was received)
    memset(left_receive_ts, 0, num_timestamps*sizeof(uint64_t));
  }

  switch ( stateful ) {
    case 0:     // stateless test is to be performed
      {
      // set common parameters for senders
      senderCommonParametersLatency scp(ipv6_frame_size,ipv4_frame_size,frame_rate,duration,n,m,hz,start_tsc,delay,num_timestamps);
    
      if ( forward ) {      // Left to right direction is active
        // set individual parameters for the left sender
    
        // first, collect the appropriate values dependig on the IP versions
        ipQuad ipq(ip_left_version,ip_right_version,&ipv4_left_real,&ipv4_right_real,&ipv4_left_virtual,&ipv4_right_virtual,
                   &ipv6_left_real,&ipv6_right_real,&ipv6_left_virtual,&ipv6_right_virtual);
    
        // then, initialize the parameter class instance
        senderParametersLatency spars(&scp,ip_left_version,pkt_pool_left_sender,leftport,"Forward",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                      ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,num_right_nets,
    				  fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max,left_send_ts);
    
        // start left sender
        if ( rte_eal_remote_launch(sendLatency, &spars, cpu_left_sender) )
          std::cout << "Error: could not start Left Sender." << std::endl;
    
        // set parameters for the right receiver
        receiverParametersLatency rpars(finish_receiving,rightport,"Forward",num_timestamps,right_receive_ts);
    
        // start right receiver
        if ( rte_eal_remote_launch(receiveLatency, &rpars, cpu_right_receiver) )
          std::cout << "Error: could not start Right Receiver." << std::endl;
      }
    
      if ( reverse ) {      // Right to Left direction is active
    
        // first, collect the appropriate values dependig on the IP versions
        ipQuad ipq(ip_right_version,ip_left_version,&ipv4_right_real,&ipv4_left_real,&ipv4_right_virtual,&ipv4_left_virtual,
                   &ipv6_right_real,&ipv6_left_real,&ipv6_right_virtual,&ipv6_left_virtual);
    
        // then, initialize the parameter class instance
        senderParametersLatency spars(&scp,ip_right_version,pkt_pool_right_sender,rightport,"Reverse",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                               ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,num_left_nets,
    			   rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max,right_send_ts);
    
        // start right sender
        if (rte_eal_remote_launch(sendLatency, &spars, cpu_right_sender) )
          std::cout << "Error: could not start Right Sender." << std::endl;
    
        // set parameters for the left receiver
        receiverParametersLatency rpars(finish_receiving,leftport,"Reverse",num_timestamps,left_receive_ts);
    
        // start left receiver
        if ( rte_eal_remote_launch(receiveLatency, &rpars, cpu_left_receiver) )
          std::cout << "Error: could not start Left Receiver." << std::endl;
      }
    
      std::cout << "Info: Testing started." << std::endl;
    
      // wait until active senders and receivers finish
      if ( forward ) {
        rte_eal_wait_lcore(cpu_left_sender);
        rte_eal_wait_lcore(cpu_right_receiver);
      }
      if ( reverse ) {
        rte_eal_wait_lcore(cpu_right_sender);
        rte_eal_wait_lcore(cpu_left_receiver);
      }
      std::cout << "Info: Test finished." << std::endl;
      break;
      }
    case 1:	// stateful test: Initiator is on the left side, Responder is on the right side
      {
      // as no timestamps are needed in the preliminary phase, we reuse the code of the Throughput::measure() function
      // set "common" parameters (currently not common with anyone, only code is reused; it will be common, when sending test frames)
      senderCommonParameters scp1(ipv6_frame_size,ipv4_frame_size,pre_rate,0,n,m,hz,start_tsc_pre); // 0: duration in seconds is not applicable
  
      // set "individual" parameters for the sender of the Initiator residing on the left side
  
      // first, collect the appropriate values dependig on the IP versions
      ipQuad ipq(ip_left_version,ip_right_version,&ipv4_left_real,&ipv4_right_real,&ipv4_left_virtual,&ipv4_right_virtual,
                 &ipv6_left_real,&ipv6_right_real,&ipv6_left_virtual,&ipv6_right_virtual);
  
      // then, initialize the parameter class instance for premiminary phase
      iSenderParameters ispars1(&scp1,ip_left_version,pkt_pool_left_sender,leftport,"Preliminary",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,num_right_nets,
                                fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max,
                                enumerate_ports,pre_frames,uniquePortComb);
  
      // start left sender
      if ( rte_eal_remote_launch(isend, &ispars1, cpu_left_sender) )
        std::cout << "Error: could not start Initiator's Sender." << std::endl;
  
      // set parameters for the right receiver
      rReceiverParameters rrpars1(finish_receiving_pre,rightport,"Preliminary",state_table_size,&valid_entries,&stateTable);
  
      // start right receiver
      if ( rte_eal_remote_launch(rreceive, &rrpars1, cpu_right_receiver) )
        std::cout << "Error: could not start Responder's Receiver." << std::endl;
  
      std::cout << "Info: Preliminary frame sending started." << std::endl;
  
      // wait until active senders and receivers finish
      rte_eal_wait_lcore(cpu_left_sender);
      rte_eal_wait_lcore(cpu_right_receiver);
  
      if ( valid_entries < state_table_size )
        printf("Error: Failed to fill state table (valid entries: %u, state table size: %u)!\n", valid_entries, state_table_size);
      else
        std::cout << "Info: Preliminary phase finished." << std::endl;
      // this is the end of code reuse
  
      if ( enumerate_ports )
        std::cout << "Warning: Port number enumeration is supported only in the preliminary phase of Latency measurements." << std::endl;
  
      // Now the real test may follow.
  
      // set common parameters for senders
      senderCommonParametersLatency scp2(ipv6_frame_size,ipv4_frame_size,frame_rate,duration,n,m,hz,start_tsc,delay,num_timestamps);
  
      if ( forward ) {      // Left to right direction is active

        // set individual parameters for the (normal stateless) left sender
  
        // initialize the parameter class instance for real test (reuse previously prepared 'ipq')
        senderParametersLatency spars2(&scp2,ip_left_version,pkt_pool_left_sender,leftport,"Forward",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                       ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,num_right_nets,
                                       fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max,left_send_ts);
  
        // start left sender
        if ( rte_eal_remote_launch(sendLatency, &spars2, cpu_left_sender) )
          std::cout << "Error: could not start Left Sender." << std::endl;
  
        // set parameters for the right receiver
        rReceiverParametersLatency rrpars2(finish_receiving,rightport,"Forward",state_table_size,&valid_entries,&stateTable,num_timestamps,right_receive_ts);
  
        // start right receiver
        if ( rte_eal_remote_launch(rreceiveLatency, &rrpars2, cpu_right_receiver) )
          std::cout << "Error: could not start Responder's Receiver." << std::endl;
      }

      if ( reverse ) {      // Right to Left direction is active
 
        // set individual parameters for the right sender
 
        // first, collect the appropriate values dependig on the IP versions
        ipQuad ipq(ip_right_version,ip_left_version,&ipv4_right_real,&ipv4_left_real,&ipv4_right_virtual,&ipv4_left_virtual,
                   &ipv6_right_real,&ipv6_left_real,&ipv6_right_virtual,&ipv6_left_virtual);
  
        // then, initialize the parameter class instance
        rSenderParametersLatency spars2(&scp2,ip_right_version,pkt_pool_right_sender,rightport,"Reverse",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                        ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,num_left_nets,
                                        rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max,
					state_table_size,stateTable,responder_tuples,right_send_ts);
  
        // start right sender
        if (rte_eal_remote_launch(rsendLatency, &spars2, cpu_right_sender) )
          std::cout << "Error: could not start Responder's Sender." << std::endl;
  
        // set parameters for the left receiver
        receiverParametersLatency rpars2(finish_receiving,leftport,"Reverse",num_timestamps,left_receive_ts);
  
        // start left receiver
        if ( rte_eal_remote_launch(receiveLatency, &rpars2, cpu_left_receiver) )
          std::cout << "Error: could not start Left Receiver." << std::endl;
      }
  
      std::cout << "Info: Testing started." << std::endl;
  
      // wait until active senders and receivers finish
      if ( forward ) {
        rte_eal_wait_lcore(cpu_left_sender);
        rte_eal_wait_lcore(cpu_right_receiver);
      }
      if ( reverse ) {
        rte_eal_wait_lcore(cpu_right_sender);
        rte_eal_wait_lcore(cpu_left_receiver);
      }
      std::cout << "Info: Test finished." << std::endl;
      break;
      }
    case 2:	// stateful test: Initiator is on the right side, Responder is on the left side
      {
      // as no timestamps are needed in the preliminary phase, we reuse the code of the Throughput::measure() function
      // set "common" parameters (currently not common with anyone, only code is reused; it will be common, when sending test frames)
      senderCommonParameters scp1(ipv6_frame_size,ipv4_frame_size,pre_rate,0,n,m,hz,start_tsc_pre); // 0: duration in seconds is not applicable
  
      // set "individual" parameters for the sender of the Initiator residing on the right side
  
      // first, collect the appropriate values dependig on the IP versions
      ipQuad ipq(ip_right_version,ip_left_version,&ipv4_right_real,&ipv4_left_real,&ipv4_right_virtual,&ipv4_left_virtual,
                 &ipv6_right_real,&ipv6_left_real,&ipv6_right_virtual,&ipv6_left_virtual);
  
      // then, initialize the parameter class instance for premiminary phase
      iSenderParameters ispars1(&scp1,ip_right_version,pkt_pool_right_sender,rightport,"Preliminary",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,num_left_nets,
                                fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max,
                                enumerate_ports,pre_frames,uniquePortComb);
  
      // start right sender
      if ( rte_eal_remote_launch(isend, &ispars1, cpu_right_sender) )
        std::cout << "Error: could not start Initiator's Sender." << std::endl;
  
      // set parameters for the left receiver
      rReceiverParameters rrpars1(finish_receiving_pre,leftport,"Preliminary",state_table_size,&valid_entries,&stateTable);
  
      // start left receiver
      if ( rte_eal_remote_launch(rreceive, &rrpars1, cpu_left_receiver) )
        std::cout << "Error: could not start Responder's Receiver." << std::endl;
  
      std::cout << "Info: Preliminary frame sending started." << std::endl;
  
      // wait until active senders and receivers finish
      rte_eal_wait_lcore(cpu_right_sender);
      rte_eal_wait_lcore(cpu_left_receiver);
  
      if ( valid_entries < state_table_size )
        printf("Error: Failed to fill state table (valid entries: %u, state table size: %u)!\n", valid_entries, state_table_size);
      else
        std::cout << "Info: Preliminary phase finished." << std::endl;
      // this is the end of code reuse
  
      if ( enumerate_ports )
        std::cout << "Warning: Port number enumeration is supported only in the preliminary phase of Latency measurements." << std::endl;
  
      // Now the real test may follow.
  
      // set common parameters for senders
      senderCommonParametersLatency scp2(ipv6_frame_size,ipv4_frame_size,frame_rate,duration,n,m,hz,start_tsc,delay,num_timestamps);
  
      if ( reverse) {      // Right to Left direction is active

        // set individual parameters for the (normal stateless) right sender
  
        // initialize the parameter class instance for real test (reuse previously prepared 'ipq')
        senderParametersLatency spars2(&scp2,ip_right_version,pkt_pool_right_sender,rightport,"Reverse",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                       ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,num_left_nets,
                                       fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max,right_send_ts);
  
        // start right sender
        if ( rte_eal_remote_launch(sendLatency, &spars2, cpu_right_sender) )
          std::cout << "Error: could not start Right Sender." << std::endl;
  
        // set parameters for the left receiver
        rReceiverParametersLatency rrpars2(finish_receiving,leftport,"Reverse",state_table_size,&valid_entries,&stateTable,num_timestamps,left_receive_ts);
  
        // start left receiver
        if ( rte_eal_remote_launch(rreceiveLatency, &rrpars2, cpu_left_receiver) )
          std::cout << "Error: could not start Left Receiver." << std::endl;
      }
      if ( forward ) {      // Left to right direction is active
        // set individual parameters for the left sender
  
        // first, collect the appropriate values dependig on the IP versions
        ipQuad ipq(ip_left_version,ip_right_version,&ipv4_left_real,&ipv4_right_real,&ipv4_left_virtual,&ipv4_right_virtual,
                   &ipv6_left_real,&ipv6_right_real,&ipv6_left_virtual,&ipv6_right_virtual);
  
        // then, initialize the parameter class instance
        rSenderParametersLatency spars2(&scp2,ip_left_version,pkt_pool_left_sender,leftport,"Forward",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                        ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,num_right_nets,
                                        rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max,
					state_table_size,stateTable,responder_tuples,left_send_ts);
  
        // start left sender
        if (rte_eal_remote_launch(rsendLatency, &spars2, cpu_left_sender) )
          std::cout << "Error: could not start Responder's Sender." << std::endl;
  
        // set parameters for the right receiver
        receiverParametersLatency rpars2(finish_receiving,rightport,"Forward",num_timestamps,right_receive_ts);
  
        // start right receiver
        if ( rte_eal_remote_launch(receiveLatency, &rpars2, cpu_right_receiver) )
          std::cout << "Error: could not start Right Receiver." << std::endl;
      }
  
      std::cout << "Info: Testing started." << std::endl;
  
      // wait until active senders and receivers finish
      if ( reverse ) {
        rte_eal_wait_lcore(cpu_right_sender);
        rte_eal_wait_lcore(cpu_left_receiver);
      }
      if ( forward ) {
        rte_eal_wait_lcore(cpu_left_sender);
        rte_eal_wait_lcore(cpu_right_receiver);
      }
      std::cout << "Info: Test finished." << std::endl;
      break;
      }
  } // end of switch
    
  // Process the timestamps
  int penalty=1000*(duration-delay)+global_timeout; // latency to be reported for lost timestamps, expressed in milliseconds
  if ( forward )
    evaluateLatency(num_timestamps, left_send_ts, right_receive_ts, hz, penalty, "Forward"); 
  if ( reverse )
    evaluateLatency(num_timestamps, right_send_ts, left_receive_ts, hz, penalty, "Reverse"); 

}

senderCommonParametersLatency::senderCommonParametersLatency(uint16_t ipv6_frame_size_, uint16_t ipv4_frame_size_, uint32_t frame_rate_, uint16_t duration_,
                                                             uint32_t n_, uint32_t m_, uint64_t hz_, uint64_t start_tsc_, 
                                                             uint16_t delay_, uint16_t num_timestamps_) :
  senderCommonParameters(ipv6_frame_size_,ipv4_frame_size_,frame_rate_,duration_,n_,m_,hz_,start_tsc_) {
  delay = delay_;
  num_timestamps = num_timestamps_;
}

senderParametersLatency::senderParametersLatency(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                                                  struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                                                  struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                                                  uint16_t num_dest_nets_, unsigned var_sport_, unsigned var_dport_,
						  uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_, uint64_t *send_ts_) :
  senderParameters(cp_,ip_version_,pkt_pool_,eth_id_,side_,dst_mac_,src_mac_,src_ipv4_,dst_ipv4_,src_ipv6_,dst_ipv6_,src_bg_,dst_bg_,num_dest_nets_,
		  var_sport_,var_dport_,sport_min_,sport_max_,dport_min_,dport_max_) {
  send_ts = send_ts_;
}

rSenderParametersLatency::rSenderParametersLatency(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                                                  struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                                                  struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                                                  uint16_t num_dest_nets_, unsigned var_sport_, unsigned var_dport_,
                                                  uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_,
						  unsigned state_table_size_, atomicFourTuple *stateTable_, unsigned responder_tuples_, uint64_t *send_ts_) :
  rSenderParameters(cp_,ip_version_,pkt_pool_,eth_id_,side_,dst_mac_,src_mac_,src_ipv4_,dst_ipv4_,src_ipv6_,dst_ipv6_,src_bg_,dst_bg_,num_dest_nets_,
                    var_sport_,var_dport_,sport_min_,sport_max_,dport_min_,dport_max_,state_table_size_,stateTable_,responder_tuples_) {
  send_ts = send_ts_;
}
    
receiverParametersLatency::receiverParametersLatency(uint64_t finish_receiving_, uint16_t eth_id_, const char *side_, 
						     uint16_t num_timestamps_, uint64_t *receive_ts_) :
  receiverParameters(finish_receiving_,eth_id_,side_) {
  num_timestamps = num_timestamps_;
  receive_ts = receive_ts_;
}

rReceiverParametersLatency::rReceiverParametersLatency(uint64_t finish_receiving_, uint16_t eth_id_, const char *side_, unsigned state_table_size_,
                      unsigned *valid_entries_, atomicFourTuple **stateTable_, uint16_t num_timestamps_, uint64_t *receive_ts_) :
  rReceiverParameters(finish_receiving_,eth_id_,side_,state_table_size_,valid_entries_,stateTable_) {
  num_timestamps = num_timestamps_;
  receive_ts = receive_ts_;
}


void evaluateLatency(uint16_t num_timestamps, uint64_t *send_ts, uint64_t *receive_ts, uint64_t hz, int penalty, const char *side) {
  double median_latency, worst_case_latency, *latency = new double[num_timestamps];
  if ( !latency )
    rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for latency values!\n");
  for ( int i=0; i<num_timestamps; i++ )
    if ( receive_ts[i] )
      latency[i] = 1000.0*(receive_ts[i]-send_ts[i])/hz; // calculate and exchange into milliseconds
    else
      latency[i] = penalty; // penalty of the lost timestamp
  if ( num_timestamps < 2 )
    median_latency = worst_case_latency = latency[0];
  else {
    std::sort(latency,latency+num_timestamps);
    if ( num_timestamps % 2 )
      median_latency = latency[num_timestamps/2]; // num_timestamps is odd: median is the middle element 
    else
      median_latency = (latency[num_timestamps/2-1]+latency[num_timestamps/2])/2; // num_timestamps is even: median is the average of the two middle elements
    worst_case_latency = latency[int(ceil(0.999*num_timestamps))-1]; // WCL is the 99.9th percentile
  }
  printf("%s TL: %lf\n", side, median_latency); // Typical Latency
  printf("%s WCL: %lf\n", side, worst_case_latency); // Worst Case Latency
}
