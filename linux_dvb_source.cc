#include "linux_dvb_source.h"

#include <stdio.h> // for perror
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>

#include <iostream>
#include <string>
#include <iomanip>

LinuxDVBSource::LinuxDVBSource(int adapter, int device) : device_(device) {
    const char* frontend = "/dev/dvb/adapter0/frontend0";
    const char* demux = "/dev/dvb/adapter0/demux0";
    const char* dvr = "/dev/dvb/adapter0/dvr0";

    // FIXME: Should probably block here and just handle threading sensibly.
    frontend_handle = open(frontend, O_RDWR | O_NONBLOCK);
    if(frontend_handle < 0) {
        std::cerr << "Error opening frontend\n";
    }

    demux_handle = open(demux, O_RDWR | O_NONBLOCK);
    if(demux_handle < 0) {
        std::cerr << "Could not open demux device\n";
    }

    dvr_handle = open(dvr, O_RDONLY);
    if(dvr_handle < 0) {
        std::cerr << "Could not open dvr device\n";
    }
    std::string name = GetName();
    std::clog << "Using device " << frontend << " - " << name << "\n";
}

std::string GetPropertyKeyFromEnum(int cmd) {
    switch(cmd) {
        case DTV_UNDEFINED: return "DTV_UNDEFINED";
    }
    return "";
}

bool LinuxDVBSource::Tune(int frequency, fe_code_rate fec, fe_modulation constellation, fe_guard_interval guard, fe_transmit_mode transmission) {
    dvb_frontend_parameters params;
    params.u.ofdm.bandwidth = BANDWIDTH_8_MHZ;
    params.u.ofdm.code_rate_HP = FEC_NONE;
    params.u.ofdm.code_rate_LP = fec;
    params.u.ofdm.constellation = constellation;
    params.u.ofdm.transmission_mode = transmission;
    params.u.ofdm.guard_interval = guard;
    params.u.ofdm.hierarchy_information = HIERARCHY_NONE;

    params.frequency = frequency;
    params.inversion = INVERSION_AUTO;

    std::cout << "Tuning new parameters\n";
    if(ioctl(frontend_handle, FE_SET_FRONTEND, &params) < 0) {
        std::cout << "Error setting frontend parameters\n";
        perror("Error setting frontend parameters");
        return false;
    }

    return true;
}

bool LinuxDVBSource::SetFilters() {
    // set the properties of the demux device.
    struct dmx_pes_filter_params params;
    params.pid = 0x2000;
    params.input = DMX_IN_FRONTEND;
    params.output = DMX_OUT_TS_TAP;
    params.pes_type = DMX_PES_OTHER;
    params.flags = DMX_IMMEDIATE_START;
  
    if(ioctl(demux_handle, DMX_SET_PES_FILTER, &params) < 0) {
        std::cout << "Could not set pes filter\n";
        return -1;
    }
  
    return true;
  }

int LinuxDVBSource::Read(uint8_t* buffer, int size) {
    int bytes = read(dvr_handle, buffer, size);
    if(bytes < 0) {
        std::cerr << "Error reading from dvr device\n";
        if(errno == EOVERFLOW) return 0;
        return -1;
    }

    return bytes;
}

fe_status_t LinuxDVBSource::GetStatus() {
    fe_status_t status;
    if(ioctl(frontend_handle, FE_READ_STATUS, &status) < 0) {
        std::cerr << "Error getting frontend status\n";
    }
    return status;
}

void LinuxDVBSource::PrintStatistics() {
    fe_status_t status = GetStatus();

    uint16_t strength, snr, ber, uncorrected;
    if(ioctl(frontend_handle, FE_READ_SIGNAL_STRENGTH, &strength) < 0 ||
        ioctl(frontend_handle, FE_READ_SNR, &snr) < 0 ||
        ioctl(frontend_handle, FE_READ_BER, &ber) < 0 ||
        ioctl(frontend_handle, FE_READ_UNCORRECTED_BLOCKS, &uncorrected) < 0) {
        std::cerr << "Failed to retrive statistics from device\n";
        return;
    }

    std::clog << "Statistics" << "\n";
    std::clog << "  frontend status is " << GetFrontendStatusFromEnum(status) << "\n";
    std::clog << "  signal strength is " << std::setprecision(3) << (strength / 65536.0f) << "\n";
    std::clog << "  snr is " << snr << "\n";
    std::clog << "  ber is " << ber << "\n";
    std::clog << "  uncorrected blocks is " << uncorrected << "\n";
    std::clog << "\n";
}

void LinuxDVBSource::GetInfo() {
    std::clog << "Querying frontend information\n";
    if(ioctl(frontend_handle, FE_GET_INFO, &info_) < 0) {
        std::cerr << "Error getting frontend information\n";
    }
}

std::string LinuxDVBSource::GetName() {
    GetInfo();
    return info_.name;
}

std::string GetFrontendStatusFromEnum(const fe_status_t& status) {
    std::string ret;
    if(status & FE_HAS_SIGNAL) ret += "HAS_SIGNAL ";
    if(status & FE_HAS_CARRIER) ret += "HAS_CARRIER ";
    if(status & FE_HAS_VITERBI) ret += "HAS_VITERBI ";
    if(status & FE_HAS_SYNC) ret += "HAS_SYNC ";
    if(status & FE_HAS_LOCK) ret += "HAS_LOCK ";
    if(status & FE_TIMEDOUT) ret += "TIMEDOUT ";
    if(status & FE_REINIT) ret += "REINIT ";
    return ret;
}

