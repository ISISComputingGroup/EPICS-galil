# GMC Files Read Me 

The GMC files in his directory are for code sent to the Galil controller. The gallil files mostly control homing behaviour they are:


## Homing Routines

File                             | What                                                              | Must have encoder
-------------------------------- | ----------------------------------------------------------------- | -----------------
galil_Home_Dummy_Axis.gmc        | Jogs a axis for 15s **USE ONLY IF YOU KNOW WHAT YOU ARE DOING**   | no 
galil_Home_Dummy_Do_Nothing.gmc  | Wait for 100ms and report homed                                   | no
galil_Home_Dummy_Power_Light.gmc | Turn power light on (servo here), wait 100ms and report homed     | no
galil_Home_FIneg.gmc             | Find encoder index in negative direction                          | yes
galil_Home_FIpos.gmc             | Find encoder index in positive direction                          | yes
galil_Home_ForwLimit.gmc         | Find forward limit switch                                         | no
galil_Home_ForwLimit_Home.gmc    |  Find forward limit switch, find home datum switch                | no
galil_Home_ForwLimit+FIneg.gmc   | Find forward limit switch, find encoder index                     | yes
galil_Home_Home.gmc              | Find home datum switch                                            | no
galil_Home_Home+FIneg.gmc        | Find home datum switch, find encoder index in negative direction  | yes
galil_Home_Home+FIpos.gmc        | Find home datum switch, find encoder index in positive direction  | yes
galil_Home_No_Home.gmc           | Do not move, do not zero (see note A)                             |no
galil_Home_Pin_Hole_Selector.gmc | IMAT homing for pin hole selector                                 | N/A
galil_Home_RevLimit.gmc          | Find reverse limit switch                                         | no
galil_Home_RevLimit_Home.gmc     | Find reverse limit switch, find home datum switch                 | no
galil_Home_RevLimit+FIpos.gmc    | Find reverse limit switch, find encoder index                     | yes
galil_Home_Thread.gmc            | Monitor galil threads until complete                              | N/A
galil_Piezo_Home.gmc             | Find edge, find edge in opposite direction; good for piezo motors | no


## Other Files

File                             | What                                                              | Must have encoder
-------------------------------- | ----------------------------------------------------------------- | -----------------
galil_Default_Footer.gmc         | Deal with command errors, tcp errors and limit switches           | N/A
galil_Default_Header.gmc         | Setup variables,  run homeA command in thread 1 and runs forever  | N/A
galil_Muon_Slits.gmc             | Drive to analogue value for muon instrument slits                 | N/A


## Notes

An encoder index is a pulse at a position as the motor moves across it
A datum is a switch it switches on or off at the position depending on the direction of travel.
There may be multiple indexes on an encoder

### Note A

The galil_Home_No_Home is useful for when home val is non-zero in SECI and zero in IBEX it will prevent them interfering if home is accidentally pressed.
