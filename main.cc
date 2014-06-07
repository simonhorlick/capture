#include <iostream>
#include <fstream>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "linux_dvb_source.h"

#include <boost/thread/thread.hpp>

int capture(long frequency, int adapter, const char* output) {
    LinuxDVBSource source(adapter,0);
    if(!source.Tune(frequency, FEC_2_3, QAM_256, GUARD_INTERVAL_1_128, TRANSMISSION_MODE_32K)) {
        std::cerr << output << ": Failed to tune\n";
        return 1;
    }
    std::clog << output << ": Setting filters\n";
    if(!source.SetFilters()) {
        std::cerr << "Failed to set dvb filters\n";
        return 1;
    }
    int outfd = open(output, O_WRONLY|O_CREAT|O_TRUNC);
    if( outfd < 0 ) {
      std::cerr << "error opening output file" << std::endl;
      return 1;
    }

    // TODO: Do this in a circular buffer
    // TODO: Do this in a separate thread

    std::clog << output << ": Reading from device\n";
    size_t total_read = 0;
    while(true) {
        fe_status_t status = source.GetStatus();
        if(!(status & FE_HAS_LOCK)) {
          std::cout << output << ": Lost lock" << std::endl;
          continue;
        }
        const int bufsize = 256*188;
        uint8_t buf[bufsize];
        int len = source.Read( buf, bufsize );
        total_read += len;
        std::cout << output << ": Read " << (total_read / (1024*1024)) << "MB\n";
        // TODO: Parse packet headers for non-recoverable errors.
        // TODO: Print current pids and how much data has been received for
        // each.
        // TODO: Print current epg programme for a pid.
        // TODO: Print current time counter for each pid.
        if( len ) {
            write(outfd, buf, len);
        }
    }
    std::cout << std::endl;

    close(outfd);
}

int main(int argc, char** argv) {

  // TODO: Parse channels.conf or initial tuning data

  // TODO: Separate threads for each reader and each writer.
  //boost::thread writer = boost::thread( boost::bind( ) );

  boost::thread r1 = boost::thread( boost::bind( &capture, 545800000, 0, "psb3.ts" ) );
  boost::thread r2 = boost::thread( boost::bind( &capture, 570000000, 1, "com7.ts" ) );

  r1.join();
  r2.join();

  return 0;
}

