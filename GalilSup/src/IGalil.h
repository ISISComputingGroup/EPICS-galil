#ifndef GALIL_H
  #define GALIL_H

  #include <string>
  #include <vector>
  #include "GalilInterface.h"
  
  class Galil
  { 
  private:
      void* m_galil;
	  
  public:
  
     static void handleError(char* error)
	 {
		 if (error != NULL) {
			 std::string msg(error);
			 IGalilFreeString(error);
			 throw msg;
		 }		 
	 }	 

	 static std::string handleErrorAndReturnString(char* error, char* str)
	 {
		 handleError(error); 
         std::string s(str);
         IGalilFreeString(str);
		 return s;
	 }

     Galil(std::string address = "") : m_galil(NULL)
	 {
		 char* error = NULL;
		 m_galil = IGalilCreate(address.c_str(), &error);
		 handleError(error); 
	 }

     ~Galil()
	 {
		 IGalilDestroy(m_galil);
		 m_galil = NULL;
	 }

     static std::string libraryVersion()
	 {
		 return IGalilLibraryVersion();
	 }

     std::string connection()
	 {
		 char* error = NULL;
		 char* conn = IGalilConnection(m_galil, &error);
		 return handleErrorAndReturnString(error, conn);
	 }

     int& timeout_ms()
	 {
		 char* error = NULL;
		 int* tmout = IGalilTimeout(m_galil, &error);
		 handleError(error); 
		 return *tmout;		 
	 }

     std::string command(
        const std::string& command = "MG TIME",
        const std::string& terminator = "\r",
        const std::string& ack = ":", bool trim = true
     )
	 {
		 char* error = NULL;
		 char* comm = IGalilCommand(m_galil, &error, command.c_str(), terminator.c_str(), ack.c_str(), (trim ? 1 : 0));
		 return handleErrorAndReturnString(error, comm); 
	 }

     double commandValue(const std::string& command = "MG TIME")
	 {
		 char* error = NULL;
	     double val = IGalilCommandValue(m_galil, &error, command.c_str());
		 handleError(error); 
		 return val;
	 }

     std::string message(int timeout_ms = 500)
	 {
		 char* error = NULL;
		 char* msg = IGalilMessage(m_galil, &error, timeout_ms);
		 return handleErrorAndReturnString(error, msg); 
	 }

     std::string programUpload()
	 {
		 char* error = NULL;
		 char* prog = IGalilProgramUpload(m_galil, &error);
		 return handleErrorAndReturnString(error, prog); 		 
	 }


     void programDownload(const std::string& program = "MG TIME\rEN")
	 {
		 char* error = NULL;
		 IGalilProgramDownload(m_galil, &error, program.c_str());
		 handleError(error); 		 
	 }

     void programUploadFile(const std::string& file = "program.dmc")
	 {
		 char* error = NULL;
		 IGalilProgramUploadFile(m_galil, &error, file.c_str());
		 handleError(error); 		 
	 }

     void programDownloadFile(const std::string& file = "program.dmc")
	 {
 		 char* error = NULL;
		 IGalilProgramDownloadFile(m_galil, &error, file.c_str());
		 handleError(error);
	 }

     std::vector<std::string> sources()
	 {
 		 char* error = NULL;
         char** sources = IGalilSources(m_galil, &error);
		 handleError(error);
		 std::vector<std::string> srcs;
		 for(int i=0; sources[i] != NULL; ++i)
		 {
			 srcs.push_back(sources[i]);
		 }
         IGalilFreeSources(sources);
         return srcs;		 
	 }

     void recordsStart(double period_ms = -1)
	 {
 		 char* error = NULL;
		 IGalilRecordsStart(m_galil, &error, period_ms);
		 handleError(error);		 
	 }

     std::vector<char> record(const std::string& method = "QR")
	 {
		 int n = 0;
		 char* error = NULL;
		 char* rec = IGalilRecord(m_galil, &n, &error, method.c_str());
		 handleError(error);		 
		 std::vector<char> vc(n);
		 std::copy(rec, rec + n, vc.begin());
		 IGalilFreeString(rec);
		 return vc;
	 }

     double sourceValue(const std::vector<char>& record, const std::string& source = "TIME")
	 {
		 char* error = NULL;
		 int n = record.size();
		 double val = IGalilSourceValue(m_galil, &(record[0]), &n, &error, source.c_str());
		 handleError(error);		 
		 return val;
	 }

     std::string source(const std::string& field = "Description", const std::string& source = "TIME")
	 {
		 char* error = NULL;
		 char* val = IGalilSource(m_galil, &error, field.c_str(), source.c_str());
		 return handleErrorAndReturnString(error, val);		 
	 }

  };

#endif
