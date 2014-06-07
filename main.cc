#include <iostream>
#include <fstream>
#include <vector>
#include <queue>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include "linux_dvb_source.h"

#include <boost/atomic.hpp>
#include <boost/lockfree/spsc_queue.hpp>
#include <boost/thread/thread.hpp>

#include <boost/circular_buffer.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/call_traits.hpp>
#include <boost/bind.hpp>

#include <mutex>
#include <condition_variable>
#include <deque>

struct buffer {
  uint8_t* buf;
  int len;
};

template <typename T>
class blocking_queue {
  public:

    void push(T const& value) {
      {
        std::unique_lock<std::mutex> lock(mutex_);
        queue_.push_front(value);
      }
      condition_.notify_one();
    }

    T pop() {
      std::unique_lock<std::mutex> lock(mutex_);
      condition_.wait(lock, [=]{ return !queue_.empty(); });
      T rc(std::move(queue_.back()));
      queue_.pop_back();
      return rc;
    }

    bool empty() {
      std::unique_lock<std::mutex> lock(mutex_);
      return queue_.empty();
    }

  private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::deque<T> queue_;
};

// Tunes a dvb device and writes the whole transponder stream to a file.
class file_writer {
  public:

    file_writer(const char* file, int adapter, long frequency)
      : stop_(false),
        done_(false),
        consumer_count_(0),
        producer_count_(0) {
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
        buffer buf;
        buf.buf = new uint8_t[188];
        buf.len = source.Read(buf.buf, 188);
        if(buf.len) {
          // add to queue
          queue_.push(buf);
          producer_count_ += buf.len;
        } else {
          delete buf.buf;
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
      while(!done_ || !queue_.empty()) {
        buffer buf = queue_.pop();
        write(output_fd, buf.buf, buf.len);
        delete buf.buf;
        consumer_count_ += buf.len;
      }

      close(output_fd);
    }

    long total_read() const {
      return producer_count_;
    }
    long total_written() const {
      return consumer_count_;
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

    boost::atomic<long> consumer_count_;
    boost::atomic<long> producer_count_;

    blocking_queue<buffer> queue_;
};

sig_atomic_t signaled = 0;

void signal_handler(int sig) { 
  signaled = 1;
}

void print_stats(const char* label, long read, long write, double s) {
    double readmb = (read*8) / (1024*1024);
    double writemb = (write*8) / (1024*1024);
    std::cout
      << label
      << "read " << readmb << " (" << (readmb/s) << "Mb/s) "
      << "written " << writemb << " (" << (writemb/s) << "Mb/s) "
      << "difference " << (read-write)/188 << " packets"
      << std::endl;
}

int main(int argc, char** argv) {

  std::clog << "file_writer" << std::endl;

  // Handle ctrl-c
  signal(SIGINT, signal_handler);

  // TODO: Parse channels.conf or initial tuning data
  boost::posix_time::ptime start(boost::posix_time::microsec_clock::local_time());

  // TODO: Optimise read and write sizes

  file_writer psb3("psb3.ts", 0, 545800000);
  file_writer com7("com7.ts", 1, 570000000);

  while(true) {
    if(signaled) {
      std::clog << "stopping threads" << std::endl;
      psb3.stop();
      com7.stop();
      break;
    }
    boost::posix_time::ptime now(boost::posix_time::microsec_clock::local_time());
    boost::posix_time::time_duration td(now-start);
    double s = td.total_microseconds() / 1e6;
    print_stats("psb3", psb3.total_read(), psb3.total_written(), s);
    print_stats("com7", com7.total_read(), com7.total_written(), s);
    sleep(1);
  }

  std::clog << "joining threads" << std::endl;
  psb3.join();
  com7.join();

  return 0;
}

