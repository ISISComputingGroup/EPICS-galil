#ifndef GALILWRAP_H
  #define GALILWRAP_H

  #include <string>
  #include <vector>  
  
  #include <shareLib.h>
  
  struct GalilH;
  
  namespace GalilIntf
  {
      epicsShareExtern GalilH* create(const char* address = "");
	  
	  epicsShareExtern void destroy(GalilH* g);
	  
      epicsShareExtern void freeMem(void* addr);

      epicsShareExtern const char* libraryVersion();

      epicsShareExtern char* connection(GalilH* g);

      epicsShareExtern int& timeout_ms(GalilH* g);

      epicsShareExtern char* command(GalilH* g, const char* command = "MG TIME", const char* terminator = "\r", 
	                const char* ack = ":", bool trim = true);

     epicsShareExtern double commandValue(GalilH* g, const char* command = "MG TIME");

     epicsShareExtern char* message(GalilH* g, int timeout_ms = 500);

     epicsShareExtern int interrupt(GalilH* g, int timeout_ms = 500);
	 
	 epicsShareExtern char* programUpload(GalilH* g);

     epicsShareExtern void programDownload(GalilH* g, const char* program = "MG TIME\rEN");

     epicsShareExtern void programUploadFile(GalilH* g, const char* file = "program.dmc");

     epicsShareExtern void programDownloadFile(GalilH* g, const char* file = "program.dmc");

     epicsShareExtern void recordsStart(GalilH* g, double period_ms = -1);

     epicsShareExtern char* record(GalilH* g, size_t& len, const char* method = "QR");

     epicsShareExtern double sourceValue(
	   GalilH* g,
       const char* record, size_t len,
       const char* source = "TIME"
     );

     epicsShareExtern char* source(
	   GalilH* g,
       const char* field = "Description",
       const char* source = "TIME"
     );

     epicsShareExtern void setSource(
	   GalilH* g,
       const char* field = "Description",
       const char* source = "TIME",
       const char* to = "Sample counter"
     );
  };

  class GalilWrap
  {
	  private:
	  GalilH* m_gh;
	  
	  public:
	  
     GalilWrap(const std::string& address = "")
	 {
		 m_gh = GalilIntf::create(address.c_str());
	 }

     ~GalilWrap()
	 {
		 GalilIntf::destroy(m_gh);
		 m_gh = NULL;
	 }
  
     static std::string libraryVersion()
	 {
		 return GalilIntf::libraryVersion();
	 }

     std::string connection()
	 {
		 char* reply = GalilIntf::connection(m_gh);
		 std::string s(reply);
		 GalilIntf::freeMem(reply);
		 return s;
	 }

     //int timeout_ms;

     std::string command(
        const std::string& command = "MG TIME",
        const std::string& terminator = "\r",
        const std::string& ack = ":", bool trim = true
     )
	 {
		 char* reply = GalilIntf::command(m_gh, command.c_str(), terminator.c_str(), ack.c_str(), trim);
		 std::string s(reply);
		 GalilIntf::freeMem(reply);
		 return s;
	 }

     double commandValue(const std::string& command = "MG TIME")
	 {
		 return GalilIntf::commandValue(m_gh, command.c_str());		 
	 }

     std::string message(int timeout_ms = 500)
	 {
		 char* reply = GalilIntf::message(m_gh, timeout_ms);
		 std::string s(reply);
		 GalilIntf::freeMem(reply);
		 return s;		 
	 }

     int interrupt(int timeout_ms = 500)
	 {
		 return GalilIntf::interrupt(m_gh, timeout_ms);		 
	 }

     std::string programUpload()
	 {
		 char* reply = GalilIntf::programUpload(m_gh);
		 std::string s(reply);
		 GalilIntf::freeMem(reply);
		 return s;		 	 
	 }
	 
     void programDownload(const std::string& program = "MG TIME\rEN")
	 {
		 GalilIntf::programDownload(m_gh, program.c_str());		 
	 }

     void programUploadFile(const std::string& file = "program.dmc")
	 {
		 GalilIntf::programUploadFile(m_gh, file.c_str());		 
	 }

     void programDownloadFile(const std::string& file = "program.dmc")
	 {
		 GalilIntf::programDownloadFile(m_gh, file.c_str());		 		 
	 }

//     int write(const std::string& bytes = "\r")
//	 {
//		 return GalilIntf::write(m_gh, bytes.c_str());		 		 		 
//	 }

//     std::string read()
//	 {
//		 char* reply = GalilIntf::read(m_gh);
//		 std::string s(reply);
//		 GalilIntf::freeMem(reply);
//		 return s;		 		 
//	 }

     void recordsStart(double period_ms = -1)
	 {
		 GalilIntf::recordsStart(m_gh, period_ms);		 
	 }

     std::vector<char> record(const std::string& method = "QR")
	 {
		 size_t len;
		 char* vals = GalilIntf::record(m_gh, len, method.c_str());
		 std::vector<char> v(vals, vals + len);
		 return v;
	 }

     double sourceValue(
       const std::vector<char>& record,
       const std::string& source = "TIME"
     )
	 {
		 return GalilIntf::sourceValue(m_gh, &(record[0]), record.size(), source.c_str());
	 }

     std::string source(
       const std::string& field = "Description",
       const std::string& source = "TIME"
     )
	 {
		 return GalilIntf::source(m_gh, field.c_str(), source.c_str());
	 }

     void setSource(
       const std::string& field = "Description",
       const std::string& source = "TIME",
       const std::string& to = "Sample counter"
     )
	 {
		 return GalilIntf::setSource(m_gh, field.c_str(), source.c_str(), to.c_str());
	 }
  };
  
  

#endif
