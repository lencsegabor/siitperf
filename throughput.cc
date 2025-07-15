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

#include "defines.h"
#include "includes.h"
#include "throughput.h"

char coresList[101]; // buffer for preparing the list of lcores for DPDK init (like a command line argument)
char numChannels[11]; // buffer for printing the number of memory channels into a string for DPDK init (like a command line argument)

Throughput::Throughput(){
  // initialize some data members to default or invalid values
  ip_left_version = 6;	 	// default value for NAT64 benchmarking
  ip_right_version = 4; 	// default value for NAT64 benchmarking
  forward = 1;			// default value, left to right direction is active
  reverse = 1;			// default value, right to left direction is active 
  promisc = 1;			// default value, promiscuous mode is set
  cpu_left_sender = -1;		// MUST be set in the config file if forward != 0
  cpu_right_receiver = -1; 	// MUST be set in the config file if forward != 0
  cpu_right_sender = -1; 	// MUST be set in the config file if reverse != 0
  cpu_left_receiver = -1; 	// MUST be set in the config file if reverse != 0
  memory_channels = 1; 		// default value, this value will be set, if not specified in the config file
  num_left_nets = 1;		// default value: single destination network
  num_right_nets = 1;		// default value: single destination network
  fwd_var_sport = 0;		// default value: use hard coded fix source port of RFC 2544
  fwd_var_dport = 0;		// default value: use hard coded fix destination port of RFC 2544
  fwd_sport_min = 1024;		// default value: use maximum range recommended by RFC 4814
  fwd_sport_max = 65535;	// default value: use maximum range recommended by RFC 4814
  fwd_dport_min = 1;		// default value: use maximum range recommended by RFC 4814
  fwd_dport_max = 49151;	// default value: use maximum range recommended by RFC 4814
  rev_var_sport = 0;		// default value: use hard coded fix source port of RFC 2544
  rev_var_dport = 0;		// default value: use hard coded fix destination port of RFC 2544
  rev_sport_min = 1024;		// default value: use maximum range recommended by RFC 4814
  rev_sport_max = 65535;	// default value: use maximum range recommended by RFC 4814
  rev_dport_min = 1;		// default value: use maximum range recommended by RFC 4814
  rev_dport_max = 49151;	// default value: use maximum range recommended by RFC 4814
  ip_left_varies = 0;          	// default value: use hard coded fix source IP address of RFC 2544
  ip_right_varies = 0;         	// default value: use hard coded fix source IP address of RFC 2544

  stateful = 0;			// default value: perform stateless test
  enumerate_ports = 0;		// default value: do not enumerate ports
  enumerate_ips = 0;		// default value: do not enumerate IP addresses
  responder_tuples = 0;		// default value: use a single four tuple (like fix port numbers)
  stateTable = 0;  		// to cause segmentation fault if not initialized
  valid_entries = 0;   		// to indicate that state table is empty (used by rsend)
  uniquePortComb = 0;		// to indicate that no memory was allocated
  uniqueIpComb = 0;		// to indicate that no memory was allocated
  uniqueFtComb = 0;		// to indicate that no memory was allocated
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
    } else if ( (pos = findKey(line, "Fwd-var-sport")) >= 0 ) {
      sscanf(line+pos, "%u", &fwd_var_sport);
      if ( fwd_var_sport > 3 ) {
        std::cerr << "Input Error: 'Fwd-var-sport' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Fwd-var-dport")) >= 0 ) {
      sscanf(line+pos, "%u", &fwd_var_dport);
      if ( fwd_var_dport > 3 ) {
        std::cerr << "Input Error: 'Fwd-var-dport' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Rev-var-sport")) >= 0 ) {
      sscanf(line+pos, "%u", &rev_var_sport);
      if ( rev_var_sport > 3 ) {
        std::cerr << "Input Error: 'Rev-var-sport' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Rev-var-dport")) >= 0 ) {
      sscanf(line+pos, "%u", &rev_var_dport);
      if ( rev_var_dport > 3 ) {
        std::cerr << "Input Error: 'Rev-var-dport' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Fwd-sport-min")) >= 0 ) {
      if ( sscanf(line+pos, "%u", &fwd_sport_min) < 1 ) {
        std::cerr << "Input Error: Unable to read 'Fwd-sport-min'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Fwd-sport-max")) >= 0 ) {
      if ( sscanf(line+pos, "%u", &fwd_sport_max) < 1 ) {
        std::cerr << "Input Error: Unable to read 'Fwd-sport-max'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Fwd-dport-min")) >= 0 ) {
      if ( sscanf(line+pos, "%u", &fwd_dport_min) < 1 ) {
        std::cerr << "Input Error: Unable to read 'Fwd-dport-min'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Fwd-dport-max")) >= 0 ) {
      if ( sscanf(line+pos, "%u", &fwd_dport_max) < 1 ) {
        std::cerr << "Input Error: Unable to read 'Fwd-dport-max'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Rev-sport-min")) >= 0 ) {
      if ( sscanf(line+pos, "%u", &rev_sport_min) < 1 ) {
        std::cerr << "Input Error: Unable to read 'Rev-sport-min'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Rev-sport-max")) >= 0 ) {
      if ( sscanf(line+pos, "%u", &rev_sport_max) < 1 ) {
        std::cerr << "Input Error: Unable to read 'Rev-sport-max'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Rev-dport-min")) >= 0 ) {
      if ( sscanf(line+pos, "%u", &rev_dport_min) < 1 ) {
        std::cerr << "Input Error: Unable to read 'Rev-dport-min'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Rev-dport-max")) >= 0 ) {
      if ( sscanf(line+pos, "%u", &rev_dport_max) < 1 ) {
        std::cerr << "Input Error: Unable to read 'Rev-dport-max'." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IP-L-var")) >= 0 ) {
      sscanf(line+pos, "%u", &ip_left_varies);
      if ( ip_left_varies > 3 ) {
        std::cerr << "Input Error: 'IP-L-var' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IP-R-var")) >= 0 ) {
      sscanf(line+pos, "%u", &ip_right_varies);
      if ( ip_right_varies > 3 ) {
        std::cerr << "Input Error: 'IP-R-var' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IP-L-min")) >= 0 ) {
      if ( sscanf(line+pos, "%i", &ip_left_min) < 1 ) {	// read decimal or hexa (in 0x... format)
        std::cerr << "Input Error: Unable to read 'IP-L-min' value." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IP-L-max")) >= 0 ) {
      if ( sscanf(line+pos, "%i", &ip_left_max) < 1 ) { // read decimal or hexa (in 0x... format)
        std::cerr << "Input Error: Unable to read 'IP-L-max' value." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IP-R-min")) >= 0 ) {
      if ( sscanf(line+pos, "%i", &ip_right_min) < 1 ) { // read decimal or hexa (in 0x... format)
        std::cerr << "Input Error: Unable to read 'IP-R-min' value." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IP-R-max")) >= 0 ) {
      if ( sscanf(line+pos, "%i", &ip_right_max) < 1 ) { // read decimal or hexa (in 0x... format)
        std::cerr << "Input Error: Unable to read 'IP-R-max' value." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv4-L-offset")) >= 0 ) {
      sscanf(line+pos, "%u", &ipv4_left_offset);
      if ( ipv4_left_offset < 1 || ipv4_left_offset > 2 ) {
        std::cerr << "Input Error: 'IPv4-L-offset' must be 1 or 2." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv6-L-offset")) >= 0 ) {
      sscanf(line+pos, "%u", &ipv6_left_offset);
      if ( ipv6_left_offset < 6 || ipv6_left_offset > 14 ) {
        std::cerr << "Input Error: 'IPv6-L-offset' must be in the [6, 14] interval." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv4-R-offset")) >= 0 ) {
      sscanf(line+pos, "%u", &ipv4_right_offset);
      if ( ipv4_right_offset < 1 || ipv4_right_offset > 2 ) {
        std::cerr << "Input Error: 'IPv4-R-offset' must be 1 or 2." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "IPv6-R-offset")) >= 0 ) {
      sscanf(line+pos, "%u", &ipv6_right_offset);
      if ( ipv6_right_offset < 6 || ipv6_right_offset > 14 ) {
        std::cerr << "Input Error: 'IPv6-R-offset' must be in the [6, 14] interval." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Stateful")) >= 0 ) {
      sscanf(line+pos, "%u", &stateful);
      if ( stateful > 2 ) {
        std::cerr << "Input Error: 'Stateful' must be 0, 1, or 2." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Responder-tuples")) >= 0 ) {
      sscanf(line+pos, "%u", &responder_tuples);
      if ( responder_tuples > 3 ) {
        std::cerr << "Input Error: 'Responder-tuples' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Enumerate-ports")) >= 0 ) {
      sscanf(line+pos, "%u", &enumerate_ports);
      if ( enumerate_ports > 3 ) {
        std::cerr << "Input Error: 'Enumerate-ports' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    } else if ( (pos = findKey(line, "Enumerate-ips")) >= 0 ) {
      sscanf(line+pos, "%u", &enumerate_ips);
      if ( enumerate_ips > 3 ) {
        std::cerr << "Input Error: 'Enumerate-ips' must be 0, 1, 2, or 3." << std::endl;
        return -1;
      }
    } else if ( nonComment(line) ) { // It may be too strict!
        std::cerr << "Input Error: Cannot interpret '" << filename << "' line " << line_no << ":" << std::endl;
        std::cerr << line << std::endl;
        return -1;
    } 
  }
  fclose(f);
  if ( !stateful ) {
    // check if at least one direction is active (compulsory for stateless tests)
    if ( forward == 0 && reverse == 0 ) {
      std::cerr << "Input Error: No active direction was specified." << std::endl;
      return -1;
    }
  } 
  
  // check if the necessary lcores were specified
  if ( stateful==1 || forward ) {
    if ( cpu_left_sender < 0 ) {
      std::cerr << "Input Error: No 'CPU-L-Send' was specified." << std::endl;
      return -1;
    }
    if ( cpu_right_receiver < 0 ) {
      std::cerr << "Input Error: No 'CPU-R-Recv' was specified." << std::endl;
      return -1;
    }
  }
  if ( stateful==2 || reverse ) {
    if ( cpu_right_sender < 0 ) {
      std::cerr << "Input Error: No 'CPU-R-Send' was specified." << std::endl;
      return -1;
    }
    if ( cpu_left_receiver < 0 ) {
      std::cerr << "Input Error: No 'CPU-L-Recv' was specified." << std::endl;
      return -1;
    }
  }
  // calculate the derived values, if any port numbers or IP addresses have to be changed
  fwd_varport = fwd_var_sport || fwd_var_dport;
  rev_varport = rev_var_sport || rev_var_dport;
  ip_varies = ip_left_varies || ip_right_varies;

  // sanity checks regarding IP address and port number eumeration
  switch ( stateful ) {
    case 0: // stateless tests
      if ( enumerate_ports ) {
        std::cerr << "Input Error: Port number enumeration is available with stateful tests only." << std::endl;
        return -1;
      }
      if ( enumerate_ips ) {
        std::cerr << "Input Error: IP address enumeration is available with stateful tests only." << std::endl;
        return -1;
      }
      break;
    case 1: // Initiator is on the left side
      if ( enumerate_ports && num_right_nets > 1 ) {
        std::cerr << "Input Error: Port enumeration is available with a single destination network only." << std::endl;
        return -1;
      }
      break;
    case 2: // Initiator is on the right side
      if ( enumerate_ports && num_left_nets > 1 ) {
        std::cerr << "Input Error: Port enumeration is available with a single destination network only." << std::endl;
        return -1;
      }
      break;
  }
  // sanity checks regarding multiple IP addresses and multiple destination networks
  if ( ip_varies && ( num_left_nets > 1 || num_right_nets > 1 ) ) {
    std::cerr << "Input Error: Usage of multiple IP address is available with a single destination network only." << std::endl;
    return -1;
  }

  // checking the constraints for "Enumerate-ips" and "Enumerate-ports"
  if ( stateful && enumerate_ips && enumerate_ports && enumerate_ips != enumerate_ports ) {
    std::cerr << "Input Error: In stateful tests, if both 'Enumerate-ips' and 'Enumerate-ports' are non-zero then they MUST be equal." << std::endl; 
    return -1;
  }

  // forcing the restriction that stateful tests with port number enumeration and multiple IP addresses MUST use IP address enumeration, too.
  if ( stateful && enumerate_ports && ip_varies && !enumerate_ips ) {
    std::cerr << "Input Error: In stateful tests, if port number enumeration and multiple IP addresses are used then IP address enumeration MUST be used, too." << std::endl;
    return -1;
  }

  // forcing the restriction that stateful tests with IP address enumeration and multiple port numbers MUST use port number enumeration, too.
  if ( stateful && enumerate_ips && (fwd_varport||rev_varport) && !enumerate_ports ) {
    std::cerr << "Input Error: In stateful tests, if IP addresses enumeration and multiple port numbers are used then port number enumeration MUST be used." << std::endl; 
    return -1;
  }

  // forcing the restriction that with stateful tests, if Enumerate-ips is non-zero then IP-L-var and IP-R-var also must be non-zero.
  if ( stateful && enumerate_ips && ( !ip_left_varies || !ip_right_varies ) ) {
    std::cerr << "Input Error: In stateful tests, Enumerate-ips is non-zero then IP-L-var and IP-R-var also must be non-zero."  << std::endl;
    return -1;
  }

  // perform masking of the proper 16 bits of the IPv4 / IPv6 addresses
  if ( ip_left_varies ) {
    uint32_t ipv4mask = htonl(0xffffffff & ~(0xffffu<<((2-ipv4_left_offset)*8)));
    ipv4_left_real &= ipv4mask;
    ipv4_left_virtual &= ipv4mask;
    ipv6_left_real.s6_addr[ipv6_left_offset]=0;
    ipv6_left_real.s6_addr[ipv6_left_offset+1]=0;
    ipv6_left_virtual.s6_addr[ipv6_left_offset]=0;
    ipv6_left_virtual.s6_addr[ipv6_left_offset+1]=0;
  }
  if ( ip_right_varies ) {
    uint32_t ipv4mask = htonl(0xffffffff & ~(0xffffu<<((2-ipv4_left_offset)*8)));
    ipv4_right_real &= ipv4mask;
    ipv4_right_virtual &= ipv4mask;
    ipv6_right_real.s6_addr[ipv6_right_offset]=0;
    ipv6_right_real.s6_addr[ipv6_right_offset+1]=0;
    ipv6_right_virtual.s6_addr[ipv6_right_offset]=0;
    ipv6_right_virtual.s6_addr[ipv6_right_offset+1]=0;
  }
  return 0;
}

// reads the command line arguments and stores the information in data members of class Throughput
// It may be called only AFTER the execution of readConfigFile
int Throughput::readCmdLine(int argc, const char *argv[]) {
  if ( argc < 7 || stateful && argc < 12 ) {
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
  if ( stateful ) {
    if ( sscanf(argv[7], "%u", &pre_frames) != 1 || pre_frames < 1 ) {
      std::cerr << "Input Error: 'N' (the number of preliminary frames) must be between 1 and 2^32-1." << std::endl;
      return -1;
    }
    if ( sscanf(argv[8], "%u", &state_table_size) != 1 || state_table_size < 1 ) {
      std::cerr << "Input Error: 'M' (the size of the state table of the Responder) must be between 1 and 2^32-1." << std::endl;
      return -1;
    }
    if ( sscanf(argv[9], "%u", &pre_rate) != 1 || frame_rate < 1 || frame_rate > 14880952 ) {
      std::cerr << "Input Error: Preliminary frame rate 'R' must be between 1 and 14880952." << std::endl;
      return -1;
    }
    if ( sscanf(argv[10], "%u", &pre_timeout) != 1 || pre_timeout < 1 || pre_timeout > 2000 ) {
      std::cerr << "Input Error: the value of 'T' (global timeout for the preliminary frames) must be between 1 and 2000." << std::endl;
      return -1;
    }
    if ( sscanf(argv[11], "%u", &pre_delay) != 1 || pre_delay < 1 || pre_delay > 100000000 ) { 
      std::cerr << "Input Error: the value of 'D' (delay caused by the preliminary phase) must be between 1 and 100000000." << std::endl;
      return -1;
    }
    if ( ((uint64_t)1000)*pre_frames/pre_rate+pre_timeout > pre_delay ) {
      std::cerr << "Input Error: 1000*N/R+T > D (preliminary test may not be performed in the available time period)." << std::endl;
      return -1;
    }
    else
      std::cout << "Info: 1000*N/R+T: " << ((uint64_t)1000)*pre_frames/pre_rate+pre_timeout << " <= D: " << pre_delay << std::endl;
    // calculate the effective number of the preliminary frames as: all preliminary frames - backgroud frames
    eff_pre_frames = pre_frames-pre_frames*(n-m)/n; // Note: pre_frames*m/n is NOT exactly correct!
    if ( eff_pre_frames < state_table_size ) {
      std::cerr << "Input Error: N-N*(n-m)/n < M (there are not enough foreground frames to fill the state table)." << std::endl;
      return -1;
    }
    if ( enumerate_ports == 3 && enumerate_ips != 3 ) {
      // unique port number combinations are required for each foreground preliminary frame
      // check if there are enough of them
      uint64_t portNumberCombinations;	// theoretically may be equal with 2**32, thus uint32_t is not enough
      if ( stateful == 1 )
	portNumberCombinations = (fwd_sport_max-fwd_sport_min+1)*(fwd_dport_max-fwd_dport_min+1);
      else // sateful is 2
	portNumberCombinations = (rev_sport_max-rev_sport_min+1)*(rev_dport_max-rev_dport_min+1);
      std::cout << "Info: number of unique port number combinations: " <<  portNumberCombinations << std::endl;
      std::cout << "Info: number of foreground preliminary frames: " << eff_pre_frames << std::endl;
      if ( portNumberCombinations < eff_pre_frames ) {
        std::cerr << "Input Error: There are not enough unique port number combinations for each (foregound) preliminary frames." << std::endl;
        return -1;
      }
    }
    if ( enumerate_ips == 3 && enumerate_ports != 3 ) {
      // unique IP address combinations are required for each foreground preliminary frame
      // check if there are enough of them
      uint64_t ipAddressCombinations;  // theoretically may be equal with 2**32, thus uint32_t is not enough
        ipAddressCombinations = (ip_left_max-ip_left_min+1)*(ip_right_max-ip_right_min+1);
      std::cout << "Info: number of unique IP address combinations: " <<  ipAddressCombinations << std::endl;
      std::cout << "Info: number of foreground preliminary frames: " << eff_pre_frames << std::endl;
      if ( ipAddressCombinations < eff_pre_frames ) {
        std::cerr << "Input Error: There are not enough unique IP address combinations for each (foregound) preliminary frames." << std::endl;
        return -1;
      }
    }

    if ( enumerate_ips == 3 && enumerate_ports == 3 ) {
      // unique IP address and port number combinations are required for each foreground preliminary frame
      // check if there are enough of them
      uint64_t c;  
      if ( stateful == 1 )
        c = (ip_left_max-ip_left_min+1)*(ip_right_max-ip_right_min+1)*(fwd_sport_max-fwd_sport_min+1)*(fwd_dport_max-fwd_dport_min+1);
      else // sateful is 2
        c = (ip_left_max-ip_left_min+1)*(ip_right_max-ip_right_min+1)*(rev_sport_max-rev_sport_min+1)*(rev_dport_max-rev_dport_min+1);
      std::cout << "Info: number of unique IP address and port number combinations: " <<  c << std::endl;
      std::cout << "Info: number of foreground preliminary frames: " << eff_pre_frames << std::endl;
      if ( c < eff_pre_frames ) {
        std::cerr << "Input Error: There are not enough unique IP address combinations for each (foregound) preliminary frames." << std::endl;
        return -1;
      }
    }

    if ( responder_tuples && state_table_size==1 ) {
      std::cerr << "Input Error: 'Responder-tuples' MUST be set to 0, if the size of the state table (M) is 1." << std::endl;
      return -1;
    }

  std::cout << "Info: Stateful test cmdline parameteres: N: " << pre_frames << ", M: " << state_table_size << 
               ", R: " << pre_rate << ", T: " << pre_timeout << ", D: " << pre_delay << std::endl;
  }
  return 0;
}

// Initializes DPDK EAL, starts network ports, creates and sets up TX/RX queues, checks NUMA localty and TSC synchronization of lcores
// sets the values of some variables (Throughput data members)
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
  if ( forward && reverse || stateful==1 && reverse || stateful==2 && forward ) {
    // both directions are active 
    snprintf(coresList, 101, "0,%d,%d,%d,%d", cpu_left_sender, cpu_right_receiver, cpu_right_sender, cpu_left_receiver);
  } else if ( forward || stateful==1 )
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
  cfg_port.txmode.mq_mode = RTE_ETH_MQ_TX_NONE;	// no multi queues 
  cfg_port.rxmode.mq_mode = RTE_ETH_MQ_RX_NONE;	// no multi queues 

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
  int effective_right_nets = (stateful==1 ? num_right_nets : 0) + (forward ? num_right_nets : 0); // preliminary traffic of stateful test + normal traffic
  int effective_forward_varport = fwd_varport || stateful==1 && enumerate_ports || stateful==2 && responder_tuples;
  int left_sender_pool_size = senderPoolSize( effective_right_nets, effective_forward_varport, ip_varies );
  int effective_left_nets = (stateful==2 ? num_left_nets : 0) + (reverse ? num_left_nets : 0); // preliminary traffic of stateful test + normal traffic
  int effective_reverse_varport = rev_varport || stateful==2 && enumerate_ports || stateful==1 && responder_tuples;
  int right_sender_pool_size = senderPoolSize( effective_left_nets, effective_reverse_varport, ip_varies );

  int receiver_pool_size = PORT_RX_QUEUE_SIZE + 2 * MAX_PKT_BURST + 100; // While one of them is processed, the other one is being filled. 

  pkt_pool_left_sender = rte_pktmbuf_pool_create ( "pp_left_sender", left_sender_pool_size, PKTPOOL_CACHE, 0, 
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

  pkt_pool_right_sender = rte_pktmbuf_pool_create ( "pp_right_sender", right_sender_pool_size, PKTPOOL_CACHE, 0,
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
  } while ( link_info.link_status == RTE_ETH_LINK_DOWN );
  trials=0;
  do {
    if ( trials++ == MAX_PORT_TRIALS ) {
      std::cerr << "Error: Right Ethernet port is DOWN, Tester exits." << std::endl;
      return -1;
    }
  rte_eth_link_get(rightport, &link_info);
  } while ( link_info.link_status == RTE_ETH_LINK_DOWN );

  // Some sanity checks: NUMA node of the cores and of the NICs are matching or not...
  if ( numa_available() == -1 )
    std::cout << "Info: This computer does not support NUMA." << std::endl;
  else {
    if ( numa_num_configured_nodes() == 1 )
      std::cout << "Info: Only a single NUMA node is configured, there is no possibilty for mismatch." << std::endl;
    else {
      if ( stateful==1 ) {
        numaCheck(leftport, "Left", cpu_left_sender, "Initiator/Sender");
        numaCheck(rightport, "Right", cpu_right_receiver, "Responder/Receiver");
      }
      if ( stateful==2 ) {
        numaCheck(rightport, "Right", cpu_right_sender, "Initiator/Sender");
        numaCheck(leftport, "Left", cpu_left_receiver, "Responder/Receiver");
      }
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
  if ( stateful==1 ) {
    check_tsc(cpu_left_sender, "Initiator/Sender");
    check_tsc(cpu_right_receiver, "Responder/Receiver");
  }
  if ( stateful==2 ) {
    check_tsc(cpu_right_sender, "Initiator/Sender");
    check_tsc(cpu_left_receiver, "Responder/Receiver");
  }
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

  if ( stateful && enumerate_ports == 3 && enumerate_ips != 3 ) {
    // Pre-generation of unique source and destination port numbers is required

    // collect port numbers and lcore info
    uint16_t src_min, src_max, dst_min, dst_max;	// port number ranges
    int cpu_isend;			// lcore for Initiator/Sender

    if ( stateful == 1 ) {
      src_min = fwd_sport_min;
      src_max = fwd_sport_max;
      dst_min = fwd_dport_min;
      dst_max = fwd_dport_max;
      cpu_isend = cpu_left_sender;
    } else { // sateful is 2
      src_min = rev_sport_min;
      src_max = rev_sport_max;
      dst_min = rev_dport_min;
      dst_max = rev_dport_max;
      cpu_isend = cpu_right_sender;
    }

    // prepare the parameters for the randomPermutationGenerator32
    randomPermutationGeneratorParameters32 pars;
    pars.addr_of_arraypointer = &uniquePortComb;
    pars.src_min = src_min;
    pars.src_max = src_max;
    pars.dst_min = dst_min;
    pars.dst_max = dst_max;
    pars.type = (char *) "port number";
    pars.hz = hz;

    // start randomPermutationGenerator32
    if ( rte_eal_remote_launch(randomPermutationGenerator32, &pars, cpu_isend) )
      std::cout << "Error: could not start randomPermutationGenerator32 for pre-generating unique port number combinations." << std::endl;
    rte_eal_wait_lcore(cpu_isend);
  }

  if ( stateful && enumerate_ips == 3 && enumerate_ports != 3 ) {
    // Pre-generation of unique source and destination IP addresses is required
    // Note: not full IP addresses, but only their critical 16-bit parts are generated

    // collect IP addresses and lcore info
    uint16_t src_min, src_max, dst_min, dst_max;	// IP address (part) ranges
    int cpu_isend;			// lcore for Initiator/Sender
    if ( stateful == 1 ) {
      src_min = ip_left_min;
      src_max = ip_left_max;
      dst_min = ip_right_min;
      dst_max = ip_right_max;
      cpu_isend = cpu_left_sender;
    } else { // sateful is 2
      src_min = ip_right_min;
      src_max = ip_right_max;
      dst_min = ip_left_min;
      dst_max = ip_left_max;
      cpu_isend = cpu_right_sender;
    }

    // prepare the parameters for the randomPermutationGenerator32
    randomPermutationGeneratorParameters32 pars;
    pars.addr_of_arraypointer = &uniqueIpComb;
    pars.src_min = src_min;
    pars.src_max = src_max;
    pars.dst_min = dst_min;
    pars.dst_max = dst_max;
    pars.type = (char *) "IP address";
    pars.hz = hz;

    // start randomPermutationGenerator32
    if ( rte_eal_remote_launch(randomPermutationGenerator32, &pars, cpu_isend) )
      std::cout << "Error: could not start randomPermutationGenerator32 for pre-generating unique IP address combinations." << std::endl;
    rte_eal_wait_lcore(cpu_isend);
  }

  if ( stateful && enumerate_ips == 3 && enumerate_ports == 3 ) {
    // Pre-generation of unique source and destination IP addresses and port numbers is required
    // Note: not full IP addresses, but only their critical 16-bit parts are generated

    // collect IP addresses, port numbers and lcore info
    uint16_t si_min, si_max, di_min, di_max;      // ranges for IP address parts
    uint16_t sp_min, sp_max, dp_min, dp_max;      // ranges for port numbers
    uint16_t src_min, src_max, dst_min, dst_max;	// IP address (part) ranges
    int cpu_isend;			// lcore for Initiator/Sender
    if ( stateful == 1 ) {
      si_min = ip_left_min;
      si_max = ip_left_max;
      di_min = ip_right_min;
      di_max = ip_right_max;
      sp_min = fwd_sport_min;
      sp_max = fwd_sport_max;
      dp_min = fwd_dport_min;
      dp_max = fwd_dport_max;
      cpu_isend = cpu_left_sender;
    } else { // sateful is 2
      si_min = ip_right_min;
      si_max = ip_right_max;
      di_min = ip_left_min;
      di_max = ip_left_max;
      sp_min = rev_sport_min;
      sp_max = rev_sport_max;
      dp_min = rev_dport_min;
      dp_max = rev_dport_max;
      cpu_isend = cpu_right_sender;
    }

    // prepare the parameters for the randomPermutationGenerator64
    randomPermutationGeneratorParameters64 pars;
    pars.addr_of_arraypointer = &uniqueFtComb;
    pars.si_min = si_min;
    pars.si_max = si_max;
    pars.di_min = di_min;
    pars.di_max = di_max;
    pars.sp_min = sp_min;
    pars.sp_max = sp_max;
    pars.dp_min = dp_min;
    pars.dp_max = dp_max;
    pars.hz = hz;

    // start randomPermutationGenerator64
    if ( rte_eal_remote_launch(randomPermutationGenerator64, &pars, cpu_isend) )
      std::cout << "Error: could not start randomPermutationGenerator64 for pre-generating unique IP address and port number combinations." << std::endl;
    rte_eal_wait_lcore(cpu_isend);
  }

  if ( !stateful) {
    // for stateless tests:
    start_tsc = rte_rdtsc()+hz*START_DELAY/1000;	// Each active sender starts sending at this time
    finish_receiving = start_tsc + hz*duration + hz*global_timeout/1000; // Each receiver stops at this time
  }
  else 
  {
    // for stateful tests:
    // the sender of the Initiator starts sending preliminary frames at this time:
    start_tsc_pre = rte_rdtsc() + hz*START_DELAY/1000; 
    // the receiver of the Responder stops receiving preliminary frames at this time:
    finish_receiving_pre = start_tsc_pre + hz*pre_frames/pre_rate + hz*pre_timeout/1000; 
    // production test starts at this time:
    start_tsc = start_tsc_pre + hz*pre_delay/1000;
    // productions test receivers stop at this time:
    finish_receiving = start_tsc + hz*duration + hz*global_timeout/1000; 
  }
  return 0;
}

// calculates sender pool size, it is a virtual member function, redefined in derived classes
int Throughput::senderPoolSize(int num_dest_nets, int varport) {
  return 2*num_dest_nets*(varport ? N : 1) + PORT_TX_QUEUE_SIZE + 100; // 2*: fg. and bg. Test Frames
  // if varport then everything exists in N copies, see the definition of N
}

// calculates sender pool size, it is a virtual member function, will be redefined in derived classes
int Throughput::senderPoolSize(int num_dest_nets, int varport, int ip_varies) {
  return 2*num_dest_nets*(varport||ip_varies ? N : 1) + PORT_TX_QUEUE_SIZE + 100; // 2*: fg. and bg. Test Frames
  // if varport then everything exists in N copies, see the definition of N
  // note: if ip_varies then num_dest_nets is surely 1 (enforced by input checking)
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

// Several functions follow to create IPv4 or IPv6 Test Frames
// Please refer to RFC 2544 Appendix C.2.6.4 "Test Frames" for the values to be set in the test frames.

// creates an IPv4 Test Frame using several helper functions
// BEHAVIOR: it sets exatly, what it is told to set :-)
struct rte_mbuf *mkFinalTestFrame4(uint16_t length, rte_mempool *pkt_pool, const char *side,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const uint32_t *src_ip, const uint32_t *dst_ip, unsigned sport, unsigned dport) {
  struct rte_mbuf *pkt_mbuf=rte_pktmbuf_alloc(pkt_pool); // message buffer for the Test Frame
  if ( !pkt_mbuf )
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the IPv4 Test Frame! \n", side);
  length -=  RTE_ETHER_CRC_LEN; // exclude CRC from the frame length
  pkt_mbuf->pkt_len = pkt_mbuf->data_len = length; // set the length in both places
  uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbuf, uint8_t *); // Access the Test Frame in the message buffer
  rte_ether_hdr *eth_hdr = reinterpret_cast<struct rte_ether_hdr *>(pkt); // Ethernet header
  rte_ipv4_hdr *ip_hdr = reinterpret_cast<rte_ipv4_hdr *>(pkt+sizeof(rte_ether_hdr)); // IPv4 header
  rte_udp_hdr *udp_hd = reinterpret_cast<rte_udp_hdr *>(pkt+sizeof(rte_ether_hdr)+sizeof(rte_ipv4_hdr)); // UDP header
  uint8_t *udp_data = reinterpret_cast<uint8_t*>(pkt+sizeof(rte_ether_hdr)+sizeof(rte_ipv4_hdr)+sizeof(rte_udp_hdr)); // UDP data

  mkEthHeader(eth_hdr, dst_mac, src_mac, 0x0800); 	// contains an IPv4 packet
  int ip_length = length - sizeof(rte_ether_hdr);
  mkIpv4Header(ip_hdr, ip_length, src_ip, dst_ip); 	// Does not set IPv4 header checksum
  int udp_length = ip_length - sizeof(rte_ipv4_hdr); 	// No IP Options are used
  mkUdpHeader(udp_hd, udp_length, sport, dport);			
  int data_legth = udp_length - sizeof(rte_udp_hdr);
  mkData(udp_data, data_legth);
  udp_hd->dgram_cksum = rte_ipv4_udptcp_cksum( ip_hdr, udp_hd ); // UDP checksum is calculated and set
  ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);	// IPv4 header checksum is set now
  return pkt_mbuf;
}

// creates an IPv4 Test Frame using mkFinalTestFrame4
// BEHAVIOR: if port number is 0, it is set according to RFC 2544 Test Frame format, otherwise it is set to 0, to be set later.
struct rte_mbuf *mkTestFrame4(uint16_t length, rte_mempool *pkt_pool, const char *side,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const uint32_t *src_ip, const uint32_t *dst_ip, unsigned var_sport, unsigned var_dport) {
  // sport/dport are set to 0, if they will change, otherwise follow RFC 2544 Test Frame format
  struct rte_mbuf *pkt_mbuf=mkFinalTestFrame4(length,pkt_pool,side,dst_mac,src_mac,src_ip,dst_ip,var_sport ? 0 : 0xC020,var_dport ? 0 : 0x0007);
  // The above function terminated the Tester if it could not allocate memory, thus no error handling is needed here. :-)
  return pkt_mbuf;
}


// creates an Ethernet header
void mkEthHeader(struct rte_ether_hdr *eth, const struct ether_addr *dst_mac, const struct ether_addr *src_mac, const uint16_t ether_type) {
  rte_memcpy(&eth->dst_addr, dst_mac, sizeof(struct rte_ether_hdr));
  rte_memcpy(&eth->src_addr, src_mac, sizeof(struct rte_ether_hdr));
  eth->ether_type = htons(ether_type);
}

// creates an IPv4 header
void mkIpv4Header(struct rte_ipv4_hdr *ip, uint16_t length, const uint32_t *src_ip, const uint32_t *dst_ip) {
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

// creates an UDP header
void mkUdpHeader(struct rte_udp_hdr *udp, uint16_t length, unsigned sport, unsigned dport) {
  udp->src_port =  htons(sport);
  udp->dst_port =  htons(dport);
  udp->dgram_len = htons(length);
  udp->dgram_cksum = 0; // UDP checksum is set to 0 now, it will be calculated later.
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
// BEHAVIOR: it sets exatly, what it is told to set :-)
struct rte_mbuf *mkFinalTestFrame6(uint16_t length, rte_mempool *pkt_pool, const char *side,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const struct in6_addr *src_ip, const struct in6_addr *dst_ip, unsigned sport, unsigned dport) {
  struct rte_mbuf *pkt_mbuf=rte_pktmbuf_alloc(pkt_pool); // message buffer for the Test Frame
  if ( !pkt_mbuf )
    rte_exit(EXIT_FAILURE, "Error: %s sender can't allocate a new mbuf for the IPv6 Test Frame! \n", side);
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
  mkData(udp_data, data_legth);
  udp_hd->dgram_cksum = rte_ipv6_udptcp_cksum( ip_hdr, udp_hd ); // UDP checksum is calculated and set
  return pkt_mbuf;
}

// creates an IPv6 Test Frame using mkFinalTestFrame6
// BEHAVIOR: if port number is 0, it is set according to RFC 2544 Test Frame format, otherwise it is set to 0, to be set later.
struct rte_mbuf *mkTestFrame6(uint16_t length, rte_mempool *pkt_pool, const char *side,
                              const struct ether_addr *dst_mac, const struct ether_addr *src_mac,
                              const struct in6_addr *src_ip, const struct in6_addr *dst_ip, unsigned var_sport, unsigned var_dport) {
  // sport/dport are set to 0, if they will change, otherwise follow RFC 2544 Test Frame format
  struct rte_mbuf *pkt_mbuf=mkFinalTestFrame6(length,pkt_pool,side,dst_mac,src_mac,src_ip,dst_ip,var_sport ? 0 : 0xC020,var_dport ? 0 : 0x0007);
  // The above function terminated the Tester if it could not allocate memory, thus no error handling is needed here. :-)
  return pkt_mbuf;
}


// creates an IPv6 header
void mkIpv6Header(struct rte_ipv6_hdr *ip, uint16_t length, const struct in6_addr *src_ip, const struct in6_addr *dst_ip) {
  ip->vtc_flow = htonl(0x60000000); // Version: 6, Traffic class: 0, Flow label: 0
  ip->payload_len = htons(length-sizeof(rte_ipv6_hdr)); 
  ip->proto = 0x11; // UDP
  ip->hop_limits = 0x0A; 
  rte_mov16((uint8_t *)&ip->src_addr,(uint8_t *)src_ip);
  rte_mov16((uint8_t *)&ip->dst_addr,(uint8_t *)dst_ip);
}

// sends Test Frames for throughput (or frame loss rate) measurement
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

  // further local variables
  uint64_t frames_to_send = duration * frame_rate;	// Each active sender sends this number of frames
  uint64_t sent_frames=0; // counts the number of sent frames
  double elapsed_seconds; // for checking the elapsed seconds during sending

  bool fg_frame, ipv4_frame; // when sending IPv4 traffic, background frames are IPv6: their UDP checksum may be 0.

  if ( !varport ) {
    // optimized code for using hard coded fix port numbers as defined in RFC 2544 https://tools.ietf.org/html/rfc2544#appendix-C.2.6.4
    if ( num_dest_nets == 1 ) { 	
      // optimized code for single destination network: always the same foreground or background frame is sent, no arrays are used
      struct rte_mbuf *fg_pkt_mbuf, *bg_pkt_mbuf; // message buffers for fg. and bg. Test Frames
      // create foreground Test Frame
      if ( ip_version == 4 )
        fg_pkt_mbuf = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, dst_ipv4, 0, 0);
      else  // IPv6
        fg_pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, 0, 0);
  
      // create backround Test Frame (always IPv6)
      bg_pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg, 0, 0);
  
      // naive sender version: it is simple and fast
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); // Beware: an "empty" loop, and further two will come!
        if ( sent_frames % n  < m )
          while ( !rte_eth_tx_burst(eth_id, 0, &fg_pkt_mbuf, 1) ); // send foreground frame
        else
           while ( !rte_eth_tx_burst(eth_id, 0, &bg_pkt_mbuf, 1) ); // send background frame
      } // this is the end of the sending cycle
    } // end of optimized code for single destination network
    else {
      // optimized code for multiple destination networks: foreground and background frames are generated for each network and pointers are stored in arrays
      // assertion: num_dest_nets <= 256 
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
    } // end of optimized code for multiple destination networks
  } // end of optimized code for fixed port numbers
  else {
    // implementation of varying port numbers recommended by RFC 4814 https://tools.ietf.org/html/rfc4814#section-4.5
    // RFC 4814 requires pseudorandom port numbers, increasing and decreasing ones are our additional, non-stantard solutions
    if ( num_dest_nets == 1 ) {
      // optimized code for single destination network: always one of the same N pre-prepared foreground or background frames is updated and sent, 
      // source and/or destination port number(s) and UDP checksum are updated
      // N size arrays are used to resolve the write after send problem
      int i; // cycle variable for the above mentioned purpose: takes {0..N-1} values
      struct rte_mbuf *fg_pkt_mbuf[N], *bg_pkt_mbuf[N], *pkt_mbuf; // pointers of message buffers for fg. and bg. Test Frames
      uint8_t *pkt; // working pointer to the current frame (in the message buffer)
      uint8_t *fg_udp_sport[N], *fg_udp_dport[N], *fg_udp_chksum[N], *bg_udp_sport[N], *bg_udp_dport[N], *bg_udp_chksum[N]; // pointers to the given fields
      uint16_t *udp_sport, *udp_dport, *udp_chksum; // working pointers to the given fields
      uint16_t fg_udp_chksum_start, bg_udp_chksum_start;  // starting values (uncomplemented checksums taken from the original frames)
      uint32_t chksum; // temporary variable for checksum calculation
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
      std::uniform_int_distribution<int> uni_dis_sport(sport_min, sport_max);	// uniform distribution in [sport_min, sport_max]
      std::uniform_int_distribution<int> uni_dis_dport(dport_min, dport_max);	// uniform distribution in [sport_min, sport_max]

      // naive sender version: it is simple and fast
      i=0; // increase maunally after each sending
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        // set the temporary variables (including several pointers) to handle the right pre-generated Test Frame
        if ( fg_frame = sent_frames % n < m ) {
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
        ipv4_frame = ip_version == 4 && fg_frame; // precalculated to have it ready when needed

        // from here, we need to handle the frame identified by the temprary variables
        if ( var_sport ) {
          // sport is varying
          switch ( var_sport ) {
            case 1:                   // increasing port numbers
              if ( (sp=sport++) == sport_max )
                sport = sport_min;
              break;
            case 2:                   // decreasing port numbers
              if ( (sp=sport--) == sport_min )
                sport = sport_max;
              break;
            case 3:                   // pseudorandom port numbers
              sp = uni_dis_sport(gen_sport);
          }
          chksum += *udp_sport = htons(sp);     // set source port and add to checksum -- corrected
        }
        if ( var_dport ) {
          // dport is varying
          switch ( var_dport ) {
            case 1:                           // increasing port numbers
              if ( (dp=dport++) == dport_max )
                dport = dport_min;
              break;
            case 2:                           // decreasing port numbers
              if ( (dp=dport--) == dport_min )
                dport = dport_max;
              break;
            case 3:                           // pseudorandom port numbers
              dp = uni_dis_dport(gen_dport);
          }
          chksum += *udp_dport = htons(dp);     // set destination port add to checksum -- corrected
        }
        chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   	// calculate 16-bit one's complement sum
        chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   	// twice is enough: 2*0xffff=0x1fffe, 0x1+x0fffe=0xffff
        chksum = (~chksum) & 0xffff;                                  	// make one's complement
        if ( ipv4_frame && chksum == 0 )        // over IPv4, checksum should not be 0 (0 means, no checksum is used)
          chksum = 0xffff;
        *udp_chksum = (uint16_t) chksum;            // set checksum in the frame
        // finally, when its time is here, send the frame
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); 	// Beware: an "empty" loop, as well as in the next line
        while ( !rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1) ); 		// send out the frame
        i = (i+1) % N;
      } // this is the end of the sending cycle
    } // end of optimized code for single destination network
    else { 
      // optimized code for multiple destination networks: N copies of foreground or background frames are prepared for each destination network,
      // N size arrays are used to resolve the write after send problem
      // source and/or destination port number(s) and UDP checksum are updated in the actually used copy before sending
      // assertion: num_dest_nets <= 256
      int j; // cycle variable to index the N size array: takes {0..N-1} values
      struct rte_mbuf *fg_pkt_mbuf[256][N], *bg_pkt_mbuf[256][N], *pkt_mbuf; // pointers of message buffers for fg. and bg. Test Frames
      uint8_t *pkt; // working pointer to the current frame (in the message buffer)
      uint8_t *fg_udp_sport[256][N], *fg_udp_dport[256][N], *fg_udp_chksum[256][N]; // pointers to the given fields of the pre-prepared Test Frames
      uint8_t *bg_udp_sport[256][N], *bg_udp_dport[256][N], *bg_udp_chksum[256][N]; // pointers to the given fields of the pre-prepared Test Frames
      uint16_t *udp_sport, *udp_dport, *udp_chksum; // working pointers to the given fields
      uint16_t fg_udp_chksum_start[256], bg_udp_chksum_start[256];  // starting values (uncomplemented checksums taken from the original frames)
      uint32_t chksum; // temporary variable for checksum calculation
      uint16_t sport, dport; // values of source and destination port numbers -- to be preserved, when increase or decrease is done
      uint16_t sp, dp; // values of source and destination port numbers -- temporary values
      uint32_t curr_dst_ipv4;     // IPv4 destination address, which will be changed
      in6_addr curr_dst_ipv6;     // foreground IPv6 destination address, which will be changed
      in6_addr curr_dst_bg;       // backround IPv6 destination address, which will be changed
      int i;                      // cycle variable for grenerating different destination network addresses
 
      if ( ip_version == 4 )
        curr_dst_ipv4 = *dst_ipv4;
      else // IPv6
        curr_dst_ipv6 = *dst_ipv6;
      curr_dst_bg = *dst_bg;

      // create Test Frames
      for ( j=0; j<N; j++ ) {
        for ( i=0; i<num_dest_nets; i++ ) {
          // create foreground Test Frame (IPv4 or IPv6)
          if ( ip_version == 4 ) {
            ((uint8_t *)&curr_dst_ipv4)[2] = (uint8_t) i; // bits 16 to 23 of the IPv4 address are rewritten, like in 198.18.x.2
            fg_pkt_mbuf[i][j] = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, &curr_dst_ipv4, var_sport, var_dport);
            pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i][j], uint8_t *); // Access the Test Frame in the message buffer
            fg_udp_sport[i][j] = pkt + 34;
            fg_udp_dport[i][j] = pkt + 36;
            fg_udp_chksum[i][j] = pkt + 40;
          } else { // IPv6
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
      thread_local std::mt19937_64 gen_net(rd()); //Standard 64-bit mersenne_twister_engine seeded with rd()
      thread_local std::mt19937_64 gen_sport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      thread_local std::mt19937_64 gen_dport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      std::uniform_int_distribution<int> uni_dis_net(0, num_dest_nets-1);     // uniform distribution in [0, num_dest_nets-1]
      std::uniform_int_distribution<int> uni_dis_sport(sport_min, sport_max);   // uniform distribution in [sport_min, sport_max]
      std::uniform_int_distribution<int> uni_dis_dport(dport_min, dport_max);   // uniform distribution in [sport_min, sport_max]

      // naive sender version: it is simple and fast
      j=0; // increase maunally after each sending
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        int index = uni_dis_net(gen_net); // index of the pre-generated Test Frame for the given destination network
        // set the temporary variables (including several pointers) to handle the right pre-generated Test Frame
        if ( fg_frame = sent_frames % n  < m ) {
          // foreground frame is to be sent
          chksum = fg_udp_chksum_start[index];
          udp_sport = (uint16_t *)fg_udp_sport[index][j];
          udp_dport = (uint16_t *)fg_udp_dport[index][j];
          udp_chksum = (uint16_t *)fg_udp_chksum[index][j];
          pkt_mbuf = fg_pkt_mbuf[index][j];
        } else {
          // background frame is to be sent
          chksum = bg_udp_chksum_start[index];
          udp_sport = (uint16_t *)bg_udp_sport[index][j];
          udp_dport = (uint16_t *)bg_udp_dport[index][j];
          udp_chksum = (uint16_t *)bg_udp_chksum[index][j];
          pkt_mbuf = bg_pkt_mbuf[index][j];
        }
        ipv4_frame = ip_version == 4 && fg_frame; // precalculated to have it ready when needed

        // from here, we need to handle the frame identified by the temprary variables
        if ( var_sport ) {
          // sport is varying
          switch ( var_sport ) {
            case 1:                   // increasing port numbers
              if ( (sp=sport++) == sport_max )
                sport = sport_min;
              break;
            case 2:                   // decreasing port numbers
              if ( (sp=sport--) == sport_min )
                sport = sport_max;
              break;
            case 3:                   // pseudorandom port numbers
              sp = uni_dis_sport(gen_sport);
          }
          chksum += *udp_sport = htons(sp);     // set source port and add to checksum -- corrected
        }
        if ( var_dport ) {
          // dport is varying
          switch ( var_dport ) {
            case 1:                           // increasing port numbers
              if ( (dp=dport++) == dport_max )
                dport = dport_min;
              break;
            case 2:                           // decreasing port numbers
              if ( (dp=dport--) == dport_min )
                dport = dport_max;
              break;
            case 3:                           // pseudorandom port numbers
              dp = uni_dis_dport(gen_dport);
          }
          chksum += *udp_dport = htons(dp);     // set destination port add to checksum -- corrected
        }
        chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);     // calculate 16-bit one's complement sum
        chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);     // twice is enough: 2*0xffff=0x1fffe, 0x1+x0fffe=0xffff
        chksum = (~chksum) & 0xffff;                                    // make one's complement
        if ( ipv4_frame && chksum == 0 )        // over IPv4, checksum should not be 0 (0 means, no checksum is used)
          chksum = 0xffff;
        *udp_chksum = (uint16_t) chksum;            // set checksum in the frame
        // finally, when its time is here, send the frame
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate );    // Beware: an "empty" loop, as well as in the next line
        while ( !rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1) );           // send out the frame
        j = (j+1) % N;
      } // this is the end of the sending cycle
    } // end of the optimized code for multiple destination networks
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


