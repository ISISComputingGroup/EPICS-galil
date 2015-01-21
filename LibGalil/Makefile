TOP = ..
include $(TOP)/configure/CONFIG

LIBGALIL_VERSION = 1.6.4

ifneq ($(findstring windows,$(EPICS_HOST_ARCH)),) 
ISWIN32=YES
endif
ifneq ($(findstring win32,$(EPICS_HOST_ARCH)),) 
ISWIN32=YES
endif

include $(TOP)/configure/RULES_DIRS

install :
ifneq ($(ISWIN32),) 
	xcopy /E /Q /I /Y /D "\\isis\inst$$\Kits$$\CompGroup\ICP\Binaries\LibGalil\$(LIBGALIL_VERSION)\$(VCVERSION)\*" "$(VCVERSION)"
endif

clean :
ifneq ($(VCVERSION),)
	$(RMDIR) "$(VCVERSION)"
endif

uninstall : clean
