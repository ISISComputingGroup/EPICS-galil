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

# Galil program code for e.g. homing, installed to $(TOP)/gmc

# Tips for writing your own code from scratch
# 1. Recommend altering generated code instead of writing code from scratch.  Generated code is dumped to file at iocBoot
# 2. Homing programs should send home<Axis> = 0 at end of homing process (home process flag) (eg. MG "homeA", homeA)
# 3. Homing programs should send homed<Axis> = 1 at end of successful homing process (homed status flag)
# 4. LIMSWI programs for axis should send home<Axis> = 0 and should send homed<Axis> = 0 at software limit violation
# 5. LIMSWI programs for axis using limits as home must not send home<Axis> = 0 at hardware limits violation
# 6. Thread programs for axis not using limits as home should send home<Axis> = 0 at hardware limits violation
# 7. If using single thread avoid use of blocking commands (eg. WT100, AD, etc)

# galil_Muon_Slits is a routine used by the Muons

# Example galil code templates
GMC += ReadMe.md
GMC += galil_Default_Footer.gmc  galil_Default_Header.gmc  galil_Home_ForwLimit.gmc  galil_Home_ForwLimit_Home.gmc
GMC += galil_Home_Home.gmc  galil_Home_Home+FIneg.gmc  galil_Home_Home+FIpos.gmc  galil_Home_RevLimit.gmc
GMC += galil_Home_RevLimit_Home.gmc  galil_Piezo_Home.gmc  galil_Home_ForwLimit+FIneg.gmc  galil_Home_RevLimit+FIpos.gmc
GMC += galil_Home_Pin_Hole_Selector.gmc  galil_Home_Thread.gmc  galil_Home_Dummy_Axis.gmc
GMC += galil_Home_FIpos.gmc galil_Home_FIneg.gmc galil_Muon_Slits.gmc 
# Dummy routines mainly used for testing
GMC += galil_Home_Dummy_Do_Nothing.gmc galil_Home_Dummy_Power_Light.gmc

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
ifneq ($(BASE3_14),YES)
galil_userdef_records8.template$(DEP) : $(TOP)/GalilSup/Db/galil_userdef_records.template $(TOP)/GalilSup/Db/galil_userdef_records8.substitutions
endif

galil_userdef_records8.template ../O.Common/galil_userdef_records8.template : $(TOP)/GalilSup/Db/galil_userdef_records.template $(TOP)/GalilSup/Db/galil_userdef_records8.substitutions
	$(MSI3_15) -o $@ -S $(TOP)/GalilSup/Db/galil_userdef_records8.substitutions $(TOP)/GalilSup/Db/galil_userdef_records.template

