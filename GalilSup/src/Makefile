TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

ifneq ($(findstring win32-x86,$(EPICS_HOST_ARCH)),)
GCLIB_ARCH=x86
else
GCLIB_ARCH=x64
endif

# For VS2010 use Standard Galil COmmunication Library Galil1.dll
# for Other VC version, build Galil2.dll from galil c library wrappers
ifneq ($(findstring 10.0,$(VCVERSION)),)
# Location of Galil communications library from their website, with directory structure rearranged to:
#  galil.h -> $(LIBGALIL)/include/galil.h
#  LibGalil-vs2010-x64/release/Galil1.lib -> $(LIBGALIL)/lib/windows-x64/Galil1.lib
#  LibGalil-vs2010-x64/debug/Galil1.lib -> $(LIBGALIL)/lib/windows-x64-debug/Galil1.lib
#  LibGalil-vs2010-x64/release/Galil1.dll -> $(LIBGALIL)/bin/windows-x64/Galil1.dll
#  LibGalil-vs2010-x64/debug/Galil1.dll -> $(LIBGALIL)/bin/windows-x64-debug/Galil1.dll
LIBGALIL = $(ICPBINARYDIR)/LibGalil/$(VCVERSION)
Galil1_DIR = $(LIBGALIL)/lib/$(EPICS_HOST_ARCH)
BIN_INSTALLS_WIN32 += $(wildcard $(LIBGALIL)/bin/$(EPICS_HOST_ARCH)/*.dll)
BIN_INSTALLS_WIN32 += $(wildcard $(LIBGALIL)/bin/$(EPICS_HOST_ARCH)/*.pdb)
LIB_INSTALLS_WIN32 += $(wildcard $(LIBGALIL)/lib/$(EPICS_HOST_ARCH)/*.lib)
GalilSupport_SYS_LIBS_WIN32 += delayimp
GalilSupport_LDFLAGS_WIN32 += /DELAYLOAD:Galil1.dll
GALILDLL=Galil1
else
# use galil c library (gclib) 
LIBGALIL = $(ICPBINARYDIR)/galil_glibc
LIBRARY_IOC += Galil2
SRC_DIRS += $(LIBGALIL)/source/wrappers/gcl
Galil2_SRCS += gcl_galil.cpp gcl_datarecord.cpp
Galil2_LIBS_WIN32 += gclib gclibo $(EPICS_BASE_IOC_LIBS)
gclib_DIR = $(LIBGALIL)/lib/dynamic/$(GCLIB_ARCH)
gclibo_DIR = $(LIBGALIL)/lib/dynamic/$(GCLIB_ARCH)
BIN_INSTALLS_WIN32 += $(wildcard $(LIBGALIL)/dll/$(GCLIB_ARCH)/*.dll)
LIB_INSTALLS_WIN32 += $(wildcard $(LIBGALIL)/lib/dynamic/$(GCLIB_ARCH)/*.lib)
GALILDLL=Galil2
endif

#==================================================
# Build an IOC support library

LIBRARY_IOC += GalilSupport

# For sCalcPostfix.h
USR_INCLUDES += -I$(CALC)/calcApp/src

USR_CXXFLAGS += -I$(LIBGALIL)/include

# motorRecord.h will be created from motorRecord.dbd
# install devMotorSoft.dbd into <top>/dbd
DBD += GalilSupport.dbd

# The following are compiled and added to the Support library
GalilSupport_SRCS += GalilController.cpp GalilAxis.cpp GalilCSAxis.cpp GalilConnector.cpp GalilPoller.cpp

GalilSupport_LIBS += asyn motor calc

GalilSupport_LIBS_WIN32 += $(GALILDLL)

GalilSupport_LIBS += $(EPICS_BASE_IOC_LIBS)

Galil_DIR = $(LIBGALIL)/lib/$(EPICS_HOST_ARCH)
GalilSupport_SYS_LIBS_Linux += Galil
BIN_INSTALLS_Linux += $(wildcard $(LIBGALIL)/lib/$(EPICS_HOST_ARCH)/*.so.*)

PROD_IOC += GalilDumpPos
GalilDumpPos_SRCS += GalilDumpPos.cpp
GalilDumpPos_LIBS_WIN32 += $(GALILDLL)
GalilDumpPos_SYS_LIBS_WIN32 += delayimp
GalilDumpPos_LDFLAGS_WIN32 += /DELAYLOAD:$(GALILDLL).dll
GalilDumpPos_LIBS += $(EPICS_BASE_IOC_LIBS)
GalilDumpPos_SYS_LIBS_Linux += Galil

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
