@echo off
REM first axis positions, number of points must agree with NumPoints below
caput -a -n %MYPVPREFIX%MOT:DMC01:Prof1:MTR0101:Positions 5 0 0.8726535498 1.4 2.0 3.0 
caput %MYPVPREFIX%MOT:DMC01:Prof1:MTR0101:UseAxis 1
caput %MYPVPREFIX%MOT:DMC01:Prof1:MTR0101:MoveMode Absolute

REM second axis positions, number of points must agree with NumPoints below
caput -a -n %MYPVPREFIX%MOT:DMC01:Prof1:MTR0102:Positions 5 0 0.8726535498 1.6 2.2 3.1
caput %MYPVPREFIX%MOT:DMC01:Prof1:MTR0102:UseAxis 1
caput %MYPVPREFIX%MOT:DMC01:Prof1:MTR0102:MoveMode Absolute

REM profile properties
caput %MYPVPREFIX%MOT:DMC01:Prof1:TimeMode Fixed
caput %MYPVPREFIX%MOT:DMC01:Prof1:FixedTime 0.1
caput %MYPVPREFIX%MOT:DMC01:Prof1:Acceleration 2 
caput %MYPVPREFIX%MOT:DMC01:Prof1:NumPoints 5

REM generate profile and print out any error messages
caput %MYPVPREFIX%MOT:DMC01:Prof1:Build 1
sleep 5
caget %MYPVPREFIX%MOT:DMC01:Prof1:BuildState
caget %MYPVPREFIX%MOT:DMC01:Prof1:BuildStatus
caget -S %MYPVPREFIX%MOT:DMC01:Prof1:BuildMessage

caget %MYPVPREFIX%MOT:DMC01:Prof1:MTR0101:MinPosition
caget %MYPVPREFIX%MOT:DMC01:Prof1:MTR0101:MaxPosition
caget %MYPVPREFIX%MOT:DMC01:Prof1:MTR0101:MaxVelocity
caget %MYPVPREFIX%MOT:DMC01:Prof1:MTR0101:MaxAcceleration

caget %MYPVPREFIX%MOT:DMC01:Prof1:MTR0102:MinPosition
caget %MYPVPREFIX%MOT:DMC01:Prof1:MTR0102:MaxPosition
caget %MYPVPREFIX%MOT:DMC01:Prof1:MTR0102:MaxVelocity
caget %MYPVPREFIX%MOT:DMC01:Prof1:MTR0102:MaxAcceleration

