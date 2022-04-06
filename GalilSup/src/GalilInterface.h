#ifndef GALIL_INTERFACE_H
#define GALIL_INTERFACE_H

#include <shareLib.h>
epicsShareExtern void* IGalilCreate(const char* address, char** error);
epicsShareExtern void IGalilDestroy(void* galil);
epicsShareExtern void IGalilFreeString(char* ptr);
epicsShareExtern const char* IGalilLibraryVersion();
epicsShareExtern char* IGalilConnection(void* galil, char** error);
epicsShareExtern int* IGalilTimeout(void* galil, char** error);
epicsShareExtern char* IGalilCommand(void* galil, char** error, const char* command = "MG TIME", const char* terminator = "\r", const char* ack = ":", int trim = 1);
epicsShareExtern double IGalilCommandValue(void* galil, char** error, const char* command = "MG TIME");
epicsShareExtern char* IGalilMessage(void* galil, char** error, int timeout_ms = 500);
epicsShareExtern char* IGalilProgramUpload(void* galil, char** error);
epicsShareExtern void IGalilProgramDownload(void* galil, char** error, const char* program = "MG TIME\rEN");
epicsShareExtern void IGalilProgramUploadFile(void* galil, char** error, const char* file = "program.dmc");
epicsShareExtern void IGalilProgramDownloadFile(void* galil, char** error, const char* file = "program.dmc");
epicsShareExtern char** IGalilSources(void* galil, char** error);
epicsShareExtern void IGalilFreeSources(char** sources);
epicsShareExtern void IGalilRecordsStart(void* galil, char** error, double period_ms = -1);
epicsShareExtern char* IGalilRecord(void* galil, int* n, char** error, const char* method = "QR");
epicsShareExtern double IGalilSourceValue(void* galil, const char* record, const int* n, char** error, const char* source = "TIME");
epicsShareExtern char* IGalilSource(void* galil, char** error, const char* field = "Description", const char* source = "TIME");

#endif /* GALIL_INTERFACE_H */
