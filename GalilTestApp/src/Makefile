TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# build an ioc application

ifeq ($(HAVE_C++11), YES)
  PROD_IOC_DEFAULT = GalilTest
endif

# <name>.dbd will be created from <name>Include.dbd
DBD += GalilTest.dbd
GalilTest_DBD += base.dbd
GalilTest_DBD += asyn.dbd
GalilTest_DBD += drvAsynSerialPort.dbd
GalilTest_DBD += drvAsynIPPort.dbd
GalilTest_DBD += motorSupport.dbd
GalilTest_DBD += calcSupport.dbd
GalilTest_DBD += sscanSupport.dbd
GalilTest_DBD += asSupport.dbd
GalilTest_DBD += busySupport.dbd
GalilTest_DBD += GalilSupport.dbd

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
GalilTest_SRCS += GalilTest_registerRecordDeviceDriver.cpp
GalilTest_SRCS_DEFAULT += GalilTestMain.cpp

#add a definition for each support application used by this application
GalilTest_LIBS += GalilSupport
GalilTest_LIBS += motor asyn busy calc sscan autosave
ifdef ONCRPC
ifeq ($(STATIC_BUILD),YES)
GalilTest_LIBS_WIN32 += oncrpc
endif
endif
ifdef SNCSEQ
GalilTest_LIBS += seq pv
endif
GalilTest_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
