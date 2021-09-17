#Makefile at top of application tree
TOP = .
include $(TOP)/configure/CONFIG

## only build on VS2010
ifneq ($(findstring 10.0,$(VCVERSION)),)
DIRS := configure GalilSup GalilTestApp iocBoot
endif

define DIR_template
 $(1)_DEPEND_DIRS = configure
endef
$(foreach dir, $(filter-out configure,$(DIRS)),$(eval $(call DIR_template,$(dir))))

GalilTestApp_DEPEND_DIRS += GalilSup
iocBoot_DEPEND_DIRS += $(filter %App,$(DIRS))

include $(TOP)/configure/RULES_TOP


