#include "galil.h"
#include <epicsExport.h>
#include "GalilInterface.h"

#define HANDLE_ERRORS(__errval) \
    catch(const std::string& s) { \
		*error = strdup(s.c_str()); \
		return __errval; \
	} \
    catch(const std::exception& ex) { \
		*error = strdup(ex.what()); \
		return __errval; \
	} 	


epicsShareExtern void* IGalilCreate(const char* address, char** error)
{
	void* ptr = NULL;
	*error = NULL;
	try {
	    return new Galil(address);
	}
	HANDLE_ERRORS(NULL);
}

epicsShareExtern void IGalilDestroy(void* galil)
{
	Galil* g = (Galil*)galil;
	delete g;
}

epicsShareExtern void IGalilFreeString(char* ptr)
{
	if (ptr != NULL) {
        free(ptr);
	}
}

epicsShareExtern const char* IGalilLibraryVersion()
{
	static std::string version = Galil::libraryVersion();
	return version.c_str();
}

epicsShareExtern char* IGalilConnection(void* galil, char** error)
{
	Galil* g = (Galil*)galil;
	*error = NULL;
	try {
        return strdup(g->connection().c_str());
	}
	HANDLE_ERRORS(NULL);
}

epicsShareExtern int* IGalilTimeout(void* galil, char** error)
{
	Galil* g = (Galil*)galil;
	*error = NULL;
	try {
	    return &(g->timeout_ms);
	}
	HANDLE_ERRORS(NULL);
}

epicsShareExtern char* IGalilCommand(void* galil, char** error, const char* command, const char* terminator, const char* ack, int trim)
{
	Galil* g = (Galil*)galil;
	*error = NULL;
	try {
        return strdup(g->command(command, terminator, ack, (trim != 0 ? true : false)).c_str());
	}		
	HANDLE_ERRORS(NULL);
}

epicsShareExtern double IGalilCommandValue(void* galil, char** error, const char* command)
{
	Galil* g = (Galil*)galil;
	*error = NULL;
	try {
        return g->commandValue(command);
	}
	HANDLE_ERRORS(0.0);
}

epicsShareExtern char* IGalilMessage(void* galil, char** error, int timeout_ms)
{
	Galil* g = (Galil*)galil;
	*error = NULL;
	try {
        return strdup(g->message(timeout_ms).c_str());
	}
	HANDLE_ERRORS(NULL);
}

epicsShareExtern char* IGalilProgramUpload(void* galil, char** error)
{
	Galil* g = (Galil*)galil;
	*error = NULL;
	try {
        return strdup(g->programUpload().c_str());
	}
	HANDLE_ERRORS(NULL);
}

epicsShareExtern void IGalilProgramDownload(void* galil, char** error, const char* program)
{
	Galil* g = (Galil*)galil;
	*error = NULL;
	try {
        g->programDownload(program);
	}
	HANDLE_ERRORS((void)0);
}

epicsShareExtern void IGalilProgramUploadFile(void* galil, char** error, const char* file)
{
	Galil* g = (Galil*)galil;
	*error = NULL;
	try {
        g->programUploadFile(file);
	}
	HANDLE_ERRORS((void)0);
}

epicsShareExtern void IGalilProgramDownloadFile(void* galil, char** error, const char* file)
{
	Galil* g = (Galil*)galil;
	*error = NULL;
	try {
        g->programDownloadFile(file);
	}
	HANDLE_ERRORS((void)0);
}

epicsShareExtern char** IGalilSources(void* galil, char** error)
{
	Galil* g = (Galil*)galil;
	*error = NULL;
	try {
	    std::vector<std::string> srcs = g->sources();
	    char** sources = (char**)malloc( (1 + srcs.size()) * sizeof(char*) );
	    if (sources != NULL) {
	        for(size_t i=0; i<srcs.size(); ++i)
	        {
		        sources[i] = strdup(srcs[i].c_str());
	        }
	        sources[srcs.size()] = NULL;
		} else {
		    *error  = strdup("IGalilSources: malloc out of memory");
		}
	    return sources;
	}
	HANDLE_ERRORS(NULL);
}

epicsShareExtern void IGalilFreeSources(char** sources)
{
	for(size_t i=0; sources[i] != NULL; ++i)
	{
		free(sources[i]);
		sources[i] = NULL;
	}
	free(sources);
}

epicsShareExtern void IGalilRecordsStart(void* galil, char** error, double period_ms)
{
	Galil* g = (Galil*)galil;
	*error = NULL;
	try {
	    g->recordsStart(period_ms);
	}		
	HANDLE_ERRORS((void)0);
}

epicsShareExtern char* IGalilRecord(void* galil, int* n, char** error, const char* method)
{
	Galil* g = (Galil*)galil;
	*n = 0;
	*error = NULL;
	try {
	    std::vector<char> v = g->record(method);
	    *n = v.size();
	    char* record = (char*)malloc(*n + 1);
		if (record != NULL) {
	        memcpy(record, &(v[0]), *n);
	        record[*n] = NULL;
		} else {
			*n = 0;
			*error  = strdup("IGalilRecord: malloc out of memory");
		}
	    return record;
	}
	HANDLE_ERRORS(NULL);
}

epicsShareExtern double IGalilSourceValue(void* galil, const char* record, const int* n, char** error, const char* source)
{
	Galil* g = (Galil*)galil;
	*error = NULL;
	std::vector<char> v(record, record + *n);
	try {
	    return g->sourceValue(v, source);
	}
	HANDLE_ERRORS(0.0);
}

epicsShareExtern char* IGalilSource(void* galil, char** error, const char* field, const char* source)
{
	Galil* g = (Galil*)galil;
	*error = NULL;
	try {
	    return strdup(g->source(field, source).c_str());
	}
	HANDLE_ERRORS(NULL);
}
