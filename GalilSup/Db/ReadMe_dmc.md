# GMC Files Read Me

The GMC files in his directory are for code sent to the Galil controller. The gallil files mostly control homing behaviour they are:


## Homing Routines

File                             | What                                                              | Must have encoder
-------------------------------- | ----------------------------------------------------------------- | -----------------
galil_Home_Dummy_Axis.dmc        | Jogs a axis for 15s **USE ONLY IF YOU KNOW WHAT YOU ARE DOING**   | no 
galil_Home_Dummy_Do_Nothing.dmc  | Wait for 100ms and report homed                                   | no
galil_Home_Dummy_Power_Light.dmc | Turn power light on (servo here), wait 100ms and report homed     | no
galil_Home_FIneg.dmc             | Find encoder index in negative direction                          | yes
galil_Home_FIpos.dmc             | Find encoder index in positive direction                          | yes
galil_Home_ForwLimit.dmc         | Find forward limit switch                                         | no
galil_Home_ForwLimit_Home.dmc    |  Find forward limit switch, find home datum switch                | no
galil_Home_ForwLimit+FIneg.dmc   | Find forward limit switch, find encoder index                     | yes
galil_Home_Home.dmc              | Find home datum switch                                            | no
galil_Home_Home+FIneg.dmc        | Find home datum switch, find encoder index in negative direction  | yes
galil_Home_Home+FIpos.dmc        | Find home datum switch, find encoder index in positive direction  | yes
galil_Home_No_Home.dmc           | Do not move, do not zero (see note A)                             |no
galil_Home_Pin_Hole_Selector.dmc | IMAT homing for pin hole selector                                 | N/A
galil_Home_RevLimit.dmc          | Find reverse limit switch                                         | no
galil_Home_RevLimit_Home.dmc     | Find reverse limit switch, find home datum switch                 | no
galil_Home_RevLimit+FIpos.dmc    | Find reverse limit switch, find encoder index                     | yes
galil_Home_Thread.dmc            | Monitor galil threads until complete                              | N/A
galil_Piezo_Home.dmc             | Find edge, find edge in opposite direction; good for piezo motors | no


## Other Files

File                             | What                                                              | Must have encoder
-------------------------------- | ----------------------------------------------------------------- | -----------------
galil_Default_Footer.dmc         | Deal with command errors, tcp errors and limit switches           | N/A
galil_Default_Header.dmc         | Setup variables,  run homeA command in thread 1 and runs forever  | N/A
galil_Muon_Slits.dmc             | Drive to analogue value for muon instrument slits                 | N/A
galil_Oscillating_Collimator_Merlin.dmc | Code to run the oscillating collimator on MERLIN           | no

## Notes

An encoder index is a pulse at a position as the motor moves across it
A datum is a switch it switches on or off at the position depending on the direction of travel.
There may be multiple indexes on an encoder

### Note A

The galil_Home_No_Home is useful for when home val is non-zero in SECI and zero in IBEX it will prevent them interfering if home is accidentally pressed.
