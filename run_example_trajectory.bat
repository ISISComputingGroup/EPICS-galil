@echo off
caput %MYPVPREFIX%MOT:DMC01:Prof1:Execute 1
sleep 1
caget %MYPVPREFIX%MOT:DMC01:Prof1:ExecuteState   
echo When finished press return
pause
caget %MYPVPREFIX%MOT:DMC01:Prof1:ExecuteState   
caget %MYPVPREFIX%MOT:DMC01:Prof1:ExecuteStatus  
caget -S %MYPVPREFIX%MOT:DMC01:Prof1:ExecuteMessage  

REM not sure these are used
rem caget %MYPVPREFIX%MOT:DMC01:Prof1:NumActualPulses
rem caget %MYPVPREFIX%MOT:DMC01:Prof1:NumPulses
rem caget %MYPVPREFIX%MOT:DMC01:Prof1:StartPulses
rem caget %MYPVPREFIX%MOT:DMC01:Prof1:EndPulses

REM not sure these are done yet
REM caput %MYPVPREFIX%MOT:DMC01:Prof1:Readback 1
REM sleep 5
REM caget %MYPVPREFIX%MOT:DMC01:Prof1:ReadbackState
REM caget %MYPVPREFIX%MOT:DMC01:Prof1:ReadbackStatus

REM caget %MYPVPREFIX%MOT:DMC01:Prof1:MTR0101:Readbacks
REM caget %MYPVPREFIX%MOT:DMC01:Prof1:MTR0101:FollowingErrors

REM caget %MYPVPREFIX%MOT:DMC01:Prof1:MTR0102:Readbacks  
REM caget %MYPVPREFIX%MOT:DMC01:Prof1:MTR0102:FollowingErrors  


