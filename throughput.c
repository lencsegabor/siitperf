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

char coresList[101]; // buffer for preparing the list of lcores for DPDK init (like a command line argument)
char numChannels[11]; // buffer for printing the number of memory channels into a string for DPDK init (like a command line argument)

Throughput::Throughput(){
  // initialize some data members to default or invalid value
  ip_left_version = 6;	 	// default value for NAT64
  ip_right_version = 4; 	// default value for NAT64
  forward = 1;			// default value, left to right direction is active
  reverse = 1;			// default value, right to left direction is active 
  promisc = 1;			// default value, promiscuous mode is set
  cpu_left_sender = -1;		// MUST be set in the config file if forward != 0
  cpu_right_receiver = -1; 	// MUST be set in the config file if forward != 0
  cpu_right_sender = -1; 	// MUST be set in the config file if reverse != 0
  cpu_left_receiver = -1; 	// MUST be set in the config file if reverse != 0
  memory_channels = 1; 		// default value, this value will be set, if not specified in the config file
  num_left_nets = 1;		// default value: single flow
  num_right_nets = 1;		// default value: single flow
};

// finds a 'key' (name of a parameter) in the 'line' string
// '#' means comment, leading spaces and tabs are skipped
// return: the starting position of the key, if found; -1 otherwise
int Throughput::findKey(const char *line, const char *key) {
  int line_len, key_len; // the lenght of the line and of the key
  int pos; // current position in the line

  line_len=strlen(line);
  key_len=strlen(key);
  for ( pos=0; pos<line_len-key_len; pos++ ) {
    if ( line[pos] == '#' ) // comment
      return -1;
    if ( line[pos] == ' ' || line[pos] == '\t' )
      continue;
    if ( strncmp(line+pos,key,key_len) == 0 )
      return pos+strlen(key);
  }
  return -1;
}

// skips leading spaces and tabs, and cuts off tail starting by a space, tab or new line character
// it is needed, because inet_pton cannot read if there is e.g. a trailing '\n'
// WARNING: the input buffer is changed!
char *prune(char *s) {
  int len, i;
 
  // skip leading spaces and tabs 
  while ( *s==' ' || *s=='\t' )
    s++;

  // trim string, if space, tab or new line occurs
  len=strlen(s);
  for ( i=0; i<len; i++ )
    if ( s[i]==' ' || s[i]=='\t' || s[i]=='\n' ) {
      s[i]=(char)0;
      break;
    }
  return s;
}
    
// checks if there is some non comment information in the line
int nonComment(const char *line) {
  int i;

  for ( i=0; i<LINELEN; i++ ) {
    if ( line[i]=='#' || line[i]=='\n' )
      return 0; // line is comment or empty 
    else if ( line[i]==' ' || line[i]=='\t' )
      continue; // skip space or tab, see next char
    else
      return 1; // there is some other character
  }
  // below code should be unreachable
  return 1;
}

