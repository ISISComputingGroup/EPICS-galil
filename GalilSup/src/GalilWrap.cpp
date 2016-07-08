#include <galil.h>

#include <epicsExport.h>
#include "GalilWrap.h"
struct GalilH
{
	Galil* g;
};

#define HANDLE_EXC(__exc) \
		 catch(const std::string& str) \
		 { \
		     __exc = strdup(str.c_str()); \
		 } \
		 catch(const std::exception& ex) \
		 { \
		     __exc = strdup(ex.what()); \
		 }
		 
      GalilH* GalilIntf::create(const char* address, char*& exc)
	  {
	      exc = NULL;
		  GalilH* gh = new GalilH;
		  gh->g = NULL;
		  try
		  {
		      gh->g = new Galil(address);
		  }
		  HANDLE_EXC(exc);
		 return gh;
	  }
	  
	  void GalilIntf::destroy(GalilH* gh)
	  {
	      delete gh->g;
		  gh->g = NULL;
		  delete gh;
	  }
	  
      void GalilIntf::freeMem(void* addr)
	  {
		  free(addr);
	  }

      const char* GalilIntf::libraryVersion()
	  {
		  static std::string version = Galil::libraryVersion();
		  return version.c_str();
	  }

      char* GalilIntf::connection(GalilH* gh)
	  {
		  return strdup(gh->g->connection().c_str());
	  }

      int& GalilIntf::timeout_ms(GalilH* gh)
	  {
		  return gh->g->timeout_ms;
	  }

      char* GalilIntf::command(GalilH* gh, const char* command, const char* terminator, 
	                const char* ack, bool trim, char*& exc)
	{
	    exc = NULL;
	    char* res = NULL;
	    try
		{
		    res = strdup(gh->g->command(command, terminator, ack, trim).c_str());	
        }			
		HANDLE_EXC(exc);
		 return res;
	}

     double GalilIntf::commandValue(GalilH* gh, const char* command, char*& exc)
	 {
	     exc = NULL;
	     double res = 0.0;
		 try
		 {
		     res = gh->g->commandValue(command);
	     }
		 HANDLE_EXC(exc);
		 return res;
	 }

     char* GalilIntf::message(GalilH* gh, int timeout_ms)
	 {
		 return strdup(gh->g->message(timeout_ms).c_str());
	 }

     int GalilIntf::interrupt(GalilH* gh, int timeout_ms)
	 {
		 return gh->g->interrupt(timeout_ms);
	 }

     void GalilIntf::programDownload(GalilH* gh, const char* program)
	 {
		 gh->g->programDownload(program);
	 }

	 char* GalilIntf::programUpload(GalilH* gh)
	 {
		 return strdup(gh->g->programUpload().c_str());
	 }
	 
     void GalilIntf::programUploadFile(GalilH* gh, const char* file)
	 {
		 gh->g->programUploadFile(file);
	 }

     void GalilIntf::programDownloadFile(GalilH* gh, const char* file)
	 {
		 gh->g->programDownloadFile(file);
	 }

     void GalilIntf::recordsStart(GalilH* gh, double period_ms, char*& exc)
	 {
	     exc = NULL;
	     try
		 {
		     gh->g->recordsStart(period_ms);
	     }
		 HANDLE_EXC(exc);
	 }

     char* GalilIntf::record(GalilH* gh, size_t& len, const char* method, char*& exc)
	 {
	     exc = NULL;
		 char* res = NULL;
	     try
	     {
	         std::vector<char> rec = gh->g->record(method);
	         len = rec.size();
	         res = (char*)malloc(len);
	         memcpy(res, &(rec[0]), len);
		 }
		 HANDLE_EXC(exc);
		 return res;		 
	 }

     double GalilIntf::sourceValue(
	   GalilH* gh,
       const char* record, 
	   size_t len,
       const char* source,
	   char*& exc
     )
	 {
	     exc = NULL;
		 double res;
	     const std::vector<char> record_v(record, record + len);
		 try 
		 {
	         res = gh->g->sourceValue(record_v, source);
	     }
		 HANDLE_EXC(exc);
		 return res;
     }
	 
     char* GalilIntf::source(
	   GalilH* gh,
       const char* field,
       const char* source
     )
	 {
		 return strdup(gh->g->source(field, source).c_str());
	 }

     void GalilIntf::setSource(
	   GalilH* gh,
       const char* field,
       const char* source,
       const char* to
     )
	 {
		 gh->g->setSource(field, source, to);
	 }
  

