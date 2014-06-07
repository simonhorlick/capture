# __STDC_FORMAT_MACROS is to enable PRIu64 in one of the bitstream headers
CXXFLAGS += -g -Wall -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS

all: capture

clean:
	rm -f *.o *.a capture

linux_dvb_source.o: linux_dvb_source.cc linux_dvb_source.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

main.o: main.cc linux_dvb_source.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

capture: main.o linux_dvb_source.o
	$(CXX) $(CXXFLAGS) $^ -lboost_thread -lpthread -o $@