// sends Test Frames for throughput (or frame loss rate) measurement using multiple source and/or destination IP addresses
int msend(void *par) {
  // collecting input parameters:
  class mSenderParameters *p = (class mSenderParameters *)par;
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
  uint16_t eth_id = p->eth_id;
  const char *side = p->side;
  struct ether_addr *dst_mac = p->dst_mac;
  struct ether_addr *src_mac = p->src_mac;
  // excluded: uint16_t num_dest_nets = p->num_dest_nets;
  uint32_t *src_ipv4 = p->src_ipv4;
  uint32_t *dst_ipv4 = p->dst_ipv4;
  struct in6_addr *src_ipv6 = p->src_ipv6;
  struct in6_addr *dst_ipv6 = p->dst_ipv6;
  struct in6_addr *src_bg= p->src_bg;
  struct in6_addr *dst_bg = p->dst_bg;
  unsigned var_sip = p->var_sip;
  unsigned var_dip = p->var_dip;
  unsigned var_ip = var_sip || var_dip; // derived logical value: at least one IP address has to be changed?
  uint16_t sip_min = p->sip_min;
  uint16_t sip_max = p->sip_max;
  uint16_t dip_min = p->dip_min;
  uint16_t dip_max = p->dip_max;
  uint16_t src_ipv4_offset = p->src_ipv4_offset;
  uint16_t dst_ipv4_offset = p->dst_ipv4_offset;
  uint16_t src_ipv6_offset = p->src_ipv6_offset;
  uint16_t dst_ipv6_offset = p->dst_ipv6_offset;
  unsigned var_sport = p->var_sport;
  unsigned var_dport = p->var_dport;
  unsigned varport = var_sport || var_dport; // derived logical value: at least one port has to be changed?
  uint16_t sport_min = p->sport_min;
  uint16_t sport_max = p->sport_max;
  uint16_t dport_min = p->dport_min;
  uint16_t dport_max = p->dport_max;


  // further local variables
  uint64_t frames_to_send = duration * frame_rate;	// Each active sender sends this number of frames
  uint64_t sent_frames=0; // counts the number of sent frames
  double elapsed_seconds; // for checking the elapsed seconds during sending

  bool fg_frame, ipv4_frame; // when sending IPv4 traffic, bacground frames are IPv6: they have no IPv4 checksum

  if ( !varport ) {
    // Implementation of multiple IP addresses (own idea) only, 
    // optimized code for using hard coded fix port numbers as defined in RFC 2544 https://tools.ietf.org/html/rfc2544#appendix-C.2.6.4

    // N size array is used to resolve the write after send problem. In its elements, the
    // varying 16-bit fields of the source and/or destination IP addresses and for IPv4, header checksum are updated.
    // Note: even if UDP port numbers do not change, UDP chechsum changes due to the pseudo-header.
    // This code has been written by simplifying the "varport" version.

    int i; // cycle variable to index the N size array: takes {0..N-1} values
    struct rte_mbuf *fg_pkt_mbuf[N], *bg_pkt_mbuf[N], *pkt_mbuf; // pointers of message buffers for fg. and bg. Test Frames
    uint8_t *pkt; // working pointer to the current frame (in the message buffer)
    uint8_t *fg_src_ip[N], *fg_dst_ip[N], *fg_ipv4_chksum[N], *bg_src_ip[N], *bg_dst_ip[N]; // pointers to the given 16-bit fields to be modified
    uint8_t *fg_udp_chksum[N], *bg_udp_chksum[N]; // pointers to the given fields
    uint16_t *ip_src, *ip_dst, *ipv4_chksum; // working pointers to the given 16-bit fields to be modified
    uint16_t *udp_chksum; // working pointers to the given fields to be modified
    uint16_t fg_ipv4_chksum_start;  // starting values (uncomplemented checksums taken from the original frames)
    uint16_t fg_udp_chksum_start, bg_udp_chksum_start;  // starting values (uncomplemented checksums taken from the original frames)
    uint32_t v4_chksum; // temporary variable: stores the starting value from the packet
    uint32_t ip_chksum; // temporary variable: stores only the accumulated value from the 16-bit IP address fields
    uint32_t chksum; // temporary variable for UDP checksum calculation (store both)
    uint16_t src_ip, dst_ip; // values of source and destination IP addresses -- to be preserved, when increase or decrease is done 
    uint16_t sip, dip; // values of source and destination IP addresses -- temporary values
    
    for ( i=0; i<N; i++ ) {
      // create foreground Test Frame
      if ( ip_version == 4 ) {
        fg_pkt_mbuf[i] = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, dst_ipv4, var_sport, var_dport);
        pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
        fg_src_ip[i] = pkt + 26 + src_ipv4_offset;  // 14+12: source IPv4 address, the 16-bits to be modified
        fg_dst_ip[i] = pkt + 30 + dst_ipv4_offset;  // 14+16: destination IPv4 address, the 16-bits to be modified
        fg_ipv4_chksum[i] = pkt + 24;               // 14+10: IPv4 header checksum
        fg_udp_chksum[i] = pkt + 40;   // 14+20+6: UDP checksum 
      } else { // IPv6
        fg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, var_sport, var_dport);
        pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
        fg_src_ip[i] = pkt + 22 + src_ipv6_offset;  // 14+8:  source IPv6 address, the 16-bits to be modified
        fg_dst_ip[i] = pkt + 38 + dst_ipv6_offset;  // 14+24: destination IPv6 address, the 16-bits to be modified
        fg_udp_chksum[i] = pkt + 60;   // 14+40+6: UDP checksum
      }
      if ( ip_version == 4 )
        fg_ipv4_chksum_start = ~*(uint16_t *)fg_ipv4_chksum[i]; // save the uncomplemented checksum value (same for all values of "i") 
      fg_udp_chksum_start = ~*(uint16_t *)fg_udp_chksum[i]; // save the uncomplemented checksum value (same for all values of "i") 
      // create backround Test Frame (always IPv6)
      bg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg, var_sport, var_dport);
      pkt = rte_pktmbuf_mtod(bg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
      bg_src_ip[i] = pkt + 22 + src_ipv6_offset;  // 14+8:  source IPv6 address, the 16-bits to be modified
      bg_dst_ip[i] = pkt + 38 + dst_ipv6_offset;  // 14+24: destination IPv6 address, the 16-bits to be modified
      bg_udp_chksum[i] = pkt + 60;   // 14+40+6: UDP checksum
      bg_udp_chksum_start = ~*(uint16_t *)bg_udp_chksum[i]; // save the uncomplemented checksum value (same for all values of "i")
    } 

    // set the starting values of the changing 16-bit fields of IP addresses, if they are increased or decreased
    if ( var_sip == 1 )
      src_ip = sip_min; 
    if ( var_sip == 2 )
      src_ip = sip_max;
    if ( var_dip == 1 )
      dst_ip = dip_min; 
    if ( var_dip == 2 )
      dst_ip = dip_max;

    // prepare random number infrastructure
    thread_local std::random_device rd;  // Will be used to obtain a seed for the random number engines
    thread_local std::mt19937_64 gen_sip(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
    thread_local std::mt19937_64 gen_dip(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<int> uni_dis_sip(sip_min, sip_max);     // uniform distribution in [sip_min, sip_max]
    std::uniform_int_distribution<int> uni_dis_dip(dip_min, dip_max);     // uniform distribution in [dip_min, dip_max]

    // naive sender version: it is simple and fast
    i=0; // increase maunally after each sending
    for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
      // set the temporary variables (including several pointers) to handle the right pre-generated Test Frame
      if ( fg_frame = sent_frames % n < m ) {
        // foreground frame is to be sent
        v4_chksum = fg_ipv4_chksum_start; // rubbish, if IPv6, but a branch instruction is spared
        ip_src = (uint16_t *)fg_src_ip[i];
        ip_dst = (uint16_t *)fg_dst_ip[i];
        ipv4_chksum = (uint16_t *)fg_ipv4_chksum[i]; // rubbish, if IPv6, but a branch instruction is spared
        chksum = fg_udp_chksum_start;
        udp_chksum = (uint16_t *)fg_udp_chksum[i];
        pkt_mbuf = fg_pkt_mbuf[i];
      } else {
        // background frame is to be sent, it is surely IPv6
        ip_src = (uint16_t *)bg_src_ip[i];
        ip_dst = (uint16_t *)bg_dst_ip[i];
        chksum = bg_udp_chksum_start;
        udp_chksum = (uint16_t *)bg_udp_chksum[i];
        pkt_mbuf = bg_pkt_mbuf[i];
      }
      ipv4_frame = ip_version == 4 && fg_frame; // precalculated to have it ready when needed
      ip_chksum=0; // first, changes are accumulated here, surely used for UDP checksum and if IPv4 then also for IPV4 checksum

      // from here, we need to handle the fields identified by the temprary variables
      if ( var_sip ) {
        // source IP is varying
        switch ( var_sip ) {
          case 1:                   // increasing 
            if ( (sip=src_ip++) == sip_max )
              src_ip = sip_min;
            break;
          case 2:                   // decreasing 
            if ( (sip=src_ip--) == sip_min )
              src_ip = sip_max;
            break;
          case 3:                   // pseudorandom 
            sip = uni_dis_sip(gen_sip);
        }
        ip_chksum += *ip_src = htons(sip);     // set src IP 16-bit field and add to checksum
      }
      if ( var_dip ) {
        // destination IP is varying
        switch ( var_dip ) {
          case 1:                           // increasing
            if ( (dip=dst_ip++) == dip_max )
              dst_ip = dip_min;
            break;
          case 2:                           // decreasing
            if ( (dip=dst_ip--) == dip_min )
              dst_ip = dip_max;
            break;
          case 3:                           // pseudorandom
            dip = uni_dis_dip(gen_dip);
        }
        ip_chksum += *ip_dst = htons(dip);     // set dst IP 16-bit field and add to checksum
      }
      if ( ipv4_frame ) {
        // only the IPv4 header contains IP checksum
        v4_chksum += ip_chksum;
        v4_chksum = ((v4_chksum & 0xffff0000) >> 16) + (v4_chksum & 0xffff);   	// calculate 16-bit one's complement sum
        v4_chksum = ((v4_chksum & 0xffff0000) >> 16) + (v4_chksum & 0xffff);   	// twice is enough: 2*0xffff=0x1fffe, 0x1+x0fffe=0xffff
        v4_chksum = (~v4_chksum) & 0xffff;                                  	// make one's complement
        *ipv4_chksum = (uint16_t) v4_chksum;            // set checksum in the frame
      }
      chksum += ip_chksum;  // UDP checksum contains the checksum of IPv4 or IPv6 pseudo header
      chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   	// calculate 16-bit one's complement sum
      chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   	// twice is enough: 2*0xffff=0x1fffe, 0x1+x0fffe=0xffff
      chksum = (~chksum) & 0xffff;                                  	// make one's complement
      if ( ipv4_frame && chksum == 0)  // over IPv4, checksum should not be 0 (0 means, no checksum is used)
        chksum = 0xffff;
      *udp_chksum = (uint16_t) chksum;            // set checksum in the frame
      // finally, when its time is here, send the frame
      while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); 	// Beware: an "empty" loop, as well as in the next line
      while ( !rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1) ); 		// send out the frame
      i = (i+1) % N;
    } // this is the end of the sending cycle
  } // end of optimized code for fixed port numbers
  else {
    // Implementation of multiple IP addresses (own idea) plus
    // varying port numbers recommended by RFC 4814 https://tools.ietf.org/html/rfc4814#section-4.5
    // RFC 4814 requires pseudorandom port numbers, increasing and decreasing ones are our additional, non-stantard solutions
   
    // N size array is used to resolve the write after send problem. In its elements, the
    // varying 16-bit fields of the source and/or destination IP addresses and for IPv4, header checksum,
    // source and/or destination port number(s) and UDP checksum are updated.
    int i; // cycle variable to index the N size array: takes {0..N-1} values
    struct rte_mbuf *fg_pkt_mbuf[N], *bg_pkt_mbuf[N], *pkt_mbuf; // pointers of message buffers for fg. and bg. Test Frames
    uint8_t *pkt; // working pointer to the current frame (in the message buffer)
    uint8_t *fg_src_ip[N], *fg_dst_ip[N], *fg_ipv4_chksum[N], *bg_src_ip[N], *bg_dst_ip[N]; // pointers to the given 16-bit fields to be modified
    uint8_t *fg_udp_sport[N], *fg_udp_dport[N], *fg_udp_chksum[N], *bg_udp_sport[N], *bg_udp_dport[N], *bg_udp_chksum[N]; // pointers to the given fields
    uint16_t *ip_src, *ip_dst, *ipv4_chksum; // working pointers to the given 16-bit fields to be modified
    uint16_t *udp_sport, *udp_dport, *udp_chksum; // working pointers to the given fields to be modified
    uint16_t fg_ipv4_chksum_start;  // starting values (uncomplemented checksums taken from the original frames)
    uint16_t fg_udp_chksum_start, bg_udp_chksum_start;  // starting values (uncomplemented checksums taken from the original frames)
    uint32_t v4_chksum; // temporary variable: stores the starting value from the packet
    uint32_t ip_chksum; // temporary variable: stores only the accumulated value from the 16-bit IP address fields
    uint32_t chksum; // temporary variable for UDP checksum calculation (store both)
    uint16_t src_ip, dst_ip; // values of source and destination IP addresses -- to be preserved, when increase or decrease is done 
    uint16_t sport, dport; // values of source and destination port numbers -- to be preserved, when increase or decrease is done 
    uint16_t sip, dip; // values of source and destination IP addresses -- temporary values
    uint16_t sp, dp; // values of source and destination port numbers -- temporary values
    
    for ( i=0; i<N; i++ ) {
      // create foreground Test Frame
      if ( ip_version == 4 ) {
        fg_pkt_mbuf[i] = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, dst_ipv4, var_sport, var_dport);
        pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
        fg_src_ip[i] = pkt + 26 + src_ipv4_offset;  // 14+12: source IPv4 address, the 16-bits to be modified
        fg_dst_ip[i] = pkt + 30 + dst_ipv4_offset;  // 14+16: destination IPv4 address, the 16-bits to be modified
        fg_ipv4_chksum[i] = pkt + 24;               // 14+10: IPv4 header checksum
        fg_udp_sport[i] = pkt + 34;    // 14+20+0: source UDP port 
        fg_udp_dport[i] = pkt + 36;    // 14+20+2: destination UDP port
        fg_udp_chksum[i] = pkt + 40;   // 14+20+6: UDP checksum 
      } else { // IPv6
        fg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, var_sport, var_dport);
        pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
        fg_src_ip[i] = pkt + 22 + src_ipv6_offset;  // 14+8:  source IPv6 address, the 16-bits to be modified
        fg_dst_ip[i] = pkt + 38 + dst_ipv6_offset;  // 14+24: destination IPv6 address, the 16-bits to be modified
        fg_udp_sport[i] = pkt + 54;    // 14+40+0: source UDP port
        fg_udp_dport[i] = pkt + 56;    // 14+40+2: destination UDP port
        fg_udp_chksum[i] = pkt + 60;   // 14+40+6: UDP checksum
      }
      if ( ip_version == 4 )
        fg_ipv4_chksum_start = ~*(uint16_t *)fg_ipv4_chksum[i]; // save the uncomplemented checksum value (same for all values of "i") 
      fg_udp_chksum_start = ~*(uint16_t *)fg_udp_chksum[i]; // save the uncomplemented checksum value (same for all values of "i") 
      // create backround Test Frame (always IPv6)
      bg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg, var_sport, var_dport);
      pkt = rte_pktmbuf_mtod(bg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
      bg_src_ip[i] = pkt + 22 + src_ipv6_offset;  // 14+8:  source IPv6 address, the 16-bits to be modified
      bg_dst_ip[i] = pkt + 38 + dst_ipv6_offset;  // 14+24: destination IPv6 address, the 16-bits to be modified
      bg_udp_sport[i] = pkt + 54;    // 14+40+0: source UDP port
      bg_udp_dport[i] = pkt + 56;    // 14+40+2: destination UDP port
      bg_udp_chksum[i] = pkt + 60;   // 14+40+6: UDP checksum
      bg_udp_chksum_start = ~*(uint16_t *)bg_udp_chksum[i]; // save the uncomplemented checksum value (same for all values of "i")
    } 

    // set the starting values of the changing 16-bit fields of IP addresses, if they are increased or decreased
    if ( var_sip == 1 )
      src_ip = sip_min; 
    if ( var_sip == 2 )
      src_ip = sip_max;
    if ( var_dip == 1 )
      dst_ip = dip_min; 
    if ( var_dip == 2 )
      dst_ip = dip_max;

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
    thread_local std::mt19937_64 gen_sip(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
    thread_local std::mt19937_64 gen_dip(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<int> uni_dis_sip(sip_min, sip_max);     // uniform distribution in [sip_min, sip_max]
    std::uniform_int_distribution<int> uni_dis_dip(dip_min, dip_max);     // uniform distribution in [dip_min, dip_max]
    thread_local std::mt19937_64 gen_sport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
    thread_local std::mt19937_64 gen_dport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<int> uni_dis_sport(sport_min, sport_max);	// uniform distribution in [sport_min, sport_max]
    std::uniform_int_distribution<int> uni_dis_dport(dport_min, dport_max);	// uniform distribution in [sport_min, sport_max]

    // naive sender version: it is simple and fast
    i=0; // increase maunally after each sending
    for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
      // set the temporary variables (including several pointers) to handle the right pre-generated Test Frame
      if ( fg_frame = sent_frames % n < m ) {
        // foreground frame is to be sent
        v4_chksum = fg_ipv4_chksum_start; // rubbish, if IPv6, but a branch instruction is spared
        ip_src = (uint16_t *)fg_src_ip[i];
        ip_dst = (uint16_t *)fg_dst_ip[i];
        ipv4_chksum = (uint16_t *)fg_ipv4_chksum[i]; // rubbish, if IPv6, but a branch instruction is spared
        chksum = fg_udp_chksum_start;
        udp_sport = (uint16_t *)fg_udp_sport[i];
        udp_dport = (uint16_t *)fg_udp_dport[i];
        udp_chksum = (uint16_t *)fg_udp_chksum[i];
        pkt_mbuf = fg_pkt_mbuf[i];
      } else {
        // background frame is to be sent, it is surely IPv6
        ip_src = (uint16_t *)bg_src_ip[i];
        ip_dst = (uint16_t *)bg_dst_ip[i];
        chksum = bg_udp_chksum_start;
        udp_sport = (uint16_t *)bg_udp_sport[i];
        udp_dport = (uint16_t *)bg_udp_dport[i];
        udp_chksum = (uint16_t *)bg_udp_chksum[i];
        pkt_mbuf = bg_pkt_mbuf[i];
      }
      ipv4_frame = ip_version == 4 && fg_frame; // precalculated to have it ready when needed
      ip_chksum=0; // first, changes are accumulated here, surely used for UDP checksum and if IPv4 then also for IPv4 checksum

      // from here, we need to handle the fields identified by the temprary variables
      if ( var_sip ) {
        // source IP is varying
        switch ( var_sip ) {
          case 1:                   // increasing 
            if ( (sip=src_ip++) == sip_max )
              src_ip = sip_min;
            break;
          case 2:                   // decreasing 
            if ( (sip=src_ip--) == sip_min )
              src_ip = sip_max;
            break;
          case 3:                   // pseudorandom 
            sip = uni_dis_sip(gen_sip);
        }
        ip_chksum += *ip_src = htons(sip);     // set src IP 16-bit field and add to checksum
      }
      if ( var_dip ) {
        // destination IP is varying
        switch ( var_dip ) {
          case 1:                           // increasing
            if ( (dip=dst_ip++) == dip_max )
              dst_ip = dip_min;
            break;
          case 2:                           // decreasing
            if ( (dip=dst_ip--) == dip_min )
              dst_ip = dip_max;
            break;
          case 3:                           // pseudorandom
            dip = uni_dis_dip(gen_dip);
        }
        ip_chksum += *ip_dst = htons(dip);     // set dst IP 16-bit field and add to checksum
      }
      if ( ipv4_frame ) {
        // only the IPv4 header contains IP checksum
        v4_chksum += ip_chksum;
        v4_chksum = ((v4_chksum & 0xffff0000) >> 16) + (v4_chksum & 0xffff);   	// calculate 16-bit one's complement sum
        v4_chksum = ((v4_chksum & 0xffff0000) >> 16) + (v4_chksum & 0xffff);   	// twice is enough: 2*0xffff=0x1fffe, 0x1+x0fffe=0xffff
        v4_chksum = (~v4_chksum) & 0xffff;                                  	// make one's complement
        *ipv4_chksum = (uint16_t) v4_chksum;            // set checksum in the frame
      }
      chksum += ip_chksum;  // UDP checksum contains the checksum of IPv4 or IPv6 pseudo header
      if ( var_sport ) {
        // sport is varying
        switch ( var_sport ) {
          case 1:                   // increasing port numbers
            if ( (sp=sport++) == sport_max )
              sport = sport_min;
            break;
          case 2:                   // decreasing port numbers
            if ( (sp=sport--) == sport_min )
              sport = sport_max;
            break;
          case 3:                   // pseudorandom port numbers
            sp = uni_dis_sport(gen_sport);
        }
        chksum += *udp_sport = htons(sp);     // set source port and add to checksum -- corrected
      }
      if ( var_dport ) {
        // dport is varying
        switch ( var_dport ) {
          case 1:                           // increasing port numbers
            if ( (dp=dport++) == dport_max )
              dport = dport_min;
            break;
          case 2:                           // decreasing port numbers
            if ( (dp=dport--) == dport_min )
              dport = dport_max;
            break;
          case 3:                           // pseudorandom port numbers
            dp = uni_dis_dport(gen_dport);
        }
        chksum += *udp_dport = htons(dp);     // set destination port add to checksum -- corrected
      }
      chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   	// calculate 16-bit one's complement sum
      chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   	// twice is enough: 2*0xffff=0x1fffe, 0x1+x0fffe=0xffff
      chksum = (~chksum) & 0xffff;                                  	// make one's complement
      if ( ipv4_frame && chksum == 0)  // over IPv4, checksum should not be 0 (0 means, no checksum is used)
        chksum = 0xffff;
      *udp_chksum = (uint16_t) chksum;            // set checksum in the frame
      // finally, when its time is here, send the frame
      while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); 	// Beware: an "empty" loop, as well as in the next line
      while ( !rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1) ); 		// send out the frame
      i = (i+1) % N;
    } // this is the end of the sending cycle

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

