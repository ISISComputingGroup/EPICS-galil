TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

#=============================
# build an ioc application

PROD_IOC = GalilTest

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
GalilTest_DBD += motorSimSupport.dbd 
GalilTest_DBD += devSoftMotor.dbd 
GalilTest_DBD += utilities.dbd devIocStats.dbd pvdump.dbd icpconfig.dbd stdSupport.dbd pvcomplete.dbd asubFunctions.dbd caPutLog.dbd

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
GalilTest_SRCS += GalilTest_registerRecordDeviceDriver.cpp
GalilTest_SRCS_DEFAULT += GalilTestMain.cpp

#add a definition for each support application used by this application
GalilTest_LIBS += GalilSupport
GalilTest_LIBS += motorSimSupport softMotor motor asyn busy calc sscan autosave pvcomplete asubFunctions
GalilTest_LIBS += devIocStats pvdump easySQLite sqlite caPutLog icpconfig std
GalilTest_LIBS += seq pv utilities pugixml pcre
GalilTest_LIBS += $(EPICS_BASE_IOC_LIBS)

ifeq ($(STATIC_BUILD),YES)
GalilSupport_LIBS_WIN32 += Galil1 # galil2
GalilSupport_SYS_LIBS_WIN32 += delayimp
GalilSupport_LDFLAGS_WIN32 += /DELAYLOAD:Galil1.dll
endif

#GalilTest_LIBS_WIN32 += Galil1 # galil2
GalilTest_SYS_LIBS_Linux += Galil

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
