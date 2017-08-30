#include <iostream>
#include <epicsString.h>
#include <epicsStdio.h>
#include <iocsh.h>
#include <epicsThread.h>
#include <epicsExit.h>
#include <epicsTime.h>
#include <errlog.h>

#include "Galil.h"

int main(int argc, char* argv[])
{
    if (argc < 6)
	{
	    std::cerr << "Usage: GalilDumpPos ip_addr axis_letter encoder_res DR_or_QR changes_only" << std::endl;
		return -1;
	}
    const char* addr = argv[1];
    const char* axis = argv[2];
    double res = atof(argv[3]);
    const char* method = argv[4]; // DR or QR
    const char* changes_only = argv[5]; // 1 or 0
    bool only_changes = false;
	if (!strcmp(changes_only, "1"))
	{
	    only_changes = true;
	}
	try
	{
	Galil* g = new Galil(std::string(addr) + " -s"); // -s is silent connect, just get tcp handle
	// clean up handles on Galil
	g->command("DR0"); // stop udp data record
	g->command("IHT=>-1"); // udp handles
	g->command("IHT=>-2"); // tcp handles
	delete g;
	
	//Open connection to address provided
	// -t 500 is default timeout (ms)
	// -mg 0 means don't set up for unsolicited messages
	int timeout = 500;
	char galil_args[128];
	epicsSnprintf(galil_args, sizeof(galil_args), "%s -mg 0 -t %d", addr, timeout);
	g = new Galil(galil_args);
	g->timeout_ms = timeout; // this didn't seem to get set with -t above and new galil C library wrapper
	g->recordsStart(0); // disable DR
	if (!strcmp(method, "DR"))
	{
	    g->recordsStart(20.0); // -1 for a fast as possible
	}
	std::vector<char> r;
	double sample, diff_time, sample_time, ratio = 0.0, ref_sample, old_sample = 0, pos, old_pos = 0.0;
	int sample_wraps = 0;
	char poscmd[10];
	sprintf(poscmd, "_TP%c", axis[0]);
	epicsTimeStamp epicsTS, ref_time;
	char tbuffer[40];
	bool first_run = true;
	while(true)
	{
        r = g->record(method);
		epicsTimeGetCurrent(&epicsTS);
	    sample = g->sourceValue(r, "TIME");
        pos = g->sourceValue(r, poscmd);
		if (first_run)
		{
		    first_run = false;
		    ref_time = epicsTS;
			ref_sample = sample;
		}
		// occasionally udp records arrive out of sync, so need to allow for this and not mistakenly think
		// a counter wrap has occurred. Thus look for a reasonably large jump
		if ( (old_sample - sample) > 10000 ) 
		{
		    ++sample_wraps;
		}
		old_sample = sample;
		sample_time = (sample - ref_sample + sample_wraps * 65536.0) / 1024.0;
		epicsTimeToStrftime(tbuffer, sizeof(tbuffer), "%Y-%m-%dT%H:%M:%S.%06f", &epicsTS);
		diff_time = epicsTimeDiffInSeconds(&epicsTS, &ref_time);
		if (sample_time > 0.0)
		{
		    ratio = diff_time / sample_time;
		}
		if (!only_changes || pos != old_pos)
		{
		    std::cout << tbuffer << " , " << diff_time << " , " << sample_time << " , " << ratio << " , " << pos * res << " , " << sample << " , " << pos << std::endl;
		    old_pos = pos;
		}
//		epicsThreadSleep(.005);
	}
	}
	catch(const std::exception& ex)
	{
	    std::cerr << ex.what() << std::endl;
	}
	catch(const std::string& s)
	{
	    std::cerr << s << std::endl;
	}
		
	return 0;
}