// reads the configuration file and stores the information in data members of class Throughput
int Throughput::readConfigFile(const char *filename) {
  FILE *f; 	// file descriptor
  char line[LINELEN+1]; // buffer for reading a line of the input file
  int pos; 	// position in the line after the key (parameter name) was found
  uint8_t *m; 	// pointer to the MAC address being read
  int line_no;	// line number for error message

  f=fopen(filename,"r");
  if ( f == NULL ) {
    std::cerr << "Input Error: Can't open file '" << filename << "'." << std::endl;
    return -1;
  }
  for ( line_no=1; fgets(line, LINELEN+1, f); line_no++ ) {
    if ( (pos = findKey(line, "IP-L-Vers")) >= 0 ) {
      sscanf(line+pos, "%d", &ip_left_version);
      if ( ip_left_version!=4 && ip_left_version!=6 ) {
        std::cerr << "Input Error: 'IP-L-Vers' must be 4 or 6." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IP-R-Vers")) >= 0 ) {
      sscanf(line+pos, "%d", &ip_right_version);
      if ( ip_right_version!=4 && ip_right_version!=6 ) {
        std::cerr << "Input Error: 'IP-R-Vers' must be 4 or 6." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv6-L-Real")) >= 0 ) {
      if ( inet_pton(AF_INET6, prune(line+pos), reinterpret_cast<void *>(&ipv6_left_real)) != 1 ) {
        std::cerr << "Input Error: Bad 'IPv6-L-Real' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv6-L-Virt")) >= 0 ) {
      if ( inet_pton(AF_INET6, prune(line+pos), reinterpret_cast<void *>(&ipv6_left_virtual)) != 1 ) {
        std::cerr << "Input Error: Bad 'IPv6-L-Virt' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv6-R-Real")) >= 0 ) {
      if ( inet_pton(AF_INET6, prune(line+pos), reinterpret_cast<void *>(&ipv6_right_real)) != 1 ) {
        std::cerr << "Input Error: Bad 'IPv6-R-Real' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv6-R-Virt")) >= 0 ) {
      if ( inet_pton(AF_INET6, prune(line+pos), reinterpret_cast<void *>(&ipv6_right_virtual)) != 1 ) {
        std::cerr << "Input Error: Bad 'IPv6-R-Virt' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv4-L-Real")) >= 0 ) {
      if ( inet_pton(AF_INET, prune(line+pos), reinterpret_cast<void *>(&ipv4_left_real)) != 1 ) {
        std::cerr << "Input Error: Bad 'IPv4-L-Real' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv4-L-Virt")) >= 0 ) {
      if ( inet_pton(AF_INET, prune(line+pos), reinterpret_cast<void *>(&ipv4_left_virtual)) != 1 ) {
         std::cerr << "Input Error: Bad 'IPv4-L-Virt' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv4-R-Real")) >= 0 ) {
      if ( inet_pton(AF_INET, prune(line+pos), reinterpret_cast<void *>(&ipv4_right_real)) != 1 ) {
        std::cerr << "Input Error: Bad 'IPv4-R-Real' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv4-R-Virt")) >= 0 ) {
      if ( inet_pton(AF_INET, prune(line+pos), reinterpret_cast<void *>(&ipv4_right_virtual)) != 1 ) {
        std::cerr << "Input Error: Bad 'IPv4-R-Virt' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "MAC-L-Tester")) >= 0 ) {
      m=mac_left_tester;
      if ( sscanf(line+pos, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) < 6 ) {
        std::cerr << "Input Error: Bad 'MAC-L-Tester' address." << std::endl;
        return -1;
      } 
    } else if ( (pos = findKey(line, "MAC-R-Tester")) >= 0 ) {
      m=mac_right_tester;
      if ( sscanf(line+pos, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) < 6 ) {
        std::cerr << "Input Error: Bad 'MAC-R-Tester' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "MAC-L-DUT")) >= 0 ) {
      m=mac_left_dut;
      if ( sscanf(line+pos, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) < 6 ) {
        std::cerr << "Input Error: Bad 'MAC-L-DUT' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "MAC-R-DUT")) >= 0 ) {
      m=mac_right_dut;
      if ( sscanf(line+pos, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) < 6 ) {
        std::cerr << "Input Error: Bad 'MAC-R-DUT' address." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Num-L-Nets")) >= 0 ) {
      sscanf(line+pos, "%hu", &num_left_nets);
      if ( num_left_nets < 1 || num_left_nets > 256 ) {
        std::cerr << "Input Error: 'Num-L-Nets' must be >= 1 and <= 256." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Num-R-Nets")) >= 0 ) {
      sscanf(line+pos, "%hu", &num_right_nets);
      if ( num_right_nets < 1 || num_right_nets > 256 ) {
        std::cerr << "Input Error: 'Num-R-Nets' must be >= 1 and <= 256." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Forward")) >= 0 ) {
      sscanf(line+pos, "%d", &forward);
    } else if ( (pos = findKey(line, "Reverse")) >= 0 ) {
      sscanf(line+pos, "%d", &reverse);
    } else if ( (pos = findKey(line, "Promisc")) >= 0 ) {
      sscanf(line+pos, "%d", &promisc);
    } else if ( (pos = findKey(line, "CPU-L-Send")) >= 0 ) {
      sscanf(line+pos, "%d", &cpu_left_sender);
      if ( cpu_left_sender < 0 || cpu_left_sender >= RTE_MAX_LCORE ) {
        std::cerr << "Input Error: 'CPU-L-Send' must be >= 0 and < RTE_MAX_LCORE." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "CPU-R-Recv")) >= 0 ) {
      sscanf(line+pos, "%d", &cpu_right_receiver);
      if ( cpu_right_receiver < 0 || cpu_right_receiver >= RTE_MAX_LCORE ) {
        std::cerr << "Input Error: 'CPU-R-Recv' must be >= 0 and < RTE_MAX_LCORE." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "CPU-R-Send")) >= 0 ) {
      sscanf(line+pos, "%d", &cpu_right_sender);
      if ( cpu_right_sender < 0 || cpu_right_sender >= RTE_MAX_LCORE ) {
        std::cerr << "Input Error: 'CPU-R-Send' must be >= 0 and < RTE_MAX_LCORE." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "CPU-L-Recv")) >= 0 ) {
      sscanf(line+pos, "%d", &cpu_left_receiver);
      if ( cpu_left_receiver < 0 || cpu_left_receiver >= RTE_MAX_LCORE ) {
        std::cerr << "Input Error: 'CPU-L-Recv' must be >= 0 and < RTE_MAX_LCORE." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "MEM-Channels")) >= 0 ) {
      sscanf(line+pos, "%hhu", &memory_channels);
      if ( memory_channels <= 0 ) {
        std::cerr << "Input Error: 'MEM-Channels' must be > 0." << std::endl;
        return -1;
      }
    } else if ( nonComment(line) ) { // It may be too strict!
        std::cerr << "Input Error: Cannot interpret '" << filename << "' line " << line_no << ":" << std::endl;
        std::cerr << line << std::endl;
        return -1;
    } 
  }
  fclose(f);
  // check if at least one direction is active
  if ( forward == 0 && reverse == 0 ) {
    std::cerr << "Input Error: No active direction was specified." << std::endl;
    return -1;
  }
  // check if the necessary lcores were specified
  if ( forward ) {
    if ( cpu_left_sender < 0 ) {
      std::cerr << "Input Error: No 'CPU-L-Send' was specified." << std::endl;
      return -1;
    }
    if ( cpu_right_receiver < 0 ) {
      std::cerr << "Input Error: No 'CPU-R-Recv' was specified." << std::endl;
      return -1;
    }
  }
  if ( reverse ) {
    if ( cpu_right_sender < 0 ) {
      std::cerr << "Input Error: No 'CPU-R-Send' was specified." << std::endl;
      return -1;
    }
    if ( cpu_left_receiver < 0 ) {
      std::cerr << "Input Error: No 'CPU-L-Recv' was specified." << std::endl;
      return -1;
    }
  }
  return 0;
}

