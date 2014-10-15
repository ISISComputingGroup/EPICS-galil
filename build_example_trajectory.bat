@echo off

set MOT1=MTR0101
set MOT2=MTR0102

for /L %%i in (1,1,8) do (
    caput %MYPVPREFIX%MOT:DMC01:Prof1:MTR010%%i:UseAxis 0
)

REM first axis positions, number of points must agree with NumPoints below
REM caput -a -n %MYPVPREFIX%MOT:DMC01:Prof1:%MOT1%:Positions 5 0 0.8726535498 1.4 2.0 3.0 
caput %MYPVPREFIX%MOT:DMC01:Prof1:%MOT1%:UseAxis 1
caput %MYPVPREFIX%MOT:DMC01:Prof1:%MOT1%:MoveMode Absolute

REM second axis positions, number of points must agree with NumPoints below
REM caput -a -n %MYPVPREFIX%MOT:DMC01:Prof1:%MOT2%:Positions 5 0 0.8726535498 1.6 2.2 3.1
caput %MYPVPREFIX%MOT:DMC01:Prof1:%MOT2%:UseAxis 1
caput %MYPVPREFIX%MOT:DMC01:Prof1:%MOT2%:MoveMode Absolute

REM profile properties
caput %MYPVPREFIX%MOT:DMC01:Prof1:TimeMode Fixed
caput %MYPVPREFIX%MOT:DMC01:Prof1:FixedTime 6
REM caput %MYPVPREFIX%MOT:DMC01:Prof1:Acceleration 2 
REM caput %MYPVPREFIX%MOT:DMC01:Prof1:NumPoints 5

REM generate profile and print out any error messages
caput %MYPVPREFIX%MOT:DMC01:Prof1:Build 1
sleep 5
caget %MYPVPREFIX%MOT:DMC01:Prof1:BuildState
caget %MYPVPREFIX%MOT:DMC01:Prof1:BuildStatus
caget -S %MYPVPREFIX%MOT:DMC01:Prof1:BuildMessage

caget %MYPVPREFIX%MOT:DMC01:Prof1:%MOT1%:MinPosition
caget %MYPVPREFIX%MOT:DMC01:Prof1:%MOT1%:MaxPosition
caget %MYPVPREFIX%MOT:DMC01:Prof1:%MOT1%:MaxVelocity
caget %MYPVPREFIX%MOT:DMC01:Prof1:%MOT1%:MaxAcceleration

caget %MYPVPREFIX%MOT:DMC01:Prof1:%MOT2%:MinPosition
caget %MYPVPREFIX%MOT:DMC01:Prof1:%MOT2%:MaxPosition
caget %MYPVPREFIX%MOT:DMC01:Prof1:%MOT2%:MaxVelocity
caget %MYPVPREFIX%MOT:DMC01:Prof1:%MOT2%:MaxAcceleration

