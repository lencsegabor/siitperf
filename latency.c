/* Siitperf is an RFC 8219 SIIT (stateless NAT64) tester written in C++ using DPDK
 *
 *  Copyright (C) 2019 Gabor Lencse
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
  if ( Throughput::readCmdLine(argc-2,argv) < 0 )
    return -1;
  if ( sscanf(argv[7], "%hu", &delay) != 1 || delay > 3600 ) {
    std::cerr << "Input Error: Delay before timestamps must be between 0 and 3600." << std::endl;
    return -1;
  }
  if ( duration <= delay ) {
    std::cerr << "Input Error: Test duration MUST be longer than the delay before timestamps." << std::endl;
    return -1;
  }
  if ( sscanf(argv[8], "%hu", &num_timestamps) != 1 || num_timestamps < 1 || num_timestamps > 50000 ) {
    std::cerr << "Input Error: Number of timestamps must be between 1 and 50000." << std::endl;
    return -1;
  }
  if ( (duration-delay)*frame_rate < num_timestamps ) {
    std::cerr << "Input Error: There are not enough test frames in the (duration-delay) interval to carry so many timestamps." << std::endl;
    return -1;
  }
  return 0;
}

int Latency::senderPoolSize(int num_dest_nets) {
  return Throughput::senderPoolSize(num_dest_nets)+num_timestamps; // frames with timestamps are also pre-generated
}

// creates a special IPv4 Test Frame tagged for latency measurement using several helper functions
struct rte_mbuf *mkLatencyFrame4(uint16_t length, rte_mempool *pkt_pool, const char *side,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const uint32_t *src_ip, const uint32_t *dst_ip, int id) {
  struct rte_mbuf *pkt_mbuf=rte_pktmbuf_alloc(pkt_pool); // message buffer for the Latency Frame
  if ( !pkt_mbuf )
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Latency Frame! \n", side);
  length -=  ETHER_CRC_LEN; // exclude CRC from the frame length
  pkt_mbuf->pkt_len = pkt_mbuf->data_len = length; // set the length in both places
  uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbuf, uint8_t *); // Access the Test Frame in the message buffer
  ether_hdr *eth_hdr = reinterpret_cast<struct ether_hdr *>(pkt); // Ethernet header
  ipv4_hdr *ip_hdr = reinterpret_cast<ipv4_hdr *>(pkt+sizeof(ether_hdr)); // IPv4 header
  udp_hdr *udp_hd = reinterpret_cast<udp_hdr *>(pkt+sizeof(ether_hdr)+sizeof(ipv4_hdr)); // UDP header
  uint8_t *udp_data = reinterpret_cast<uint8_t*>(pkt+sizeof(ether_hdr)+sizeof(ipv4_hdr)+sizeof(udp_hdr)); // UDP data

  mkEthHeader(eth_hdr, dst_mac, src_mac, 0x0800);       // contains an IPv4 packet
  int ip_length = length - sizeof(ether_hdr);
  mkIpv4Header(ip_hdr, ip_length, src_ip, dst_ip);      // Does not set IPv4 header checksum
  int udp_length = ip_length - sizeof(ipv4_hdr);        // No IP Options are used
  mkUdpHeader(udp_hd, udp_length);
  int data_legth = udp_length - sizeof(udp_hdr);
  mkDataLatency(udp_data, data_legth, id);
  udp_hd->dgram_cksum = rte_ipv4_udptcp_cksum( ip_hdr, udp_hd ); // UDP checksum is calculated and set
  ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);
  return pkt_mbuf;
}

// creates a special IPv6 Test Frame tagged for latency measurement using several helper functions
struct rte_mbuf *mkLatencyFrame6(uint16_t length, rte_mempool *pkt_pool, const char *side,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const struct in6_addr *src_ip, const struct in6_addr *dst_ip, uint16_t id) {
  struct rte_mbuf *pkt_mbuf=rte_pktmbuf_alloc(pkt_pool); // message buffer for the Latency Frame
  if ( !pkt_mbuf )
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Latency Frame! \n", side);
  length -=  ETHER_CRC_LEN; // exclude CRC from the frame length
  pkt_mbuf->pkt_len = pkt_mbuf->data_len = length; // set the length in both places
  uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbuf, uint8_t *); // Access the Test Frame in the message buffer
  ether_hdr *eth_hdr = reinterpret_cast<struct ether_hdr *>(pkt); // Ethernet header
  ipv6_hdr *ip_hdr = reinterpret_cast<ipv6_hdr *>(pkt+sizeof(ether_hdr)); // IPv6 header
  udp_hdr *udp_hd = reinterpret_cast<udp_hdr *>(pkt+sizeof(ether_hdr)+sizeof(ipv6_hdr)); // UDP header
  uint8_t *udp_data = reinterpret_cast<uint8_t*>(pkt+sizeof(ether_hdr)+sizeof(ipv6_hdr)+sizeof(udp_hdr)); // UDP data

  mkEthHeader(eth_hdr, dst_mac, src_mac, 0x86DD); // contains an IPv6 packet
  int ip_length = length - sizeof(ether_hdr);
  mkIpv6Header(ip_hdr, ip_length, src_ip, dst_ip);
  int udp_length = ip_length - sizeof(ipv6_hdr); // No IP Options are used
  mkUdpHeader(udp_hd, udp_length);
  int data_legth = udp_length - sizeof(udp_hdr);
  mkDataLatency(udp_data, data_legth, id);
  udp_hd->dgram_cksum = rte_ipv6_udptcp_cksum( ip_hdr, udp_hd ); // UDP checksum is calculated and set
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
  uint8_t eth_id = p->eth_id;
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
  uint64_t *send_ts = p->send_ts;

  uint64_t frames_to_send = duration * frame_rate;      // Each active sender sends this number of packets
  uint64_t sent_frames=0; // counts the number of sent frames
  double elapsed_seconds; // for checking the elapsed seconds during sending
  int latency_test_time = duration-delay;	// lenght of the time interval, while latency frames are sent
  uint64_t frames_to_send_during_latency_test = latency_test_time * frame_rate; // precalcalculated value to speed up calculation in the loop

  if ( num_dest_nets== 1 ) {
    // optimized code for single flow: always the same foreground or background frame is sent, except latency frames, which are stored in an array
    struct rte_mbuf *fg_pkt_mbuf, *bg_pkt_mbuf; // message buffers for fg. and bg. Test Frames
    // create foreground Test Frame
    if ( ip_version == 4 )
      fg_pkt_mbuf = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, dst_ipv4);
    else  // IPv6
      fg_pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6);

    // create backround Test Frame (always IPv6)
    bg_pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg);

    // create Latency Test Frames (may be foreground frames and background frames as well)
    struct rte_mbuf ** latency_frames = new struct rte_mbuf *[num_timestamps];
    if ( !latency_frames )
      rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for latency frame pointers!\n");

    uint64_t start_latency_frame = delay*frame_rate; // the ordinal numbert of the very first latency frame
    for ( int i=0; i<num_timestamps; i++ )
      if ( (start_latency_frame+i*frame_rate*latency_test_time/num_timestamps) % n  < m ) {
        if ( ip_version == 4 )  // foreground frame, may be IPv4 or IPv6
          latency_frames[i] = mkLatencyFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, dst_ipv4, i);
        else  // IPv6
          latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, i);
      } else {
        // background frame, must be IPv6
        latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg, i);
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
    // optimized code for multiple flows: foreground and background frames are generated for each flow and pointers are stored in arrays
    // latency frames are also pre-generated and stored in an array
    // num_dest_nets <= 256
    struct rte_mbuf *fg_pkt_mbuf[256], *bg_pkt_mbuf[256]; // message buffers for fg. and bg. Test Frames
    uint32_t curr_dst_ipv4;     // IPv4 destination address, which will be changed
    in6_addr curr_dst_ipv6;     // foreground IPv6 destination address, which will be changed
    in6_addr curr_dst_bg;       // backround IPv6 destination address, which will be changed
    int i;                      // cycle variable for grenerating different destination network addresses
    if ( ip_version == 4 )
      curr_dst_ipv4 = *dst_ipv4;
    else // IPv6
      curr_dst_ipv6 = *dst_ipv6;
    curr_dst_bg = *dst_bg;

    for ( i=0; i<num_dest_nets; i++ ) {
      // create foreground Test Frame
      if ( ip_version == 4 ) {
        ((uint8_t *)&curr_dst_ipv4)[2] = (uint8_t) i; // bits 16 to 23 of the IPv4 address are rewritten, like in 198.18.x.2
        fg_pkt_mbuf[i] = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, &curr_dst_ipv4);
      }
      else { // IPv6
        ((uint8_t *)&curr_dst_ipv6)[7] = (uint8_t) i; // bits 56 to 63 of the IPv6 address are rewritten, like in 2001:2:0:00xx::1
        fg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, &curr_dst_ipv6);
      }
      // create backround Test Frame (always IPv6)
      ((uint8_t *)&curr_dst_bg)[7] = (uint8_t) i; // see comment above
      bg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, &curr_dst_bg);
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

    uint64_t start_latency_frame = delay*frame_rate; // the ordinal numbert of the very first latency frame
    for ( int i=0; i<num_timestamps; i++ )
      if ( (start_latency_frame+i*frame_rate*latency_test_time/num_timestamps) % n  < m ) {
        if ( ip_version == 4 ) { 
          // random IPv4 destination network
          ((uint8_t *)&curr_dst_ipv4)[2] = (uint8_t) uni_dis(gen); // bits 16 to 23 of the IPv4 address are rewritten, like in 198.18.x.2
          latency_frames[i] = mkLatencyFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, &curr_dst_ipv4, i);
        } else {
          // random IPv6 destination network
          ((uint8_t *)&curr_dst_ipv6)[7] = (uint8_t) i; // bits 56 to 63 of the IPv6 address are rewritten, like in 2001:2:0:00xx::1
          latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, &curr_dst_ipv6, i);
        }
      } else {
        // background frame, must be IPv6, choose random network
        ((uint8_t *)&curr_dst_bg)[7] = (uint8_t) i; // see comment above
        latency_frames[i] = mkLatencyFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, &curr_dst_bg, i);
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
  } // end of optimized code for multiple flows

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
  uint8_t eth_id = p->eth_id;
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
            rte_exit(EXIT_FAILURE, "Error: Latency Frame with invalid frame ID was received!\n"); // to avoid segmentation fault
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
            rte_exit(EXIT_FAILURE, "Error: Latency Frame with invalid frame ID was received!\n"); // to avoid segmentation fault
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

// performs latency measurement
void Latency::measure(uint16_t leftport, uint16_t rightport) {
  uint64_t *left_send_ts, *right_send_ts, *left_receive_ts, *right_receive_ts; // pointers for timestamp arrays

  // set common parameters for senders
  senderCommonParametersLatency scp(ipv6_frame_size,ipv4_frame_size,frame_rate,duration,n,m,hz,start_tsc,delay,num_timestamps);

  if ( forward ) {      // Left to right direction is active

    // create dynamic arrays for timestamps
    left_send_ts = new uint64_t[num_timestamps];
    right_receive_ts = new uint64_t[num_timestamps];
    if ( !left_send_ts || !right_receive_ts )
      rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for timestamps!\n");
    // fill with 0 (will be used to chek, if frame with timestamp was received)
    memset(right_receive_ts, 0, num_timestamps*sizeof(uint64_t));

    // set individual parameters for the left sender

    // first, collect the appropriate values dependig on the IP versions
    ipQuad ipq(ip_left_version,ip_right_version,&ipv4_left_real,&ipv4_right_real,&ipv4_left_virtual,&ipv4_right_virtual,
               &ipv6_left_real,&ipv6_right_real,&ipv6_left_virtual,&ipv6_right_virtual);

    // then, initialize the parameter class instance
    senderParametersLatency spars(&scp,ip_left_version,pkt_pool_left_sender,leftport,"Forward",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                  ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,num_right_nets,left_send_ts);

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

    // create dynamic arrays for timestamps
    right_send_ts = new uint64_t[num_timestamps];
    left_receive_ts = new uint64_t[num_timestamps];
    if ( !right_send_ts || !left_receive_ts )
      rte_exit(EXIT_FAILURE, "Error: Tester can't allocate memory for timestamps!\n");
    // fill with 0 (will be used to chek, if frame with timestamp was received)
    memset(left_receive_ts, 0, num_timestamps*sizeof(uint64_t));

    // first, collect the appropriate values dependig on the IP versions
    ipQuad ipq(ip_right_version,ip_left_version,&ipv4_right_real,&ipv4_left_real,&ipv4_right_virtual,&ipv4_left_virtual,
               &ipv6_right_real,&ipv6_left_real,&ipv6_right_virtual,&ipv6_left_virtual);

    // then, initialize the parameter class instance
    senderParametersLatency spars(&scp,ip_right_version,pkt_pool_right_sender,rightport,"Reverse",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                           ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,num_left_nets,right_send_ts);

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

  // Process the timestamps
  int penalty=1000*(duration-delay)+global_timeout; // latency to be reported for lost timestamps, expressed in milliseconds
  if ( forward )
    evaluateLatency(num_timestamps, left_send_ts, right_receive_ts, hz, penalty, "Forward"); 
  if ( reverse )
    evaluateLatency(num_timestamps, right_send_ts, left_receive_ts, hz, penalty, "Reverse"); 

  std::cout << "Info: Test finished." << std::endl;
}

senderCommonParametersLatency::senderCommonParametersLatency(uint16_t ipv6_frame_size_, uint16_t ipv4_frame_size_, uint32_t frame_rate_, uint16_t duration_,
                                                             uint32_t n_, uint32_t m_, uint64_t hz_, uint64_t start_tsc_, 
                                                             uint16_t delay_, uint16_t num_timestamps_) :
  senderCommonParameters(ipv6_frame_size_,ipv4_frame_size_,frame_rate_,duration_,n_,m_,hz_,start_tsc_) {
  delay = delay_;
  num_timestamps = num_timestamps_;
}

senderParametersLatency::senderParametersLatency(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint8_t eth_id_, const char *side_,
                                                  struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                                                  struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                                                  uint16_t num_dest_nets_, uint64_t *send_ts_) :
  senderParameters(cp_,ip_version_,pkt_pool_,eth_id_,side_,dst_mac_,src_mac_,src_ipv4_,dst_ipv4_,src_ipv6_,dst_ipv6_,src_bg_,dst_bg_,num_dest_nets_) {
  send_ts = send_ts_;
}
    
receiverParametersLatency::receiverParametersLatency(uint64_t finish_receiving_, uint8_t eth_id_, const char *side_, 
						     uint16_t num_timestamps_, uint64_t *receive_ts_) :
  receiverParameters(finish_receiving_,eth_id_,side_) {
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
