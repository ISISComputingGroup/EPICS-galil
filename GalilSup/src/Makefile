TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#==================================================
# Build an IOC support library

LIBRARY_IOC += GalilSupport

# Location of Galil communications library from their website, with directory structure rearranged to:
#  galil.h -> $(LIBGALIL)/include/galil.h
#  LibGalil-vs2010-x64/release/Galil1.lib -> $(LIBGALIL)/lib/windows-x64/Galil1.lib
#  LibGalil-vs2010-x64/debug/Galil1.lib -> $(LIBGALIL)/lib/windows-x64-debug/Galil1.lib
#  LibGalil-vs2010-x64/release/Galil1.dll -> $(LIBGALIL)/bin/windows-x64/Galil1.dll
#  LibGalil-vs2010-x64/debug/Galil1.dll -> $(LIBGALIL)/bin/windows-x64-debug/Galil1.dll
LIBGALIL = $(ICPBINARYDIR)/LibGalil/$(VCVERSION)

USR_CXXFLAGS += -I$(LIBGALIL)/include
# For sCalcPostfix.h
USR_INCLUDES += -I$(CALC)/calcApp/src

# motorRecord.h will be created from motorRecord.dbd
# install devMotorSoft.dbd into <top>/dbd
DBD += GalilSupport.dbd

# The following are compiled and added to the Support library
GalilSupport_SRCS += GalilController.cpp GalilAxis.cpp GalilCSAxis.cpp GalilConnector.cpp GalilPoller.cpp

GalilSupport_LIBS += asyn motor calc

Galil_DIR = $(LIBGALIL)/lib/$(EPICS_HOST_ARCH)
GalilSupport_SYS_LIBS_Linux += Galil

Galil1_DIR = $(LIBGALIL)/lib/$(EPICS_HOST_ARCH)
galil2_DIR = $(LIBGALIL)/lib/$(EPICS_HOST_ARCH)
GalilSupport_LIBS_WIN32 += Galil1 # galil2
GalilSupport_SYS_LIBS_WIN32 += delayimp
GalilSupport_LDFLAGS_WIN32 += /DELAYLOAD:Galil1.dll

# Galil communications library from their website
#GalilSupport_SYS_LIBS += Galil
GalilSupport_LIBS += $(EPICS_BASE_IOC_LIBS)

BIN_INSTALLS_Linux += $(wildcard $(LIBGALIL)/lib/$(EPICS_HOST_ARCH)/*.so.*)

BIN_INSTALLS_WIN32 += $(wildcard $(LIBGALIL)/bin/$(EPICS_HOST_ARCH)/*.dll)
BIN_INSTALLS_WIN32 += $(wildcard $(LIBGALIL)/bin/$(EPICS_HOST_ARCH)/*.pdb)
LIB_INSTALLS_WIN32 += $(wildcard $(LIBGALIL)/lib/$(EPICS_HOST_ARCH)/*.lib)

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