// reads the command line arguments and stores the information in data members of class Throughput
// It may be called only AFTER the execution of readConfigFile
int Throughput::readCmdLine(int argc, const char *argv[]) {
  if (argc < 7) {
    std::cerr << "Input Error: Too few command line arguments." << std::endl;
    return -1;
  }
  if ( sscanf(argv[1], "%hu", &ipv6_frame_size) != 1 || ipv6_frame_size < 84 || ipv6_frame_size > 1538 ) {
    std::cerr << "Input Error: IPv6 frame size must be between 84 and 1538." << std::endl;
    return -1;
  }
  // Further checking of the frame size will be done, when n and m are read.
  ipv4_frame_size=ipv6_frame_size-20;
  if ( sscanf(argv[2], "%u", &frame_rate) != 1 || frame_rate < 1 || frame_rate > 14880952 ) { 
    // 14,880,952 is the maximum frame rate for 10Gbps Ethernet using 64-byte frame size
    std::cerr << "Input Error: Frame rate must be between 1 and 14880952." << std::endl;
    return -1;
  }
  if ( sscanf(argv[3], "%hu", &duration) != 1 || duration < 1 || duration > 3600 ) {
    std::cerr << "Input Error: Test duration must be between 1 and 3600." << std::endl;
    return -1;
  }
  if ( sscanf(argv[4], "%hu", &global_timeout) != 1 || global_timeout > 60000 ) {
    std::cerr << "Input Error: Global timeout must be between 0 and 60000." << std::endl;
    return -1;
  }
  if ( sscanf(argv[5], "%u", &n) != 1 || n < 2  ) {
    std::cerr << "Input Error: The value of 'n' must be at least 2." << std::endl;
    return -1;
  }
  if ( sscanf(argv[6], "%u", &m) != 1 ) {
    std::cerr << "Input Error: Cannot read the value of 'm'." << std::endl;
    return -1;
  }

  if ( ipv6_frame_size > 1518 && ( forward && ip_left_version == 6 || reverse && ip_right_version == 6 || m < n ) ) {
    std::cerr << "Input Error: IPv6 frame sizes between 1518 and 1538 are allowed for pure IPv4 traffic only (as IPv4 frames are 20 bytes shorter)." << std::endl;
    return -1;
  }

  return 0;
}

