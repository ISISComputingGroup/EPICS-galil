TOP = ..
include $(TOP)/configure/CONFIG

include $(TOP)/configure/RULES_DIRS

install :
	xcopy /E /Q /I /Y /D "\\isis\inst$$\Kits$$\CompGroup\ICP\Binaries\LibGalil\$(VCVERSION)\*" "$(VCVERSION)"

clean :
	$(RMDIR) "$(VCVERSION)"

uninstall : clean
