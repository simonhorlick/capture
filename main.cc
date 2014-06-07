#include <iostream>
#include <fstream>
#include <vector>

#include <fcntl.h>
#include <unistd.h>

#include "linux_dvb_source.h"

int main(int argc, char** argv) {

  // TODO: Parse channels.conf or initial tuning data

    LinuxDVBSource source(0, 0);
    if(!source.Tune(545800000, FEC_2_3, QAM_256, GUARD_INTERVAL_1_128, TRANSMISSION_MODE_32K)) {
        std::cerr << "Failed to tune\n";
        return 1;
    }

    std::clog << "Setting filters\n";
    if(!source.SetFilters()) {
        std::cerr << "Failed to set dvb filters\n";
        return 1;
    }

    int outfd = open("output.ts", O_WRONLY|O_CREAT|O_TRUNC);
    if( outfd < 0 ) {
      std::cerr << "error opening output file" << std::endl;
      return 1;
    }

    // TODO: Do this in a circular buffer
    std::clog << "Reading from device\n";
    size_t total_read = 0;
    while(true) {
        fe_status_t status = source.GetStatus();
        if(!(status & FE_HAS_LOCK)) {
          std::cout << "\rLost lock";
          continue;
        }
        const int bufsize = 256*188;
        uint8_t buf[bufsize];
        int len = source.Read( buf, bufsize );
        total_read += len;
        std::cout << "\rRead " << (total_read / (1024*1024)) << "MB";
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

    return 0;
}