// Initializes DPDK EAL, starts network ports, creates and sets up TX/RX queues, checks NUMA localty and TSC synchronization of lcores
int Throughput::init(const char *argv0, uint16_t leftport, uint16_t rightport) {
  const char *rte_argv[6]; // parameters for DPDK EAL init, e.g.: {NULL, "-l", "4,5,6,7", "-n", "2", NULL};
  int rte_argc = static_cast<int>(sizeof(rte_argv) / sizeof(rte_argv[0])) - 1; // argc value for DPDK EAL init
  struct rte_eth_conf cfg_port;		// for configuring the Ethernet ports
  struct rte_eth_link link_info;	// for retrieving link info by rte_eth_link_get()
  int trials; 	// cycle variable for port state checking

  // prepare 'command line' arguments for rte_eal_init
  rte_argv[0]=argv0; 	// program name
  rte_argv[1]="-l";	// list of lcores will follow
  // Only lcores for the active directions are to be included (at least one of them MUST be non-zero)
  if ( forward && reverse ) {
    // both directions are active 
    snprintf(coresList, 101, "0,%d,%d,%d,%d", cpu_left_sender, cpu_right_receiver, cpu_right_sender, cpu_left_receiver);
  } else if ( forward )
    snprintf(coresList, 101, "0,%d,%d", cpu_left_sender, cpu_right_receiver); // only forward (left to right) is active 
  else 
    snprintf(coresList, 101, "0,%d,%d", cpu_right_sender, cpu_left_receiver); // only reverse (right to left) is active
  rte_argv[2]=coresList;
  rte_argv[3]="-n";
  snprintf(numChannels, 11, "%hhu", memory_channels);
  rte_argv[4]=numChannels;
  rte_argv[5]=0;

  if ( rte_eal_init(rte_argc, const_cast<char **>(rte_argv)) < 0 ) {
    std::cerr << "Error: DPDK RTE initialization failed, Tester exits." << std::endl;
    return -1;
  }

  if ( !rte_eth_dev_is_valid_port(leftport) ) {
    std::cerr << "Error: Network port #" << leftport << " provided as Left Port is not available, Tester exits." << std::endl;
    return -1;
  }

  if ( !rte_eth_dev_is_valid_port(rightport) ) {
    std::cerr << "Error: Network port #" << rightport << " provided as Right Port is not available, Tester exits." << std::endl;
    return -1;
  }

  // prepare for configuring the Ethernet ports
  memset(&cfg_port, 0, sizeof(cfg_port)); 	// e.g. no CRC generation offloading, etc. (May be improved later!)
  cfg_port.txmode.mq_mode = ETH_MQ_TX_NONE;	// no multi queues 
  cfg_port.rxmode.mq_mode = ETH_MQ_RX_NONE;	// no multi queues 

  if ( rte_eth_dev_configure(leftport, 1, 1, &cfg_port) < 0 ) {
    std::cerr << "Error: Cannot configure network port #" << leftport << " provided as Left Port, Tester exits." << std::endl;
    return -1;
  }

  if ( rte_eth_dev_configure(rightport, 1, 1, &cfg_port) < 0 ) {
    std::cerr << "Error: Cannot configure network port #" << rightport << " provided as Right Port, Tester exits." << std::endl;
    return -1;
  }

  // Important remark: with no regard whether actual test will be performed in the forward or reverese direcetion, 
  // all TX and RX queues MUST be set up properly, otherwise rte_eth_dev_start() will cause segmentation fault.
  // Sender pool size calculation uses 0 instead of num_{left,right}_nets, when no actual frame sending is needed. 

  // calculate packet pool sizes and then create the pools
  int receiver_pool_size = PORT_RX_QUEUE_SIZE + 2 * MAX_PKT_BURST + 100; // While one of them is processed, the other one is being filled. 

  pkt_pool_left_sender = rte_pktmbuf_pool_create ( "pp_left_sender", senderPoolSize(forward ? num_right_nets: 0 ), PKTPOOL_CACHE, 0, 
                                                   RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id(cpu_left_sender));
  if ( !pkt_pool_left_sender ) {
    std::cerr << "Error: Cannot create packet pool for Left Sender, Tester exits." << std::endl;
    return -1;
  }
  pkt_pool_right_receiver = rte_pktmbuf_pool_create ( "pp_right_receiver", receiver_pool_size, PKTPOOL_CACHE, 0, 
                                                      RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id(cpu_right_receiver));
  if ( !pkt_pool_right_receiver ) {
    std::cerr << "Error: Cannot create packet pool for Right Receiver, Tester exits." << std::endl;
    return -1;
  }

  pkt_pool_right_sender = rte_pktmbuf_pool_create ( "pp_right_sender", senderPoolSize(reverse ? num_left_nets : 0), PKTPOOL_CACHE, 0,
                                                    RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id(cpu_right_sender));
  if ( !pkt_pool_right_sender ) {
    std::cerr << "Error: Cannot create packet pool for Right Sender, Tester exits." << std::endl;
    return -1;
  }
  pkt_pool_left_receiver = rte_pktmbuf_pool_create ( "pp_left_receiver", receiver_pool_size, PKTPOOL_CACHE, 0,
                                                     RTE_MBUF_DEFAULT_BUF_SIZE, rte_lcore_to_socket_id(cpu_left_receiver));
  if ( !pkt_pool_left_receiver ) {
    std::cerr << "Error: Cannot create packet pool for Left Receiver, Tester exits." << std::endl;
    return -1;
  }

  // set up the TX/RX queues 
  if ( rte_eth_tx_queue_setup(leftport, 0, PORT_TX_QUEUE_SIZE, rte_eth_dev_socket_id(leftport), NULL) < 0) {
    std::cerr << "Error: Cannot setup TX queue for Left Sender, Tester exits." << std::endl;
    return -1;
  }
  if ( rte_eth_rx_queue_setup(rightport, 0, PORT_RX_QUEUE_SIZE, rte_eth_dev_socket_id(rightport), NULL, pkt_pool_right_receiver) < 0) {
    std::cerr << "Error: Cannot setup RX queue for Right Receiver, Tester exits." << std::endl;
    return -1;
  }
  if ( rte_eth_tx_queue_setup(rightport, 0, PORT_TX_QUEUE_SIZE, rte_eth_dev_socket_id(rightport), NULL) < 0) {
    std::cerr << "Error: Cannot setup TX queue for Right Sender, Tester exits." << std::endl;
    return -1;
  }
  if ( rte_eth_rx_queue_setup(leftport, 0, PORT_RX_QUEUE_SIZE, rte_eth_dev_socket_id(leftport), NULL, pkt_pool_left_receiver) < 0) {
    std::cerr << "Error: Cannot setup RX queue for Left Receiver, Tester exits." << std::endl;
    return -1;
  }

  // start the Ethernet ports
  if ( rte_eth_dev_start(leftport) < 0 ) {
    std::cerr << "Error: Cannot start network port #" << leftport << " provided as Left Port, Tester exits." << std::endl;
    return -1;
  }
  if ( rte_eth_dev_start(rightport) < 0 ) {
    std::cerr << "Error: Cannot start network port #" << rightport << " provided as Right Port, Tester exits." << std::endl;
    return -1;
  }

  if ( promisc ) {
    rte_eth_promiscuous_enable(leftport);
    rte_eth_promiscuous_enable(rightport);
  }

  // check links' states (wait for coming up), try maximum MAX_PORT_TRIALS times
  trials=0;
  do {
    if ( trials++ == MAX_PORT_TRIALS ) { 
      std::cerr << "Error: Left Ethernet port is DOWN, Tester exits." << std::endl;
      return -1;
    }
  rte_eth_link_get(leftport, &link_info);
  } while ( link_info.link_status == ETH_LINK_DOWN );
  trials=0;
  do {
    if ( trials++ == MAX_PORT_TRIALS ) {
      std::cerr << "Error: Right Ethernet port is DOWN, Tester exits." << std::endl;
      return -1;
    }
  rte_eth_link_get(rightport, &link_info);
  } while ( link_info.link_status == ETH_LINK_DOWN );

  // Some sanity checks: NUMA node of the cores and of the NICs are matching or not...
  if ( numa_available() == -1 )
    std::cout << "Info: This computer does not support NUMA." << std::endl;
  else {
    if ( numa_num_configured_nodes() == 1 )
      std::cout << "Info: Only a single NUMA node is configured, there is no possibilty for mismatch." << std::endl;
    else {
      if ( forward ) {
        numaCheck(leftport, "Left", cpu_left_sender, "Left Sender");
        numaCheck(rightport, "Right", cpu_right_receiver, "Right Receiver");
      }
      if ( reverse ) {
        numaCheck(rightport, "Right", cpu_right_sender, "Right Sender");
        numaCheck(leftport, "Left", cpu_left_receiver, "Left Receiver");
      }
    }
  }

  // Some sanity checks: TSCs of the used cores are synchronized or not...
  if ( forward ) {
    check_tsc(cpu_left_sender, "Left Sender");
    check_tsc(cpu_right_receiver, "Right Receiver");
  }
  if ( reverse ) {
    check_tsc(cpu_right_sender, "Right Sender");
    check_tsc(cpu_left_receiver, "Left Receiver");
  }

  // prepare further values for testing
  hz = rte_get_timer_hz();		// number of clock cycles per second
  start_tsc = rte_rdtsc()+hz*START_DELAY/1000;	// Each active sender starts sending at this time
  finish_receiving = start_tsc + hz*(duration+global_timeout/1000.0); 	// Each receiver stops at this time
  return 0;
}

