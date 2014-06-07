#ifndef LINUX_DVB_SOURCE_H
#define LINUX_DVB_SOURCE_H

#include <string>
#include <stdint.h>
#include <linux/dvb/frontend.h>

class LinuxDVBSource {
public:
    LinuxDVBSource(int adapter, int device);

    bool Tune(int frequency, fe_code_rate fec, fe_modulation constellation, fe_guard_interval guard, fe_transmit_mode transmission);
    
    bool SetFilters();

    int Read(uint8_t* buffer, int size);

    std::string GetName();

    void PrintStatistics();

private:
    int device_;

    void GetInfo();
    fe_status_t GetStatus();

    dvb_frontend_info info_;

    int frontend_handle;
    int demux_handle;
    int dvr_handle;
};

std::string GetFrontendStatusFromEnum(const fe_status_t& status);

#endif // LINUX_DVB_SOURCE_H

