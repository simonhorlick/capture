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

fe_code_rate parse_fec(const std::string& str) {
  if(str == "1/2") return FEC_1_2;
  if(str == "2/3") return FEC_2_3;
  if(str == "3/4") return FEC_3_4;
  if(str == "4/5") return FEC_4_5;
  if(str == "5/6") return FEC_5_6;
  if(str == "6/7") return FEC_6_7;
  if(str == "7/8") return FEC_7_8;
  if(str == "8/9") return FEC_8_9;
  if(str == "NONE") return FEC_NONE;
  return FEC_AUTO;
}

fe_guard_interval parse_guard(const std::string& str) {
  if(str == "1/16") return GUARD_INTERVAL_1_16;
  if(str == "1/32") return GUARD_INTERVAL_1_32;
  if(str == "1/4") return GUARD_INTERVAL_1_4;
  if(str == "1/8") return GUARD_INTERVAL_1_8;
  if(str == "1/128") return GUARD_INTERVAL_1_128;
  return GUARD_INTERVAL_AUTO;
}

fe_modulation parse_constellation(const std::string& str) {
  if(str == "QPSK") return QPSK;
  if(str == "QAM128") return QAM_128;
  if(str == "QAM16") return QAM_16;
  if(str == "QAM256") return QAM_256;
  if(str == "QAM32") return QAM_32;
  if(str == "QAM64") return QAM_64;
  return QAM_AUTO;
}

fe_transmit_mode parse_transmission(const std::string& str) {
  if(str == "2k") return TRANSMISSION_MODE_2K;
  if(str == "8k") return TRANSMISSION_MODE_8K;
  if(str == "32k") return TRANSMISSION_MODE_32K;
  return TRANSMISSION_MODE_AUTO;
};

bool parse_line(const std::string& line, long& frequency, fe_code_rate& fec, fe_modulation& constellation, fe_guard_interval& guard, fe_transmit_mode& transmission, std::string& identifier) {
  // expecting something like this:
  // T 546000000 8MHz  2/3 NONE   QAM256  32k 1/128 NONE # PSB3 BBCB

  if(line.empty() || line[0] == '#') {
    return false;
  }

  std::istringstream is(line);
  std::string type, sfec, sconstellation, sguard, stransmission, _;
  is >> _ >> frequency >> _ >> sfec >> _ >> sconstellation >> stransmission >> sguard >> _;

  fec = parse_fec(sfec);
  constellation = parse_constellation(sconstellation);
  guard = parse_guard(sguard);
  transmission = parse_transmission(stransmission);

  // read end of line comment
  is >> _;
  if("#" == _) {
    is >> _;
    identifier = _;
  }

  return true;
}


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

    file_writer(const std::string& file, int adapter, long frequency, fe_code_rate fec, fe_modulation constellation, fe_guard_interval guard, fe_transmit_mode transmission)
      : stop_(false),
        done_(false),
        consumer_count_(0),
        producer_count_(0),
        file_(file) {
      consumer_thread_ = boost::thread(boost::bind(&file_writer::consume, this, file));
      producer_thread_ = boost::thread(boost::bind(&file_writer::produce, this, adapter, frequency, fec, constellation, guard, transmission));
    }

    int tune(LinuxDVBSource& source, long frequency, fe_code_rate fec, fe_modulation constellation, fe_guard_interval guard, fe_transmit_mode transmission) {
      if(!source.Tune(frequency, fec, constellation, guard, transmission)) {
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

    void produce(int adapter, long frequency, fe_code_rate fec, fe_modulation constellation, fe_guard_interval guard, fe_transmit_mode transmission) {
      std::clog << "starting producer" << std::endl;
      LinuxDVBSource source(adapter,0);
      if(tune(source, frequency, fec, constellation, guard, transmission)) {
        throw std::runtime_error("failed to tune");
      }
      while(!stop_) {
        buffer buf;
        buf.buf = new uint8_t[188];
        buf.len = source.Read(buf.buf, 188);
        if(buf.len) {
          // add to queue
          if(stop_) done_ = true;
          queue_.push(buf);
          producer_count_ += buf.len;
        } else {
          delete buf.buf;
        }
      }
      std::clog << "stopped producer" << std::endl;
      done_ = true;
    }

    void consume(const std::string& file) {
      std::clog << "starting consumer" << std::endl;
      // open file for writing
      int output_fd = open(file.c_str(), O_WRONLY|O_CREAT|O_TRUNC,
          S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP);
      if(output_fd < 0) {
        throw std::runtime_error("error opening output file for writing");
      }

      // read off queue
      while(!done_ || !queue_.empty()) {
        buffer buf = queue_.pop();
        write(output_fd, buf.buf, buf.len);

        // TODO: Parse tables, print streams and other interesting information.

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

    const std::string& file() const {
      return file_;
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

    std::string file_;
};

sig_atomic_t signaled = 0;

void signal_handler(int sig) { 
  signaled = 1;
}

void print_stats(const std::string& label, long read, long write, double s) {
    double readmb = (read*8) / (1024*1024);
    double writemb = (write*8) / (1024*1024);
    std::cout
      << label << " "
      << "read " << readmb << " (" << (readmb/s) << "Mb/s) "
      << "written " << writemb << " (" << (writemb/s) << "Mb/s) "
      << "difference " << (read-write)/188 << " packets"
      << std::endl;
}

std::vector<file_writer*> read_config(const char* file) {
  std::vector<file_writer*> writers;
  int adapter = 0;

  std::clog << "reading from " << file << std::endl;
  std::ifstream conf(file);
  if(!conf.is_open())
    throw std::runtime_error("could not open file");

  while(!conf.eof()) {
    std::string line;
    std::getline(conf,line);
    long frequency;
    fe_code_rate fec;
    fe_modulation constellation;
    fe_guard_interval guard;
    fe_transmit_mode transmission;
    std::string identifier;
    if(parse_line(line, frequency, fec, constellation, guard, transmission, identifier)) {
      std::clog << "subscribing to " << line << std::endl;
      writers.push_back(new file_writer(identifier+".ts", adapter++, frequency, fec, constellation, guard, transmission));
    }
  }

  return writers;
}

int main(int argc, char** argv) {

  std::clog << "file_writer" << std::endl;

  // Handle ctrl-c
  signal(SIGINT, signal_handler);

  // TODO: Optimise read and write sizes

  std::vector<file_writer*> writers = read_config("uk-CrystalPalace");

  boost::posix_time::ptime start(boost::posix_time::microsec_clock::local_time());
  while(!signaled) {
    boost::posix_time::ptime now(boost::posix_time::microsec_clock::local_time());
    boost::posix_time::time_duration td(now-start);
    double s = td.total_microseconds() / 1e6;
    for(auto it = writers.begin(); it != writers.end(); ++it) {
      print_stats((*it)->file(), (*it)->total_read(), (*it)->total_written(), s);
    }
    sleep(1);
  }

  std::clog << "stopping threads" << std::endl;
  for(auto it = writers.begin(); it != writers.end(); ++it) {
    (*it)->stop();
  }

  std::clog << "joining threads" << std::endl;
  for(auto it = writers.begin(); it != writers.end(); ++it) {
    (*it)->join();
    delete *it;
  }

  return 0;
}