// calculates sender pool size, it is a virtual member function, redefined in derived classes
int Throughput::senderPoolSize(int num_dest_nets) {
  return 3*num_dest_nets + PORT_TX_QUEUE_SIZE + 100; // 3*: IPv4, IPv6 fg, IPv6 bg
}

//checks NUMA localty: is the NUMA node of network port and CPU the same?
void Throughput::numaCheck(uint16_t port, const char *port_side, int cpu, const char *cpu_name) {
  int n_port, n_cpu;
  n_port = rte_eth_dev_socket_id(port);
  n_cpu = numa_node_of_cpu(cpu);
  if ( n_port == n_cpu )
    std::cout << "Info: " << port_side << " port and " << cpu_name << " CPU core belong to the same NUMA node: " << n_port << std::endl;
  else
    std::cout << "Warning: " << port_side << " port and " << cpu_name << " CPU core belong to NUMA nodes " <<
      n_port << ", " << n_cpu << ", respectively." << std::endl; 
}

// reports the TSC of the core (in the variable pointed by the input parameter), on which it is running
int report_tsc(void *par) {
   *(uint64_t *)par = rte_rdtsc();
   return 0;
}

// checks if the TSC of the given lcore is synchronized with that of the main core
// Note that TSCs of different pysical CPUs may be different, which would prevent siitperf from working correctly!
void check_tsc(int cpu, const char *cpu_name) {
  uint64_t tsc_before, tsc_reported, tsc_after;

  tsc_before = rte_rdtsc();
  if ( rte_eal_remote_launch(report_tsc, &tsc_reported, cpu) )
    rte_exit(EXIT_FAILURE, "Error: could not start TSC checker on core #%i for %s!\n", cpu, cpu_name);
  rte_eal_wait_lcore(cpu);
  tsc_after = rte_rdtsc();
  if ( tsc_reported < tsc_before || tsc_reported > tsc_after )
    rte_exit(EXIT_FAILURE, "Error: TSC of core #%i for %s is not synchronized with that of the main core!\n", cpu, cpu_name);
}

