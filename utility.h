#include <string>
#include <cctype>
#include <iostream>
#include <algorithm>


#include "const.h"

#ifndef UTILITY_H
#define UTILITY_H

using namespace std;
using namespace consts;

namespace utility {

  void get_from_buffer_h(uint32_t &number, char* buffer, int& pos)
  {
    memcpy(&(number), buffer + pos, sizeof(uint32_t));
    pos += sizeof(uint32_t);
    number = be32toh(number);
  }
  
  bool is_number(char* s)
  {
    while(*s != '\0') {
      if(!isdigit(*s)) {
        return false;
      }
      s++;
    }
    return true;
  }

  int error(string s)
  {
    cerr << "Error: " << s << endl;
    return ERROR;
  }

  // splits ip:port from str
  void split_port_ip(string &str, string &port)
  {
    int counter = count(str.begin(), str.end(), ':');
    if( counter == 1 ) {
      int sep = str.find(':');
      port = str.substr(sep + 1,  string::npos);
      str = str.substr(0, sep);
    }

  }

  int32_t get_sample(uint32_t seed){
    static int32_t r = seed;
    return (((int64_t)r * 279470273) % 4294967291);
  }
}

#endif
