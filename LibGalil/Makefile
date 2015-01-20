TOP = ..
include $(TOP)/configure/CONFIG

include $(TOP)/configure/RULES

install :
ifeq ($(OS_CLASS),"WIN32")
	xcopy /E /Q /I /Y /D "\\isis\inst$$\Kits$$\CompGroup\ICP\Binaries\LibGalil\$(VCVERSION)\*" "$(VCVERSION)"
endif

clean ::
ifeq ($(OS_CLASS),"WIN32")
	$(RMDIR) "$(VCVERSION)"
endif

uninstall : clean
