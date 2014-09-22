#!../../bin/linux-x86/GalilTest

< envPaths

cd ${TOP}

## Register all support components
dbLoadDatabase("dbd/GalilTest.dbd",0,0)
GalilTest_registerRecordDeviceDriver(pdbbase)

cd ${TOP}/iocBoot/${IOC}

epicsEnvSet("IOCNAME", "$(IOC)")
# this is to avoid a warning message about undefined macros from galil driver
epicsEnvSet("SIMULATE", "0")

##ISIS## Run IOC initialisation 
< $(IOCSTARTUP)/init.cmd

##ISIS## Load common DB records 
< $(IOCSTARTUP)/dbload.cmd

### Scan-support software
# crate-resident scan.  This executes 1D, 2D, 3D, and 4D scans, and caches
# 1D data, but it doesn't store anything to disk.  (See 'saveData' below for that.)
#dbLoadRecords("$(SSCAN)/sscanApp/Db/scan.db","P=IOC01:,MAXPTS1=8000,MAXPTS2=1000,MAXPTS3=10,MAXPTS4=10,MAXPTSH=8000")

### autosave
# specify additional directories in which to to search for included request files
set_requestfile_path("${GALIL}/GalilSup/Db", "")
set_requestfile_path("${MOTOR}/motorApp/Db", "")


epicsEnvSet("GALILCONFIG","$(ICPCONFIGROOT)/galil")

# how many galil crates we have
epicsEnvSet("GALILNUMCRATES", "7")

# this defines macros we can use for conditional loading later
calc("IFDMC01", "$(GALILNUMCRATES) >= 1", 4)
calc("IFDMC02", "$(GALILNUMCRATES) >= 2", 4)
calc("IFDMC03", "$(GALILNUMCRATES) >= 3", 4)
calc("IFDMC04", "$(GALILNUMCRATES) >= 4", 4)
calc("IFDMC05", "$(GALILNUMCRATES) >= 5", 4)
calc("IFDMC06", "$(GALILNUMCRATES) >= 6", 4)
calc("IFDMC07", "$(GALILNUMCRATES) >= 7", 4)

# Configure an example controller
< galil.cmd

#< autosave.cmd

## motor util package
dbLoadRecords("$(MOTOR)/db/motorUtil.db","P=$(MYPVPREFIX)$(IOCNAME):, PVPREFIX=$(MYPVPREFIX)")

##ISIS## Stuff that needs to be done after all records are loaded but before iocInit is called 
< $(IOCSTARTUP)/preiocinit.cmd

# Start the IOC
iocInit()

##ISIS## Stuff that needs to be done after iocInit is called e.g. sequence programs 
< $(IOCSTARTUP)/postiocinit.cmd

motorUtilInit("$(MYPVPREFIX)$(IOCNAME):")

# Save motor positions every 5 seconds
create_monitor_set("$(IOCNAME)_positions.req", 5, "P=$(MYPVPREFIX)MOT:,IFDMC01=$(IFDMC01),IFDMC02=$(IFDMC02),IFDMC03=$(IFDMC03),IFDMC04=$(IFDMC04),IFDMC05=$(IFDMC05),IFDMC06=$(IFDMC06),IFDMC07=$(IFDMC07)")

# Save motor settings every 30 seconds
create_monitor_set("$(IOCNAME)_settings.req", 30, "P=$(MYPVPREFIX)MOT:,IFDMC01=$(IFDMC01),IFDMC02=$(IFDMC02),IFDMC03=$(IFDMC03),IFDMC04=$(IFDMC04),IFDMC05=$(IFDMC05),IFDMC06=$(IFDMC06),IFDMC07=$(IFDMC07)")

# Save motor positions every 5 seconds
#create_monitor_set("galilTest_positions.req", 5,"P=DMC01:")
# Save motor settings every 30 seconds
#create_monitor_set("galilTest_settings.req", 30,"P=DMC01:")