// Initiator/Sender: sends Preliminary Frames (no more used for sending real Test Frames)
int isend(void *par) {
  // collecting input parameters:
  class iSenderParameters *p = (class iSenderParameters *)par;
  class senderCommonParameters *cp = p->cp;

  // parameters directly correspond to the data members of class Throughput
  uint16_t ipv6_frame_size = cp->ipv6_frame_size;
  uint16_t ipv4_frame_size = cp->ipv4_frame_size;
  uint32_t frame_rate = cp->frame_rate;
  // uint16_t duration = cp->duration; 	// not used in isend
  uint32_t n = cp->n;
  uint32_t m = cp->m;
  uint64_t hz = cp->hz;
  uint64_t start_tsc = cp->start_tsc;

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
  uint16_t sport_min = p->sport_min;
  uint16_t sport_max = p->sport_max;
  uint16_t dport_min = p->dport_min;
  uint16_t dport_max = p->dport_max;
  bits32 *uniquePortComb = p->uniquePortComb;	// array of pre-generated unique port number combinations (Enumerate-ports 3)

  unsigned enumerate_ports = p->enumerate_ports;

  bool fg_frame, ipv4_frame;    // the current frame belongs to the foreground traffic: needed for port number enumerataion, 
                                // and when sending IPv4 traffic, background frames are IPv6: their UDP checksum may be 0.
  // further local variables
  uint64_t frames_to_send;
  uint64_t sent_frames=0; 	// counts the number of sent frames
  bits32 *uniquePC=uniquePortComb;	// working pointer to the current element of uniquePortComb

  frames_to_send = p->pre_frames;	// use the specified value for sending preliminary frames

  unsigned varport = var_sport || var_dport || enumerate_ports; // derived logical value: at least one port has to be changed?

  if ( !varport ) {
    // optimized code for using hard coded fix port numbers as defined in RFC 2544 https://tools.ietf.org/html/rfc2544#appendix-C.2.6.4
    if ( num_dest_nets == 1 ) { 	
      // optimized code for single destination network: always the same foreground or background frame is sent, no arrays are used
      struct rte_mbuf *fg_pkt_mbuf, *bg_pkt_mbuf; // message buffers for fg. and bg. Test Frames
      // create foreground Test Frame
      if ( ip_version == 4 )
        fg_pkt_mbuf = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, dst_ipv4, 0, 0);
      else  // IPv6
        fg_pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, 0, 0);
  
      // create backround Test Frame (always IPv6)
      bg_pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg, 0, 0);
  
      // naive sender version: it is simple and fast
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); // Beware: an "empty" loop, and further two will come!
        if ( sent_frames % n  < m )
          while ( !rte_eth_tx_burst(eth_id, 0, &fg_pkt_mbuf, 1) ); // send foreground frame
        else
           while ( !rte_eth_tx_burst(eth_id, 0, &bg_pkt_mbuf, 1) ); // send background frame
      } // this is the end of the sending cycle
    } // end of optimized code for single destination network
    else {
      // optimized code for multiple destination networks: foreground and background frames are generated for each network and pointers are stored in arrays
      // assertion: num_dest_nets <= 256 
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
    } // end of optimized code for multiple destination networks
  } // end of optimized code for fixed port numbers
  else {
    // implementation of varying port numbers recommended by RFC 4814 https://tools.ietf.org/html/rfc4814#section-4.5
    // RFC 4814 requires pseudorandom port numbers, increasing and decreasing ones are our additional, non-stantard solutions
    if ( num_dest_nets == 1 ) {
      // optimized code for single destination network: always one of the same N pre-prepared foreground or background frames is updated and sent, 
      // source and/or destination port number(s) and UDP checksum are updated
      // N size arrays are used to resolve the write after send problem
      int i; // cycle variable for the above mentioned purpose: takes {0..N-1} values
      struct rte_mbuf *fg_pkt_mbuf[N], *bg_pkt_mbuf[N], *pkt_mbuf; // pointers of message buffers for fg. and bg. Test Frames
      uint8_t *pkt; // working pointer to the current frame (in the message buffer)
      uint8_t *fg_udp_sport[N], *fg_udp_dport[N], *fg_udp_chksum[N], *bg_udp_sport[N], *bg_udp_dport[N], *bg_udp_chksum[N]; // pointers to the given fields
      uint16_t *udp_sport, *udp_dport, *udp_chksum; // working pointers to the given fields
      uint16_t fg_udp_chksum_start, bg_udp_chksum_start;  // starting values (uncomplemented checksums taken from the original frames)
      uint32_t chksum; // temporary variable for checksum calculation
      uint16_t sport, dport; // values of source and destination port numbers -- to be preserved, when increase or decrease is done 
      uint16_t e_sport, e_dport; // values of source and destination port numbers -- to be preserved, used for port enumeration of foreground traffic
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

      // prepare for the different kinds of port number enumarations
      switch ( enumerate_ports ) {
        case 0: // no port number enumeration is done 
          // set the starting values of port numbers, if they are increased or decreased
          if ( var_sport == 1 )
            sport = sport_min; 
          if ( var_sport == 2 )
            sport = sport_max;
          if ( var_dport == 1 )
            dport = dport_min; 
          if ( var_dport == 2 )
            dport = dport_max;
	  break;
        case 1: // port numbers are enumerated in increasing order 
          e_sport = sport_min;
          e_dport = dport_min;
	  break;
        case 2: // port numbers are enumerated in decreasing order
          e_sport = sport_max;
          e_dport = dport_max;
	  break;
	case 3: 
	  if ( !uniquePortComb )
            rte_exit(EXIT_FAILURE, "Error: Initiator/Sender received a NULL pointer to the array of pre-prepaired unique random port numbers!\n");
	  // unique pseudorandom port number pairs are guarandteed by pre-prepaired random permutation
	  break;
      } 

      // prepare random number infrastructure
      thread_local std::random_device rd;  // Will be used to obtain a seed for the random number engines
      thread_local std::mt19937_64 gen_sport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      thread_local std::mt19937_64 gen_dport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      std::uniform_int_distribution<int> uni_dis_sport(sport_min, sport_max);	// uniform distribution in [sport_min, sport_max]
      std::uniform_int_distribution<int> uni_dis_dport(dport_min, dport_max);	// uniform distribution in [sport_min, sport_max]

      // naive sender version: it is simple and fast
      i=0; // increase maunally after each sending
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        // set the temporary variables (including several pointers) to handle the right pre-generated Test Frame
        if ( fg_frame = sent_frames % n  < m ) {
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
        ipv4_frame = ip_version == 4 && fg_frame; // precalculated to have it ready when needed

        // from here, we need to handle the frame identified by the temprary variables
        if ( enumerate_ports && fg_frame ) {
	  switch ( enumerate_ports ) {
	    case 1: // port numbers are enumerated in incresing order
       	      // sport is the low order counter, dport is the high order counter
    	      if ( (sp=e_sport++) == sport_max ) {
                e_sport = sport_min;
                if ( (dp=e_dport++) == dport_max )
                   e_dport = dport_min;
              } else 
                dp = e_dport;
              chksum += *udp_sport = htons(sp);     // set source port and add to checksum -- corrected
              chksum += *udp_dport = htons(dp);     // set destination port add to checksum -- corrected
	      break;
            case 2: // port numbers are enumerated in decresing order
              // sport is the low order counter, dport is the high order counter
              if ( (sp=e_sport--) == sport_min ) {
                e_sport = sport_max;
                if ( (dp=e_dport--) == dport_min )
                   e_dport = dport_max;
              } else
                dp = e_dport;
              chksum += *udp_sport = htons(sp);     // set source port and add to checksum -- corrected
              chksum += *udp_dport = htons(dp);     // set destination port add to checksum -- corrected
              break;
	    case 3: // the next unique pseudorandom port number pair is taken
	      sp = uniquePC->field.src;	// read source port number
	      dp = uniquePC->field.dst;	// read destination port number
	      uniquePC++;		// increase pointer: no check needed, we have surely enough
              chksum += *udp_sport = htons(sp);     // set source port and add to checksum -- corrected
              chksum += *udp_dport = htons(dp);     // set destination port add to checksum -- corrected
	      break;
          } // end of switch
        } else {
 	  // port numbers are handled as before
          if ( var_sport ) {
            // sport is varying
            switch ( var_sport ) {
              case 1:			// increasing port numbers
                if ( (sp=sport++) == sport_max )
                  sport = sport_min;
                break;
              case 2:			// decreasing port numbers
                if ( (sp=sport--) == sport_min )
                  sport = sport_max;
                break;
              case 3:			// pseudorandom port numbers
                sp = uni_dis_sport(gen_sport);
            }
            chksum += *udp_sport = htons(sp);     // set source port and add to checksum -- corrected
          }
          if ( var_dport ) {
            // dport is varying
            switch ( var_dport ) {
              case 1:                   	// increasing port numbers
                if ( (dp=dport++) == dport_max )
                  dport = dport_min;
                break;
              case 2:                   	// decreasing port numbers
                if ( (dp=dport--) == dport_min )
                  dport = dport_max;
                break;
              case 3:                   	// pseudorandom port numbers
                dp = uni_dis_dport(gen_dport);
            }
            chksum += *udp_dport = htons(dp);     // set destination port add to checksum -- corrected
          }
        }
        chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   	// calculate 16-bit one's complement sum
        chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   	// twice is enough: 2*0xffff=0x1fffe, 0x1+x0fffe=0xffff
        chksum = (~chksum) & 0xffff;                                  	// make one's complement
        if ( ipv4_frame && chksum == 0 )        // over IPv4, checksum should not be 0 (0 means, no checksum is used)
          chksum = 0xffff;
        *udp_chksum = (uint16_t) chksum;            // set checksum in the frame
        // finally, when its time is here, send the frame
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); 	// Beware: an "empty" loop, as well as in the next line
        while ( !rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1) ); 		// send out the frame
        i = (i+1) % N;
      } // this is the end of the sending cycle
    } // end of optimized code for single destination network
    else { 
      // optimized code for multiple destination networks: N copies of foreground or background frames are prepared for each destination network,
      // N size arrays are used to resolve the write after send problem
      // source and/or destination port number(s) and UDP checksum are updated in the actually used copy before sending
      // assertion: num_dest_nets <= 256
      int j; // cycle variable to index the N size array: takes {0..N-1} values
      struct rte_mbuf *fg_pkt_mbuf[256][N], *bg_pkt_mbuf[256][N], *pkt_mbuf; // pointers of message buffers for fg. and bg. Test Frames
      uint8_t *pkt; // working pointer to the current frame (in the message buffer)
      uint8_t *fg_udp_sport[256][N], *fg_udp_dport[256][N], *fg_udp_chksum[256][N]; // pointers to the given fields of the pre-prepared Test Frames
      uint8_t *bg_udp_sport[256][N], *bg_udp_dport[256][N], *bg_udp_chksum[256][N]; // pointers to the given fields of the pre-prepared Test Frames
      uint16_t *udp_sport, *udp_dport, *udp_chksum; // working pointers to the given fields
      uint16_t fg_udp_chksum_start[256], bg_udp_chksum_start[256];  // starting values (uncomplemented checksums taken from the original frames)
      uint32_t chksum; // temporary variable for checksum calculation
      uint16_t sport, dport; // values of source and destination port numbers -- to be preserved, when increase or decrease is done
      uint16_t sp, dp; // values of source and destination port numbers -- temporary values
      uint32_t curr_dst_ipv4;     // IPv4 destination address, which will be changed
      in6_addr curr_dst_ipv6;     // foreground IPv6 destination address, which will be changed
      in6_addr curr_dst_bg;       // backround IPv6 destination address, which will be changed
      int i;                      // cycle variable for grenerating different destination network addresses
 
      if ( ip_version == 4 )
        curr_dst_ipv4 = *dst_ipv4;
      else // IPv6
        curr_dst_ipv6 = *dst_ipv6;
      curr_dst_bg = *dst_bg;

      // create Test Frames
      for ( j=0; j<N; j++ ) {
        for ( i=0; i<num_dest_nets; i++ ) {
          // create foreground Test Frame (IPv4 or IPv6)
          if ( ip_version == 4 ) {
            ((uint8_t *)&curr_dst_ipv4)[2] = (uint8_t) i; // bits 16 to 23 of the IPv4 address are rewritten, like in 198.18.x.2
            fg_pkt_mbuf[i][j] = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, &curr_dst_ipv4, var_sport, var_dport);
            pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i][j], uint8_t *); // Access the Test Frame in the message buffer
            fg_udp_sport[i][j] = pkt + 34;
            fg_udp_dport[i][j] = pkt + 36;
            fg_udp_chksum[i][j] = pkt + 40;
          } else { // IPv6
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
      thread_local std::mt19937_64 gen_net(rd()); //Standard 64-bit mersenne_twister_engine seeded with rd()
      thread_local std::mt19937_64 gen_sport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      thread_local std::mt19937_64 gen_dport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      std::uniform_int_distribution<int> uni_dis_net(0, num_dest_nets-1);     // uniform distribution in [0, num_dest_nets-1]
      std::uniform_int_distribution<int> uni_dis_sport(sport_min, sport_max);   // uniform distribution in [sport_min, sport_max]
      std::uniform_int_distribution<int> uni_dis_dport(dport_min, dport_max);   // uniform distribution in [sport_min, sport_max]

      // naive sender version: it is simple and fast
      j=0; // increase maunally after each sending
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        int index = uni_dis_net(gen_net); // index of the pre-generated Test Frame for the given destination network
        // set the temporary variables (including several pointers) to handle the right pre-generated Test Frame
        if ( fg_frame = sent_frames % n  < m ) {
          // foreground frame is to be sent
          chksum = fg_udp_chksum_start[index];
          udp_sport = (uint16_t *)fg_udp_sport[index][j];
          udp_dport = (uint16_t *)fg_udp_dport[index][j];
          udp_chksum = (uint16_t *)fg_udp_chksum[index][j];
          pkt_mbuf = fg_pkt_mbuf[index][j];
        } else {
          // background frame is to be sent
          chksum = bg_udp_chksum_start[index];
          udp_sport = (uint16_t *)bg_udp_sport[index][j];
          udp_dport = (uint16_t *)bg_udp_dport[index][j];
          udp_chksum = (uint16_t *)bg_udp_chksum[index][j];
          pkt_mbuf = bg_pkt_mbuf[index][j];
        }
        ipv4_frame = ip_version == 4 && fg_frame; // precalculated to have it ready when needed

        // from here, we need to handle the frame identified by the temprary variables
        if ( var_sport ) {
          // sport is varying
          switch ( var_sport ) {
            case 1:                   // increasing port numbers
              if ( (sp=sport++) == sport_max )
                sport = sport_min;
              break;
            case 2:                   // decreasing port numbers
              if ( (sp=sport--) == sport_min )
                sport = sport_max;
              break;
            case 3:                   // pseudorandom port numbers
              sp = uni_dis_sport(gen_sport);
          }
          chksum += *udp_sport = htons(sp);     // set source port and add to checksum -- corrected
        }
        if ( var_dport ) {
          // dport is varying
          switch ( var_dport ) {
            case 1:                           // increasing port numbers
              if ( (dp=dport++) == dport_max )
                dport = dport_min;
              break;
            case 2:                           // decreasing port numbers
              if ( (dp=dport--) == dport_min )
                dport = dport_max;
              break;
            case 3:                           // pseudorandom port numbers
              dp = uni_dis_dport(gen_dport);
          }
          chksum += *udp_dport = htons(dp);     // set destination port add to checksum -- corrected
        }
        chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);     // calculate 16-bit one's complement sum
        chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);     // twice is enough: 2*0xffff=0x1fffe, 0x1+x0fffe=0xffff
        chksum = (~chksum) & 0xffff;                                    // make one's complement
        if ( ipv4_frame && chksum == 0)        // over IPv4, checksum should not be 0 (0 means, no checksum is used)
          chksum = 0xffff;
        *udp_chksum = (uint16_t) chksum;            // set checksum in the frame
        // finally, when its time is here, send the frame
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate );    // Beware: an "empty" loop, as well as in the next line
        while ( !rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1) );           // send out the frame
        j = (j+1) % N;
      } // this is the end of the sending cycle
    } // end of the optimized code for multiple destination networks
  } // end of implementation of varying port numbers 

  // Now, we check the time
  uint64_t elapsed_tsc = rte_rdtsc()-start_tsc;
  printf("Info: %s sender's sending took %3.10lf seconds.\n", side, (double)elapsed_tsc/hz);
  // this is a preliminary test, 'duration' is not valid
  if ( elapsed_tsc > hz*frames_to_send/frame_rate*TOLERANCE )
    printf("Warning: %s sending was too slow (only %3.10lf percent of required rate), the test is invalid.\n", side,
             100.0*frames_to_send/elapsed_tsc*hz/frame_rate);
  else
    printf("%s frames sent: %lu\n", side, sent_frames);

  if ( uniquePortComb ) 
    rte_free(uniquePortComb);	// free the array for pre-generated unique port number combinations
  return 0;
}

