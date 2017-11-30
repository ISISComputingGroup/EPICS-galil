TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# Build an IOC support library

LIBRARY_IOC += GalilSupport

# motorRecord.h will be created from motorRecord.dbd
# install devMotorSoft.dbd into <top>/dbd
DBD += GalilSupport.dbd

#Require C++ 2011 standard compatibility
USR_CXXFLAGS_Linux += -std=c++11

# For sCalcPostfix.h
USR_INCLUDES += -I$(CALC)/calcApp/src

# The following are compiled and added to the Support library
GalilSupport_SRCS += GalilController.cpp GalilAxis.cpp GalilCSAxis.cpp GalilConnector.cpp GalilPoller.cpp

GalilSupport_LIBS += asyn motor calc sscan autosave busy
GalilSupport_LIBS += $(EPICS_BASE_IOC_LIBS)

#PROD_IOC += GalilDumpPos
#GalilDumpPos_SRCS += GalilDumpPos.cpp
#GalilDumpPos_LIBS_WIN32 += Galil1
#GalilDumpPos_SYS_LIBS_WIN32 += delayimp
#GalilDumpPos_LDFLAGS_WIN32 += /DELAYLOAD:Galil1.dll
#GalilDumpPos_LIBS += $(EPICS_BASE_IOC_LIBS)
#GalilDumpPos_SYS_LIBS_Linux += Galil

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
