epicsEnvSet("GALILCRATE","01")
calc("GCID", "$(GALILCRATE) - 1", 2, 2)

#Load motor records
dbLoadRecords("$(TOP)/db/galil_motor.db", "P=$(MYPVPREFIX)MOT,CC=$(GCID),CCP=$(GALILCRATE),IFSIM=#,IFNOTSIM=")

#Load extra features that have controller wide scope (eg.  Limit switch type, home switch type)
dbLoadRecords("$(TOP)/db/galil_ctrl_extras.db", "P=$(MYPVPREFIX)MOT,CC=$(GCID),CCP=$(GALILCRATE),IFSIM=#,IFNOTSIM=")

#Load extra features for axis/motors (eg. Motor type, encoder type)
dbLoadRecords("$(TOP)/db/galil_motor_extras.db", "P=$(MYPVPREFIX)MOT,CC=$(GCID),CCP=$(GALILCRATE),IFSIM=#,IFNOTSIM=")

#Load coordinate system features
#dbLoadTemplate("$(TOP)/GalilTestApp/Db/galil_coordinate_systems.substitutions")

#Load digital IO databases
#dbLoadTemplate("$(TOP)/GalilTestApp/Db/galil_digital_ports.substitutions")

#Load analog IO databases
#dbLoadTemplate("$(TOP)/GalilTestApp/Db/galil_analog_ports.substitutions")

#Load user defined functions
#dbLoadTemplate("$(TOP)/GalilTestApp/Db/galil_userdef_records.substitutions")
dbLoadRecords("$(TOP)/db/galil_userdef_records8.db", "P=$(MYPVPREFIX)MOT,CC=$(GCID),CCP=$(GALILCRATE),IFSIM=#,IFNOTSIM=")

#Load profiles
#dbLoadTemplate("$(TOP)/GalilTestApp/Db/galil_profileMoveController.substitutions")
#dbLoadTemplate("$(TOP)/GalilTestApp/Db/galil_profileMoveAxis.substitutions")
dbLoadRecords("$(TOP)/db/galil_profileMoveController.db", "P=$(MYPVPREFIX)MOT:,CC=$(GCID),CCP=$(GALILCRATE),IFSIM=#,IFNOTSIM=")
dbLoadRecords("$(TOP)/db/galil_profileMoveAxis.db", "P=$(MYPVPREFIX)MOT:,CC=$(GCID),CCP=$(GALILCRATE),IFSIM=#,IFNOTSIM=")

# GalilCreateController command parameters are:
#
# 1. Const char *portName 	- The name of the asyn port that will be created for this controller
# 2. Const char *address 	- The address of the controller
# 3. double updatePeriod	- The time in ms between datarecords 8ms minimum.  Async if controller + bus supports it, otherwise is polled/synchronous.

# Create a Galil controller
GalilCreateController("Galil", "130.246.51.169", 20)


# Create a Galil controller
#GalilCreateController("RIO", "192.168.0.101", 8)

# GalilCreateAxis command parameters are:
#
# 1. char *portName Asyn port for controller
# 2. char  axis A-H,
# 3. int   limits_as_home (0 off 1 on), 
# 4. char  *Motor interlock digital port number 1 to 8 eg. "1,2,4".  1st 8 bits are supported
# 5. int   Interlock switch type 0 normally open, all other values is normally closed interlock switch type

# Create the axis
GalilCreateAxis("Galil","A",1,"",1)
GalilCreateAxis("Galil","B",1,"",1)
GalilCreateAxis("Galil","C",1,"",1)
GalilCreateAxis("Galil","D",1,"",1)
GalilCreateAxis("Galil","E",1,"",1)
GalilCreateAxis("Galil","F",1,"",1)
GalilCreateAxis("Galil","G",1,"",1)
GalilCreateAxis("Galil","H",1,"",1)

# GalilCreateCSAxis command parameters are:
#
# 1. char *portName Asyn port for controller
# 2. char *forward Comma separated list of forward kinematic transforms eg. I=(A+B)/2,J=B-A
# 3. char *reverse Comma separated list of reverse kinematic transforms eg. A=I-J/2,B=I+J/2
#
#    Real axis are: A-H
#    Coordinate axis are: I-P
#    Variables are: Q-Z
#    16 Args maximum per equation
#
#    Real, csaxis, and variables are allowed in forward transform equation
#    Real, csaxis, and variables are allowed in reverse transform equation
#
# Create example coordinate system axis
# 2Slit example using real motors A, and B
# I = Slit centre coordinate system axis
# J = Slit width coordinate system axis

#Normal slit example
GalilCreateCSAxes("Galil","I=(A+B)/2,J=B-A","A=I-J/2,B=I+J/2")

#Example to lock slit motor B position, and move only A slit
#GalilCreateCSAxes("Galil","I=(A+B)/2,J=B-A","A=B-J,B=B")

#Example to swap slit blades using kinematic variable Z
#GalilCreateCSAxes("Galil","I=(A+B)/2,J=(Z==0)?B-A:A-B","A=(Z==0)?I-J/2:I+J/2,B=(Z==0)?I+J/2:I-J/2")

# GalilStartController command parameters are:
#
# 1. char *portName Asyn port for controller
# 2. char *code file to deliver to the controller we are starting. "" = use generated code
# 3. int   Burn to EEPROM conditions
#             0 = transfer code if differs from eeprom, dont burn code to eeprom, then finally execute/restart code.
#             1 = transfer code if differs from eeprom, burn code to eeprom, then finally execute/restart code.
#
# 4. int   display code. Set bit 1 to display generated code and or the code file specified.  Set bit 2 to display uploaded code

# Start the controller
GalilStartController("Galil", "", 0, 0)

#GalilStartController("RIO", "rio.gmc", 1, 0)

# GalilCreateProfile command parameters are:
#
# 1. char *portName Asyn port for controller
# 2. Int maxPoints in trajectory

# Create trajectory profiles
GalilCreateProfile("Galil", 2000)

