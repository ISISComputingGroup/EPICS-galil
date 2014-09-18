TOP=../..
include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE

#----------------------------------------------------
#  Optimization of db files using dbst (DEFAULT: NO)
#DB_OPT = YES

#----------------------------------------------------
# Create and install (or just install) into <top>/db
# databases, templates, substitutions like this

#----------------------------------------------------
# If <anyname>.db template is not named <anyname>*.template add
# <anyname>_template = <templatename>

DB += galil_userdef_records8.template

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

galil_userdef_records8.template ../O.Common/galil_userdef_records8.template : $(TOP)/galilSup/Db/galil_userdef_records.template $(TOP)/galilSup/Db/galil_userdef_records8.substitutions
	$(MSI) -o $@ -S $(TOP)/galilSup/Db/galil_userdef_records8.substitutions $(TOP)/galilSup/Db/galil_userdef_records.template
