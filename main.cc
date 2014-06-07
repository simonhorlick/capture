#include <iostream>
#include <fstream>
#include <vector>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include "linux_dvb_source.h"

#include <boost/atomic.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/thread/thread.hpp>

// Tunes a dvb device and writes the whole transponder stream to a file.
class file_writer {
  public:

    file_writer(const char* file, int adapter, long frequency)
      : stop_(false),
        done_(false) {
      consumer_thread_ = boost::thread(boost::bind(&file_writer::consume, this, file));
      producer_thread_ = boost::thread(boost::bind(&file_writer::produce, this, adapter, frequency));
    }

    int tune(LinuxDVBSource& source, int adapter, long frequency) {
      if(!source.Tune(frequency, FEC_2_3, QAM_256, GUARD_INTERVAL_1_128, TRANSMISSION_MODE_32K)) {
          std::cerr << "Failed to tune\n";
          return 1;
      }
      std::clog << "Setting filters\n";
      if(!source.SetFilters()) {
          std::cerr << "Failed to set dvb filters\n";
          return 1;
      }
      return 0;
    }

    void produce(int adapter, long frequency) {
      std::clog << "starting producer" << std::endl;
      LinuxDVBSource source(adapter,0);
      if(tune(source, adapter, frequency)) {
        throw std::runtime_error("failed to tune");
      }
      while(!stop_) {
        uint8_t buf[188];
        int len = source.Read(buf, 188);
        if(len) {
          // add to queue
          queue_.push(buf, len);
        }
      }
      std::clog << "stopped producer" << std::endl;
      done_ = true;
    }

    void consume(const char* file) {
      std::clog << "starting consumer" << std::endl;
      // open file for writing
      int output_fd = open(file, O_WRONLY|O_CREAT|O_TRUNC,
          S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
      if(output_fd < 0) {
        throw std::runtime_error("error opening output file for writing");
      }

      // read off queue
      while(!done_) {
        uint8_t buf[188];
        int len;
        while((len = queue_.pop(buf, 188))) {
          write(output_fd, buf, len);
        }
      }

      close(output_fd);
    }

    void join() {
      producer_thread_.join();
      consumer_thread_.join();
    }

    void stop() {
      stop_ = true;
    }

  private:
    boost::thread producer_thread_;
    boost::thread consumer_thread_;
 
    boost::atomic<bool> stop_;
    boost::atomic<bool> done_;

    const static int capacity = 64 * 1024;
    boost::lockfree::spsc_queue<uint8_t, boost::lockfree::capacity<capacity> > queue_;
};

sig_atomic_t signaled = 0;

void signal_handler(int sig) { 
  signaled = 1;
}

int main(int argc, char** argv) {

  std::clog << "file_writer" << std::endl;

  // Handle ctrl-c
  signal(SIGINT, signal_handler);

  // TODO: Parse channels.conf or initial tuning data

  file_writer psb3("psb3.ts", 0, 545800000);
  file_writer com7("com7.ts", 1, 570000000);

  while(true) {
    if(signaled) {
      std::clog << "stopping threads" << std::endl;
      psb3.stop();
      com7.stop();
      break;
    }
    sleep(1);
  }

  std::clog << "joining threads" << std::endl;
  psb3.join();
  com7.join();

  return 0;
}

