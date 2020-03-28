#!/bin/bash

#From config/MASTER_RELEASE extract paths to modules that provide qegui screens
export galilpath=`grep "GALIL" config/GALILRELEASE | cut -d'=' -f2`

#From module top, add offset path to qegui screens
export galilpath=$galilpath/GalilSup/op/ui

#QEGUI path to screens
export QE_UI_PATH=$galilpath

#Determine Qt version
export QTVERSION=`echo $QTDIR | cut -b 14`

#Determine Qt style to use from version
if [ ${QTVERSION} = "4" ]; then
#Qt4 detected
   export QTSTYLE="plastique"
elif [ ${QTVERSION} = "5" ]; then
#Qt5 detected
   export QTSTYLE="fusion"
else
#Unknown assume Qt4
   export QTSTYLE="plastique"
fi

#Invoke QEGUI
qegui -style ${QTSTYLE} -e -m "R=Galil,RIO=RIO01:" galil_rio_ctrl.ui &

