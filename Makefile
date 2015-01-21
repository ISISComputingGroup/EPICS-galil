#Makefile at top of application tree
TOP = .
include $(TOP)/configure/CONFIG

DIRS := configure LibGalil GalilSup GalilTestApp iocBoot

define DIR_template
 $(1)_DEPEND_DIRS = configure
endef
$(foreach dir, $(filter-out configure,$(DIRS)),$(eval $(call DIR_template,$(dir))))

GalilSup_DEPEND_DIRS += LibGalil
GalilTestApp_DEPEND_DIRS += GalilSup
iocBoot_DEPEND_DIRS += $(filter %App,$(DIRS))

include $(TOP)/configure/RULES_TOP


