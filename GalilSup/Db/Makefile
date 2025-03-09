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

DB += $(patsubst ../%, %, $(wildcard ../*.template))
DB += $(patsubst ../%, %, $(wildcard ../*.dmc))

#----------------------------------------------------
# If <anyname>.db template is not named <anyname>*.template add
# <anyname>_template = <templatename>
DB += galil_userdef_records8.template

# Galil program code for e.g. homing, installed to $(TOP)/gmc

# Tips for writing your own code from scratch
# 1. Recommend altering generated code instead of writing code from scratch.  Generated code is dumped to file at iocBoot
# 2. Homing programs should send home<Axis> = 0 at end of homing process (home process flag) (eg. MG "homeA", homeA)
# 3. Homing programs should send homed<Axis> = 1 at end of successful homing process (homed status flag)
# 4. LIMSWI programs for axis should send home<Axis> = 0 and should send homed<Axis> = 0 at software limit violation
# 5. LIMSWI programs for axis using limits as home must not send home<Axis> = 0 at hardware limits violation
# 6. Thread programs for axis not using limits as home should send home<Axis> = 0 at hardware limits violation
# 7. If using single thread avoid use of blocking commands (eg. WT100, AD, etc)

# Example galil code templates
# These are renamed and installed to a GMC directory for historic reasons
# See ReadMe_dmc.md for details
GMC += ReadMe_dmc.md
GMC += $(patsubst ../%.dmc, %.gmc, $(wildcard ../*.dmc))

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

ifdef T_A

%.gmc : ../%.dmc
	$(CP) $< $@

ifneq ($(BASE3_14),YES)
galil_userdef_records8.template$(DEP) : ../galil_userdef_records.template ../galil_userdef_records8.substitutions
endif

galil_userdef_records8.template ../O.Common/galil_userdef_records8.template : ../galil_userdef_records.template ../galil_userdef_records8.substitutions
	$(MSI3_15) -V -o $@ -S ../galil_userdef_records8.substitutions ../galil_userdef_records.template

endif
