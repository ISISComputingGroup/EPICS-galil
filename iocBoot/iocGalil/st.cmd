#!../../bin/linux-x86/GalilTestApp

< envPaths

cd ${TOP}

## Register all support components
dbLoadDatabase("dbd/GalilTestApp.dbd",0,0)
GalilTestApp_registerRecordDeviceDriver(pdbbase)

cd ${TOP}/iocBoot/${IOC}

epicsEnvSet("IOCNAME", "$(IOC)")
# this is to avoid a warning message about undefined macros from galil driver
epicsEnvSet("SIMULATE", "0")

### Scan-support software
# crate-resident scan.  This executes 1D, 2D, 3D, and 4D scans, and caches
# 1D data, but it doesn't store anything to disk.  (See 'saveData' below for that.)
#dbLoadRecords("$(SSCAN)/sscanApp/Db/scan.db","P=IOC01:,MAXPTS1=8000,MAXPTS2=1000,MAXPTS3=10,MAXPTS4=10,MAXPTSH=8000")

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

# Start the IOC
iocInit()

# Save motor positions every 5 seconds
#create_monitor_set("galilTestApp_positions.req", 5,"P=DMC01:")
# Save motor settings every 30 seconds
#create_monitor_set("galilTestApp_settings.req", 30,"P=DMC01:")

motorUtilInit("$(MYPVPREFIX)$(IOCNAME):")