// creates an IPv4 Test Frame using several helper functions
struct rte_mbuf *mkTestFrame4(uint16_t length, rte_mempool *pkt_pool, const char *side,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const uint32_t *src_ip, const uint32_t *dst_ip) {
  struct rte_mbuf *pkt_mbuf=rte_pktmbuf_alloc(pkt_pool); // message buffer for the Test Frame
  if ( !pkt_mbuf )
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Test Frame! \n", side);
  length -=  ETHER_CRC_LEN; // exclude CRC from the frame length
  pkt_mbuf->pkt_len = pkt_mbuf->data_len = length; // set the length in both places
  uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbuf, uint8_t *); // Access the Test Frame in the message buffer
  ether_hdr *eth_hdr = reinterpret_cast<struct ether_hdr *>(pkt); // Ethernet header
  ipv4_hdr *ip_hdr = reinterpret_cast<ipv4_hdr *>(pkt+sizeof(ether_hdr)); // IPv4 header
  udp_hdr *udp_hd = reinterpret_cast<udp_hdr *>(pkt+sizeof(ether_hdr)+sizeof(ipv4_hdr)); // UDP header
  uint8_t *udp_data = reinterpret_cast<uint8_t*>(pkt+sizeof(ether_hdr)+sizeof(ipv4_hdr)+sizeof(udp_hdr)); // UDP data

  mkEthHeader(eth_hdr, dst_mac, src_mac, 0x0800); 	// contains an IPv4 packet
  int ip_length = length - sizeof(ether_hdr);
  mkIpv4Header(ip_hdr, ip_length, src_ip, dst_ip); 	// Does not set IPv4 header checksum
  int udp_length = ip_length - sizeof(ipv4_hdr); 	// No IP Options are used
  mkUdpHeader(udp_hd, udp_length);			
  int data_legth = udp_length - sizeof(udp_hdr);
  mkData(udp_data, data_legth);
  udp_hd->dgram_cksum = rte_ipv4_udptcp_cksum( ip_hdr, udp_hd ); // UDP checksum is calculated and set
  ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);	// IPv4 header checksum is set now
  return pkt_mbuf;
}

// Please refer to RFC 2544 Appendx C.2.6.4 Test Frames for the values to be set in the test frames.

// creates and Ethernet header
void mkEthHeader(struct ether_hdr *eth, const struct ether_addr *dst_mac, const struct ether_addr *src_mac, const uint16_t ether_type) {
  rte_memcpy(&eth->d_addr, dst_mac, sizeof(struct ether_hdr));
  rte_memcpy(&eth->s_addr, src_mac, sizeof(struct ether_hdr));
  eth->ether_type = htons(ether_type);
}

// creates and IPv4 header
void mkIpv4Header(struct ipv4_hdr *ip, uint16_t length, const uint32_t *src_ip, const uint32_t *dst_ip) {
  ip->version_ihl=0x45; // Version: 4, IHL: 20/4=5
  ip->type_of_service=0; 
  ip->total_length = htons(length);
  ip->packet_id = 0;
  ip->fragment_offset = 0;
  ip->time_to_live = 0x0A; 
  ip->next_proto_id = 0x11; // UDP
  ip->hdr_checksum = 0;
  rte_memcpy(&ip->src_addr,src_ip,4);
  rte_memcpy(&ip->dst_addr,dst_ip,4);
  // May NOT be set now, only after the UDP header checksum calculation: ip->hdr_checksum = rte_ipv4_cksum(ip);
}

// creates and UDP header
void mkUdpHeader(struct udp_hdr *udp, uint16_t length) {
  udp->src_port =  htons(0xC020);
  udp->dst_port =  htons(0x0007); // Echo
  udp->dgram_len = htons(length);
  udp->dgram_cksum = 0; // Checksum is set to 0 now.
  // UDP checksum is calculated later.
}

// fills the data field of the Test Frame
void mkData(uint8_t *data, uint16_t length) {
  unsigned i;
  uint8_t identify[8]= { 'I', 'D', 'E', 'N', 'T', 'I', 'F', 'Y' };      // Identification of the Test Frames
  uint64_t *id=(uint64_t *) identify;
  *(uint64_t *)data = *id;
  data += 8;
  length -= 8;
  for ( i=0; i<length; i++ )
    data[i] = i % 256;
}

// creates an IPv6 Test Frame using several helper functions
struct rte_mbuf *mkTestFrame6(uint16_t length, rte_mempool *pkt_pool, const char *side,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const struct in6_addr *src_ip, const struct in6_addr *dst_ip) {
  struct rte_mbuf *pkt_mbuf=rte_pktmbuf_alloc(pkt_pool); // message buffer for the Test Frame
  if ( !pkt_mbuf )
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the Test Frame! \n", side);
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
  mkData(udp_data, data_legth);
  udp_hd->dgram_cksum = rte_ipv6_udptcp_cksum( ip_hdr, udp_hd ); // UDP checksum is calculated and set
  return pkt_mbuf;
}

// creates and IPv6 header
void mkIpv6Header(struct ipv6_hdr *ip, uint16_t length, const struct in6_addr *src_ip, const struct in6_addr *dst_ip) {
  ip->vtc_flow = htonl(0x60000000); // Version: 6, Traffic class: 0, Flow label: 0
  ip->payload_len = htons(length-sizeof(ipv6_hdr)); 
  ip->proto = 0x11; // UDP
  ip->hop_limits = 0x0A; 
  rte_mov16((uint8_t *)&ip->src_addr,(uint8_t *)src_ip);
  rte_mov16((uint8_t *)&ip->dst_addr,(uint8_t *)dst_ip);
}