// Initiator/Sender with multi IP: sends Preliminary Frames using multiple source and/or destination IP addresses
// derived from msend(), may be used only with multi-ip.
// also reused a lot of code from isend()
int imsend(void *par) {
  // collecting input parameters:
  class imSenderParameters *p = (class imSenderParameters *)par;
  class senderCommonParameters *cp = p->cp;

  // parameters directly correspond to the data members of class Throughput
  uint16_t ipv6_frame_size = cp->ipv6_frame_size;
  uint16_t ipv4_frame_size = cp->ipv4_frame_size;
  uint32_t frame_rate = cp->frame_rate;
  // uint16_t duration = cp->duration;	// not used in imsend
  uint32_t n = cp->n;
  uint32_t m = cp->m;
  uint64_t hz = cp->hz;
  uint64_t start_tsc = cp->start_tsc;

  // parameters which are different for the Left sender and the Right sender
  int ip_version = p->ip_version;
  rte_mempool *pkt_pool = p->pkt_pool;
  uint16_t eth_id = p->eth_id;
  const char *side = p->side;
  struct ether_addr *dst_mac = p->dst_mac;
  struct ether_addr *src_mac = p->src_mac;
  // excluded: uint16_t num_dest_nets = p->num_dest_nets;
  uint32_t *src_ipv4 = p->src_ipv4;
  uint32_t *dst_ipv4 = p->dst_ipv4;
  struct in6_addr *src_ipv6 = p->src_ipv6;
  struct in6_addr *dst_ipv6 = p->dst_ipv6;
  struct in6_addr *src_bg= p->src_bg;
  struct in6_addr *dst_bg = p->dst_bg;
  unsigned var_sip = p->var_sip;
  unsigned var_dip = p->var_dip;
  // will be defined below: unsigned var_ip = var_sip || var_dip; // derived logical value: at least one IP address has to be changed?
  uint16_t sip_min = p->sip_min;
  uint16_t sip_max = p->sip_max;
  uint16_t dip_min = p->dip_min;
  uint16_t dip_max = p->dip_max;
  uint16_t src_ipv4_offset = p->src_ipv4_offset;
  uint16_t dst_ipv4_offset = p->dst_ipv4_offset;
  uint16_t src_ipv6_offset = p->src_ipv6_offset;
  uint16_t dst_ipv6_offset = p->dst_ipv6_offset;
  unsigned var_sport = p->var_sport;
  unsigned var_dport = p->var_dport;
  // will be defined below: unsigned varport = var_sport || var_dport; // derived logical value: at least one port has to be changed?
  uint16_t sport_min = p->sport_min;
  uint16_t sport_max = p->sport_max;
  uint16_t dport_min = p->dport_min;
  uint16_t dport_max = p->dport_max;
  bits32 *uniqueIpComb = p->uniqueIpComb;   // array of pre-generated unique IP address combinations (Enumerate-ips 3, but Enumerate-ports 0)
  bits64 *uniqueFtComb = p->uniqueFtComb;   // array of pre-generated unique 4-tuple combinations (Enumerate-ips 3, Enumerate-ports 3)

  unsigned enumerate_ips = p->enumerate_ips;
  unsigned enumerate_ports = p->enumerate_ports;

  // further local variables
  uint64_t frames_to_send = p->pre_frames;	// use the specified value for sending preliminary frames
  uint64_t sent_frames=0; // counts the number of sent frames
  bits32 *uniqueIC=uniqueIpComb;      // working pointer to the current element of uniqueIpComb
  bits64 *uniqueFC=uniqueFtComb;      // working pointer to the current element of uniqueFtComb

  bool fg_frame, ipv4_frame;    // When sending IPv4 traffic, bacground frames are IPv6: they have no header checksum, 
               	                // and they treat UDP checksum differently (0 is an allowed value).
                                // Also needed for IP address and port number enumeration (it is only for foreground frames).

  unsigned var_ip = var_sip || var_dip || enumerate_ips; // derived logical value: at least one IP address has to be changed?
  unsigned varport = var_sport || var_dport || enumerate_ports; // derived logical value: at least one port has to be changed?

  if ( !varport ) {
    // Implementation of multiple IP addresses (own idea) only, 
    // optimized code for using hard coded fix port numbers as defined in RFC 2544 https://tools.ietf.org/html/rfc2544#appendix-C.2.6.4

    // N size array is used to resolve the write after send problem. In its elements, the
    // varying 16-bit fields of the source and/or destination IP addresses and for IPv4, header checksum are updated.
    // Note: even if UDP port numbers do not change, UDP chechsum changes due to the pseudo-header.

    int i; // cycle variable to index the N size array: takes {0..N-1} values
    struct rte_mbuf *fg_pkt_mbuf[N], *bg_pkt_mbuf[N], *pkt_mbuf; // pointers of message buffers for fg. and bg. Test Frames
    uint8_t *pkt; // working pointer to the current frame (in the message buffer)
    uint8_t *fg_src_ip[N], *fg_dst_ip[N], *fg_ipv4_chksum[N], *bg_src_ip[N], *bg_dst_ip[N]; // pointers to the given 16-bit fields to be modified
    uint8_t *fg_udp_chksum[N], *bg_udp_chksum[N]; // pointers to the given fields
    uint16_t *ip_src, *ip_dst, *ipv4_chksum; // working pointers to the given 16-bit fields to be modified
    uint16_t *udp_chksum; // working pointers to the given fields to be modified
    uint16_t fg_ipv4_chksum_start;  // starting values (uncomplemented checksums taken from the original frames)
    uint16_t fg_udp_chksum_start, bg_udp_chksum_start;  // starting values (uncomplemented checksums taken from the original frames)
    uint32_t v4_chksum; // temporary variable: stores the starting value from the packet
    uint32_t ip_chksum; // temporary variable: stores only the accumulated value from the 16-bit IP address fields
    uint32_t chksum; // temporary variable for UDP checksum calculation (store both)
    uint16_t src_ip, dst_ip; // values of source and destination IP addresses -- to be preserved, when increase or decrease is done 
    uint16_t e_sip, e_dip; // values of source and destination IP addresses -- to be preserved, used for IP addresses enumeration of foreground traffic
    uint16_t sip, dip; // values of source and destination IP addresses -- temporary values
    
    for ( i=0; i<N; i++ ) {
      // create foreground Test Frame
      if ( ip_version == 4 ) {
        fg_pkt_mbuf[i] = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, dst_ipv4, var_sport, var_dport);
        pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
        fg_src_ip[i] = pkt + 26 + src_ipv4_offset;  // 14+12: source IPv4 address, the 16-bits to be modified
        fg_dst_ip[i] = pkt + 30 + dst_ipv4_offset;  // 14+16: destination IPv4 address, the 16-bits to be modified
        fg_ipv4_chksum[i] = pkt + 24;               // 14+10: IPv4 header checksum
        fg_udp_chksum[i] = pkt + 40;   // 14+20+6: UDP checksum 
      } else { // IPv6
        fg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, var_sport, var_dport);
        pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
        fg_src_ip[i] = pkt + 22 + src_ipv6_offset;  // 14+8:  source IPv6 address, the 16-bits to be modified
        fg_dst_ip[i] = pkt + 38 + dst_ipv6_offset;  // 14+24: destination IPv6 address, the 16-bits to be modified
        fg_udp_chksum[i] = pkt + 60;   // 14+40+6: UDP checksum
      }
      if ( ip_version == 4 )
        fg_ipv4_chksum_start = ~*(uint16_t *)fg_ipv4_chksum[i]; // save the uncomplemented checksum value (same for all values of "i") 
      fg_udp_chksum_start = ~*(uint16_t *)fg_udp_chksum[i]; // save the uncomplemented checksum value (same for all values of "i") 
      // create backround Test Frame (always IPv6)
      bg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg, var_sport, var_dport);
      pkt = rte_pktmbuf_mtod(bg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
      bg_src_ip[i] = pkt + 22 + src_ipv6_offset;  // 14+8:  source IPv6 address, the 16-bits to be modified
      bg_dst_ip[i] = pkt + 38 + dst_ipv6_offset;  // 14+24: destination IPv6 address, the 16-bits to be modified
      bg_udp_chksum[i] = pkt + 60;   // 14+40+6: UDP checksum
      bg_udp_chksum_start = ~*(uint16_t *)bg_udp_chksum[i]; // save the uncomplemented checksum value (same for all values of "i")
    } 

      // prepare for the different kinds of IP address enumarations
      switch ( enumerate_ips ) {
        case 0: // no IP address enumeration is done
          // set the starting values of the changing 16-bit fields of IP addresses, if they are increased or decreased
          if ( var_sip == 1 )
            src_ip = sip_min; 
          if ( var_sip == 2 )
            src_ip = sip_max;
          if ( var_dip == 1 )
            dst_ip = dip_min; 
          if ( var_dip == 2 )
            dst_ip = dip_max;
          break;
        case 1: // IP addresses are enumerated in increasing order
          e_sip = sip_min;
          e_dip = dip_min;
          break;
        case 2: // IP addresses are enumerated in decreasing order
          e_sip = sip_max;
          e_dip = dip_max;
          break;
        case 3:
          if ( !uniqueIpComb )
            rte_exit(EXIT_FAILURE, "Error: Initiator/Sender received a NULL pointer to the array of pre-prepaired unique random IP addresses!\n");
          // unique pseudorandom IP address pairs are guarandteed by pre-prepaired random permutation
          break;
      }

    // prepare random number infrastructure
    thread_local std::random_device rd;  // Will be used to obtain a seed for the random number engines
    thread_local std::mt19937_64 gen_sip(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
    thread_local std::mt19937_64 gen_dip(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<int> uni_dis_sip(sip_min, sip_max);     // uniform distribution in [sip_min, sip_max]
    std::uniform_int_distribution<int> uni_dis_dip(dip_min, dip_max);     // uniform distribution in [dip_min, dip_max]

    // naive sender version: it is simple and fast
    i=0; // increase maunally after each sending
    for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
      // set the temporary variables (including several pointers) to handle the right pre-generated Test Frame
      if ( fg_frame = sent_frames % n  < m ) {
        // foreground frame is to be sent
        v4_chksum = fg_ipv4_chksum_start; // rubbish, if IPv6, but a branch instruction is spared
        ip_src = (uint16_t *)fg_src_ip[i];
        ip_dst = (uint16_t *)fg_dst_ip[i];
        ipv4_chksum = (uint16_t *)fg_ipv4_chksum[i]; // rubbish, if IPv6, but a branch instruction is spared
        chksum = fg_udp_chksum_start;
        udp_chksum = (uint16_t *)fg_udp_chksum[i];
        pkt_mbuf = fg_pkt_mbuf[i];
      } else {
        // background frame is to be sent, it is surely IPv6
        ip_src = (uint16_t *)bg_src_ip[i];
        ip_dst = (uint16_t *)bg_dst_ip[i];
        chksum = bg_udp_chksum_start;
        udp_chksum = (uint16_t *)bg_udp_chksum[i];
        pkt_mbuf = bg_pkt_mbuf[i];
      }
      ipv4_frame = ip_version == 4 && fg_frame; // precalculated to have it ready when needed
      ip_chksum=0; // first, changes are accumulated here, surely used for UDP checksum and if IPv4 then also for IPv4 checksum
      // from here, we need to handle the fields identified by the temprary variables

      if ( enumerate_ips && fg_frame ) {
        switch ( enumerate_ips ) {
          case 1: // IP addresses are enumerated in increasing order
            // sip is the low order counter, dip is the high order counter
            if ( (sip=e_sip++) == sip_max ) {
              e_sip = sip_min;
              if ( (dip=e_dip++) == dip_max )
                 e_dip = dip_min;
            } else
              dip = e_dip;
            ip_chksum += *ip_src = htons(sip);     // set src IP 16-bit field and add to checksum
            ip_chksum += *ip_dst = htons(dip);     // set dst IP 16-bit field and add to checksum
            break;
          case 2: // port numbers are enumerated in decresing order
            // sip is the low order counter, dip is the high order counter
            if ( (sip=e_sip--) == sip_min ) {
              e_sip = sip_max;
              if ( (dip=e_dip--) == dip_min )
                 e_dip = dip_max;
            } else
              dip = e_dip;
            ip_chksum += *ip_src = htons(sip);     // set src IP 16-bit field and add to checksum
            ip_chksum += *ip_dst = htons(dip);     // set dst IP 16-bit field and add to checksum
            break;
          case 3: // the next unique pseudorandom IP address pair is taken
            sip = uniqueIC->field.src; // read source IP address
            dip = uniqueIC->field.dst; // read destination IP address
            uniqueIC++;               // increase pointer: no check needed, we have surely enough
            ip_chksum += *ip_src = htons(sip);     // set src IP 16-bit field and add to checksum
            ip_chksum += *ip_dst = htons(dip);     // set dst IP 16-bit field and add to checksum
            break;
        } // end of switch
      } else {
        // IP addresses are handled as before (there is no enumeration)
        if ( var_sip ) {
          // source IP is varying
          switch ( var_sip ) {
            case 1:                   // increasing 
              if ( (sip=src_ip++) == sip_max )
                src_ip = sip_min;
              break;
            case 2:                   // decreasing 
              if ( (sip=src_ip--) == sip_min )
                src_ip = sip_max;
              break;
            case 3:                   // pseudorandom 
              sip = uni_dis_sip(gen_sip);
          }
          ip_chksum += *ip_src = htons(sip);     // set src IP 16-bit field and add to checksum
        }
        if ( var_dip ) {
          // destination IP is varying
          switch ( var_dip ) {
            case 1:                           // increasing
              if ( (dip=dst_ip++) == dip_max )
                dst_ip = dip_min;
              break;
            case 2:                           // decreasing
              if ( (dip=dst_ip--) == dip_min )
                dst_ip = dip_max;
              break;
            case 3:                           // pseudorandom
              dip = uni_dis_dip(gen_dip);
          }
          ip_chksum += *ip_dst = htons(dip);     // set dst IP 16-bit field and add to checksum
        }
      } // end of the else of "if ( enumerate_ips && fg_frame )" 

      if ( ipv4_frame ) {
        // only the IPv4 header contains IP checksum
        v4_chksum += ip_chksum;
        v4_chksum = ((v4_chksum & 0xffff0000) >> 16) + (v4_chksum & 0xffff);   	// calculate 16-bit one's complement sum
        v4_chksum = ((v4_chksum & 0xffff0000) >> 16) + (v4_chksum & 0xffff);   	// twice is enough: 2*0xffff=0x1fffe, 0x1+x0fffe=0xffff
        v4_chksum = (~v4_chksum) & 0xffff;                                  	// make one's complement
        *ipv4_chksum = (uint16_t) v4_chksum;    // set checksum in the frame
      }
      chksum += ip_chksum;  // UDP checksum contains the checksum of IPv4 or IPv6 pseudo header
      chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   	// calculate 16-bit one's complement sum
      chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   	// twice is enough: 2*0xffff=0x1fffe, 0x1+x0fffe=0xffff
      chksum = (~chksum) & 0xffff;                                  	// make one's complement
      if ( ipv4_frame && chksum == 0 )          // over IPv4, checksum should not be 0 (0 means, no checksum is used)
        chksum = 0xffff;
      *udp_chksum = (uint16_t) chksum;          // set checksum in the frame
      // finally, when its time is here, send the frame
      while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); 	// Beware: an "empty" loop, as well as in the next line
      while ( !rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1) ); 		// send out the frame
      i = (i+1) % N;
    } // this is the end of the sending cycle
  } // end of optimized code for fixed port numbers
  else {
    // Implementation of multiple IP addresses (own idea) plus
    // varying port numbers recommended by RFC 4814 https://tools.ietf.org/html/rfc4814#section-4.5
    // RFC 4814 requires pseudorandom port numbers, increasing and decreasing ones are our additional, non-stantard solutions
   
    // N size array is used to resolve the write after send problem. In its elements, the
    // varying 16-bit fields of the source and/or destination IP addresses and for IPv4, header checksum,
    // source and/or destination port number(s) and UDP checksum are updated.
    int i; // cycle variable to index the N size array: takes {0..N-1} values
    struct rte_mbuf *fg_pkt_mbuf[N], *bg_pkt_mbuf[N], *pkt_mbuf; // pointers of message buffers for fg. and bg. Test Frames
    uint8_t *pkt; // working pointer to the current frame (in the message buffer)
    uint8_t *fg_src_ip[N], *fg_dst_ip[N], *fg_ipv4_chksum[N], *bg_src_ip[N], *bg_dst_ip[N]; // pointers to the given 16-bit fields to be modified
    uint8_t *fg_udp_sport[N], *fg_udp_dport[N], *fg_udp_chksum[N], *bg_udp_sport[N], *bg_udp_dport[N], *bg_udp_chksum[N]; // pointers to the given fields
    uint16_t *ip_src, *ip_dst, *ipv4_chksum; // working pointers to the given 16-bit fields to be modified
    uint16_t *udp_sport, *udp_dport, *udp_chksum; // working pointers to the given fields to be modified
    uint16_t fg_ipv4_chksum_start;  // starting values (uncomplemented checksums taken from the original frames)
    uint16_t fg_udp_chksum_start, bg_udp_chksum_start;  // starting values (uncomplemented checksums taken from the original frames)
    uint32_t v4_chksum; // temporary variable: stores the starting value from the packet
    uint32_t ip_chksum; // temporary variable: stores only the accumulated value from the 16-bit IP address fields
    uint32_t chksum; // temporary variable for UDP checksum calculation (store both)
    uint16_t src_ip, dst_ip; // values of source and destination IP addresses -- to be preserved, when increase or decrease is done 
    uint16_t sport, dport; // values of source and destination port numbers -- to be preserved, when increase or decrease is done 
    uint16_t e_sip, e_dip; // values of source and destination IP addresses -- to be preserved, used for IP addresses enumeration of foreground traffic
    uint16_t e_sport, e_dport; // values of source and destination port numbers -- to be preserved, used for port enumeration of foreground traffic
    uint16_t sip, dip; // values of source and destination IP addresses -- temporary values
    uint16_t sp, dp; // values of source and destination port numbers -- temporary values
    
    for ( i=0; i<N; i++ ) {
      // create foreground Test Frame
      if ( ip_version == 4 ) {
        fg_pkt_mbuf[i] = mkTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv4, dst_ipv4, var_sport, var_dport);
        pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
        fg_src_ip[i] = pkt + 26 + src_ipv4_offset;  // 14+12: source IPv4 address, the 16-bits to be modified
        fg_dst_ip[i] = pkt + 30 + dst_ipv4_offset;  // 14+16: destination IPv4 address, the 16-bits to be modified
        fg_ipv4_chksum[i] = pkt + 24;               // 14+10: IPv4 header checksum
        fg_udp_sport[i] = pkt + 34;    // 14+20+0: source UDP port 
        fg_udp_dport[i] = pkt + 36;    // 14+20+2: destination UDP port
        fg_udp_chksum[i] = pkt + 40;   // 14+20+6: UDP checksum 
      } else { // IPv6
        fg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, var_sport, var_dport);
        pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
        fg_src_ip[i] = pkt + 22 + src_ipv6_offset;  // 14+8:  source IPv6 address, the 16-bits to be modified
        fg_dst_ip[i] = pkt + 38 + dst_ipv6_offset;  // 14+24: destination IPv6 address, the 16-bits to be modified
        fg_udp_sport[i] = pkt + 54;    // 14+40+0: source UDP port
        fg_udp_dport[i] = pkt + 56;    // 14+40+2: destination UDP port
        fg_udp_chksum[i] = pkt + 60;   // 14+40+6: UDP checksum
      }
      if ( ip_version == 4 )
        fg_ipv4_chksum_start = ~*(uint16_t *)fg_ipv4_chksum[i]; // save the uncomplemented checksum value (same for all values of "i") 
      fg_udp_chksum_start = ~*(uint16_t *)fg_udp_chksum[i]; // save the uncomplemented checksum value (same for all values of "i") 
      // create backround Test Frame (always IPv6)
      bg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg, var_sport, var_dport);
      pkt = rte_pktmbuf_mtod(bg_pkt_mbuf[i], uint8_t *); // Access the Test Frame in the message buffer
      bg_src_ip[i] = pkt + 22 + src_ipv6_offset;  // 14+8:  source IPv6 address, the 16-bits to be modified
      bg_dst_ip[i] = pkt + 38 + dst_ipv6_offset;  // 14+24: destination IPv6 address, the 16-bits to be modified
      bg_udp_sport[i] = pkt + 54;    // 14+40+0: source UDP port
      bg_udp_dport[i] = pkt + 56;    // 14+40+2: destination UDP port
      bg_udp_chksum[i] = pkt + 60;   // 14+40+6: UDP checksum
      bg_udp_chksum_start = ~*(uint16_t *)bg_udp_chksum[i]; // save the uncomplemented checksum value (same for all values of "i")
    } 

    // prepare for the different kinds of IP address enumarations
    switch ( enumerate_ips ) { 
      case 0: // no IP address enumeration is done
        // set the starting values of the changing 16-bit fields of IP addresses, if they are increased or decreased
        if ( var_sip == 1 )
          src_ip = sip_min; 
        if ( var_sip == 2 )
          src_ip = sip_max;
        if ( var_dip == 1 )
          dst_ip = dip_min; 
        if ( var_dip == 2 )
          dst_ip = dip_max;
        // set the starting values of port numbers, if they are increased or decreased
        if ( var_sport == 1 )
          sport = sport_min; 
        if ( var_sport == 2 )
          sport = sport_max;
        if ( var_dport == 1 )
          dport = dport_min; 
        if ( var_dport == 2 )
          dport = dport_max;
        break;
        case 1: // IP addresses and port numbers are enumerated in increasing order
          e_sip = sip_min;
          e_dip = dip_min;
          e_sport = sport_min;
          e_dport = dport_min;
          break;
        case 2: // IP addresses and port numbers are enumerated in decreasing order
          e_sip = sip_max;
          e_dip = dip_max;
          e_sport = sport_max;
          e_dport = dport_max;
          break;
        case 3:
          if ( !uniqueFtComb )
            rte_exit(EXIT_FAILURE, "Error: Initiator/Sender received a NULL pointer to the array of pre-prepaired unique random IP addresses and port numbers!\n");
          // unique pseudorandom four tuples are guaranteed by pre-prepaired random permutation
          break;
      }

    // prepare random number infrastructure
    thread_local std::random_device rd;  // Will be used to obtain a seed for the random number engines
    thread_local std::mt19937_64 gen_sip(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
    thread_local std::mt19937_64 gen_dip(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<int> uni_dis_sip(sip_min, sip_max);     // uniform distribution in [sip_min, sip_max]
    std::uniform_int_distribution<int> uni_dis_dip(dip_min, dip_max);     // uniform distribution in [dip_min, dip_max]
    thread_local std::mt19937_64 gen_sport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
    thread_local std::mt19937_64 gen_dport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
    std::uniform_int_distribution<int> uni_dis_sport(sport_min, sport_max);	// uniform distribution in [sport_min, sport_max]
    std::uniform_int_distribution<int> uni_dis_dport(dport_min, dport_max);	// uniform distribution in [sport_min, sport_max]

    // naive sender version: it is simple and fast
    i=0; // increase maunally after each sending
    for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
      // set the temporary variables (including several pointers) to handle the right pre-generated Test Frame
      if ( fg_frame = sent_frames % n  < m ) {
        // foreground frame is to be sent
        v4_chksum = fg_ipv4_chksum_start; // rubbish, if IPv6, but a branch instruction is spared
        ip_src = (uint16_t *)fg_src_ip[i];
        ip_dst = (uint16_t *)fg_dst_ip[i];
        ipv4_chksum = (uint16_t *)fg_ipv4_chksum[i]; // rubbish, if IPv6, but a branch instruction is spared
        chksum = fg_udp_chksum_start;
        udp_sport = (uint16_t *)fg_udp_sport[i];
        udp_dport = (uint16_t *)fg_udp_dport[i];
        udp_chksum = (uint16_t *)fg_udp_chksum[i];
        pkt_mbuf = fg_pkt_mbuf[i];
      } else {
        // background frame is to be sent, it is surely IPv6
        ip_src = (uint16_t *)bg_src_ip[i];
        ip_dst = (uint16_t *)bg_dst_ip[i];
        chksum = bg_udp_chksum_start;
        udp_sport = (uint16_t *)bg_udp_sport[i];
        udp_dport = (uint16_t *)bg_udp_dport[i];
        udp_chksum = (uint16_t *)bg_udp_chksum[i];
        pkt_mbuf = bg_pkt_mbuf[i];
      }
      ipv4_frame = ip_version == 4 && fg_frame; // precalculated to have it ready when needed
      ip_chksum=0; // first, changes are accumulated here, surely used for UDP checksum and if IPv4 then also for IPV4 checksum
      // from here, we need to handle the fields identified by the temprary variables

      if ( enumerate_ips && fg_frame ) { // Note: enumerate_ips == enumerate_ports is guaranteed here.
        switch ( enumerate_ips ) {
          case 1: // IP addresses and port numbers are enumerated in increasing order
            // order of fields from most significant one to least significant one: dip, sip, dp, sp
            if ( (sp=e_sport++) == sport_max ) {
              e_sport = sport_min;
              if ( (dp=e_dport++) == dport_max ) {
                 e_dport = dport_min;
                 if ( (sip=e_sip++) == sip_max ) {
                   e_sip = sip_min;
                   if ( (dip=e_dip++) == dip_max )
                     e_dip = dip_min;
                 } else
                   dip = e_dip;
              } else
                sip = e_sip, dip = e_dip;
            } else
              dp = e_dport, sip = e_sip, dip = e_dip;
            break;
          case 2:
            // order of fields from most significant one to least significant one: dip, sip, dp, sp
            if ( (sp=e_sport--) == sport_min ) {
              e_sport = sport_max;
              if ( (dp=e_dport--) == dport_min ) {
                 e_dport = dport_max;
                 if ( (sip=e_sip--) == sip_min ) {
                   e_sip = sip_max;
                   if ( (dip=e_dip--) == dip_min )
                     e_dip = dip_max;
                 } else
                   dip = e_dip;
              } else
                sip = e_sip, dip = e_dip;
            } else
              dp = e_dport, sip = e_sip, dip = e_dip;
            break;
          case 3: // the next unique pseudorandom IP address and port number combination is taken
            sip = uniqueFC->ft.sip;	// read source IP address
            dip = uniqueFC->ft.dip;	// read destination IP address
            sp = uniqueFC->ft.sport;	// read source port number
            dp = uniqueFC->ft.dport; 	// read destination port number
            uniqueFC++;               	// increase pointer: no check needed, we have surely enough
            break;
        } // end of switch
        ip_chksum += *ip_src = htons(sip);  // set src IP 16-bit field and add to checksum
        ip_chksum += *ip_dst = htons(dip);  // set dst IP 16-bit field and add to checksum
        chksum += *udp_sport = htons(sp);   // set source port and add to checksum 
        chksum += *udp_dport = htons(dp);   // set destination port and add to checksum 
      } else {
        // IP addresses and port numbers are handled as before (there is no enumeration)
        if ( var_sip ) {
          // source IP is varying
          switch ( var_sip ) {
            case 1:                   // increasing 
              if ( (sip=src_ip++) == sip_max )
                src_ip = sip_min;
              break;
            case 2:                   // decreasing 
              if ( (sip=src_ip--) == sip_min )
                src_ip = sip_max;
              break;
            case 3:                   // pseudorandom 
              sip = uni_dis_sip(gen_sip);
          }
          ip_chksum += *ip_src = htons(sip);     // set src IP 16-bit field and add to checksum
        }
        if ( var_dip ) {
          // destination IP is varying
          switch ( var_dip ) {
            case 1:                           // increasing
              if ( (dip=dst_ip++) == dip_max )
                dst_ip = dip_min;
              break;
            case 2:                           // decreasing
              if ( (dip=dst_ip--) == dip_min )
                dst_ip = dip_max;
              break;
            case 3:                           // pseudorandom
              dip = uni_dis_dip(gen_dip);
          }
          ip_chksum += *ip_dst = htons(dip);     // set dst IP 16-bit field and add to checksum
        }
        if ( var_sport ) {
          // sport is varying
          switch ( var_sport ) {
            case 1:                   // increasing port numbers
              if ( (sp=sport++) == sport_max )
                sport = sport_min;
              break;
            case 2:                   // decreasing port numbers
              if ( (sp=sport--) == sport_min )
                sport = sport_max;
              break;
            case 3:                   // pseudorandom port numbers
              sp = uni_dis_sport(gen_sport);
          }
          chksum += *udp_sport = htons(sp);     // set source port and add to checksum -- corrected
        }
        if ( var_dport ) {
          // dport is varying
          switch ( var_dport ) {
            case 1:                           // increasing port numbers
              if ( (dp=dport++) == dport_max )
                dport = dport_min;
              break;
            case 2:                           // decreasing port numbers
              if ( (dp=dport--) == dport_min )
                dport = dport_max;
              break;
            case 3:                           // pseudorandom port numbers
              dp = uni_dis_dport(gen_dport);
          }
          chksum += *udp_dport = htons(dp);     // set destination port add to checksum -- corrected
        }
      } // end of the else of "if ( enumerate_ips && fg_frame )"

      if ( ipv4_frame ) {
        // only the IPv4 header contains IP checksum
        v4_chksum += ip_chksum;
        v4_chksum = ((v4_chksum & 0xffff0000) >> 16) + (v4_chksum & 0xffff);   	// calculate 16-bit one's complement sum
        v4_chksum = ((v4_chksum & 0xffff0000) >> 16) + (v4_chksum & 0xffff);   	// twice is enough: 2*0xffff=0x1fffe, 0x1+x0fffe=0xffff
        v4_chksum = (~v4_chksum) & 0xffff;                                  	// make one's complement
        *ipv4_chksum = (uint16_t) v4_chksum;            // set checksum in the frame
      }
      chksum += ip_chksum;  // UDP checksum contains the checksum of IPv4 or IPv6 pseudo header
      chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   	// calculate 16-bit one's complement sum
      chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   	// twice is enough: 2*0xffff=0x1fffe, 0x1+x0fffe=0xffff
      chksum = (~chksum) & 0xffff;                                  	// make one's complement
      if ( ipv4_frame && chksum == 0)          // over IPv4, checksum should not be 0 (0 means, no checksum is used)
        chksum = 0xffff;
      *udp_chksum = (uint16_t) chksum;            // set checksum in the frame
      // finally, when its time is here, send the frame
      while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); 	// Beware: an "empty" loop, as well as in the next line
      while ( !rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1) ); 		// send out the frame
      i = (i+1) % N;
    } // this is the end of the sending cycle

  } // end of implementation of varying port numbers 
  // Now, we check the time
  uint64_t elapsed_tsc = rte_rdtsc()-start_tsc;
  printf("Info: %s sender's sending took %3.10lf seconds.\n", side, (double)elapsed_tsc/hz);
  // this is a preliminary test, 'duration' is not valid
  if ( elapsed_tsc > hz*frames_to_send/frame_rate*TOLERANCE )
    printf("Warning: %s sending was too slow (only %3.10lf percent of required rate), the test is invalid.\n", side,
             100.0*frames_to_send/elapsed_tsc*hz/frame_rate);
  else
    printf("%s frames sent: %lu\n", side, sent_frames);

  if ( uniqueIpComb )
    rte_free(uniqueIpComb);   // free the array for pre-generated unique IP address combinations
  if ( uniqueFtComb )
    rte_free(uniqueFtComb);   // free the array for pre-generated unique 4-tuple combinations
  return 0;

}
// Responder/Sender: sends Test Frames for throughput (or frame loss rate) measurement
int rsend(void *par) {
  // collecting input parameters:
  class rSenderParameters *p = (class rSenderParameters *)par;
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

  unsigned state_table_size = p->state_table_size;
  atomicFourTuple *stateTable = p->stateTable;
  unsigned responder_tuples = p->responder_tuples;

  // further local variables
  uint64_t frames_to_send = duration * frame_rate;	// Each active sender sends this number of frames
  uint64_t sent_frames=0; // counts the number of sent frames
  double elapsed_seconds; // for checking the elapsed seconds during sending

  unsigned index;   	// current state table index for reading a 4-tuple (used when 'responder-ports' is 1 or 2)
  fourTuple ft;		// 4-tuple is read from the state table into this 
  bool fg_frame, ipv4_frame; // the current frame belongs to the foreground traffic: will be handled in a stateful way (if it is IPv4)

  if ( !responder_tuples ) {
    // optimized code for using a single 4-tuple taken from the very first preliminary frame (as foreground traffic)
    // ( similar to using hard coded fix port numbers as defined in RFC 2544 https://tools.ietf.org/html/rfc2544#appendix-C.2.6.4 )
    ft=stateTable[0];	// read only once
    if ( num_dest_nets == 1 ) { 	
      // optimized code for single destination network: always the same foreground or background frame is sent, no arrays are used
      struct rte_mbuf *fg_pkt_mbuf, *bg_pkt_mbuf; // message buffers for fg. and bg. Test Frames
      // create foreground Test Frame
      if ( ip_version == 4 )
        fg_pkt_mbuf = mkFinalTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, &ft.resp_addr, &ft.init_addr, ft.resp_port, ft.init_port);
      else  // IPv6 -- stateful operation is not yet supported!
        fg_pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, 0, 0);
  
      // create backround Test Frame (always IPv6)
      bg_pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, dst_bg, 0, 0);
  
      // naive sender version: it is simple and fast
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); // Beware: an "empty" loop, and further two will come!
        if ( sent_frames % n  < m )
          while ( !rte_eth_tx_burst(eth_id, 0, &fg_pkt_mbuf, 1) ); // send foreground frame
        else
           while ( !rte_eth_tx_burst(eth_id, 0, &bg_pkt_mbuf, 1) ); // send background frame
      } // this is the end of the sending cycle
    } // end of optimized code for single destination network
    else {
      // optimized code for multiple destination networks -- only regarding background traffic! 
      // always the same foreground frame is sent!
      // background frames are generated for each network and pointers are stored in arrays
      // assertion: num_dest_nets <= 256 
      struct rte_mbuf *fg_pkt_mbuf, *bg_pkt_mbuf[256]; // message buffers for fg. and bg. Test Frames
      in6_addr curr_dst_bg; 	// backround IPv6 destination address, which will be changed
      int i; 			// cycle variable for grenerating different destination network addresses

      curr_dst_bg = *dst_bg;
  
      // create foreground Test Frame
      if ( ip_version == 4 ) {
        fg_pkt_mbuf = mkFinalTestFrame4(ipv4_frame_size, pkt_pool, side, dst_mac, src_mac, &ft.resp_addr, &ft.init_addr, ft.resp_port, ft.init_port);
      }
      else { // IPv6 -- stateful operation is not yet supported!
        fg_pkt_mbuf = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, 0, 0);
      } 

      for ( i=0; i<num_dest_nets; i++ ) { 
        // create backround Test Frames (always IPv6)
        ((uint8_t *)&curr_dst_bg)[7] = (uint8_t) i; // see comment above
        bg_pkt_mbuf[i] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_bg, &curr_dst_bg, 0, 0);
      }
   
      // random number infrastructure is taken from: https://en.cppreference.com/w/cpp/numeric/random/uniform_int_distribution
      // MT64 is used because of https://medium.com/@odarbelaeze/how-competitive-are-c-standard-random-number-generators-f3de98d973f0
      // thread_local is used on the basis of https://stackoverflow.com/questions/40655814/is-mersenne-twister-thread-safe-for-cpp
      thread_local std::random_device rd;  // Will be used to obtain a seed for the random number engine
      thread_local std::mt19937_64 gen(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      std::uniform_int_distribution<int> uni_dis_net(0, num_dest_nets-1);	// uniform distribution in [0, num_dest_nets-1]
  
      // naive sender version: it is simple and fast
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); // Beware: an "empty" loop, and further two will come!
        if ( sent_frames % n  < m )
          while ( !rte_eth_tx_burst(eth_id, 0, &fg_pkt_mbuf, 1) ); // send foreground frame
        else {
          int net_index = uni_dis_net(gen);	// index of the pre-generated frame
          while ( !rte_eth_tx_burst(eth_id, 0, &bg_pkt_mbuf[net_index], 1) ); // send background frame
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
    uint32_t ipv4_zero = 0;	// IPv4 address 0.0.0.0 used as a placeholder for UDP checksum calculation (value will be set later)
    if ( num_dest_nets == 1 ) {
      // optimized code for single destination network: always one of the same N pre-prepared foreground or background frames is updated and sent, 
      // N size arrays are used to resolve the write after send problem.
      // IPv4 addresses, source and/or destination port number(s) and UDP checksum are updated in the actually used copy.
      int i; // cycle variable for the above mentioned purpose: takes {0..N-1} values
      struct rte_mbuf *fg_pkt_mbuf[N], *bg_pkt_mbuf[N], *pkt_mbuf; // pointers of message buffers for fg. and bg. Test Frames
      uint8_t *pkt; // working pointer to the current frame (in the message buffer)
      uint8_t *fg_udp_sport[N], *fg_udp_dport[N], *fg_udp_chksum[N], *bg_udp_sport[N], *bg_udp_dport[N], *bg_udp_chksum[N]; // pointers to the given fields
      uint8_t *fg_rte_ipv4_hdr[N], *fg_ipv4_chksum[N], *fg_ipv4_src[N], *fg_ipv4_dst[N]; // further ones for stateful tests
      rte_ipv4_hdr *rte_ipv4_hdr_start; // used for IPv4 header checksum calculation
      uint16_t *udp_sport, *udp_dport, *udp_chksum, *ipv4_chksum; // working pointers to the given fields
      uint32_t *ipv4_src, *ipv4_dst; // further ones for stateful tests
      uint16_t fg_udp_chksum_start, bg_udp_chksum_start;  // starting values (uncomplemented checksums taken from the original frames)
      uint32_t chksum; // temporary variable for checksum calculation
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
      std::uniform_int_distribution<int> uni_dis_sport(sport_min, sport_max);	// uniform distribution in [sport_min, sport_max]
      std::uniform_int_distribution<int> uni_dis_dport(dport_min, dport_max);	// uniform distribution in [sport_min, sport_max]
      std::uniform_int_distribution<unsigned> uni_dis_index(0, state_table_size-1); // uniform distribution in [0, state_table_size-1]

      // naive sender version: it is simple and fast
      i=0; // increase maunally after each sending
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        // set the temporary variables (including several pointers) to handle the right pre-generated Test Frame
        if ( fg_frame = sent_frames % n  < m ) {
          // foreground frame is to be sent
          chksum = fg_udp_chksum_start;
          udp_sport = (uint16_t *)fg_udp_sport[i];
          udp_dport = (uint16_t *)fg_udp_dport[i];
          udp_chksum = (uint16_t *)fg_udp_chksum[i];
	  rte_ipv4_hdr_start = (rte_ipv4_hdr *)fg_rte_ipv4_hdr[i];
	  ipv4_chksum = (uint16_t *) fg_ipv4_chksum[i];
	  ipv4_src = (uint32_t *)fg_ipv4_src[i];	// this is rubbish if IP version is 6
	  ipv4_dst = (uint32_t *)fg_ipv4_dst[i];	// this is rubbish if IP version is 6 
          pkt_mbuf = fg_pkt_mbuf[i];
        } else {
          // background frame is to be sent
          chksum = bg_udp_chksum_start;
          udp_sport = (uint16_t *)bg_udp_sport[i];
          udp_dport = (uint16_t *)bg_udp_dport[i];
          udp_chksum = (uint16_t *)bg_udp_chksum[i];
          pkt_mbuf = bg_pkt_mbuf[i];
        }
        ipv4_frame = ip_version == 4 && fg_frame; // precalculated to have it ready when needed

        // from here, we need to handle the frame identified by the temprary variables
        if ( ipv4_frame ) {
	  // this frame is handled in a stateful way
	  switch ( responder_tuples ) { 			// here, it is surely not 0
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
          chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   // calculate 16-bit one's complement sum
          chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   // twice is enough: 2*0xffff=0x1fffe, 0x1+x0fffe=0xffff
	  chksum = (~chksum) & 0xffff;                                  // make one's complement
	  if (chksum == 0)                      // this is an IPv4 frame, checksum should not be 0 (0 means, no checksum is used)
	    chksum = 0xffff;
	  *udp_chksum = (uint16_t) chksum;      // set checksum in the frame
	  *ipv4_chksum = 0;        		// IPv4 header checksum is set to 0 
	  *ipv4_chksum = rte_ipv4_cksum(rte_ipv4_hdr_start);        // IPv4 header checksum is set now
	  // this is the end of handling the frame in a stateful way
	} else {
	  // this frame is handled in the old way
          if ( var_sport ) {
            // sport is varying
            switch ( var_sport ) {
              case 1:                   // increasing port numbers
                if ( (sp=sport++) == sport_max )
                  sport = sport_min;
                break;
              case 2:                   // decreasing port numbers
                if ( (sp=sport--) == sport_min )
                  sport = sport_max;
                break;
              case 3:                   // pseudorandom port numbers
                sp = uni_dis_sport(gen_sport);
            }
            chksum += *udp_sport = htons(sp);     // set source port and add to checksum -- corrected
          }
          if ( var_dport ) {
            // dport is varying
            switch ( var_dport ) {
              case 1:                           // increasing port numbers
                if ( (dp=dport++) == dport_max )
                  dport = dport_min;
                break;
              case 2:                           // decreasing port numbers
                if ( (dp=dport--) == dport_min )
                  dport = dport_max;
                break;
              case 3:                           // pseudorandom port numbers
                dp = uni_dis_dport(gen_dport);
            }
            chksum += *udp_dport = htons(dp);     // set destination port add to checksum -- corrected
          }
          chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);  	// calculate 16-bit one's complement sum
          chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   // twice is enough: 2*0xffff=0x1fffe, 0x1+x0fffe=0xffff
          chksum = (~chksum) & 0xffff;                                  // make one's complement
          // Note: this is the else of "if ( ipv4_frame )", thus this is an IPv6 frame, its checksum may be 0.
          *udp_chksum = (uint16_t) chksum;            // set checksum in the frame
  	  // this is the end of handling the frame in the old way
	}

        // finally, when its time is here, send the frame
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate ); 	// Beware: an "empty" loop, as well as in the next line
        while ( !rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1) ); 		// send out the frame
        i = (i+1) % N;
      } // this is the end of the sending cycle
    } // end of optimized code for single destination network
    else { 
      // optimized code for multiple destination networks:
      // N copies of foreground frames are prepared, and N cpopies of background frames for each destination network are prepared,
      // N size arrays are used to resolve the write after send problem
      // IPv4 addresses, source and/or destination port number(s) and UDP checksum are updated in the actually used copy before sending
      // assertion: num_dest_nets <= 256
      int j; // cycle variable to index the N size array: takes {0..N-1} values
      struct rte_mbuf *fg_pkt_mbuf[N], *bg_pkt_mbuf[256][N], *pkt_mbuf; // pointers of message buffers for fg. and bg. Test Frames
      uint8_t *pkt; // working pointer to the current frame (in the message buffer)
      uint8_t *fg_udp_sport[N], *fg_udp_dport[N], *fg_udp_chksum[N]; // pointers to the given fields of the pre-prepared Test Frames
      uint8_t *bg_udp_sport[256][N], *bg_udp_dport[256][N], *bg_udp_chksum[256][N]; // pointers to the given fields of the pre-prepared Test Frames
      uint8_t *fg_rte_ipv4_hdr[N], *fg_ipv4_chksum[N], *fg_ipv4_src[N], *fg_ipv4_dst[N]; // further ones for stateful tests, but not per dest. networks!
      rte_ipv4_hdr *rte_ipv4_hdr_start; // used for IPv4 header checksum calculation
      uint16_t *udp_sport, *udp_dport, *udp_chksum, *ipv4_chksum; // working pointers to the given fields
      uint32_t *ipv4_src, *ipv4_dst; // further ones for stateful tests
      uint16_t fg_udp_chksum_start, bg_udp_chksum_start[256];  // starting values (uncomplemented checksums taken from the original frames)
      uint32_t chksum; // temporary variable for checksum calculation
      uint16_t sport, dport; // values of source and destination port numbers -- to be preserved, when increase or decrease is done
      uint16_t sp, dp; // values of source and destination port numbers -- temporary values
      in6_addr curr_dst_bg;       // backround IPv6 destination address, which will be changed
      int i;                      // cycle variable for grenerating different destination network addresses
 
      curr_dst_bg = *dst_bg;

      // create Test Frames
      for ( j=0; j<N; j++ ) {
        // create foreground Test Frame (IPv4 or IPv6)
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
        } else { // IPv6 -- stateful operation is not yet supported!
          fg_pkt_mbuf[j] = mkTestFrame6(ipv6_frame_size, pkt_pool, side, dst_mac, src_mac, src_ipv6, dst_ipv6, var_sport, var_dport);
          pkt = rte_pktmbuf_mtod(fg_pkt_mbuf[j], uint8_t *); // Access the Test Frame in the message buffer
          fg_udp_sport[j] = pkt + 54;
          fg_udp_dport[j] = pkt + 56;
          fg_udp_chksum[j] = pkt + 60;
        }
        fg_udp_chksum_start = ~*(uint16_t *)fg_udp_chksum[j]; // save the uncomplemented checksum value (same for all values of "j")

        // create backround Test Frames for all destination networks (always IPv6)
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
      thread_local std::mt19937_64 gen_net(rd()); //Standard 64-bit mersenne_twister_engine seeded with rd()
      thread_local std::mt19937_64 gen_sport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      thread_local std::mt19937_64 gen_dport(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      thread_local std::mt19937_64 gen_index(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
      std::uniform_int_distribution<int> uni_dis_net(0, num_dest_nets-1);     // uniform distribution in [0, num_dest_nets-1]
      std::uniform_int_distribution<int> uni_dis_sport(sport_min, sport_max);   // uniform distribution in [sport_min, sport_max]
      std::uniform_int_distribution<int> uni_dis_dport(dport_min, dport_max);   // uniform distribution in [sport_min, sport_max]
      std::uniform_int_distribution<unsigned> uni_dis_index(0, state_table_size-1); // uniform distribution in [0, state_table_size-1]

      // naive sender version: it is simple and fast
      j=0; // increase maunally after each sending
      for ( sent_frames = 0; sent_frames < frames_to_send; sent_frames++ ){ // Main cycle for the number of frames to send
        // set the temporary variables (including several pointers) to handle the right pre-generated Test Frame
        if ( fg_frame = sent_frames % n  < m ) {
          // foreground frame is to be sent
          chksum = fg_udp_chksum_start;
          udp_sport = (uint16_t *)fg_udp_sport[j];
          udp_dport = (uint16_t *)fg_udp_dport[j];
          udp_chksum = (uint16_t *)fg_udp_chksum[j];
          rte_ipv4_hdr_start = (rte_ipv4_hdr *)fg_rte_ipv4_hdr[j];
          ipv4_chksum = (uint16_t *) fg_ipv4_chksum[j];
          ipv4_src = (uint32_t *)fg_ipv4_src[j];        // this is rubbish if IP version is 6
          ipv4_dst = (uint32_t *)fg_ipv4_dst[j];        // this is rubbish if IP version is 6
          pkt_mbuf = fg_pkt_mbuf[j];
        } else {
          // background frame is to be sent
          int net_index = uni_dis_net(gen_net); // index of the pre-generated Test Frame for the given destination network
          chksum = bg_udp_chksum_start[net_index];
          udp_sport = (uint16_t *)bg_udp_sport[net_index][j];
          udp_dport = (uint16_t *)bg_udp_dport[net_index][j];
          udp_chksum = (uint16_t *)bg_udp_chksum[net_index][j];
          pkt_mbuf = bg_pkt_mbuf[net_index][j];
        }
        ipv4_frame = ip_version == 4 && fg_frame; // precalculated to have it ready when needed

        // from here, we need to handle the frame identified by the temprary variables
        if ( ipv4_frame ) {
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
          chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   // calculate 16-bit one's complement sum
          chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   // twice is enough: 2*0xffff=0x1fffe, 0x1+x0fffe=0xffff
          chksum = (~chksum) & 0xffff;                                  // make one's complement
          if (chksum == 0)                      // this is an IPv4 frame, checksum should not be 0 (0 means, no checksum is used)
            chksum = 0xffff;
          *udp_chksum = (uint16_t) chksum;      // set checksum in the frame
          *ipv4_chksum = 0;                             // IPv4 header checksum is set to 0
          *ipv4_chksum = rte_ipv4_cksum(rte_ipv4_hdr_start);        // IPv4 header checksum is set now
          // this is the end of handling the frame in a stateful way
        } else {
          // this frame is handled in the old way
          if ( var_sport ) {
            // sport is varying
            switch ( var_sport ) {
              case 1:                   // increasing port numbers
                if ( (sp=sport++) == sport_max )
                  sport = sport_min;
                break;
              case 2:                   // decreasing port numbers
                if ( (sp=sport--) == sport_min )
                  sport = sport_max;
                break;
              case 3:                   // pseudorandom port numbers
                sp = uni_dis_sport(gen_sport);
            }
            chksum += *udp_sport = htons(sp);     // set source port and add to checksum -- corrected
          }
          if ( var_dport ) {
            // dport is varying
            switch ( var_dport ) {
              case 1:                           // increasing port numbers
                if ( (dp=dport++) == dport_max )
                  dport = dport_min;
                break;
              case 2:                           // decreasing port numbers
                if ( (dp=dport--) == dport_min )
                  dport = dport_max;
                break;
              case 3:                           // pseudorandom port numbers
                dp = uni_dis_dport(gen_dport);
            }
            chksum += *udp_dport = htons(dp);     // set destination port add to checksum -- corrected
          }
          chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   // calculate 16-bit one's complement sum
          chksum = ((chksum & 0xffff0000) >> 16) + (chksum & 0xffff);   // twice is enough: 2*0xffff=0x1fffe, 0x1+x0fffe=0xffff
          chksum = (~chksum) & 0xffff;                                  // make one's complement
          // Note: this is the else of "if ( ipv4_frame )", thus this is an IPv6 frame, its checksum may be 0.
          *udp_chksum = (uint16_t) chksum;            // set checksum in the frame
          // this is the end of handling the frame in the old way
	}

        // finally, when its time is here, send the frame
        while ( rte_rdtsc() < start_tsc+sent_frames*hz/frame_rate );    // Beware: an "empty" loop, as well as in the next line
        while ( !rte_eth_tx_burst(eth_id, 0, &pkt_mbuf, 1) );           // send out the frame
        j = (j+1) % N;
      } // this is the end of the sending cycle
    } // end of the optimized code for multiple destination networks
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

// receives Test Frames for throughput (or frame loss rate) measurements
// Offsets from the start of the Ethernet Frame:
// EtherType: 6+6=12
// IPv6 Next header: 14+6=20, UDP Data for IPv6: 14+40+8=62
// IPv4 Protolcol: 14+9=23, UDP Data for IPv4: 14+20+8=42
int receive(void *par) {
  // collecting input parameters:
  class receiverParameters *p = (class receiverParameters *)par;
  uint64_t finish_receiving = p->finish_receiving;
  uint16_t eth_id = p->eth_id;
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

// Responder/Receiver: receives Preliminary or Test Frames for throughput (or frame loss rate) measurement
// Offsets from the start of the Ethernet Frame:
// EtherType: 6+6=12
// IPv6 Next header: 14+6=20, UDP Data for IPv6: 14+40+8=62
// IPv4 Protocol: 14+9=23, UDP Data for IPv4: 14+20+8=42
int rreceive(void *par) {
  // collecting input parameters:
  class rReceiverParameters *p = (class rReceiverParameters *)par;
  uint64_t finish_receiving = p->finish_receiving;
  uint16_t eth_id = p->eth_id;
  const char *side = p->side;
  unsigned state_table_size = p->state_table_size;
  unsigned *valid_entries = p->valid_entries;
  atomicFourTuple **stateTable = p->stateTable;

  atomicFourTuple *stTbl;		// state table 
  unsigned index = 0; 			// state table index: first write will happen to this position
  fourTuple four_tuple;			// 4-tuple for collecting IPv4 addresses and port numbers

  // further local variables
  int frames, i;
  struct rte_mbuf *pkt_mbufs[MAX_PKT_BURST]; // pointers for the mbufs of received frames
  uint16_t ipv4=htons(0x0800); // EtherType for IPv4 in Network Byte Order
  uint16_t ipv6=htons(0x86DD); // EtherType for IPv6 in Network Byte Order
  uint8_t identify[8]= { 'I', 'D', 'E', 'N', 'T', 'I', 'F', 'Y' };	// Identificion of the Test Frames
  uint64_t *id=(uint64_t *) identify;
  uint64_t fg_received=0, bg_received=0;	// number of received (fg, bg) frames (counted separetely)

  if ( !*valid_entries ) {
    // This is preliminary phase: state table is allocated from the memory of this NUMA node
    stTbl = (atomicFourTuple *) rte_malloc("Responder/Receiver's state table", (sizeof(atomicFourTuple))*state_table_size, 128);
    if ( !stTbl )
      rte_exit(EXIT_FAILURE, "Error: Responder/Receiver can't allocate memory for state table!\n");
    *stateTable = stTbl;		// return the address of the state table
  } else { 
    // A real test is performed now: we use the previously allocated state table
    stTbl = *stateTable; 
  }

  // frames are received and their four tuples are recorded
  while ( rte_rdtsc() < finish_receiving ){
    frames = rte_eth_rx_burst(eth_id, 0, pkt_mbufs, MAX_PKT_BURST);
    for (i=0; i < frames; i++){
      uint8_t *pkt = rte_pktmbuf_mtod(pkt_mbufs[i], uint8_t *); // Access the Test Frame in the message buffer
      // check EtherType at offset 12: IPv6, IPv4, or anything else
      if ( *(uint16_t *)&pkt[12]==ipv6 ) { /* IPv6  */
        /* check if IPv6 Next Header is UDP, and the first 8 bytes of UDP data is 'IDENTIFY' */
        if ( likely( pkt[20]==17 && *(uint64_t *)&pkt[62]==*id ) )
          bg_received++;	// it is considered a background frame: we do not deal with it any more
      } else if ( *(uint16_t *)&pkt[12]==ipv4 ) { /* IPv4 */
        if ( likely( pkt[23]==17 && *(uint64_t *)&pkt[42]==*id ) ) {
          fg_received++;	// it is considered a freground frame: we must learn its 4-tuple
          // copy IPv4 fields to the four_tuple -- without using conversion from network byte order to host byte order
          four_tuple.init_addr = *(uint32_t *)&pkt[26]; 	// 14+12: source IPv4 address
          four_tuple.resp_addr = *(uint32_t *)&pkt[30]; 	// 14+16: destination IPv4 address
          four_tuple.init_port = *(uint16_t *)&pkt[34]; 	// 14+20: source UDP port
          four_tuple.resp_port = *(uint16_t *)&pkt[36]; 	// 14+22: destination UDP port
	  stTbl[index] = four_tuple; 				// atomic write
	  index = ++index % state_table_size;			// maintain write pointer
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

// performs throughput (or frame loss rate) measurement
void Throughput::measure(uint16_t leftport, uint16_t rightport) {
  time_t now; // needed for printing out a timestamp in Info message

  // Several parameters are provided to the various sender functions (send, isend, msend, imsend) 
  // and receiver functions (receive, ) in the following 'struct'-s.
  // They are declared here so that they will not be overwritten in the stack when the program leaves an 'if' block.
  senderCommonParameters scp1, scp2;
  senderParameters spars1, spars2; 
  iSenderParameters ispars;
  mSenderParameters mspars1, mspars2;
  imSenderParameters imspars;
  receiverParameters rpars1, rpars2;
  rReceiverParameters rrpars1, rrpars2;
  rSenderParameters rspars;

  switch ( stateful ) {
    case 0:	// stateless test is to be performed
      {

      // set common parameters for senders
      scp1=senderCommonParameters(ipv6_frame_size,ipv4_frame_size,frame_rate,duration,n,m,hz,start_tsc);

      if ( forward ) {	// Left to Right direction is active
        // set individual parameters for the left sender
  
        // collect the appropriate values dependig on the IP versions 
        ipQuad ipq(ip_left_version,ip_right_version,&ipv4_left_real,&ipv4_right_real,&ipv4_left_virtual,&ipv4_right_virtual,
                   &ipv6_left_real,&ipv6_right_real,&ipv6_left_virtual,&ipv6_right_virtual);

        if ( !ip_varies ) { // use traditional single source and destination IP addresses
     
          // initialize the parameter class instance
          spars1=senderParameters(&scp1,ip_left_version,pkt_pool_left_sender,leftport,"Forward",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                  ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,num_right_nets,
                                  fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max);
          // start left sender
          if ( rte_eal_remote_launch(send, &spars1, cpu_left_sender) )
            std::cout << "Error: could not start Left Sender." << std::endl;

        } else { // use multiple source and/or destination IP addresses

          // initialize the parameter class instance
          mspars1=mSenderParameters(&scp1,ip_left_version,pkt_pool_left_sender,leftport,"Forward",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                    ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,
                                    ip_left_varies,ip_right_varies,ip_left_min,ip_left_max,ip_right_min,ip_right_max,
                                    ipv4_left_offset,ipv4_right_offset,ipv6_left_offset,ipv6_right_offset,
                                    fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max);

          // start left sender
          if ( rte_eal_remote_launch(msend, &mspars1, cpu_left_sender) )
            std::cout << "Error: could not start Left Sender." << std::endl;
        }

        // set parameters for the right receiver
        rpars1=receiverParameters(finish_receiving,rightport,"Forward");
    
        // start right receiver
        if ( rte_eal_remote_launch(receive, &rpars1, cpu_right_receiver) )
          std::cout << "Error: could not start Right Receiver." << std::endl;
      }
    
      if ( reverse ) {	// Right to Left direction is active 
        // set individual parameters for the right sender
    
        // collect the appropriate values dependig on the IP versions
        ipQuad ipq(ip_right_version,ip_left_version,&ipv4_right_real,&ipv4_left_real,&ipv4_right_virtual,&ipv4_left_virtual,
                   &ipv6_right_real,&ipv6_left_real,&ipv6_right_virtual,&ipv6_left_virtual);

        if ( !ip_varies ) { // use traditional single source and destination IP addresses
    
          // initialize the parameter class instance
          spars2=senderParameters(&scp1,ip_right_version,pkt_pool_right_sender,rightport,"Reverse",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                  ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,num_left_nets,
                                  rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max);
          // start right sender
          if (rte_eal_remote_launch(send, &spars2, cpu_right_sender) )
            std::cout << "Error: could not start Right Sender." << std::endl;
    
        } else { // use multiple source and/or destination IP addresses

          // initialize the parameter class instance
          mspars2=mSenderParameters(&scp1,ip_right_version,pkt_pool_right_sender,rightport,"Reverse",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                    ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,
                                    ip_right_varies,ip_left_varies,ip_right_min,ip_right_max,ip_left_min,ip_left_max,
                                    ipv4_right_offset,ipv4_left_offset,ipv6_right_offset,ipv6_left_offset,
                                    rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max);

          // start right sender
          if (rte_eal_remote_launch(msend, &mspars2, cpu_right_sender) )
            std::cout << "Error: could not start Right Sender." << std::endl;

        }

        // set parameters for the left receiver
        rpars2=receiverParameters(finish_receiving,leftport,"Reverse");

        // start left receiver
        if ( rte_eal_remote_launch(receive, &rpars2, cpu_left_receiver) )
          std::cout << "Error: could not start Left Receiver." << std::endl;

      }
    
      now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      std::cout << "Info: Testing initiated at " << std::put_time(localtime(&now), "%F %T") << std::endl;
    
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
      // set "common" parameters (currently not common with anyone, only code is reused; it will be common, when sending test frames)
      scp1=senderCommonParameters(ipv6_frame_size,ipv4_frame_size,pre_rate,0,n,m,hz,start_tsc_pre); // 0: duration in seconds is not applicable

      // set "individual" parameters for the sender of the Initiator residing on the left side
  
      // collect the appropriate values dependig on the IP versions 
      ipQuad ipq(ip_left_version,ip_right_version,&ipv4_left_real,&ipv4_right_real,&ipv4_left_virtual,&ipv4_right_virtual,
                 &ipv6_left_real,&ipv6_right_real,&ipv6_left_virtual,&ipv6_right_virtual);

      if ( !ip_varies  ) { // use traditional single source and destination IP addresses
  
        // initialize the parameter class instance for premiminary phase
        ispars=iSenderParameters(&scp1,ip_left_version,pkt_pool_left_sender,leftport,"Preliminary",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                 ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,num_right_nets,
                                 fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max,
  			         enumerate_ports,pre_frames,uniquePortComb);
                                
        // start left sender
        if ( rte_eal_remote_launch(isend, &ispars, cpu_left_sender) )
          std::cout << "Error: could not start Initiator's Sender." << std::endl;

      } else { // use multiple source and/or destination IP addresses (because ip_varies OR enumerate_ips)

        // initialize the parameter class instance for premiminary phase
        imspars=imSenderParameters(&scp1,ip_left_version,pkt_pool_left_sender,leftport,"Preliminary",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                   ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,
                                   ip_left_varies,ip_right_varies,ip_left_min,ip_left_max,ip_right_min,ip_right_max,
                                   ipv4_left_offset,ipv4_right_offset,ipv6_left_offset,ipv6_right_offset,
                                   fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max,
				   enumerate_ips,enumerate_ports,pre_frames,uniqueIpComb,uniqueFtComb);

          // start left sender
          if ( rte_eal_remote_launch(imsend, &imspars, cpu_left_sender) )
            std::cout << "Error: could not start Initiator's Sender." << std::endl;
      } 
  
      // set parameters for the right receiver
      rrpars1=rReceiverParameters(finish_receiving_pre,rightport,"Preliminary",state_table_size,&valid_entries,&stateTable); 
  
      // start right receiver
      if ( rte_eal_remote_launch(rreceive, &rrpars1, cpu_right_receiver) )
        std::cout << "Error: could not start Responder's Receiver." << std::endl;
 
      now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      std::cout << "Info: Preliminary frame sending initiated at " << std::put_time(localtime(&now), "%F %T") << std::endl;
    
      // wait until active senders and receivers finish 
      rte_eal_wait_lcore(cpu_left_sender);
      rte_eal_wait_lcore(cpu_right_receiver);

      if ( valid_entries < state_table_size )
        printf("Error: Failed to fill state table (valid entries: %u, state table size: %u)!\n", valid_entries, state_table_size);
      else
      	std::cout << "Info: Preliminary phase finished." << std::endl;

      // Now the real test may follow.

      // set "common" parameters 
      scp2=senderCommonParameters(ipv6_frame_size,ipv4_frame_size,frame_rate,duration,n,m,hz,start_tsc); 
  
      if ( forward ) {  // Left to right direction is active

        if ( !ip_varies ) { // use traditional single source and destination IP addresses

          // set "individual" parameters for the (normal) sender of the Initiator residing on the left side
    
          // initialize the parameter class instance for real test (reuse previously prepared 'ipq')
          spars2=senderParameters(&scp2,ip_left_version,pkt_pool_left_sender,leftport,"Forward",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                  ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,num_right_nets,
                                  fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max);
    
          // start left sender
          if ( rte_eal_remote_launch(send, &spars2, cpu_left_sender) )
            std::cout << "Error: could not start Initiator's Sender." << std::endl;

        } else { // use multiple source and/or destination IP addresses
 
          // initialize the parameter class instance for real test (reuse previously prepared 'ipq')
          mspars2=mSenderParameters(&scp2,ip_left_version,pkt_pool_left_sender,leftport,"Forward",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                   ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,
                                   ip_left_varies,ip_right_varies,ip_left_min,ip_left_max,ip_right_min,ip_right_max,
                                   ipv4_left_offset,ipv4_right_offset,ipv6_left_offset,ipv6_right_offset,
                                   fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max);

          // start left sender
          if ( rte_eal_remote_launch(msend, &mspars2, cpu_left_sender) )
            std::cout << "Error: could not start Left Sender." << std::endl;
        }
  
        // set parameters for the right receiver
        rrpars2=rReceiverParameters(finish_receiving,rightport,"Forward",state_table_size,&valid_entries,&stateTable);
  
        // start right receiver
        if ( rte_eal_remote_launch(rreceive, &rrpars2, cpu_right_receiver) )
          std::cout << "Error: could not start Responder's Receiver." << std::endl;
      }

      if ( reverse ) {  // Right to Left direction is active
        // set individual parameters for the right sender

        // first, collect the appropriate values dependig on the IP versions
        ipQuad ipq(ip_right_version,ip_left_version,&ipv4_right_real,&ipv4_left_real,&ipv4_right_virtual,&ipv4_left_virtual,
                   &ipv6_right_real,&ipv6_left_real,&ipv6_right_virtual,&ipv6_left_virtual);

        // then, initialize the parameter class instance
        rspars=rSenderParameters(&scp2,ip_right_version,pkt_pool_right_sender,rightport,"Reverse",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                 ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,num_left_nets,
                                 rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max,
				 state_table_size,stateTable,responder_tuples);

        // start right sender
        if (rte_eal_remote_launch(rsend, &rspars, cpu_right_sender) )
          std::cout << "Error: could not start Right Sender." << std::endl;

        // set parameters for the left receiver
        rpars2=receiverParameters(finish_receiving,leftport,"Reverse");

        // start left receiver
        if ( rte_eal_remote_launch(receive, &rpars2, cpu_left_receiver) )
          std::cout << "Error: could not start Left Receiver." << std::endl;
      }

      now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      std::cout << "Info: Testing initiated at " << std::put_time(localtime(&now), "%F %T") << std::endl;

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
      // set "common" parameters (currently not common with anyone, only code is reused; it will be common, when sending test frames)
      scp1=senderCommonParameters(ipv6_frame_size,ipv4_frame_size,pre_rate,0,n,m,hz,start_tsc_pre); // 0: duration in seconds is not applicable

      // set "individual" parameters for the sender of the Initiator residing on the right side

      // collect the appropriate values dependig on the IP versions
      ipQuad ipq(ip_right_version,ip_left_version,&ipv4_right_real,&ipv4_left_real,&ipv4_right_virtual,&ipv4_left_virtual,
                 &ipv6_right_real,&ipv6_left_real,&ipv6_right_virtual,&ipv6_left_virtual);

      if ( !ip_varies ) { // use traditional single source and destination IP addresses
  
        // initialize the parameter class instance for preliminary phase
        ispars=iSenderParameters(&scp1,ip_right_version,pkt_pool_right_sender,rightport,"Preliminary",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                 ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,num_left_nets,
                                 rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max,
  			         enumerate_ports,pre_frames,uniquePortComb);
  
        // start right sender
        if ( rte_eal_remote_launch(isend, &ispars, cpu_right_sender) )
          std::cout << "Error: could not Initiator's Sender." << std::endl;
  
      } else { // use multiple source and/or destination IP addresses (because ip_varies OR enumerate_ips)

        // initialize the parameter class instance for preliminary phase
        imspars=imSenderParameters(&scp1,ip_right_version,pkt_pool_right_sender,rightport,"Preliminary",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                   ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,
                                   ip_right_varies,ip_left_varies,ip_right_min,ip_right_max,ip_left_min,ip_left_max,
                                   ipv4_right_offset,ipv4_left_offset,ipv6_right_offset,ipv6_left_offset,
                                   rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max,
                                   enumerate_ips,enumerate_ports,pre_frames,uniqueIpComb,uniqueFtComb);

        // start right sender
        if ( rte_eal_remote_launch(imsend, &imspars, cpu_right_sender) )
          std::cout << "Error: could not Initiator's Sender." << std::endl;
      }

      // set parameters for the left receiver
      rrpars1=rReceiverParameters(finish_receiving_pre,leftport,"Preliminary",state_table_size,&valid_entries,&stateTable); 

      // start left receiver
      if ( rte_eal_remote_launch(rreceive, &rrpars1, cpu_left_receiver) )
        std::cout << "Error: could not start Responder's Receiver." << std::endl;

      now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      std::cout << "Info: Preliminary frame sending initiated at " << std::put_time(localtime(&now), "%F %T") << std::endl;

      // wait until active senders and receivers finish
      rte_eal_wait_lcore(cpu_right_sender);
      rte_eal_wait_lcore(cpu_left_receiver);
      
      if ( valid_entries < state_table_size )
        printf("Error: Failed to fill state table (valid entries: %u, state table size: %u)!\n", valid_entries, state_table_size);
      else
        std::cout << "Info: Preliminary phase finished." << std::endl;

      // Now the real test may follow.

      // set "common" parameters
      scp2=senderCommonParameters(ipv6_frame_size,ipv4_frame_size,frame_rate,duration,n,m,hz,start_tsc);

      if ( reverse ) {  // Right to Left direction is active

        if ( !ip_varies ) { // use traditional single source and destination IP addresses (no enumeration in phase 2)

        // set "individual" parameters for the sender of the Initiator residing on the right side
    
          // initialize the parameter class instance for real test (reuse previously prepared 'ipq')
          spars2=senderParameters(&scp2,ip_right_version,pkt_pool_right_sender,rightport,"Reverse",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                  ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,num_left_nets,
                                  rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max);
    
          // start right sender
          if ( rte_eal_remote_launch(send, &spars2, cpu_right_sender) )
            std::cout << "Error: could not start Initiator's Sender." << std::endl;
   
        } else { // use multiple source and/or destination IP addresses

          // initialize the parameter class instance
          mspars2=mSenderParameters(&scp2,ip_right_version,pkt_pool_right_sender,rightport,"Reverse",(ether_addr *)mac_right_dut,(ether_addr *)mac_right_tester,
                                    ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_right_real,&ipv6_left_real,
                                    ip_right_varies,ip_left_varies,ip_right_min,ip_right_max,ip_left_min,ip_left_max,
                                    ipv4_right_offset,ipv4_left_offset,ipv6_right_offset,ipv6_left_offset,
                                    rev_var_sport,rev_var_dport,rev_sport_min,rev_sport_max,rev_dport_min,rev_dport_max);

          // start right sender
          if (rte_eal_remote_launch(msend, &mspars2, cpu_right_sender) )
            std::cout << "Error: could not start Right Sender." << std::endl;
        }

        // set parameters for the left receiver
        rrpars2=rReceiverParameters(finish_receiving,leftport,"Reverse",state_table_size,&valid_entries,&stateTable);
  
        // start left receiver
        if ( rte_eal_remote_launch(rreceive, &rrpars2, cpu_left_receiver) )
          std::cout << "Error: could not start Responder's Receiver." << std::endl;
      }

      if ( forward ) {  // Left to right direction is active
        // set individual parameters for the left sender

        // first, collect the appropriate values dependig on the IP versions
        ipQuad ipq(ip_left_version,ip_right_version,&ipv4_left_real,&ipv4_right_real,&ipv4_left_virtual,&ipv4_right_virtual,
                   &ipv6_left_real,&ipv6_right_real,&ipv6_left_virtual,&ipv6_right_virtual);

        // then, initialize the parameter class instance
        rspars=rSenderParameters(&scp2,ip_left_version,pkt_pool_left_sender,leftport,"Forward",(ether_addr *)mac_left_dut,(ether_addr *)mac_left_tester,
                                 ipq.src_ipv4,ipq.dst_ipv4,ipq.src_ipv6,ipq.dst_ipv6,&ipv6_left_real,&ipv6_right_real,num_right_nets,
                                 fwd_var_sport,fwd_var_dport,fwd_sport_min,fwd_sport_max,fwd_dport_min,fwd_dport_max,
                                 state_table_size,stateTable,responder_tuples);

        // start left sender
        if (rte_eal_remote_launch(rsend, &rspars, cpu_left_sender) )
          std::cout << "Error: could not start Left Sender." << std::endl;

        // set parameters for the right receiver
        rpars2=receiverParameters(finish_receiving,rightport,"Forward");

        // start right receiver
        if ( rte_eal_remote_launch(receive, &rpars2, cpu_right_receiver) )
          std::cout << "Error: could not start Right Receiver." << std::endl;
      }

      now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
      std::cout << "Info: Testing initiated at " << std::put_time(localtime(&now), "%F %T") << std::endl;

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
  }
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
senderCommonParameters::senderCommonParameters()
{
}

// sets the values of the data fields
senderParameters::senderParameters(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                                   struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                                   struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                                   uint16_t num_dest_nets_, unsigned var_sport_, unsigned var_dport_,
                   		   uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_) {
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
  var_sport = var_sport_;
  var_dport = var_dport_;
  sport_min = sport_min_;
  sport_max = sport_max_;
  dport_min = dport_min_;
  dport_max = dport_max_;
}
senderParameters::senderParameters()
{
}

// sets the values of the data fields
mSenderParameters::mSenderParameters(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                                   struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                                   struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                                   unsigned var_sip_, unsigned var_dip_,
                                   uint16_t sip_min_, uint16_t sip_max_, uint16_t dip_min_, uint16_t dip_max_,
                                   uint16_t src_ipv4_offset_, uint16_t dst_ipv4_offset_, uint16_t src_ipv6_offset_, uint16_t dst_ipv6_offset_,
                                   unsigned var_sport_, unsigned var_dport_,
                   		   uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_) {
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
  // excluded: num_dest_nets = num_dest_nets_;
  var_sip = var_sip_;
  var_dip = var_dip_;
  sip_min = sip_min_;
  sip_max = sip_max_;
  dip_min = dip_min_;
  dip_max = dip_max_;
  src_ipv4_offset = src_ipv4_offset_;
  dst_ipv4_offset = dst_ipv4_offset_;
  src_ipv6_offset = src_ipv6_offset_;
  dst_ipv6_offset = dst_ipv6_offset_;
  var_sport = var_sport_;
  var_dport = var_dport_;
  sport_min = sport_min_;
  sport_max = sport_max_;
  dport_min = dport_min_;
  dport_max = dport_max_;
}
mSenderParameters::mSenderParameters()
{
}

// sets the values of the data fields
imSenderParameters::imSenderParameters(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                                   struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                                   struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                                   unsigned var_sip_, unsigned var_dip_,
                                   uint16_t sip_min_, uint16_t sip_max_, uint16_t dip_min_, uint16_t dip_max_,
                                   uint16_t src_ipv4_offset_, uint16_t dst_ipv4_offset_, uint16_t src_ipv6_offset_, uint16_t dst_ipv6_offset_,
                                   unsigned var_sport_, unsigned var_dport_,
                   		   uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_,
				   unsigned enumerate_ips_, unsigned enumerate_ports_, uint32_t pre_frames_,
                   		   bits32 *uniqueIpComb_, bits64 *uniqueFtComb_) {
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
  // excluded: num_dest_nets = num_dest_nets_;
  var_sip = var_sip_;
  var_dip = var_dip_;
  sip_min = sip_min_;
  sip_max = sip_max_;
  dip_min = dip_min_;
  dip_max = dip_max_;
  src_ipv4_offset = src_ipv4_offset_;
  dst_ipv4_offset = dst_ipv4_offset_;
  src_ipv6_offset = src_ipv6_offset_;
  dst_ipv6_offset = dst_ipv6_offset_;
  var_sport = var_sport_;
  var_dport = var_dport_;
  sport_min = sport_min_;
  sport_max = sport_max_;
  dport_min = dport_min_;
  dport_max = dport_max_;
  enumerate_ips = enumerate_ips_;
  enumerate_ports = enumerate_ports_;
  pre_frames = pre_frames_;
  uniqueIpComb = uniqueIpComb_;
  uniqueFtComb = uniqueFtComb_;
}
imSenderParameters::imSenderParameters()
{
}

// sets the values of the data fields
iSenderParameters::iSenderParameters(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                                     struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                                     struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                                     uint16_t num_dest_nets_, unsigned var_sport_, unsigned var_dport_,
                                     uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_,
				     unsigned enumerate_ports_,  uint32_t pre_frames_,  bits32 *uniquePortComb_) :
  senderParameters(cp_, ip_version_, pkt_pool_, eth_id_, side_, dst_mac_, src_mac_, src_ipv4_, dst_ipv4_, src_ipv6_, dst_ipv6_, src_bg_, dst_bg_,
		   num_dest_nets_, var_sport_, var_dport_, sport_min_, sport_max_, dport_min_, dport_max_) {
  enumerate_ports = enumerate_ports_;
  pre_frames = pre_frames_;
  uniquePortComb = uniquePortComb_;
}
iSenderParameters::iSenderParameters()
{
}

// sets the values of the data fields
rSenderParameters::rSenderParameters(class senderCommonParameters *cp_, int ip_version_, rte_mempool *pkt_pool_, uint16_t eth_id_, const char *side_,
                                     struct ether_addr *dst_mac_,  struct ether_addr *src_mac_,  uint32_t *src_ipv4_, uint32_t *dst_ipv4_,
                                     struct in6_addr *src_ipv6_, struct in6_addr *dst_ipv6_, struct in6_addr *src_bg_, struct in6_addr *dst_bg_,
                                     uint16_t num_dest_nets_, unsigned var_sport_, unsigned var_dport_,
                                     uint16_t sport_min_, uint16_t sport_max_, uint16_t dport_min_, uint16_t dport_max_,
                   		     unsigned state_table_size_, atomicFourTuple *stateTable_, unsigned responder_tuples_) :
  senderParameters(cp_, ip_version_, pkt_pool_, eth_id_, side_, dst_mac_, src_mac_, src_ipv4_, dst_ipv4_, src_ipv6_, dst_ipv6_, src_bg_, dst_bg_,
                   num_dest_nets_, var_sport_, var_dport_, sport_min_, sport_max_, dport_min_, dport_max_) {
  state_table_size = state_table_size_;
  stateTable = stateTable_;
  responder_tuples = responder_tuples_;
}
rSenderParameters::rSenderParameters()
{
}

// sets the values of the data fields
receiverParameters::receiverParameters(uint64_t finish_receiving_, uint16_t eth_id_, const char *side_) {
  finish_receiving=finish_receiving_;
  eth_id = eth_id_;
  side = side_;
}
receiverParameters::receiverParameters()
{
}

// sets the values of the data fields
rReceiverParameters::rReceiverParameters(uint64_t finish_receiving_, uint16_t eth_id_, const char *side_, unsigned state_table_size_,
                                         unsigned *valid_entries_, atomicFourTuple **stateTable_) :
  receiverParameters::receiverParameters(finish_receiving_,eth_id_,side_) {
  state_table_size = state_table_size_;
  valid_entries = valid_entries_;
  stateTable = stateTable_;
}
rReceiverParameters::rReceiverParameters()
{
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

// prepares unique random port number or IP address combinations using random permutation
void randomPermutation32(bits32 *array, uint16_t src_min, uint16_t src_max, uint16_t dst_min, uint16_t dst_max) {
  uint16_t s, d; // relative source and destination port numbers
  uint16_t sfield, dfield; // source and destination fields
  uint32_t ssize = src_max-src_min+1; // range size of source fields
  uint32_t dsize = dst_max-dst_min+1; // range size of destination fields
  uint64_t size = ssize*dsize;  // size of the entire array 
  uint32_t index, random; 	// index and random variables

  // Preliminary filling the array with linearly enumerated port number combinations would look like so:
  // for ( sfield=src_min; sfield<=src_max; sfield++ )
  //   for ( dfield=dst_min; dfield<=dst_max; dfield++ )
  //    array[(sfield-src_min)*dsize+dfield-dst_min];
  // But it is not done, to avoid exchanges, elements are generated in place.

  // prepare random permutation using Fisher–Yates shuffle, as implemented by Durstenfeld (in-place)
  // http://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle#The_.22inside-out.22_algorithm

  // random number infrastructure is taken from: https://en.cppreference.com/w/cpp/numeric/random/uniform_real_distribution
  // MT64 is used because of https://medium.com/@odarbelaeze/how-competitive-are-c-standard-random-number-generators-f3de98d973f0
  // thread_local is used on the basis of https://stackoverflow.com/questions/40655814/is-mersenne-twister-thread-safe-for-cpp
  thread_local std::random_device rd;  // Will be used to obtain a seed for the random number engine
  thread_local std::mt19937_64 gen(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
  std::uniform_real_distribution<double> uni_dis(0, 1.0);

  // set the very first element
  array[0].field.src=src_min;
  array[0].field.dst=dst_min;
  for ( index=1; index<size; index++ ){
    // prepare the coordinates
    s = index / dsize;	// source field relative to src_min
    d = index % dsize;	// destination field relative to dst_min
    sfield = s + src_min;	// real source field
    dfield = d + dst_min;	// real destination field
    // generate a random integer in the range [0, index] using uni_dis(gen), a random double in [0, 1).
    random = uni_dis(gen)*(index+1);

    // condition "if ( random != index )" is left out to spare a branch instruction on the cost of a redundant copy
    array[index].data=array[random].data;
    array[random].field.src=sfield;
    array[random].field.dst=dfield;
  }
}

// allocate NUMA local memory and pre-generate random permutation -- Execute by the core of Initiator/Sender!
int randomPermutationGenerator32(void *par) {
  // collecting input parameters:
  class randomPermutationGeneratorParameters32 *p = (class randomPermutationGeneratorParameters32 *)par;
  uint16_t src_min = p->src_min;
  uint16_t src_max = p->src_max;
  uint16_t dst_min = p->dst_min;
  uint16_t dst_max = p->dst_max;
  char *type = p->type;		// to be able to give info what is being generated
  uint64_t hz = p->hz;		// just for giving info about execution time

  uint64_t start_gen, end_gen;  // timestamps for the above purpose 
  bits32 *array;	// array for storing the unique IP address or port number combinations
  uint64_t size;	// sizes of the above array

  size = (src_max-src_min+1)*(dst_max-dst_min+1);

  array = (bits32 *) rte_malloc("Pre-geneated unique 2x16 bit field combinations", (sizeof(bits32))*size, 128);
  if ( !array )
    rte_exit(EXIT_FAILURE, "Error: Can't allocate memory for Pre-geneated unique 2x16 bit field combinations array!\n");
  std::cerr << "Info: Pre-generating unique " << type << " combinations... " ;
  start_gen = rte_rdtsc();
  randomPermutation32(array,src_min,src_max,dst_min,dst_max);
  end_gen = rte_rdtsc();
  std::cerr << "Done. Lasted " << 1.0*(end_gen-start_gen)/hz << " seconds." << std::endl;

  *(p->addr_of_arraypointer) = array;	// set the pointer in the caller
  return 0;
}

// prepares unique random port number combinations using random permutation
void randomPermutation64(bits64 *array, uint16_t si_min, uint16_t si_max, uint16_t di_min, uint16_t di_max,
                         uint16_t sp_min, uint16_t sp_max, uint16_t dp_min, uint16_t dp_max) {
//    printf("\nrandomPermutation64 di_min %i, di_max: %i, si_min: %i, si_max: %i dp_min %i, dp_max: %i, sp_min: %i, sp_max: %i.\n", di_min, di_max, si_min, si_max, dp_min, dp_max, sp_min, sp_max);
  uint16_t si, di; // relative source and destination IP addresses
  uint16_t sip, dip; // source and destination IP addresses
  uint16_t sp, dp; // relative source and destination port numbers
  uint16_t sport, dport; // source and destination port numbers
  uint32_t sisize = si_max-si_min+1; // range size of source IP addresses 
  uint32_t disize = di_max-di_min+1; // range size of destination IP addresses
  uint32_t spsize = sp_max-sp_min+1; // range size of source ports 
  uint32_t dpsize = dp_max-dp_min+1; // range size of destination ports 
  uint64_t size = sisize*disize*spsize*dpsize;  // size of the entire array 
  uint64_t index, random; 	// index and random variables

  // prepare random permutation using Fisher–Yates shuffle, as implemented by Durstenfeld (in-place)
  // http://en.wikipedia.org/wiki/Fisher%E2%80%93Yates_shuffle#The_.22inside-out.22_algorithm

  // random number infrastructure is taken from: https://en.cppreference.com/w/cpp/numeric/random/uniform_real_distribution
  // MT64 is used because of https://medium.com/@odarbelaeze/how-competitive-are-c-standard-random-number-generators-f3de98d973f0
  // thread_local is used on the basis of https://stackoverflow.com/questions/40655814/is-mersenne-twister-thread-safe-for-cpp
  thread_local std::random_device rd;  // Will be used to obtain a seed for the random number engine
  thread_local std::mt19937_64 gen(rd()); // Standard 64-bit mersenne_twister_engine seeded with rd()
  std::uniform_real_distribution<double> uni_dis(0, 1.0);

  // set the very first element
  array[0].ft.sip=si_min;
  array[0].ft.dip=di_min;
  array[0].ft.sport=sp_min;
  array[0].ft.dport=dp_min;
//    printf("\nrandomPermutation64 dip: %i, sip: %i, dp: %i, sp: %i.\n", di_min, si_min, dp_min, sp_min);
  // precalculated values
  uint32_t for_di = sisize*dpsize*spsize;
  uint32_t for_si = dpsize*spsize;
  uint32_t rest;
  for ( index=1; index<size; index++ ){
    // prepare the coordinates
    di = index / for_di;	// sisize*dpsize*spsize 
    rest = index % for_di;
    si = rest / for_si;		// dpsize*spsize
    rest = rest % for_si;
    dp = rest / spsize;
    sp = rest % spsize;
    sip = si + si_min;		// real source IP address (part)
    dip = di + di_min;		// real destination IP address (part)
    sport = sp + sp_min;	// real source port number
    dport = dp + dp_min;	// real destination port number
    // generate a random integer in the range [0, index] using uni_dis(gen), a random double in [0, 1).
    random = uni_dis(gen)*(index+1);

    // condition "if ( random != index )" is left out to spare a branch instruction on the cost of a redundant copy
    array[index].data=array[random].data;
    array[random].ft.sip=sip;
    array[random].ft.dip=dip;
    array[random].ft.sport=sport;
    array[random].ft.dport=dport;
//    printf("randomPermutation64 dip: %i, sip: %i, dp: %i, sp: %i.\n", dip, sip, dport, sport);

  }
//  printf("The random permutation array has the following content:\n");
//  int i;
//  bits64 *a = array;
//  for ( i=0; i<size; i++ )
//    printf("array[%i]: dip: %i, sip: %i, dp: %i, sp: %i.\n", i, a[i].ft.dip, a[i].ft.sip, a[i].ft.dport, a[i].ft.sport );
}

// allocate NUMA local memory and pre-generate random permutation -- Execute by the core of Initiator/Sender!
int randomPermutationGenerator64(void *par) {
  // collecting input parameters:
  class randomPermutationGeneratorParameters64 *p = (class randomPermutationGeneratorParameters64 *)par;
  uint16_t si_min = p->si_min;
  uint16_t si_max = p->si_max;
  uint16_t di_min = p->di_min;
  uint16_t di_max = p->di_max;
  uint16_t sp_min = p->sp_min;
  uint16_t sp_max = p->sp_max;
  uint16_t dp_min = p->dp_min;
  uint16_t dp_max = p->dp_max;

  uint64_t hz = p->hz;		// just for giving info about execution time

  uint64_t start_gen, end_gen;  // timestamps for the above purpose 
  bits64 *array;	// array for storing the unique port number combinations
  uint64_t size=1;	// size of the above array

  size *= (si_max-si_min+1)*(di_max-di_min+1)*(sp_max-sp_min+1)*(dp_max-dp_min+1); // calculatin is done in 64 bits :-)

  array = (bits64 *) rte_malloc("Pre-geneated unique port number combinations", (sizeof(bits64))*size, 128);
  if ( !array )
    rte_exit(EXIT_FAILURE, "Error: Can't allocate memory for Pre-geneated unique IP address and port number combinations array!\n");
  std::cerr << "Info: Pre-generating unique IP address and port number combinations... " ;
  start_gen = rte_rdtsc();
  randomPermutation64(array,si_min,si_max,di_min,di_max,sp_min,sp_max,dp_min,dp_max);
  end_gen = rte_rdtsc();
  std::cerr << "Done. Lasted " << 1.0*(end_gen-start_gen)/hz << " seconds." << std::endl;

  *(p->addr_of_arraypointer) = array;	// set the pointer in the caller
  return 0;
}

