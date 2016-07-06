#include <galil.h>

#include <epicsExport.h>
#include "GalilWrap.h"
struct GalilH
{
	Galil* g;
};

      GalilH* GalilIntf::create(const char* address)
	  {
		  GalilH* gh = new GalilH;
		  gh->g = new Galil(address);
		  return gh;
	  }
	  
	  void GalilIntf::destroy(GalilH* gh)
	  {
	      delete gh->g;
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
	                const char* ack, bool trim)
	{
		return strdup(gh->g->command(command, terminator, ack, trim).c_str());						
	}

     double GalilIntf::commandValue(GalilH* gh, const char* command)
	 {
		 return gh->g->commandValue(command);
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

     void GalilIntf::recordsStart(GalilH* gh, double period_ms)
	 {
		 gh->g->recordsStart(period_ms);
	 }

     char* GalilIntf::record(GalilH* gh, size_t& len, const char* method)
	 {
	std::vector<char> rec = gh->g->record(method);
	len = rec.size();
	char* res = (char*)malloc(len);
	memcpy(res, &(rec[0]), len);
	return res;
		 
	 }

     double GalilIntf::sourceValue(
	   GalilH* gh,
       const char* record, size_t len,
       const char* source
     )
	 {
	std::vector<char> record_v(record, record + len);
	return gh->g->sourceValue(record_v, source);
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
  