// sends Test Frames for throughput (or frame loss rate) measurements
int send(void *par) {
  // collecting input parameters:
  class senderParameters *p = (class senderParameters *)par;
  class senderCommonParameters *cp = p->cp;

  // parameters directly correspond to the data members of class Throughput
  uint16_t ipv6_frame_size = cp->ipv6_frame_size;
  uint16_t ipv4_frame_size = cp->ipv4_frame_size;
  uint32_t frame_rate = cp->frame_rate;
  uint16_t duration = cp->duration;
  uint32_t n = cp->n;
  uint32_t m = cp->m;
  uint64_t hz = cp->hz;
  uint64_t start_tsc = cp->start_tsc;

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

  // further local variables
  uint64_t frames_to_send = duration * frame_rate;	// Each active sender sends this number of frames
  uint64_t sent_frames=0; // counts the number of sent frames
  double elapsed_seconds; // for checking the elapsed seconds during sending

  if ( num_dest_nets== 1 ) { 	
    // optimized code for single flow: always the same foreground or background frame is sent, no arrays are used
    struct rte_mbuf *fg_pkt_mbuf, *bg_pkt_mbuf; // message buffers for fg. and bg. Test Frames
    // create foreground Test Frame
    if ( ip_version == 4 )
      fg_pkt_mbuf = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, dst_ipv4);
    else  // IPv6
      fg_pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6);

    // create backround Test Frame (always IPv6)
    bg_pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg);

    // naive sender version: it is simple and fast
    for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
      while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); // Beware: an "empty" loop, and further two will come!
      if ( sent_frames % n  < m )
        while ( !rte_eth_tx_burst(eth_id, 0, &fg_pkt_mbuf, 1) ); // send foreground frame
      else
         while ( !rte_eth_tx_burst(eth_id, 0, &bg_pkt_mbuf, 1) ); // send background frame
    } // this is the end of the sending cycle
  } // end of optimized code for single flow
  else {
    // optimized code for multiple flows: foreground and background frames are generated for each flow and pointers are stored in arrays
    // num_dest_nets <= 256 
    struct rte_mbuf *fg_pkt_mbuf[256], *bg_pkt_mbuf[256]; // message buffers for fg. and bg. Test Frames
    uint32_t curr_dst_ipv4; 	// IPv4 destination address, which will be changed
    in6_addr curr_dst_ipv6; 	// foreground IPv6 destination address, which will be changed
    in6_addr curr_dst_bg; 	// backround IPv6 destination address, which will be changed
    int i; 			// cycle variable for grenerating different destination network addresses
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
    thread_local std::random_device rd;  // Will be used to obtain a seed for the random number engine
    thread_local std::mt19937_64 gen(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<int> uni_dis(0, num_dest_nets-1);	// uniform distribution in [0, num_dest_nets-1]

    // naive sender version: it is simple and fast
    for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
      int index = uni_dis(gen);	// index of the pre-generated frame
      while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); // Beware: an "empty" loop, and further two will come!
      if ( sent_frames % n  < m )
        while ( !rte_eth_tx_burst(eth_id, 0, &fg_pkt_mbuf[index], 1) ); // send foreground frame
      else
         while ( !rte_eth_tx_burst(eth_id, 0, &bg_pkt_mbuf[index], 1) ); // send background frame
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

// receives Test Frames for throughput (or frame loss rate) measurements
// Offsets from the start of the Ethernet Frame:
// EtherType: 6+6=12
// IPv6 Next header: 14+6=20, UDP Data for IPv6: 14+40+8=62
// IPv4 Protolcol: 14+9=23, UDP Data for IPv4: 14+20+8=42
int receive(void *par) {
  // collecting input parameters:
  class receiverParameters *p = (class receiverParameters *)par;
  uint64_t finish_receiving = p->finish_receiving;
  uint8_t eth_id = p->eth_id;
  const char *side = p->side;

  // further local variables
  int frames, i;
  struct rte_mbuf *pkt_mbufs[MAX_PKT_BURST]; // pointers for the mbufs of received frames
  uint16_t ipv4=htons(0x0800); // EtherType for IPv4 in Network Byte Order
  uint16_t ipv6=htons(0x86DD); // EtherType for IPv6 in Network Byte Order
  uint8_t identify[8]= { 'I', 'D', 'E', 'N', 'T', 'I', 'F', 'Y' };	// Identificion of the Test Frames
  uint64_t *id=(uint64_t *) identify;
  uint64_t received=0; 	// number of received frames

  while ( rte_rdtsc() < finish_receiving ){
    frames = rte_eth_rx_burst(eth_id, 0, pkt_mbufs, MAX_PKT_BURST);
    for (i=0; i < frames; i++){
      uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbufs[i], uint8_t *); // Access the Test Frame in the message buffer
      // check EtherType at offset 12: IPv6, IPv4, or anything else
      if ( *(uint16_t *)&pkt[12]==ipv6 ) { /* IPv6  */
        /* check if IPv6 Next Header is UDP, and the first 8 bytes of UDP data is 'IDENTIFY' */
        if ( likely( pkt[20]==17 && *(uint64_t *)&pkt[62]==*id ) )
          received++;
      } else if ( *(uint16_t *)&pkt[12]==ipv4 ) { /* IPv4 */
        if ( likely( pkt[23]==17 && *(uint64_t *)&pkt[42]==*id ) )
           received++;
      }
      rte_pktmbuf_free(pkt_mbufs[i]);
    }
  }
  printf("%s frames received: %lu\n", side, received);
  return received;
}

// performs throughput (or frame loss rate) measurement
void Throughput::measure(uint16_t leftport, uint16_t rightport) {
  // set common parameters for senders
  senderCommonParameters scp(ipv6_frame_size,ipv4_frame_size,frame_rate,duration,n,m,hz,start_tsc);

  if ( forward ) {	// Left to right direction is active
    // set individual parameters for the left sender

    // first, collect the appropriate values dependig on the IP versions 
    ipQuad ipq(ip_left_version,ip_right_version,&ipv4_left_real,&ipv4_right_real,&ipv4_left_virtual,&ipv4_right_virtual,
               &ipv6_left_real,&ipv6_right_real,&ipv6_left_virtual,&ipv6_right_virtual);

    // then, initialize the parameter class instance
    senderParameters spars(&scp,ip_left_version,pkt_pool_left_sender,leftport,"Forward",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                           ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,num_right_nets);
                            
    // start left sender
    if ( rte_eal_remote_launch(send, &spars, cpu_left_sender) )
      std::cout << "Error: could not start Left Sender." << std::endl;

    // set parameters for the right receiver
    receiverParameters rpars(finish_receiving,rightport,"Forward");

    // start right receiver
    if ( rte_eal_remote_launch(receive, &rpars, cpu_right_receiver) )
      std::cout << "Error: could not start Right Receiver." << std::endl;
  }

  if ( reverse ) {	// Right to Left direction is active 
    // set individual parameters for the right sender

    // first, collect the appropriate values dependig on the IP versions
    ipQuad ipq(ip_right_version,ip_left_version,&ipv4_right_real,&ipv4_left_real,&ipv4_right_virtual,&ipv4_left_virtual,
               &ipv6_right_real,&ipv6_left_real,&ipv6_right_virtual,&ipv6_left_virtual);

    // then, initialize the parameter class instance
    senderParameters spars(&scp,ip_right_version,pkt_pool_right_sender,rightport,"Reverse",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                           ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,num_left_nets);

    // start right sender
    if (rte_eal_remote_launch(send, &spars, cpu_right_sender) )
      std::cout << "Error: could not start Right Sender." << std::endl;

    // set parameters for the left receiver
    receiverParameters rpars(finish_receiving,leftport,"Reverse");

    // start left receiver
    if ( rte_eal_remote_launch(receive, &rpars, cpu_left_receiver) )
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
}

// sets the values of the data fields
senderCommonParameters::senderCommonParameters(uint16_t ipv6_frame_size_, uint16_t ipv4_frame_size_, uint32_t frame_rate_, uint16_t duration_, 
                                               uint32_t n_, uint32_t m_, uint64_t hz_, uint64_t start_tsc_) {
  ipv6_frame_size = ipv6_frame_size_;
  ipv4_frame_size = ipv4_frame_size_;
  frame_rate = frame_rate_;
  duration = duration_;
  n = n_;
  m = m_;
  hz = hz_;
  start_tsc = start_tsc_;
}

// sets the values of the data fields
senderParameters::senderParameters(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint8_t eth_id_, const char *side_,
                                   struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                                   struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                                   uint16_t num_dest_nets_) {
  cp = cp_;
  ip_version = ip_version_;
  pkt_pool = pkt_pool_;
  eth_id = eth_id_;
  side = side_;
  dst_mac = dst_mac_;
  src_mac = src_mac_;
  src_ipv4 = src_ipv4_;
  dst_ipv4 = dst_ipv4_;
  src_ipv6 = src_ipv6_;
  dst_ipv6 = dst_ipv6_;
  src_bg = src_bg_;
  dst_bg = dst_bg_;
  num_dest_nets = num_dest_nets_;
}

// sets the values of the data fields
receiverParameters::receiverParameters(uint64_t finish_receiving_, uint8_t eth_id_, const char *side_) {
  finish_receiving=finish_receiving_;
  eth_id = eth_id_;
  side = side_;
}

// collects the apppropriate IP addresses
// for simplicity, both source and destionation address fields exist in both v4 and v6, but only the appropriate version IP addresses are set, 
// and not all input parameters are used
ipQuad::ipQuad(int ip_A_version, int ip_B_version, uint32_t *ipv4_A_real, uint32_t *ipv4_B_real, uint32_t *ipv4_A_virtual,  uint32_t *ipv4_B_virtual,
               struct in6_addr *ipv6_A_real, struct in6_addr *ipv6_B_real, struct in6_addr *ipv6_A_virtual, struct in6_addr *ipv6_B_virtual) {
  if ( ip_A_version == 6 ) {
    // side A sender version is 6
    src_ipv6 = ipv6_A_real;
    if ( ip_B_version == 4 )
      dst_ipv6 = ipv6_B_virtual; // NAT64
    else
      dst_ipv6 = ipv6_B_real;    // pure IPv6, same as background traffic
  }
  else {
    // side A sender version is 4
    src_ipv4 = ipv4_A_real;
    if ( ip_B_version == 4 )
      dst_ipv4 = ipv4_B_real;    // IPv4 traffic
    else
      dst_ipv4 = ipv4_B_virtual; // NAT46
  }
}
