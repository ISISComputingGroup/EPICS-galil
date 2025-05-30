//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// Licence as published by the Free Software Foundation; either
// version 2.1 of the Licence, or (at your option) any later version.
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public Licence for more details.
//
// You should have received a copy of the GNU Lesser General Public
// Licence along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
//
// Contact details:
// Mark Clift
// email: padmoz@tpg.com.au

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#if defined(_WIN32) /* _WIN32 include x64 */
#include <windows.h>
#endif /* _WIN32 */
#include <iostream>  //cout
#include <sstream>   //ostringstream istringstream
#include <typeinfo>  //std::bad_typeid
#include <vector>
#include <algorithm> //std::replace

using namespace std; //cout ostringstream vector string

#include <epicsString.h>
#include <iocsh.h>
#include <epicsThread.h>
#include <errlog.h>
#include <initHooks.h>
#include <shareLib.h>

#include <asynOctetSyncIO.h>

#include "GalilController.h"
#include <epicsExport.h>

static void pollServicesThreadC(void *pPvt);
static void axisStatusThreadC(void *pPvt);
static void eventMonitorThreadC(void *pPvt);
static const char* lookupStopCode(int sc);

// These are the GalilAxis methods

/** Creates a new GalilAxis object.
  * \param[in] pC Pointer to the GalilController to which this axis belongs. 
  * \param[in] axisName A-H
  * \param[in] limit_as_home 0=no, 1=yes. Using a limit switch as home
  * \param[in] enables_string A coma separated list of digital input(s) to use for motor enable/disable function
  * \param[in] motor enable/disable switch type
  */
GalilAxis::GalilAxis(class GalilController *pC, //Pointer to controller instance
		     char *axisname,		//axisname A-H
		     int limit_as_home,		//0=no, 1=yes. Using a limit switch as home
		     char *enables_string,	//digital input(s) to use for motor enable/disable function
		     int switch_type)		//motor enable/disable switch type
  : asynMotorAxis(pC, (toupper(axisname[0]) - AASCII)),
    pC_(pC), last_encoder_position_(0), smoothed_encoder_position_(0), encoder_smooth_factor_(0.0), motor_dly_(0.0),
    first_poll_(true),encDirOk_(true), pollRequest_(10, sizeof(int))
{
  string limit_code;				//Code generated for limits interrupt on this axis
  string digital_code;				//Code generated for digital interrupt related to this axis
  string thread_code;				//Code generated for the axis (eg. home code, limits response)

  //Initial default for stoppedTime
  epicsTimeGetCurrent(&stop_begint_);
  stop_nowt_ = stop_begint_;
 
  //encoder ratio has not been set yet
  encmratioset_ = false;

  //Store axis name in axis class instance
  axisName_ = (char)(toupper(axisname[0]));

  //Used to check thread status on controller
  //Used to set galil code variables too
  //Store axis name in axisList
  pC_->axisList_[pC_->numAxes_] = axisName_;
  //Increment internal axis counter
  pC_->numAxes_++;

  //Create stop time reset event
  stoppedTimeReset_ = epicsEventMustCreate(epicsEventEmpty);
  //Create begin motion event
  beginEvent_ = epicsEventMustCreate(epicsEventEmpty);
  //Create stop motion event
  stopEvent_ = epicsEventMustCreate(epicsEventEmpty);
  //Create caller event
  callerEvent_ = epicsEventMustCreate(epicsEventEmpty);
  //Create event monitor start event
  eventMonitorStart_ = epicsEventMustCreate(epicsEventEmpty);
  //Create event monitor done event
  eventMonitorDone_ = epicsEventMustCreate(epicsEventEmpty);

  //store settings, and set defaults
  setDefaults(limit_as_home, enables_string, switch_type);
  //store the motor enable/disable digital IO setup
  store_motors_enable();

  //Generate the code for this axis based on specified settings
  //Initialize the code generator
  initialize_codegen(thread_code, limit_code, digital_code);
  //Generate code for limits interrupt.  Motor behaviour on limits active
  gen_limitcode(limit_code);
  //Generate code for axis homing routine
  gen_homecode(thread_code);
  /* insert motor interlock code into thread A */
  if (axisName_ == 'A') {
        thread_code += "IF(mlock=1);";
        thread_code += "II ,,dpon,dvalues;ENDIF\n";
  }
  //Insert the final jump statement for the current thread code
  thread_code += "JP #THREAD" + string(1, axisName_) + "\n";

  //Copy this axis code into the controller class code buffers
  pC->thread_code_ += thread_code;
  pC->limit_code_ += limit_code;
  pC->digital_code_ += digital_code;
  
  // Create the thread that will service poll requests
  // To write to the controller
  epicsThreadCreate("pollServices", 
                    epicsThreadPriorityMax,
                    epicsThreadGetStackSize(epicsThreadStackMedium),
                    (EPICSTHREADFUNC)pollServicesThreadC, (void *)this);

  // Create the event monitor thread
  epicsThreadCreate("eventMonitor", 
                    epicsThreadPriorityMax,
                    epicsThreadGetStackSize(epicsThreadStackMedium),
                    (EPICSTHREADFUNC)eventMonitorThreadC, (void *)this);

  axisStatusRunning_ = true;
  axisStatusShutRequest_ = epicsEventMustCreate(epicsEventEmpty);
  axisStatusShutdown_ = epicsEventMustCreate(epicsEventEmpty);
  pC_->setDoubleParam(axisNo_, pC_->GalilStatusPollDelay_, 1);
  // Create the thread for polling axis and encoder status
  epicsThreadCreate("GalilAxisStatusPoll",
                    epicsThreadPriorityLow,
                    epicsThreadGetStackSize(epicsThreadStackMedium),
                    (EPICSTHREADFUNC)axisStatusThreadC, (void *)this);

  // We assume motor with encoder
  setIntegerParam(pC->motorStatusGainSupport_, 1);
  setIntegerParam(pC->motorStatusHasEncoder_, 1);
  //Wrong limit protection not stopping motor right now
  setIntegerParam(pC->GalilWrongLimitProtectionStop_, 0);
  //Default stall/following error status
  setIntegerParam(pC_->motorStatusSlip_, 0);
  callParamCallbacks();
}

//GalilAxis destructor
GalilAxis::~GalilAxis()
{
  //Set flag axis shut down in progress
  shuttingDown_ = true;
  //Now flag set, send eventMonitor thread to shutdown
  epicsEventSignal(eventMonitorStart_);
  //Shutdown axis status thread
  axisStatusShutdown();
  //Free RAM used
  free(enables_string_);
  if (profilePositions_ != NULL)
     free(profilePositions_);
  if (profileBackupPositions_ != NULL)
     free(profileBackupPositions_);

  //Destroy events
  //Sleep to preempt this thread, and give time
  //for event Monitor thread to exit
  epicsThreadSleep(.002);
  epicsEventDestroy(beginEvent_);
  epicsEventDestroy(stopEvent_);
  epicsEventDestroy(callerEvent_);
  epicsEventDestroy(eventMonitorStart_);
  epicsEventDestroy(eventMonitorDone_);
  epicsEventDestroy(stoppedTimeReset_);
  epicsEventDestroy(axisStatusShutdown_);
  epicsEventDestroy(axisStatusShutRequest_);
}

/*--------------------------------------------------------------------------------*/
/* Store settings, set defaults for motor */
/*--------------------------------------------------------------------------------*/

asynStatus GalilAxis::setDefaults(int limit_as_home, char *enables_string, int switch_type)
{
   //const char *functionName = "GalilAxis::setDefaults";

   //Five polls considered minimum to detect move start, stop
   double multiplier = 5.0 / (BEGIN_TIMEOUT / (pC_->updatePeriod_ / 1000.0));
   //Min multiplier is 1
   multiplier = (multiplier > 1) ? multiplier : 1;

   //Not shutting done
   shuttingDown_ = false;

   //Move method should not signal caller by default
   signalCaller_ = false;

   //Move has not been pushed to this axis Motor Record
   moveThruRecord_ = false;

   //Tell poller we dont want signal when events occur
   requestedEventSent_ = true;

   //Default event timeout
   requestedTimeout_ = BEGIN_TIMEOUT * multiplier;

   homingRoutineName = "";

   //Store limits as home setting						       
   limit_as_home_ = (limit_as_home > 0) ? 1 : 0;

   //Store switch type setting for motor enable/disable function			       
   switch_type_ = (switch_type > 0) ? 1 : 0;

   //Store motor enable/disable string
   enables_string_ = (char *)calloc(strlen(enables_string) + 1, sizeof(char));
   strcpy(enables_string_, enables_string);
        
   //Invert ssi flag
   invert_ssi_ = false;

   //Possible encoder stall not detected
   pestall_detected_ = false;

   //This axis is not performing a deferred move
   deferredMove_ = false;

   //Store axis in ParamList
   setIntegerParam(pC_->GalilAxis_, axisNo_);

   //Give default readback values for positions, movement direction
   motor_position_ = 0;
   last_encoder_position_ = 0;
   encoder_position_ = 0;
   direction_ = 1;

   //Pass default step count/aux encoder value to motorRecord
   setDoubleParam(pC_->motorPosition_, motor_position_);
   //Pass default encoder value to motorRecord
   setDoubleParam(pC_->motorEncoderPosition_, encoder_position_);
   //Pass default direction value to motorRecord
   setIntegerParam(pC_->motorStatusDirection_, direction_);

   setIntegerParam(pC_->motorStatusMoving_, 0);
   setIntegerParam(pC_->motorStatusDone_, 1);

   //Motor not homing now
   //This flag does not include JAH
   homing_ = false;
   //This flag does include JAH
   setIntegerParam(pC_->GalilHoming_, 0);
   //Custom home routine hasn't been provided
   customHome_ = false;

   //Motor stop mesg not sent to pollServices thread for stall or wrong limit
   stopSent_ = false;

   //Motor record post mesg has not been sent to pollServices thread
   postExecuted_ = postSent_ = false;

   //Homed mesg not sent to pollServices thread
   homedExecuted_ = homedSent_ = false;

   //Sync encoded stepper at stop message not sent to pollServices
   syncEncodedStepperAtStopSent_ = syncEncodedStepperAtStopExecuted_ = false;

   //Sync encoded stepper at encoder move message not sent to pollServices
   syncEncodedStepperAtEncSent_ = false;

   //Motor power auto on/off mesg not sent to pollServices thread yet
   autooffSent_ = false;

   //Motor brake auto on mesg not sent to pollServices thread yet
   autobrakeonSent_ = false;

   //Axis not ready until necessary motor record fields have been pushed into driver
   axisReady_ = false;

   //Have not allocated profile data backup array
   //Data restored automatically after profile built
   profileBackupPositions_ = NULL;

   //We dont need to restore any profile data now
   restoreProfile_ = false;

   //Jog after home not in progress
   jogAfterHome_ = false;

   //Dont use CSAxis dynamics yet
   useCSADynamics_ = false;

   //Driver is not requesting axis stop
   stopInternal_ = false;

   //Driver has not stopped axis motor record
   stoppedMR_ = false;

   //Default motor record write enable
   pC_->setIntegerParam(axisNo_, pC_->GalilMotorSetValEnable_, 0);

   //Default motor record stop
   pC_->setIntegerParam(axisNo_, pC_->GalilMotorRecordStop_, 0);
   
   //Default related csaxes list
   csaxesList_[0] = '\0';

   //Default softLimits
   lowLimit_ = 0.0;
   highLimit_ = 0.0;
   
   //Operator has not used MR SET field yet
   setPositionIn_ = false;
   setPositionOut_ = false;

   return asynSuccess;
}

/*--------------------------------------------------------------------------------*/
/* Store motor enable/disable settings for motor in controller structure */
/*--------------------------------------------------------------------------------*/

void GalilAxis::store_motors_enable(void)
{
   int motors_index;			/* The current index for the motor array */
   int i,k;				/* General loop variable */
   char state = switch_type_;		/* The state we are adding to the list */
   struct Galilmotor_enables *motor_enables=NULL;  //Convenience pointer to GalilController motor_enables[digport]
   int digport[8];
   int add_motor;				/* Add motor to list flag */

   //Load default value into digport before reading user specified values
   for (i = 0;i < 8;i++)
      digport[i] = 0;

   //retrieve port numbers from user specified parameter
   sscanf(enables_string_,"%d,%d,%d,%d,%d,%d,%d,%d",&digport[0],&digport[1],&digport[2],&digport[3],&digport[4],&digport[5],&digport[6],&digport[7]);

   k = 0;
   //Loop until we hit our loaded default
   while (digport[k] != 0) {
      //Did user specify interlock function
      if (digport[k]>0 && digport[k]<9) {
         //Retrieve structure for digital port from controller instance
         motor_enables = (Galilmotor_enables *)&pC_->motor_enables_[digport[k]-1];
         motors_index = (int)strlen(motor_enables->motors);
         //Add motor, and digital IO state provided into motor_enables structure
         add_motor = 1;
         // Check to make sure motor is not already in list
         for (i=0;i<motors_index;i++) {
            if (motor_enables->motors[i] == axisName_)
               add_motor = 0;
         }
         if (add_motor) {
            motor_enables->motors[motors_index] = axisName_;
            motor_enables->motors[motors_index + 1] = '\0';
            //Interrupt for digital IO can only be programmed to occur for one state
            //So we only pickup the last specified state for this digital in port
            motor_enables->disablestates[motors_index] = state;
            motor_enables->disablestates[motors_index + 1] = '\0';
         }
      }
      //Move on to next digital port specified by user
      k++;
   }//While
}

/*--------------------------------------------------------------------------------*/
/* Initialize code buffers, insert program labels, set generator variables */

void GalilAxis::initialize_codegen(string &thread_code,
			  	   string &limit_code,
			  	   string &digital_code)
{
  //Program label for digital input interrupt program
  string digital_label="#ININT\n";

  /*Empty code buffers for this axis*/
  thread_code.clear();
  limit_code.clear();
		
  //Insert code to start motor thread that will be constructed
  //thread 0 (motor A) is auto starting
  if (axisName_ != 'A')
     pC_->card_code_ += "XQ #THREAD" + string(1, axisName_) + "," + tsp(axisNo_) + "\n";

  //Insert label for motor thread we are constructing	
  thread_code += "#THREAD" + string(1, axisName_) + "\n";

  //Insert limit switch interrupt label, if not done so already
  if (!pC_->rio_ && pC_->limit_code_.empty()) {
     //setup #LIMSWI label
     pC_->limit_code_ = "#LIMSWI\n";
  }

  //Setup ININT program label for digital input interrupts.  Used for motor enable/disable.
  if (pC_->digitalinput_init_ == false && strcmp(enables_string_, "") != 0) {
     //Insert digital input program label #ININT
     digital_code = digital_label;
     //Insert code to initialize dpoff (digital ports off) used for motor interlocks management
     digital_code += "dpoff=dpon\n";
     //Digital input label has been inserted
     pC_->digitalinput_init_ = true;
  }
  else	//Empty digital input code buffer
     digital_code.clear();
}

/*--------------------------------------------------------------------------------*/
/* Generate the required limit code for this axis */

void GalilAxis::gen_limitcode(string &limit_code)
{
  string lc;		//Local limit code

  //Setup the LIMSWI interrupt routine. The Galil Code Below, is called once per limit activate on ANY axis **
  //Determine axis that requires stop based on stop code and moving status
  //Set user desired deceleration, motor stop occurs automatically
  //Hitting limit when homing is normal
  lc = "IF(((_SC?=2)|(_SC?=3))&(_BG?=1))\nDC?=limdc?;VDS=limdc?;VDT=limdc?;ENDIF\n";

  //Replace ? symbol with axisName_
  replace( lc.begin(), lc.end(), '?', axisName_);

  //Append code to limit code buffer
  limit_code += lc;
}

/*--------------------------------------------------------------------------------*/
/* Generate galil code that checks if possible to move in direction specified by home
   jog speed (hjs)*/
/*--------------------------------------------------------------------------------*/

void GalilAxis::gen_EnsureOkToMove(string &tc)
{
  if (pC_->model_[3] == '1' || pC_->model_[3] == '2')// Model 21x3 does not have LD (limit disable) command
     tc += "IF(((_LR?=1)&(hjs?<0))|((_LF?=1)&(hjs?>0)))\n";
  else// All other models have LD (limit disable) command
     tc += "IF((((_LR?=1)|(_LD?>1))&(hjs?<0))|(((_LF?=1)|(_LD?=1)|(_LD?=3))&(hjs?>0)))\n";
}

/*--------------------------------------------------------------------------------*/
/* Generate home code.*/
/*--------------------------------------------------------------------------------*/

void GalilAxis::gen_homecode(string &thread_code)
{
   string tc;	//Temporary thread code string

   tc = "IF(home?=1)\n";
   
   //Setup home code
   //hjog%c=0 Home just started, no galil code home jogs have happened
   //hjog%c=1 Jog off limit switch has occurred, or skipped
   //hjog%c=2 Jog to find home active has occurred, or skipped
   //hjog%c=3 Jog to find requested home edge has occurred, or skipped
   //hjog%c=4 Jog to find encoder index has occurred, or skipped
   //hjog%c=5 Final home position found

   //Correct limit active, and no galil code home jogs have happened
   tc += "IF((((_LR?=0)&(hjs?>0))|((_LF?=0)&(hjs?<0)))&(hjog?=0))\n";
   //If (limits as home false and home switch inactive, or limits as home true) AND
   //Amplifier is on and axis not moving     
   tc += "IF((((ulah?=0)&(_HM?=hswiact?))|(ulah?=1))&(_MO?=0)&(_BG?=0))\n";
   //Ensure ok to move in desired direction
   gen_EnsureOkToMove(tc);
   //Jog off limit
   tc += "JG?=hjs?;DC?=limdc?;WT10;BG?;hjog?=1;ENDIF\n";
   //Else If limits as home false and home switch active then home active already found
   tc += "ELSE;IF((ulah?=0)&(_HM?=hswact?)&(_MO?=0)&(_BG?=0));hjog?=2;ENDIF;ENDIF;ENDIF\n";

   //Stop when limit deactivates
   tc += "IF((((_LR?=1)&(hjs?>0))|((_LF?=1)&(hjs?<0)))&(hjog?=1)&(_BG?=1));ST?;ENDIF\n";

   //If limit inactive, axis stopped and jog off limit happened
   tc += "IF((((_LR?=1)&(hjs?>0))|((_LF?=1)&(hjs?<0)))&(hjog?=1)&(_BG?=0))\n";
   //If (limits as home false and home switch inactive) AND
   //Amplifier is on and axis not moving
   tc += "IF((ulah?=0)&(_HM?=hswiact?)&(_MO?=0)&(_BG?=0))\n";
   //Ensure ok to move in desired direction
   gen_EnsureOkToMove(tc);
   //Move to find home active state, else skip step
   tc += "JG?=hjs?;DC?=limdc?;WT10;BG?;hjog?=2;ENDIF;ELSE;hjog?=2;ENDIF;ENDIF\n";

   //If use limits as home false and home switch active and axis moving
   //Stop motor when home switch becomes active
   tc += "IF((ulah?=0)&(_HM?=hswact?)&(_MO?=0)&(_BG?=1)&(hjog?>-1)&(hjog?<3))\n";
   tc += "ST?;DC?=limdc?;hjog?=2;ENDIF\n";

   //If use limits as home false and home switch active and axis stopped
   //Find home edge requested
   tc += "IF((ulah?=0)&(_HM?=hswact?)&(hjog?=2)&(_MO?=0)&(_BG?=0))\n";
   //Ensure ok to move
   if (pC_->model_[3] == '1' || pC_->model_[3] == '2')// Model 21x3 does not have LD (limit disable) command
      tc += "IF(((_LR?=1)&(hswedg=0))|((_LF?=1)&(hswedg=1)))\n";
   else
      tc += "IF((((_LR?=1)|(_LD?>1))&(hswedg=0))|(((_LF?=1)|(_LD?=1)|(_LD?=3))&(hswedg=1)))\n";
   //Move to home edge requested
   tc += "SP?=hjs?;DC?=limdc?;WT10;FE?;BG?;hjog?=3;ENDIF\n";
   //Else if use limits as home true skip find home edge step
   tc += "ELSE;IF((ulah?=1)&(hjog?=2));hjog?=3;ENDIF;ENDIF\n";

   //Find encoder index
   tc += "IF((hjog?=3)&(_BG?=0))\n";
   //Ensure ok to move in desired direction
   gen_EnsureOkToMove(tc);
   //Start index search
   if (pC_->model_[3] == '1' || pC_->model_[3] == '2')// Model 21x3 does not latch index and return, limit deceleration used
      tc += "IF((_MO?=0)&(ueip?=1)&(ui?=1));JG?=hjs?;DC?=limdc?;FI?;WT10;BG?;hjog?=4\n";
   else //Model 4xxx will latch index and return, so normal deceleration used
      tc += "IF((_MO?=0)&(ueip?=1)&(ui?=1));JG?=hjs?;DC?=nrmdc?;FI?;WT10;BG?;hjog?=4\n";

   //If no encoder we are home already
   tc += "ELSE;hjog?=5;ENDIF;ENDIF;ENDIF\n";
   //If encoder index complete we are home
   tc += "IF((hjog?=4)&(_BG?=0));hjog?=5;ENDIF\n";
   //Unset home flag
   if (pC_->model_[3] == '1' || pC_->model_[3] == '2')// Model 21x3 does not have LD (limit disable) command
      tc += "IF((_LR?=1)&(_LF?=1)&(hjog?=5)&(_BG?=0))\n";
   else
      tc += "IF(((_LR?=1)|(_LD?>1))&((_LF?=1)|(_LD?=1)|(_LD?=3))&(hjog?=5)&(_BG?=0))\n";
   //Flag homing complete, and send unsolicited messages to epics informing of homed status
   tc += "WT10;hjog?=0;home?=0;homed?=1;MG \"homed?\",homed?;ENDIF;ENDIF\n";

   //Replace ? symbol with axisName_
   replace( tc.begin(), tc.end(), '?', axisName_);

   //Append home code to thread code buffer
   thread_code += tc;
}

/*  Sets acceleration and velocity for this axis
  * \param[in] acceleration Units=steps/sec/sec.
  * \param[in] velocity Units=steps/sec.*/
asynStatus GalilAxis::setAccelVelocity(double acceleration, double velocity, bool setVelocity)
{
   double mres;			//MotorRecord mres
   double egu_after_limit;	//Egu after limit parameter
   double distance;		//Used for kinematic calcs
   long deceleration;		//limits deceleration final value sent to controller
   double decel;		//double version of above
   double accel;		//Adjusted acceleration/deceleration for normal moves
   double vel;			//Velocity final value sent to controller
   char c = axisName_;
   string cmd = "";
   int status;

   //Set acceleration and deceleration for normal moves
   //Ensure acceleration is within maximum for this model
   acceleration = (acceleration > pC_->maxAcceleration_) ? pC_->maxAcceleration_ : acceleration;
   //Find closest hardware setting
   accel = (long)lrint(acceleration/1024.0) * 1024;
   accel = (accel == 0) ? 1024 : accel; // galil manual says AC and DC must be at least 1024
   //Format the command string
   cmd = "AC" + string(1, c) + "=" + tsp(accel, 0) + ";DC" + string(1, c) + "=" + tsp(accel, 0);

   //Are we done here?
   if (!setVelocity) {
      strcpy(pC_->cmd_, cmd.c_str());
      status = pC_->sync_writeReadController();
      return (asynStatus)status;
   }

   //Set velocity
   //Find closest hardware setting
   vel = (long)lrint(velocity/2.0) * 2;
   cmd += ";SP" + string(1, c) + "=" + tsp(vel, 0);

   //Set deceleration when limit activated
   //Retrieve required values from paramList
   pC_->getDoubleParam(axisNo_, pC_->motorResolution_, &mres);
   pC_->getDoubleParam(axisNo_, pC_->GalilAfterLimit_, &egu_after_limit);
   //recalculate limit deceleration given velocity and allowed steps after home/limit activation
   distance = (egu_after_limit < fabs(mres)) ? fabs(mres) : egu_after_limit;
   //suvat equation for acceleration
   decel = fabs((velocity * velocity)/((distance/mres) * 2.0));
   //Find closest hardware setting
   deceleration = (long)(lrint(decel/1024.0) * 1024);
   deceleration = (deceleration == 0) ? 1024 : deceleration; // galil manual says AC and DC must be at least 1024
   //Ensure deceleration is within maximum for this model
   deceleration = (deceleration > pC_->maxAcceleration_) ? pC_->maxAcceleration_ : deceleration;
   //Set limit deceleration galil code variable
   limdc_ = (double)deceleration;
   cmd += ";limdc" + string(1, c) + "=" + tsp(limdc_, 0);
   cmd += ";hjgdc" + string(1, c) + "=" + tsp(limdc_, 0);  // for old homing code
   //Set normal deceleration galil code variable
   cmd += ";nrmdc" + string(1, c) + "=" + tsp(accel, 0);
   //Write the command
   strcpy(pC_->cmd_, cmd.c_str());
   status = pC_->sync_writeReadController();

   return (asynStatus)status;
}

//Tell this axis to use CSAxis dynamics
void GalilAxis::setCSADynamics(double acceleration, double velocity)
{
   csaAcceleration_ = acceleration;
   csaVelocity_ = velocity;
   useCSADynamics_ = true;
}

/** Move the motor to an absolute position
  * \param[in] position  The absolute position to move to Units=steps
  * \param[in] signalCaller  Should axisEventMonitor signal caller when done */
asynStatus GalilAxis::moveThruMotorRecord(double position, bool signalCaller)
{
   double mres;		//Axis motor resolution
   double eres;		//Axis encoder resolution
   int status;		//Success
   double off;		//Motor record offset
   int dir, dirm;	//Motor record dir, direction multiplier
   int ueip;		//Motor record ueip
   double drbv;		//Dial readback value
   double drbvmpos;	//Dial readback value converted to motor steps
   long rpos, npos;	//Long readback, and new position
   long diff;     	//Motor steps between readback and new position

   //Retrieve needed motor record parameters
   status = pC_->getDoubleParam(axisNo_, pC_->motorResolution_, &mres);
   status |= pC_->getDoubleParam(axisNo_, pC_->GalilEncoderResolution_, &eres);
   status |= pC_->getIntegerParam(axisNo_, pC_->GalilUseEncoder_, &ueip);
   status |= pC_->getDoubleParam(axisNo_, pC_->GalilUserOffset_, &off);
   status |= pC_->getIntegerParam(axisNo_, pC_->GalilDirection_, &dir);

   if (!status) {
      //Params retrieved okay

      //Calculate direction multiplier
      dirm = (dir == 0) ? 1 : -1;

      //Calculate mr dial readback in motor steps
      drbv = (ueip) ? encoder_position_ * eres : motor_position_ * mres;
      drbvmpos = drbv / mres;
      //Calculate mr readback, and new position in motor steps
      rpos = NINT(drbvmpos);
      npos = NINT(position);

      //Calculate difference between dial readback (in motor steps)
      //and new position in motor steps
      diff = labs(rpos - npos);

      //Calculate requested position in user coordinates
      position = (position * mres * dirm) + off;

      //If new position differs from readback by 1 motor step or more, and
      //No asynParam list error, then write new position
      if (diff >= 1) {
         //Set flag that move has been pushed to this axis motor record
         moveThruRecord_ = true;
         //Set signalCaller as instructed
         signalCaller_ = signalCaller;
         //Set requested position
         pC_->setDoubleParam(axisNo_, pC_->GalilMotorSetVal_, position);
         //Enable writes to motor record
         //Writes are disabled by GalilAxis::move when called by MR
         //Writes are disabled by caller, or GalilCSAxis::startDeferredMovesThread when
         //GalilAxis::move is not called by MR
         //A timeout period, and moveThruRecord_ are used to detect if MR calls GalilAxis::move
         //Eg. GalilAxis::move isn't called by MR when new position - readback < rdbd, spdb
         pC_->setIntegerParam(axisNo_, pC_->GalilMotorSetValEnable_, 1);
         //Do callbacks
         pC_->callParamCallbacks(axisNo_);
      }
      else //New position same as motor record rbv already
         status = asynError;
   }

   return (asynStatus)status;
}

/** Move the motor to an absolute location or by a relative amount.
  * \param[in] position  The absolute position to move to (if relative=0) or the relative distance to move 
  * by (if relative=1). Units=steps.
  * \param[in] relative  Flag indicating relative move (1) or absolute move (0).
  * \param[in] minVelocity The initial velocity, often called the base velocity. Units=steps/sec.
  * \param[in] maxVelocity The maximum velocity, often called the slew velocity. Units=steps/sec.
  * \param[in] acceleration The acceleration value. Units=steps/sec/sec*/
asynStatus GalilAxis::move(double position, int relative, double minVelocity, double maxVelocity, double acceleration)
{
  static const char *functionName = "GalilAxis::move";
  char move_command[128]; // at least enough to take a stringout record, but extra in case of a waveform
  bool used_move_command = false;
  int deferredMode;				//Deferred move mode
  //Is controller using main or auxillary encoder register for positioning
  double readback = (motorIsServo_) ? encoder_position_ : motor_position_;
  double mres;
  asynStatus status = asynError;

  //If this axis is being driven by a CSAxis
  //Use the requested CSAxis velocity, acceleration instead of that provided by mr
  if (useCSADynamics_) {
     maxVelocity = csaVelocity_;
     acceleration = csaAcceleration_;
  }

  std::cerr << "MOVE: axis " << axisName_ << " to " << position << (relative != 0 ? " (relative) " : " (absolute)") << " current readback " << readback << std::endl;
  std::cerr << "MOVE: axis " << axisName_ << " motor readback " << motor_position_ << " encoder readback " << encoder_position_ << std::endl;
  //Check for move thru motor record
  if (moveThruRecord_) {
     //Set flag false
     moveThruRecord_ = false;
     //Now GalilAxis::move has been called
     //Disable further writes to axis motor record from driver
     pC_->setIntegerParam(axisNo_, pC_->GalilMotorSetValEnable_, 0);
  }

  //Are moves to be deferred ?
  if (pC_->movesDeferred_ != 0) {
     //Moves are deferred
     //Return if specified maxVelocity is <= 2.000000
     if (trunc(maxVelocity * 1000000.0)  < trunc(2.000000 * 1000000.0)) {
        return asynSuccess;
     }
     //Retrieve deferred moves mode
     pC_->getIntegerParam(pC_->GalilDeferredMode_, &deferredMode);
     //Sync start and stop motor moves require relative move
     deferredPosition_ = (deferredMode && !relative) ? position - readback : position;
     //Store required parameters for deferred move in GalilAxis
     pC_->getIntegerParam(0, pC_->GalilCoordSys_, &deferredCoordsys_);
     deferredVelocity_ = maxVelocity;
     deferredAcceleration_ = acceleration;
     deferredRelative_ = (deferredMode) ? 1 : relative;
     deferredMove_ = true;
     deferredMode_ = deferredMode;

     //Clear controller message for ad-hoc deferred moves (not from CSAxis)
     if (!useCSADynamics_)
        pC_->setCtrlError("");

     status = asynSuccess;
  }
  else {
     //Moves are not deferred
     //Check axis is ok to go
     if (!beginCheck(functionName, axisName_, maxVelocity)) {
        move_command[0] = '\0';
        if ( (pC_->getStringParam(axisNo_, pC_->GalilMoveCommand_, sizeof(move_command), move_command) == asynSuccess) && (move_command[0] != '\0' && move_command[0] != ' ') )
        {
            move_command[sizeof(move_command)-1] = '\0';
            pC_->getDoubleParam(axisNo_, pC_->motorResolution_, &mres);
            if (epicsSnprintf(pC_->cmd_, sizeof(pC_->cmd_), move_command, position * mres) > 0)
            {
                pC_->sync_writeReadController();
            }
            used_move_command = true;
        }
        //Set absolute or relative move
        else if (relative) {
           //Set the relative move
           sprintf(pC_->cmd_, "PR%c=%.0lf", axisName_, position);
           pC_->sync_writeReadController();
        }
        else {
           //Set the absolute move
           sprintf(pC_->cmd_, "PA%c=%.0lf", axisName_, position);
           pC_->sync_writeReadController();
        }
        
        //set acceleration and velocity
        setAccelVelocity(acceleration, maxVelocity);
        //Begin the move
        if (!used_move_command) {
            status = beginMotion(functionName, position, relative, true);
            // signal poller we have started moving
            pC_->motion_started_.signal();
        }
     }
  }

  //Return status
  return status;
}

/** Move the motor to the home position.
  * \param[in] maxVelocity The maximum velocity, often called the slew velocity. Units=steps/sec.
  * \param[in] forwards  Flag indicating to move the motor in the forward direction(1) or reverse direction(0).
*/
asynStatus GalilAxis::setupHome(double maxVelocity, int forwards)
{
   int home_direction;  //Muliplier to change direction of the jog off home switch
   double hjs;          //home jog speed
   double hvel;         //Home velocity
   int homeEdge;        //Home switch edge to find
   int useSwitch;       //Jog toward switch
   int useIndex;        //Find encoder index
   int useEncoder;      //Use encoder if present

   //Calculate direction of home jog
   home_direction = (forwards == 0) ? 1 : -1;

   //Retrieve use switch for this axis
   pC_->getIntegerParam(axisNo_, pC_->GalilUseSwitch_, &useSwitch);
   //When not using switch assume index search
   home_direction = (useSwitch) ? home_direction : home_direction * -1;

   //Set home switch active/inactive states for _HM command
   sprintf(pC_->cmd_, "hswact%c=%d\n", axisName_, pC_->hswact_);
   pC_->sync_writeReadController();
   sprintf(pC_->cmd_, "hswiact%c=%d\n", axisName_, pC_->hswiact_);
   pC_->sync_writeReadController();
   //Retrieve controller home edge
   pC_->getIntegerParam(0, pC_->GalilHomeEdge_, &homeEdge);
   //Set home edge parameter
   sprintf(pC_->cmd_, "hsedge=%d\n", homeEdge);
   pC_->sync_writeReadController();

   //Set use limits as home (ulah) parameter
   sprintf(pC_->cmd_, "ulah%c=%d\n", axisName_, limit_as_home_);
   pC_->sync_writeReadController();

   //Calculate home jog speed, direction that controller home program will use
   hjs = maxVelocity * home_direction;
   sprintf(pC_->cmd_, "hjs%c=%.0lf\n", axisName_, hjs);
   pC_->sync_writeReadController();
   
   // old galil variable name still used in dmc files
   sprintf(pC_->cmd_, "hjgsp%c=%.0lf\n", axisName_, hjs);
   pC_->sync_writeReadController();

   //Set Homed status to false
   sprintf(pC_->cmd_, "homed%c=0\n", axisName_);
   pC_->sync_writeReadController();

   //Set use encoder index
   pC_->getIntegerParam(axisNo_, pC_->GalilUseIndex_, &useIndex);
   sprintf(pC_->cmd_, "ui%c=%d", axisName_, useIndex);
   pC_->sync_writeReadController();

   //Set use encoder if present
   pC_->getIntegerParam(axisNo_, pC_->GalilUseEncoder_, &useEncoder);
   sprintf(pC_->cmd_, "ueip%c=%d", axisName_, useEncoder);
   pC_->sync_writeReadController();

   //Set motorRecord MSTA bit 15 motorStatusHomed_
   //Homed is not part of Galil data record, we support it using Galil code and unsolicited messages instead
   //We must use asynMotorAxis version of setIntegerParam to set MSTA bits for this MotorAxis
   setIntegerParam(pC_->motorStatusHomed_, 0);
   if (customHome_) {
       // we do not want to change hjog, it needs to be left at 0 at start for custom home code
       // and should already be zero from how home routine works
       std::cerr << "Using custom home code" << std::endl;
   }
   else if (useSwitch) {
     //Driver will start jog toward switch
     //Then controller home program will be called by setting home%c=1
     //calculate jog toward switch speed, direction
     hvel = maxVelocity * home_direction * -1;

     ///@todo we disabled this previously
     //sprintf(pC_->cmd_, "JG%c=%.0lf", axisName_, hvel);
     //pC_->sync_writeReadController();
     //Tell controller home program that jog off switch is necessary
     sprintf(pC_->cmd_, "hjog%c=0", axisName_);
     pC_->sync_writeReadController();
   }
   else {
     //Controller home program will be called by setting home%c=1
     //Tell controller home program that jog off switch is done already
     sprintf(pC_->cmd_, "hjog%c=3", axisName_);
     pC_->sync_writeReadController();
   }

   return asynSuccess;
}

/** Move the motor to the home position.
  * \param[in] minVelocity The initial velocity, often called the base velocity. Units=steps/sec.
  * \param[in] maxVelocity The maximum velocity, often called the slew velocity. Units=steps/sec.
  * \param[in] acceleration The acceleration value. Units=steps/sec/sec.
  * \param[in] forwards  Flag indicating to move the motor in the forward direction(1) or reverse direction(0).
  *                      Some controllers need to be told the direction, others know which way to go to home. */
asynStatus GalilAxis::home(double minVelocity, double maxVelocity, double acceleration, int forwards)
{
  static const char *functionName = "GalilAxis::home";
  bool ctrlType;		//Controller type convenience variable
  int homeAllowed;		//Home types allowed
  int ssiinput;			//SSI encoder register
  int ssicapable;		//SSI capable
  int ssiconnect;		//SSI connect status
  int bissInput;		//BISS input register
  int bissCapable;		//BISS capable
  int useSwitch;		//Use switch when homing
  int limitDisable;		//Limit disable setting
  string mesg;			//Controller mesg
  asynStatus status = asynError;

  //Retrieve needed param
  pC_->getIntegerParam(axisNo_, pC_->GalilSSIInput_, &ssiinput);
  pC_->getIntegerParam(pC_->GalilSSICapable_, &ssicapable);
  pC_->getIntegerParam(axisNo_, pC_->GalilSSIConnected_, &ssiconnect);
  pC_->getIntegerParam(axisNo_, pC_->GalilHomeAllowed_, &homeAllowed);
  pC_->getIntegerParam(axisNo_, pC_->GalilUseSwitch_, &useSwitch);
  pC_->getIntegerParam(axisNo_, pC_->GalilBISSInput_, &bissInput);
  pC_->getIntegerParam(pC_->GalilBISSCapable_, &bissCapable);
  pC_->getIntegerParam(axisNo_, pC_->GalilLimitDisable_, &limitDisable);

  //Check if requested home type is allowed
  if (!homeAllowed) {
     mesg = string(functionName) + ": " + string(1, axisName_) + " ";
     mesg += "motor extra settings do not allow home";
  }
  if (homeAllowed == HOME_REV && forwards) {
     mesg = string(functionName) + ": " + string(1, axisName_) + " ";
     mesg += "motor extra settings do not allow forward home";
  }
  if (homeAllowed == HOME_FWD && !forwards) {
     mesg = string(functionName) + ": " + string(1, axisName_) + " ";
     mesg += "motor extra settings do not allow reverse home";
  }

  //Check if requested home type valid given limit disable setting
  //Construct controller type convenience variable
  ctrlType = (bool)(pC_->model_[3] != '2' && pC_->model_[3] != '1') ? true : false;
  if (!customHome_ && limit_as_home_ && ctrlType) {
     if (useSwitch && forwards && (limitDisable == 1 || limitDisable ==3)) {
        mesg = string(functionName) + ": " + string(1, axisName_) + " ";
        mesg += "axis can't home to fwd limit as fwd limit is disabled";
     }
     if (useSwitch && !forwards && limitDisable > 1) {
        mesg = string(functionName) + ": " + string(1, axisName_) + " ";
        mesg += "axis can't home to rev limit as rev limit is disabled";
     }
  }

  //Homing not supported for absolute encoders, just move it where you want
  if ((ssiinput && ssicapable && ssiconnect) || (bissInput && bissCapable)) {
     mesg = string(functionName) + ": " + string(1, axisName_) + " ";
     mesg += "axis has no home process because of SSI encoder";
  }

  //If problem with settings, do nothing
  if (!mesg.empty()) {
     pC_->setCtrlError(mesg);
     return asynSuccess;
  }

  if (homing_)
  {
      errlogSevPrintf(errlogInfo, "Axis %c already homing - request ignored.\n", axisName_);
      return asynSuccess;  //Nothing to do
  }
  // check homing thread is available
  if ( !pC_->checkGalilThreads() )
  {
    errlogPrintf("Galil home threads are not running. Attempting to restart homing threads.\n");
    sprintf(pC_->cmd_, "HX0;HX1");
    pC_->sync_writeReadController();
    sprintf(pC_->cmd_, "XQ 0,0");
    pC_->sync_writeReadController();
    epicsThreadSleep(.2);
    if ( !pC_->checkGalilThreads() )
    {
      errlogPrintf("Unable to start Galil homing threads.\n");
      return asynError;
    }
    else
    {
      errlogPrintf("Galil homing threads restarted successfully.\n");
    }
  }

  //If axis is ok to go, begin motion
  if (!beginCheck(functionName, axisName_, maxVelocity)) {
     //set acceleration and velocity
     setAccelVelocity(acceleration, maxVelocity);
     //Setup home parameters on controller
     setupHome(maxVelocity, forwards);
     //Begin the jog toward switch
     if (useSwitch)
        status = beginMotion(functionName);
     else//Prepare for a move, but let controller code take over
        status = beginMotion(functionName, 0.0, false, false, false);
     //Set home flags if start successful
     if (!status) {
        //Since we may be calling galil code before motion begins
        //Reset stopped time, so homing doesn't timeout immediately
        resetStoppedTime_ = true;  //Request poll thread reset stopped time if done
        //Wait for poller to reset stopped time on this axis
        //ensure synchronous poller is not blocked
        pC_->unlock();
        epicsEventWaitWithTimeout(stoppedTimeReset_, pC_->updatePeriod_/1000.0);
        pC_->lock();
        //Start was successful
        //This homing status does not include JAH
        //Flag homing true
        homing_ = true;
        //This homing status does include JAH
        //Flag homing true
        setIntegerParam(pC_->GalilHoming_, 1);
        //Homing has not been cancelled yet
        cancelHomeSent_ = false;
        //tell controller which axis we are doing a home on
        //We do this last so home algorithm doesn't cancel home jog in incase motor
        //is sitting on opposite limit to which we are homing
        sprintf(pC_->cmd_, "home%c=1", axisName_);
        pC_->sync_writeReadController();
        // signal poller we have started moving
        pC_->motion_started_.signal();
        std::cerr << "Home started axis " << axisName_ << std::endl;
     }
  }

  //Return status
  return status;
}

//Do all checks, make sure axis is good to go
asynStatus GalilAxis::beginCheck(const char *caller, char callaxis, double maxVelocity, bool resetCtrlMessage)
{
  string mesg;				//Controller message
  int rev, fwd;				//Limit status
  int motoron;				//Motor Amp on status
  int autoonoff;			//Auto amp on/off status
  int wlp;				//Wrong limit protection enable state
  int limitDisable;                     //Limit disable setting

  //Clear controller messages
  if (resetCtrlMessage)
     pC_->setCtrlError("");

  if (!axisReady_) {
     mesg = string(caller) + " " + string(1, callaxis) + " failed, " + string(1, axisName_);
     mesg += " axis still initializing";
  }

  //Dont start if velocity 0
  if (lrint(maxVelocity) == 0) {
     mesg = string(caller) + " " + string(1, callaxis) + " failed, " + string(1, axisName_);
     mesg += " requested velocity is 0";
  }

  //Check motor enable
  motor_enabled(caller, mesg);

  //Motor on and Amp auto on/off status check
  pC_->getIntegerParam(axisNo_, pC_->motorStatusPowerOn_, &motoron);
  pC_->getIntegerParam(axisNo_, pC_->GalilAutoOnOff_, &autoonoff);
  if (!motoron && !autoonoff) {
     mesg = string(caller) + " " + string(1, callaxis) + " failed, " + string(1, axisName_);
     mesg += " motor amplifier is off";
  }

  //Protect against user issuing further moves in bad direction after wlp stop
  pC_->getIntegerParam(axisNo_, pC_->motorStatusLowLimit_, &rev);
  pC_->getIntegerParam(axisNo_, pC_->motorStatusHighLimit_, &fwd);
  pC_->getIntegerParam(axisNo_, pC_->GalilWrongLimitProtection_, &wlp);
  pC_->getIntegerParam(axisNo_, pC_->GalilLimitDisable_, &limitDisable);
  //If wlp is enabled and motor limits direction is not_consistent
  //Don't allow move when a limit is active
  if (wlp && (limitsDirState_ == not_consistent) && ((rev_ && limitDisable < 2) ||
     (fwd_ && (!limitDisable || limitDisable == 2)))) {
     mesg = string(caller) + " " + string(1, callaxis) + " failed, " + string(1, axisName_);
     mesg += " wrong limit protect stop";
  }

  //Check for error
  if (!mesg.empty()) {
     //Set controller error mesg
     pC_->setCtrlError(mesg);
     return asynError;
  }

  //Everything ok so far
  return asynSuccess;
}

/** Check axis physical limits given move request
  * \param[in] caller - Caller function name
  * \param[in] callaxis - Caller axis
  * \param[in] position  The absolute position to move to Units=steps*/
asynStatus GalilAxis::checkLimits(const char *caller, char callaxis, double position) {
   string mesg = "";    	//Controller mesg
   int status;			//Return status
   int rev, fwd;		//Motor limit status
   double readback;		//Readback in steps

   if (!checkEncoderMotorSync(true))
   {
        pC_->setCtrlError("Encoder and motor registers out of sync - you may need to rehome");
        // return asynError;  // should we stop move attempts? 
   }
   //Retrieve needed motor record parameters
   status = pC_->getIntegerParam(axisNo_, pC_->motorStatusLowLimit_, &rev);
   status |= pC_->getIntegerParam(axisNo_, pC_->motorStatusHighLimit_, &fwd);

   if (!status) {
      //Readback in steps
      readback = (motorIsServo_) ? encoder_position_ : motor_position_;
      //Check physical limits
      if ((position < readback && rev) || (position > readback && fwd)) {
         mesg = string(caller) + " " + string(1, callaxis) + " failed, " + string(1, axisName_) + " ";
         mesg += "limit active in move direction";
      }
   }

   //Display mesg if any
   if (!mesg.empty()) {
      pC_->setCtrlError(mesg);
      status = asynError;
   }

   //Return result
   return (asynStatus)status;
}

/** Check axis soft limits given move request
  * \param[in] caller - Caller function name
  * \param[in] callaxis - Caller axis
  * \param[in] position  The absolute position to move to Units=steps*/
asynStatus GalilAxis::checkSoftLimits(const char *caller, char callaxis, double position) {
   bool softlimits;		//Soft limits enable status
   string mesg = "";    	//Controller mesg
   int status = asynSuccess;	//Return status

   //Determine if soft limits are active
   softlimits = (bool)(lowLimit_ == highLimit_ && lowLimit_ == 0.0) ? false : true;

   //Check soft limits
   if ((position < lowLimit_ && softlimits) || (position > highLimit_ && softlimits)) {
      mesg = string(caller) + " " + string(1, callaxis) + " failed, " + string(1, axisName_) + " ";
      mesg += "move would violate soft limits";
   }

   //Display mesg if any
   if (!mesg.empty()) {
      pC_->setCtrlError(mesg);
      status = asynError;
   }

   //Return result
   return (asynStatus)status;
}

/** Check axis motor record settings
  * \param[in] caller - Caller function name
  * \param[in] callaxis - Caller axis
  * \param[in] moveVelocity - Is the requested move a jog ? */
asynStatus GalilAxis::checkMRSettings(const char *caller, char callaxis, bool moveVelocity) {
   string mesg = "";		//Controller mesg
   int spmg;			//Motor record stop pause move go
   int set;			//Motor record set
   int dmov;			//Motor record dmov
   int status;			//Return status

   //Retrieve needed params
   status = pC_->getIntegerParam(axisNo_, pC_->GalilStopPauseMoveGo_, &spmg);
   status |= pC_->getIntegerParam(axisNo_, pC_->GalilMotorSet_, &set);
   status |= pC_->getIntegerParam(axisNo_, pC_->GalilDmov_, &dmov);
   //Return if any paramlist error
   if (status) return asynError;

   //Check motor record status
   if (spmg != spmgGo && spmg != spmgMove) {
      mesg = string(caller) + " " + string(1, callaxis) + " failed, " + string(1, axisName_);
      mesg += " spmg is not set to \"go\" or \"move\"";
   }
   if (set && !moveVelocity) {
      mesg = string(caller) + " " + string(1, callaxis) + " failed, " + string(1, axisName_);
      mesg += " set field is not set to \"use\"";
   }
   //Galil EPICS driver will allow related CSAxis to move if movesDeferred true
   //So only check dmov of axis if movesDeferred false
   if (!pC_->movesDeferred_) {
      if (!dmov) {
         mesg = string(caller) + " " + string(1, callaxis) + " failed, " + string(1, axisName_);
         mesg += " dmov field is false";
      }
   }

   //Display any controller mesg
   if (!mesg.empty()) {
      pC_->setCtrlError(mesg);
      status = asynError;
   }

   //Return status
   return (asynStatus)status;
}

/** Check axis motor record settings
  * \param[in] caller - Caller function name
  * \param[in] callaxis - Caller axis
  * \param[in] position - New position setpoint Units=steps
  * \param[in] maxVelocity - Requested velocity Units=Steps/Sec */
asynStatus GalilAxis::checkAllSettings(const char *caller, char callaxis, double position, double maxVelocity) {
   int status;              //Return status

   //Check motor record settings before move
   status = checkMRSettings(caller, axisName_, false);
   //Check axis limits
   status |= checkLimits(caller, axisName_, position);
   //Check axis softlimits
   status |= checkSoftLimits(caller, axisName_, position);
   //Check axis is ready to go
   status |= beginCheck(caller, axisName_, maxVelocity, false);

   return (asynStatus)status;
}

/** Move the motor at a fixed velocity until told to stop.
  * \param[in] minVelocity The initial velocity, often called the base velocity. Units=steps/sec.
  * \param[in] maxVelocity The maximum velocity, often called the slew velocity. Units=steps/sec.
  * \param[in] acceleration The acceleration value. Units=steps/sec/sec. */
asynStatus GalilAxis::moveVelocity(double minVelocity, double maxVelocity, double acceleration)
{
  static const char *functionName = "moveVelocity";
  asynStatus status = asynError;

  std::cerr << "MOVEVELOCITY: axis " << axisName_ << " minVelocity " << minVelocity << " maxVelocity " << maxVelocity << " acceleration " << acceleration << std::endl;
  //If axis ok to go, begin motion
  if (!beginCheck(functionName, axisName_, maxVelocity))
  	{
	//set acceleration and velocity
	setAccelVelocity(acceleration, maxVelocity);

	//Give jog speed and direction
	sprintf(pC_->cmd_, "JG%c=%.0lf", axisName_, maxVelocity);
	pC_->sync_writeReadController();
				
	//Begin the move
	status = beginMotion(functionName);
    // signal poller we have started moving
    pC_->motion_started_.signal();
	}
   
  //Return status
  return status;
}

/** Stop the motor.  Called by motor record
  * \param[in] acceleration The acceleration value. Units=steps/sec/sec. */
asynStatus GalilAxis::stop(double acceleration)
{
  GalilCSAxis *pCSAxis;			//GalilCSAXis
  unsigned i;				//Looping
  unsigned j;				//Looping
  bool found;				//Axis found in CSAxis

  //Check source of call to GalilAxis::stop
  if (stoppedMR_) {
     //GalilAxis:stop called this time by driver calling stopMotorRecord
     //Reset flag now GalilAxis:stop has been called
     stoppedMR_ = false;
     //Set axis motor record stop to zero
     pC_->setIntegerParam(axisNo_, pC_->GalilMotorRecordStop_, 0);
     //Backlash, retries are now prevented
     //Do nothing further, let internal stop continue undisturbed
     return asynSuccess;
  }

  //Should we do coordinated stop?
  if (useCSADynamics_) {
     //This axis is being driven by a CSAxis
     //Stop entire CSAxis in coordinated way
     //Loop thru CSAxis
     for (i = MAX_GALIL_AXES; i < MAX_GALIL_AXES + MAX_GALIL_CSAXES; i++) {
        found = false;
        //Retrieve the CSAxis
        pCSAxis = pC_->getCSAxis(i);
        //Skip or continue
        if (!pCSAxis) continue;
        //Search retrieved CSAxis revaxes for this axis
        for (j = 0; pCSAxis->revaxes_[j] != '\0'; j++) {
           if (pCSAxis->revaxes_[j] == axisName_)
              found = true;
        }
        //Stop the CSAxis that is moving, and contains this axis
        //Using coordinated stop
        pCSAxis->stopSent_ = true;
        pCSAxis->stop_reason_ = stop_reason_;
        if (found && !pCSAxis->done_ && pCSAxis->move_started_ && (stop_reason_ == MOTOR_STOP_ONWLP || stop_reason_ == MOTOR_STOP_ONSTALL))
           pCSAxis->stopInternal(); //WLP or encoder stall are emergency stop
        else if (found && !pCSAxis->done_  && pCSAxis->move_started_ && stop_reason_ != MOTOR_STOP_ONWLP && stop_reason_ != MOTOR_STOP_ONSTALL)
           pCSAxis->stopInternal(false); //Normal stop (eg. spmg)
     }
  }
  else {
     //Stop this axis independently
     //cancel any home, and home switch jog off operations that may be underway
     // we do not check homing_ as we may have resrtred when one was in progress?
     // but we may stop on startup anyway?
     if (customHome_) {
          // hjog=0 not needed, might be a race condition if it is set (reexecute hjog==0 section)
         sprintf(pC_->cmd_, "home%c=0", axisName_);
     }
     else {
         sprintf(pC_->cmd_, "home%c=0;hjog%c=0", axisName_, axisName_);
     }
     pC_->sync_writeReadController();
     //Cancel limit/home switch jog off operations that may be underway
     //Set deceleration back to normal
     sprintf(pC_->cmd_, "DC%c=nrmdc%c", axisName_, axisName_);
     pC_->sync_writeReadController();
     //Set homing flag false
     //This flag does not include JAH
     homing_ = false;
     //This flag does include JAH
     setIntegerParam(pC_->GalilHoming_, 0);
     //For internal stop, prevent backlash, retries from this axis motor record
     stopMotorRecord();
     //Stop the axis
     sprintf(pC_->cmd_, "ST%c", axisName_);
     pC_->sync_writeReadController();
     //After stop, set deceleration specified
     setAccelVelocity(acceleration, 0, false);
     //Clear defer move flag
     deferredMove_ = false;
  }

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

/** Stop the motor.  Called by driver internally
  * Blocks backlash, retries attempts from motorRecord until dmov
  * \param[in] acceleration The acceleration value. Units=steps/sec/sec. */
asynStatus GalilAxis::stopInternal(double acceleration)
{
  //Indicate stop source is the driver not motor record
  stopInternal_ = true;

  //Stop the motor
  stop(acceleration);

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

/** Stop axis motor record.  Called by driver internally.
  * Prevents backlash, and retry attempts from motorRecord */
asynStatus GalilAxis::stopMotorRecord(void) {
   int status = asynSuccess; //Return status
   //What is the source of the stop request ?
   //Possible sources are the driver, or the motor record
   if (stopInternal_) {
      //Set flag indicating next call to GalilAxis::stop caused by
      //setting axis motor record stop field 1
      stoppedMR_ = true;
      //Stop request is from driver, not motor record
      //Send stop to this axis MR to prevent backlash, and retry attempts
      status = pC_->setIntegerParam(axisNo_, pC_->GalilMotorRecordStop_, 1);
      //Do callbacks
      pC_->callParamCallbacks(axisNo_);
   }
   //Return result
   return (asynStatus)status;
}

/** Copy encoder position into motor step count (aux) register
*/
asynStatus GalilAxis::syncPosition(void)
{
   int status = 0;
   double eres, mres;	//Encoder, motor resolution
   sprintf(pC_->cmd_, "MT%c=?", axisName_);
   pC_->sync_writeReadController();
   int motor_type = atoi(pC_->resp_); // servo is -1.5, -1, 1, or 1.5 so abs(int(motor)) is 1 
   //Retrieve needed params
   status = pC_->getDoubleParam(axisNo_, pC_->motorResolution_, &mres);
   status |= pC_->getDoubleParam(axisNo_, pC_->GalilEncoderResolution_, &eres);
   if (status || abs(motor_type) == 1 || !ueip_)
   {
       return asynSuccess;
   }
   //Calculate step count from existing encoder_position
   double new_motor_pos = encoder_position_ * (eres/mres);

   if (abs(motor_type) == 1) // currently servo branch should never get executed, not sure it ever should?
   {
       sprintf(pC_->cmd_, "DE%c=%.0lf", axisName_, new_motor_pos);  //Servo motor, use aux register for step count
   }
   else
   {
       sprintf(pC_->cmd_, "DP%c=%.0lf", axisName_, new_motor_pos);  //Stepper motor, main register for step count
   }

   //Write command to controller
   status = pC_->sync_writeReadController();
   std::cerr << "syncPosition axis " << axisName_ << " changed motor counter from " << motor_position_ << " to " << new_motor_pos << std::endl;
   return (asynStatus)status;
}

/** Set the current position of the motor.
  * \param[in] position The new absolute motor position that should be set in the hardware. Units=steps.*/
asynStatus GalilAxis::setPosition(double position)
{
  static const char *functionName = "GalilAxis::setPosition";
  double mres, eres;				//MotorRecord mres, and eres
  double enc_pos;				//Calculated encoder position
  int motor;

  if (pC_->init_state_ < initHookAfterIocRunning)
  {
      std::cerr << functionName << ": Request to redefine motor position to " << position << " on axis " << axisName_ << " before ioc is running" << std::endl;
  }
  //Retrieve motor setting direct from controller rather than ParamList as IocInit may be in progress
  sprintf(pC_->cmd_, "MT%c=?", axisName_);
  pC_->sync_writeReadController();
  motor = atoi(pC_->resp_);

  //Calculate encoder counts, from provided motor position
  //encmratio_ is non zero only during autosave restore of position, or after user changes eres, mres, ueip or position
  if (encmratioset_)
  	enc_pos = (position * encmratio_);//Autosave restore for positions, or user changed mres, or eres.  IocInit may be in progress
  else
	{
	//User has not changed mres, eres, ueip, and autosave restore of position was not necessary or didnt happen
	//IocInit also completed.
	pC_->getDoubleParam(axisNo_, pC_->GalilEncoderResolution_, &eres);
	pC_->getDoubleParam(axisNo_, pC_->motorResolution_, &mres);
	enc_pos = (position * mres)/eres;
	}
  // print some details about current position on controller
  double tp = getGalilAxisVal("_TP"); // current position (from encoder if present)
  double td = getGalilAxisVal("_TD"); // current position (motor steps)
  double rp = getGalilAxisVal("_RP"); // commanded position (motor steps)
  std::cerr << functionName << ": Redefining motor position to " << position << " on axis " << axisName_ << std::endl;
  std::cerr << functionName << ":   before: _TP=" << tp << " _TD=" << td << " _RP=" << rp << std::endl;
  //output motor position (step count) to aux encoder register on controller
  //DP and DE command function is different depending on motor type
  if (abs(motor) == 1 || abs(motor) == 1.5)
	sprintf(pC_->cmd_, "DE%c=%.0lf", axisName_, position);  //Servo motor, use aux register for step count
  else
	sprintf(pC_->cmd_, "DP%c=%.0lf", axisName_, position);  //Stepper motor, aux register for step count
  //Set step count/aux encoder position on controller
  pC_->sync_writeReadController();
  //Set step count/aux encoder position in GalilAxis
  motor_position_ = position;
  //Pass step count/aux encoder value to motorRecord
  setDoubleParam(pC_->motorPosition_, motor_position_);

  tp = getGalilAxisVal("_TP"); // current position (from encoder if present)
  td = getGalilAxisVal("_TD"); // current position (motor steps)
  rp = getGalilAxisVal("_RP"); // commanded position (motor steps)
  std::cerr << functionName << ":    after: _TP=" << tp << " _TD=" << td << " _RP=" << rp << std::endl;
  
  //Set encoder position
  setEncoderPosition(enc_pos);
  
  //GalilAxis position changed via MR SET field, inform any GalilCSAxis
  setPositionOut_ = true;

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

// return true if in sync
// only needed for stepper motors
bool GalilAxis::checkEncoderMotorSync(bool correct_motor)
{
    static const char *functionName = "GalilAxis::checkEncoderMotorSync";
    double posdiff_tol = 0.0;  // in physical egu
    asynStatus status = pC_->getDoubleParam(axisNo_, pC_->GalilMotorEncoderSyncTol_, &posdiff_tol);
    sprintf(pC_->cmd_, "MT%c=?", axisName_);
    pC_->sync_writeReadController();
    int motor = atoi(pC_->resp_); // servo is -1.5, -1, 1, or 1.5 so abs(int(motor)) is 1 
    if ( status != asynSuccess || abs(motor) == 1 || !ueip_ )
    {
        return true;
    }
    double mres = 0.0, eres = 0.0;				// MotorRecord mres, and eres
    pC_->getDoubleParam(axisNo_, pC_->GalilEncoderResolution_, &eres);
    pC_->getDoubleParam(axisNo_, pC_->motorResolution_, &mres);
    double posdiff_egu = motor_position_ * mres - encoder_position_ * eres;
    if (posdiff_tol <= 0.0)
    {
        std::cerr << "Current Motor - Encoder drift: " << posdiff_egu << " egu" << std::endl;
        return true;
    }
    else if (fabs(posdiff_egu) < posdiff_tol)
    {
        std::cerr << "Motor and Encoder are in sync by " << posdiff_egu << " < " << posdiff_tol << " egu" << std::endl;
        return true;
    }
    else
    {
        std::cerr << "Motor and Encoder registers are out of sync by " << posdiff_egu << " > " << posdiff_tol << " egu" << std::endl;
    }
    if (!correct_motor)
    {
        return false;
    }
    double new_motor_pos = encoder_position_ * eres / mres;		
    std::cerr << "Raw motor position corrected from " << motor_position_ << " to " << new_motor_pos << " using encoder for axis " << axisName_ << std::endl;
    if (abs(motor) == 1) // currently servo branch should never get executed
    {
        sprintf(pC_->cmd_, "DE%c=%.0f", axisName_, new_motor_pos);  //Servo motor, use aux register for step count
    }
    else
    {
        sprintf(pC_->cmd_, "DP%c=%.0f", axisName_, new_motor_pos);  //Stepper motor, main register for step count
    }
    pC_->sync_writeReadController();
    return true;
}

/** Set the current position of the encoder.
  * \param[in] position The new absolute encoder position that should be set in the hardware. Units=steps.*/
asynStatus GalilAxis::setEncoderPosition(double position)
{
  static const char *functionName = "GalilAxis::setEncoderPosition";
  asynStatus status;
  int motor;

  if (pC_->init_state_ < initHookAfterIocRunning)
  {
      std::cerr << functionName << ": request to redefine encoder position to " << position << " on axis " << axisName_ << " before ioc running" << std::endl;
  }
  //Retrieve motor setting direct from controller rather than ParamList as IocInit may be in progress
  sprintf(pC_->cmd_, "MT%c=?", axisName_);
  pC_->sync_writeReadController();
  motor = atoi(pC_->resp_);
  // print some details about current position on controller
  double tp = getGalilAxisVal("_TP"); // current position (from encoder if present)
  double td = getGalilAxisVal("_TD"); // current position (motor steps)
  double rp = getGalilAxisVal("_RP"); // commanded position (motor steps)
  std::cerr << functionName << ": Redefining encoder position to " << position << " on axis " << axisName_ << std::endl;
  std::cerr << functionName << ":   before: _TP=" << tp << " _TD=" << td << " _RP=" << rp << std::endl;
  //output encoder counts to main encoder register on controller
  //DP and DE command function is different depending on motor type
  if (abs(motor) == 1 || abs(motor) == 1.5)
  	sprintf(pC_->cmd_, "DP%c=%.0lf", axisName_, position);   //Servo motor, encoder is main register
  else
	sprintf(pC_->cmd_, "DE%c=%.0lf", axisName_, position);   //Stepper motor, encoder is main register
  //Set encoder count on controller
  status = pC_->sync_writeReadController();
  //Set encoder counts in GalilAxis
  encoder_position_ = position;
  //Pass encoder value to motorRecord
  setDoubleParam(pC_->motorEncoderPosition_, encoder_position_);

  tp = getGalilAxisVal("_TP"); // current position (from encoder if present)
  td = getGalilAxisVal("_TD"); // current position (motor steps)
  rp = getGalilAxisVal("_RP"); // commanded position (motor steps)
  std::cerr << functionName << ":    after: _TP=" << tp << " _TD=" << td << " _RP=" << rp << std::endl;

  return status;
}

/** Set the motor encoder ratio. 
  * \param[in] ratio The new encoder ratio */
asynStatus GalilAxis::setEncoderRatio(double ratio)
{
  //setEncoder is called during IocInit/Autosave for position restore, and when user changes 
  //eres, mres, ueip or position is changed
  //Store the ratio in GalilAxis instance
  //Motor settings must be autosave/restored in pass 0, to ensure ratio is set properly by devMotorAsyn.c init_controller
  encmratio_ = ratio;
  encmratioset_ = true;

  return asynSuccess;
}

/** Set the high limit position of the motor.
  * \param[in] highLimit The new high limit position that should be set in the hardware. Units=steps.*/
asynStatus GalilAxis::setHighLimit(double highLimit)
{
  char mesg[MAX_GALIL_STRING_SIZE];	//Controller mesg

  //this gets called at init for every mR
  if (highLimit < -2147483648.0) {
     //Specified high limit too low
     highLimit = -2147483648.0;
     sprintf(mesg, "%c limiting high soft limit to min of -2147483648 cts", axisName_);
     pC_->setCtrlError(mesg);
  }
  if (highLimit > 2147483646.0) {
     //Specified high limit too large
     highLimit = 2147483646.0;
     sprintf(mesg, "%c limiting high soft limit to max of 2147483646 cts", axisName_);
     pC_->setCtrlError(mesg);
  }

  //Assemble Galil Set High Limit, forward limit in Galil language
  highLimit_ = highLimit;
  if (highLimit_ == 0.0 && lowLimit_ == 0.0) {
     //Construct command, and mesg
     sprintf(pC_->cmd_, "FL%c=%lf;BL%c=%lf", axisName_, 2147483647.0, axisName_, -2147483648.0);
     sprintf(mesg, "%c soft limits disabled", axisName_);
     pC_->setCtrlError(mesg);
  }
  else {
     //Construct command, and mesg
     sprintf(pC_->cmd_, "FL%c=%lf;BL%c=%lf", axisName_, highLimit_, axisName_, lowLimit_);
  }

  //Write command to controller
  pC_->sync_writeReadController();

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

/** Set the low limit position of the motor.
  * \param[in] lowLimit The new low limit position that should be set in the hardware. Units=steps.*/
asynStatus GalilAxis::setLowLimit(double lowLimit)
{
  char mesg[MAX_GALIL_STRING_SIZE];	//Controller mesg

  //this gets called at init for every mR
  if (lowLimit > 2147483647.0) {
     //Specified low limit too high
     lowLimit = 2147483647.0;
     sprintf(mesg, "%c limiting low soft limit to max of 2147483647 cts", axisName_);
     pC_->setCtrlError(mesg);
  }
  if (lowLimit < -2147483647.0) {
     //Specified low limit too low
     lowLimit = -2147483647.0;
     sprintf(mesg, "%c limiting low soft limit to min of -2147483647 cts", axisName_);
     pC_->setCtrlError(mesg);
  }

  //Assemble Galil Set low Limit, reverse limit in Galil language
  lowLimit_ = lowLimit;
  if (highLimit_ == 0.0 && lowLimit_ == 0.0) {
     //Disable soft limits
     //Construct command, and mesg
     sprintf(pC_->cmd_, "FL%c=%lf;BL%c=%lf", axisName_, 2147483647.0, axisName_, -2147483648.0);
     sprintf(mesg, "%c soft limits disabled", axisName_);
     pC_->setCtrlError(mesg);
  }
  else {
     //Construct command, and mesg
     sprintf(pC_->cmd_, "FL%c=%lf;BL%c=%lf", axisName_, highLimit_, axisName_, lowLimit_);
  }

  //Write command to controller
  pC_->sync_writeReadController();

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

/** Set the proportional gain of the motor.
  * \param[in] pGain The new proportional gain. */
asynStatus GalilAxis::setPGain(double pGain)
{
  //Parse pGain value
  pGain = fabs(pGain);
  pGain = pGain * KPMAX;
  pGain = (pGain > KPMAX) ? KPMAX : pGain;
  //Assemble KP command
  sprintf(pC_->cmd_, "KP%c=%lf",axisName_, pGain);
  pC_->sync_writeReadController();
  
  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

/** Set the integral gain of the motor.
  * \param[in] iGain The new integral gain. */
asynStatus GalilAxis::setIGain(double iGain)
{
  double kimax;					//Maximum integral gain depends on controller model 
  //Model 21X3 has different kimax from 41x3, and 40x0
  kimax = (pC_->model_[3] == '2') ? 2047.992 : 255.999;
  //Parse iGain value
  iGain = fabs(iGain);
  iGain = iGain * kimax;
  iGain = (iGain > kimax) ? kimax : iGain;
  //Assemble KI command
  sprintf(pC_->cmd_, "KI%c=%lf",axisName_, iGain);
  pC_->sync_writeReadController();

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

/** Set the derivative gain of the motor.
  * \param[in] dGain The new derivative gain. */
asynStatus GalilAxis::setDGain(double dGain)
{
  //Parse dGain value
  dGain = fabs(dGain);
  dGain = dGain * KDMAX;
  dGain = (dGain > KDMAX) ? KDMAX : dGain;
  //Assemble KD command
  sprintf(pC_->cmd_, "KD%c=%lf",axisName_, dGain);
  pC_->sync_writeReadController();

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

/** Set the motor closed loop status. 
  * \param[in] closedLoop true = close loop, false = open loop. */
asynStatus GalilAxis::setClosedLoop(bool closedLoop)
{
  asynStatus status;

  //Enable or disable motor amplifier
  if (closedLoop)
	sprintf(pC_->cmd_, "SH%c", axisName_);
  else
	sprintf(pC_->cmd_, "MO%c", axisName_);

  //Write setting to controller
  status = pC_->sync_writeReadController();

  return status;
}

//Clear axis ethercat faults
asynStatus GalilAxis::clearEtherCatFault()
{
   int status;			//Return status
   int ecatcapable;		//Controller EtherCat capable
   int ecatup;			//EtherCat network status
   int ecatfault;		//EtherCat drive fault status

   //Retrieve required parameters
   status = pC_->getIntegerParam(pC_->GalilEtherCatCapable_, &ecatcapable);
   status |= pC_->getIntegerParam(pC_->GalilEtherCatNetwork_, &ecatup);
   status |= pC_->getIntegerParam(axisNo_, pC_->GalilEtherCatFault_, &ecatfault);

   if (ecatcapable && ecatup && ecatfault && !status)
      {
      //Controller is ethercat capable, network is up, and this axis has a fault
      //Clear fault for this axis
      sprintf(pC_->cmd_, "EK %d", (1 << axisNo_));
      status |= pC_->sync_writeReadController();
      }

   return (asynStatus)status;
}

/* These are the functions for profile moves */
asynStatus GalilAxis::initializeProfile(size_t maxProfilePoints)
{
  if (profilePositions_)       free(profilePositions_);
  profilePositions_ =         (double *)calloc(maxProfilePoints, sizeof(double));
  return asynSuccess;
}

/** Set the motor brake
  * \param[in] enable true = brake, false = release brake. */
asynStatus GalilAxis::setBrake(bool enable)
{
  asynStatus status = asynSuccess;
  int brakeport;
  //Retrieve the digital port used to actuate this axis brake
  status = pC_->getIntegerParam(axisNo_, pC_->GalilBrakePort_, &brakeport);
  //Enable or disable motor brake
  if (axisReady_ && brakeport > 0 && !status)
     {
     if (!enable)
        sprintf(pC_->cmd_, "SB %d", brakeport);
     else
        sprintf(pC_->cmd_, "CB %d", brakeport);
     //Write setting to controller
     status = pC_->sync_writeReadController();
     }
  return status;
}

//Copy profileBackupPositions_ back into profilePositions_ after a CSAxis profile has been built
void GalilAxis::restoreProfileData(void)
{
  unsigned i;		//Looping
  int nPoints;		//Number of points in profile

  //Retrieve required attributes from ParamList
  pC_->getIntegerParam(0, pC_->profileNumPoints_, &nPoints);
  //Act only if restore is required
  if (restoreProfile_)
     {
     //Restore original GalilAxis profile data
     for (i = 0; i < (unsigned)nPoints; i++)
        profilePositions_[i] = profileBackupPositions_[i];
     //Free the buffer used to backup axis profile data
     free(profileBackupPositions_);
     profileBackupPositions_ = NULL;
     //After restore, reset flag to false
     restoreProfile_ = false;
     }
}

//Reverse direction of binary SSI encoder
//Only possible with stepper motors
asynStatus GalilAxis::invert_ssi(void)
{
   int ssiinput;	//SSI encoder input location
   int ssitotalbits;	//SSI total bits
   int ssierrbits;	//SSI error bits
   int ssidataform;	//Binary or gray code
   char mesg[MAX_GALIL_STRING_SIZE];	//Controller mesg

   //Retrieve SSI dataform
   pC_->getIntegerParam(axisNo_, pC_->GalilSSIData_, &ssidataform);
   //Direction invert only for stepper and binary dataform combination
   if (ssidataform == 0 && !motorIsServo_)
      {
      //Binary data form
      //Invert encoder direction
      //Retrieve needed params
      pC_->getIntegerParam(axisNo_, pC_->GalilSSIInput_, &ssiinput);
      pC_->getIntegerParam(axisNo_, pC_->GalilSSITotalBits_, &ssitotalbits);
      pC_->getIntegerParam(axisNo_, pC_->GalilSSIErrorBits_, &ssierrbits);
      //Determine SSI input location
      if ((!encoderSwapped_ && ssiinput == 1) || (encoderSwapped_ && ssiinput == 2))//SSI in main
         encoder_position_ = (pow(2.0,(ssitotalbits - ssierrbits)) - 1) - encoder_position_;
      }
   else
      {
      if (axisReady_)
         {
         sprintf(mesg, "%c SSI direction invert only for open loop motor and binary encoder", axisName_);
         pC_->setCtrlError(mesg);
         invert_ssi_ = false;
         pC_->setIntegerParam(axisNo_, pC_->GalilSSIInvert_, 0);
         }
      }

   return asynSuccess;
}

//Extract axis data from GalilController data record and
//store in GalilAxis (motorRecord attributes) or asyn ParamList (other record attributes)
//Return status of GalilController data record acquisition
asynStatus GalilAxis::getStatus(void)
{
   char src[MAX_GALIL_STRING_SIZE]="\0";	//data source to retrieve
   int motoron;					//paramList items to update
   double userData;				//paramList items to update
   double errorlast;				//paramList items to update
   double velocitylast;				//paramList items to update
   double userDataDeadb;			//UserData dead band
   double eres;					//Motor encoder resolution
   unsigned digport = 0;			//paramList items to update.  Used for brake status
   unsigned mask;				//Mask used to calc brake port status
   int brakeport;				//Brake port for this axis
   int limitDisable = 0;                        //Limit disabled param
   bool ctrlType;				//Controller type
   double reference_position;                   //Reference position

   //If data record query success in GalilController::acquireDataRecord
   if (pC_->recstatus_ == asynSuccess) {
      //extract relevant axis data from GalilController data-record, store in GalilAxis
      //If connected, then proceed
      if (pC_->connected_) {
         //extract relevant axis data from GalilController record, store in asynParamList
         //moving status
         strcpy(src, "_BGx");
         src[3] = axisName_;
         inmotion_ = (bool)(pC_->sourceValue(pC_->recdata_, src) == 1) ? 1 : 0;
         //Stop code
         strcpy(src, "_SCx");
         src[3] = axisName_;
         stop_code_ = (int)pC_->sourceValue(pC_->recdata_, src);
         //direction
         if (inmotion_) {
            strcpy(src, "JGx-");
            src[2] = axisName_;
            direction_ = (bool)(pC_->sourceValue(pC_->recdata_, src) == 1) ? 0 : 1;
         }
         //Motor error
         strcpy(src, "_TEx");
         src[3] = axisName_;
         error_ = pC_->sourceValue(pC_->recdata_, src);
         pC_->getDoubleParam(axisNo_, pC_->GalilError_, &errorlast);
         if (error_ != errorlast)
            pC_->setDoubleParam(axisNo_, pC_->GalilError_, error_);
         //Servo motor velocity
         strcpy(src, "_TVx");
         src[3] = axisName_;
         velocity_ = pC_->sourceValue(pC_->recdata_, src);
         //Adjust velocity given controller time base setting
         velocity_ *= pC_->timeMultiplier_;
         pC_->getDoubleParam(axisNo_, pC_->GalilMotorVelocityRAW_, &velocitylast);
         if (velocity_ != velocitylast) {
            pC_->setDoubleParam(axisNo_, pC_->GalilMotorVelocityRAW_, velocity_);
            pC_->getDoubleParam(axisNo_, pC_->GalilEncoderResolution_, &eres);
            pC_->setDoubleParam(axisNo_, pC_->GalilMotorVelocityEGU_, velocity_ * eres);
         }
         //Motor on
         strcpy(src, "_MOx");
         src[3] = axisName_;
         motoron = (pC_->sourceValue(pC_->recdata_, src) == 1) ? 0 : 1;
         //Set motorRecord status
         setIntegerParam(pC_->motorStatusPowerOn_, motoron);
         //aux encoder data
         strcpy(src, "_TDx");
         src[3] = axisName_;
         motor_position_ = pC_->sourceValue(pC_->recdata_, src);
         //main encoder data
         strcpy(src, "_TPx");
         src[3] = axisName_;
         encoder_position_ = pC_->sourceValue(pC_->recdata_, src);
         //If this is a stepper then profile might be done, but the pulse output might not be done yet
         //Pulse output done when reference_position (commanded target) = motor position (steps output by controller)
         if (!motorIsServo_ && !inmotion_) {
            //Reference position (commanded target)
            strcpy(src, "_RPx");
            src[3] = axisName_;
            reference_position = pC_->sourceValue(pC_->recdata_, src);
            if (motor_position_ != reference_position) {
               //Profile complete, but pulse output isn't
               inmotion_ = true;
            }
         }
         //Invert SSI encoder direction
         if (invert_ssi_)
            invert_ssi();
         //Before setting limits, readback limit disable parameter
         pC_->getIntegerParam(axisNo_, pC_->GalilLimitDisable_, &limitDisable);
         //reverse limit
         strcpy(src, "_LRx");
         src[3] = axisName_;
         rev_ = (bool)(pC_->sourceValue(pC_->recdata_, src) == 1) ? 0 : 1;
         //Construct controller type convenience variable
         ctrlType = (bool)(pC_->model_[3] != '2' && pC_->model_[3] != '1') ? true : false;
         if (limitDisable >= 2 && ctrlType)
            rev_ = 0;
         //forward limit
         strcpy(src, "_LFx");
         src[3] = axisName_;
         fwd_ = (bool)(pC_->sourceValue(pC_->recdata_, src) == 1) ? 0 : 1;
         if ((limitDisable == 1 && ctrlType) || (limitDisable == 3 && ctrlType))
            fwd_ = 0;
         //home switch
         strcpy(src, "_HMx");
         src[3] = axisName_;
         home_ = (bool)(pC_->sourceValue(pC_->recdata_, src) == pC_->hswact_) ? 1 : 0;
         //User data
         strcpy(src, "_ZAx");
         src[3] = axisName_;
         userData = pC_->sourceValue(pC_->recdata_, src);
         //User data dead band
         pC_->getDoubleParam(pC_->GalilUserDataDeadb_, &userDataDeadb);
         if ((userData < (userDataPosted_ - userDataDeadb_)) || (userData > (userDataPosted_ + userDataDeadb_)))
            {
            //user data is outside dead band, so post it to upper layers
            pC_->setDoubleParam(axisNo_, pC_->GalilUserData_, userData);
            userDataPosted_ = userData;
            }
         //Brake port status
         //Retrieve the brake port used for this axis
         pC_->getIntegerParam(axisNo_, pC_->GalilBrakePort_, &brakeport);
         //Applies to DMC only, so port numbering begins at 1
         if (brakeport > 0) {
            strcpy(src, "_OP0");
            digport = (unsigned)pC_->sourceValue(pC_->recdata_, src);
            mask = (unsigned)(1 << (brakeport - 1));
            digport = digport & mask;
            //Calculate brake status
            digport = (digport == mask) ? 0 : 1;
            pC_->setIntegerParam(axisNo_, pC_->GalilBrake_, digport);
         }
         else//This axis doesn't have a brake port assigned
            pC_->setIntegerParam(axisNo_, pC_->GalilBrake_, 0);
      }
   }
  return pC_->recstatus_;
}

//Set poller internal status variables based on data record info
//Called by poll
void GalilAxis::setStatus(bool *moving)
{
  int encoder_direction = -1;	//Determined encoder move direction
  int urip = 0;
  double mres, eres;
  pC_->getIntegerParam(axisNo_, pC_->GalilUseReadback_, &urip);
  //Encoder move status
  encoderMove_ = false;
  if (ueip_ || urip || motorIsServo_)
     {
     //Check encoder move
     if (last_encoder_position_ > (encoder_position_ + enc_tol_))
        {
        encoder_direction = 0;
        encoderMove_ = true;
        }
     if (last_encoder_position_ < (encoder_position_ - enc_tol_))
        {
        encoder_direction = 1;
        encoderMove_ = true;
        }
     //Encoder not moving
     if (fabs(last_encoder_position_ - encoder_position_) <= enc_tol_)
        encoder_direction = direction_;

     //Encoder direction ok flag
     encDirOk_ = (encoder_direction == direction_) ? true : false;
     }
  if (urip)
  {
	  pC_->getDoubleParam(axisNo_, pC_->motorResolution_, &mres);
      pC_->getDoubleParam(axisNo_, pC_->GalilEncoderResolution_, &eres);
	  motor_position_ = (encoder_position_ * eres)/mres;
  }
   //Determine move status
   //Motors with deferred moves pending set to status moving
   if (inmotion_ || deferredMove_)
      {
      //Set moving flags
      *moving = true;
      done_ = 0;
      }
}

//Called by poll without lock
//When encoder problem detected
//May stop motor via pollServices thread
void GalilAxis::checkEncoder(void)
{
   char message[MAX_GALIL_STRING_SIZE];	//Safety stop message
   double estall_time;			//Allowed encoder stall time specified by user
   double pestall_time;			//Possible encoder stall has been happening for this many secs

   if (((ueip_ || motorIsServo_) && !done_ && !deferredMove_ && (!encoderMove_ || !encDirOk_)))
      {
      //Record time when possible stall was first detected
      if (!pestall_detected_)
         {
         //Get time when possible encoder stall first detected
         epicsTimeGetCurrent(&pestall_begint_);
         //Flag possible encoder stall detected
         pestall_detected_ = true;
         }
      else
         {
         //Get time now
         epicsTimeGetCurrent(&pestall_nowt_);
         //Calculate stall time so far
         pestall_time = epicsTimeDiffInSeconds(&pestall_nowt_, &pestall_begint_);
         //Retrieve desired encoder stall time from ParamList
         pC_->getDoubleParam(axisNo_, pC_->GalilEStallTime_, &estall_time);
         //Check time to see if possible stall is now a stall
         if (pestall_time >= estall_time && !stopSent_)
            {
            // get last stop code
            double sc_code = getGalilAxisVal("_SC");
            // get axis moving state
            double bg_code = getGalilAxisVal("_BG");
            //Pass stall status to higher layers
            setIntegerParam(pC_->motorStatusSlip_, 1);
            //Set the stop reason so limit deceleration is applied during stop
            stop_reason_ = MOTOR_STOP_ONSTALL;
            //stop the motor
            pollRequest_.send((void*)&MOTOR_STOP, sizeof(int));
            //Flag the motor has been stopped
            stopSent_ = true;
            //Inform user
            sprintf(message, "Encoder stall stop motor %c", axisName_);
            //Set controller error mesg monitor
            pC_->setCtrlError(message);
            std::cerr << "STALL: pestall_time=" << pestall_time << " (>" << estall_time << ") encoderMove_=" << encoderMove_ << " encDirOk_=" << encDirOk_ << " _SC" << axisName_ << "=" << sc_code << " [" << lookupStopCode((int)sc_code) << "] _BG" << axisName_ << "=" << bg_code << std::endl;
            }
         }
      }
  else if (((ueip_ || motorIsServo_) && !done_ && encoderMove_ && encDirOk_ && !stopSent_) || (!done_ && !ueip_ && !motorIsServo_ && !stopSent_))
      {
      //Reset stalled encoder flag when moving ok
      //Pass stall status to higher layers
      setIntegerParam(pC_->motorStatusSlip_, 0);
      //possible encoder stall not detected
      pestall_detected_ = false;
      }
  else if (done_)
      {
      //Reset possible encoder stall detected flag so stall timer will start over
      //Leave ParamList values un-changed until user attempts to move again
      pestall_detected_ = false;
      }
}

//Called by poll without lock
//For encoded open loop steppers only
//Copy encoder value to step count register if ueip = 1
void GalilAxis::syncEncodedStepper(void)
{
   int status;		//Asyn paramList status
   int homing;		//Home flag that includes JAH
   double mres;		//Motor resolution
   double eres;		//Encoder resolution
   double rdbd;		//Motor retry deadband
   double mreadback;	//Dial readback position calculated from aux encoder/step reg
   double ereadback;	//Dial readback position calculated from main encoder

   //Retrieve homing status that includes JAH
   pC_->getIntegerParam(axisNo_, pC_->GalilHoming_, &homing);

   //Motor just stopped
   if (ueip_ && !motorIsServo_ && done_ && !last_done_ && !syncEncodedStepperAtStopSent_ &&
       !homing_ && !homing) {
      //Request encoder value be copied to step register
      pollRequest_.send((void*)&MOTOR_STEP_SYNC_ATSTOP, sizeof(int));
      //Flag that sync encoded stepper message has been sent
      syncEncodedStepperAtStopSent_ = true;
   }

   //Calculate dial readbacks
   status = pC_->getDoubleParam(axisNo_, pC_->motorResolution_, &mres);
   status |= pC_->getDoubleParam(axisNo_, pC_->GalilEncoderResolution_, &eres);
   status |= pC_->getDoubleParam(axisNo_, pC_->GalilMotorRdbd_, &rdbd);
   if (!status) {
      mreadback = motor_position_ * mres;
      ereadback = encoder_position_ * eres;
      //Stepper motor not moving, but encoder moved more than retry deadband
      if (ueip_ && !motorIsServo_ && done_ && last_done_ && !homing_ && !homing && 
          !syncEncodedStepperAtEncSent_ && (mreadback < ereadback - rdbd || mreadback > ereadback + rdbd)) {
         //Request encoder value be copied to step register
         pollRequest_.send((void*)&MOTOR_STEP_SYNC_ATENC, sizeof(int));
         //Flag that sync encoded stepper at encoder move (drift) message sent
         syncEncodedStepperAtEncSent_ = true;
      }
   }

   //Clear syncEncodedStepperAtStop flags when inmotion_
   if ((inmotion_ && syncEncodedStepperAtStopSent_ && syncEncodedStepperAtStopExecuted_)) {
      //Encoder stepper at stop not yet synchronized
      syncEncodedStepperAtStopSent_ = false;
      syncEncodedStepperAtStopExecuted_ = false;
   }
}

//Called by poll
//Sets motor stop time
void GalilAxis::setStopTime(void)
{   
   //Reset stopped time if moving
   if (!done_)
      stoppedTime_ = 0.0;

   //Any request for stopped time to be reset?
   if (resetStoppedTime_ && done_)
      {
      //Get time stop first detected
      epicsTimeGetCurrent(&stop_begint_);
      //Request completed
      resetStoppedTime_ = false;
      //Signal requesting thread that stopped time has been reset
      epicsEventSignal(stoppedTimeReset_);
      }
   if (done_ && !last_done_)
      {
      //Get time stop first detected
      epicsTimeGetCurrent(&stop_begint_);
      }
   if (done_ && last_done_)
      {
      //Get time stopped for
      epicsTimeGetCurrent(&stop_nowt_);
      //Result
      stoppedTime_ = epicsTimeDiffInSeconds(&stop_nowt_, &stop_begint_);
      }
}

//Called by poll thread
//Check for soft limit violation
//Check for homing that has just stopped for some reason
//Cancel home if either true
void GalilAxis::checkHoming(void)
{
   bool softlimits;
   char message[MAX_GALIL_STRING_SIZE];

   //Determine if soft limits are active
   softlimits = (bool)(lowLimit_ == highLimit_ && lowLimit_ == 0.0) ? false : true;

   //Is controller using main or auxillary encoder register for positioning
   double readback = (motorIsServo_) ? encoder_position_ : motor_position_;
   double estall_time;
   pC_->getDoubleParam(axisNo_, pC_->GalilEStallTime_, &estall_time);
   double homing_timeout = (HOMING_TIMEOUT < estall_time ? estall_time : HOMING_TIMEOUT);

   // ISIS: need to confirm limits high/low limit behaviour
   bool home_timeout = homing_ && (stoppedTime_ >= homing_timeout) && !cancelHomeSent_;
   bool home_soft_limits_hit = (((readback > highLimit_ && softlimits) || (readback < lowLimit_ && softlimits)) && homing_ && !cancelHomeSent_ && done_);
   if (home_timeout || home_soft_limits_hit)
      {
      sprintf(pC_->cmd_, "MG homed%c\n", axisName_);
      pC_->sync_writeReadController();
      double homed = atof(pC_->resp_);
      
      if (homed == 1)
      {
            std::cerr << "Looks like homing completed OK but unsolicited message from controller got lost" << std::endl;
            // execute logic as per GalilController::processUnsolicitedMesgs
            this->homedExecuted_ = false;
            this->pollRequest_.send((void*)&MOTOR_HOMED, sizeof(int));
            this->homedSent_ = true;
            //pC_->setIntegerParam(axisNo_, pC_->GalilHomed_, 1);
            pC_->setIntegerParam(axisNo_, pC_->motorStatusHomed_, 1);
            this->homing_ = false;
      }
      else
      {

      // get last stop code
      sprintf(pC_->cmd_, "MG _SC%c\n", axisName_);
      pC_->sync_writeReadController();
      double sc_code = atof(pC_->resp_);

      // get axis moving state
      sprintf(pC_->cmd_, "MG _BG%c\n", axisName_);
      pC_->sync_writeReadController();
      double bg_code = atof(pC_->resp_);

      sprintf(pC_->cmd_, "MG hjog%c\n", axisName_);
      pC_->sync_writeReadController();
      double hjog = atof(pC_->resp_);

      // get limit status
      sprintf(pC_->cmd_, "MG _LF%c\n", axisName_);
      pC_->sync_writeReadController();
      double lf = atof(pC_->resp_);
      sprintf(pC_->cmd_, "MG _LR%c\n", axisName_);
      pC_->sync_writeReadController();
      double lr = atof(pC_->resp_);

      epicsSnprintf(message, sizeof(message), "Homing aborted after %f seconds: _BG%c=%.0f _LF%c=%.0f _LR%c=%.0f _SC%c=%.0f [%s] hjog%c=%.0f homed%c=%.0f",
                  stoppedTime_, axisName_, bg_code, axisName_, lf, axisName_, lr,
                  axisName_, sc_code, lookupStopCode((int)sc_code), axisName_, hjog, axisName_, homed);
      pC_->setCtrlError(message);

      //Cancel home
      pollRequest_.send((void*)&MOTOR_CANCEL_HOME, sizeof(int));
      //Flag home has been cancelled
      cancelHomeSent_ = true;
      //Inform user
      sprintf(message, "%c Homing%s%s", axisName_,
                      (stoppedTime_ >= homing_timeout ? " timed out" : ""),
                      (home_soft_limits_hit ? " violated soft limits" : ""));
      //Set controller error mesg monitor
      pC_->setCtrlError(message);
      }
      }
}

//Called by poll thread
//Check motor direction/limits consistency
void GalilAxis::checkMotorLimitConsistency(void)
{
   int status;         // Return status
   int connectedlast;  // Motor connected status from last poll cycle
   int connected;      // Determined here
   int limitDisable;   // limitDisable status
   int wlp;            // Wrong limit protection enable state. 0=disabled, 1 enabled

   // Retrieve required parameters
   status = pC_->getIntegerParam(axisNo_, pC_->GalilWrongLimitProtection_, &wlp);
   status |= pC_->getIntegerParam(axisNo_, pC_->GalilMotorConnected_, &connectedlast);
   status |=  pC_->getIntegerParam(axisNo_, pC_->GalilLimitDisable_, &limitDisable);

   //Determine motor connected status
   if (limitDisable == 0)
      connected = (rev_ && fwd_) ? 0 : 1;
   else
      connected = 1;
   if (connectedlast != connected || !axisReady_)
      pC_->setIntegerParam(axisNo_, pC_->GalilMotorConnected_, connected);
   //If motor just connected or disconnected
   //Then it's unknown if motor direction matches limit orientation
   if ((!connectedlast && connected) || (connectedlast && !connected)) {
      limitsDirState_ = unknown;
   }
   //Perform motor/limits direction consistency check while moving
   if (!status && !done_) {
      //Check motor/limits consistency
      //Look for limit transitions that indicate motor direction
      //is consistent with limit orientation
      if ((rev_ && !revlast_ && !direction_) ||
          (!rev_ && revlast_ && direction_)) {
         limitsDirState_ = consistent;
      }
      if ((fwd_ && !fwdlast_ && direction_) ||
          (!fwd_ && fwdlast_ && !direction_)) {
         limitsDirState_ = consistent;
      }
      //Look for limit transitions that indicate motor direction
      //is not consistent with limit orientation
      if (((direction_ && rev_ && !revlast_) ||
           (!direction_ && fwd_ && !fwdlast_))) {
         //Set limit direction state to not_consistent with motor
         limitsDirState_ = not_consistent;
         //Given limit disable state and wlp enable state
         //Determine if wlp stop is required
         if ((wlp && direction_ && rev_ && !revlast_ && (limitDisable < 2)) ||
             (wlp && !direction_ && fwd_ && !fwdlast_ && (!limitDisable || limitDisable == 2))) {
            //Send wlp axis stop, if haven't already
            wrongLimitStop();
         }
      }
      //Pass motor/limits direction consistency to paramList
      pC_->setIntegerParam(axisNo_, pC_->GalilLimitConsistent_, limitsDirState_);
      //Below is duplicate of beginCheck, anyhow it's good to be safe
      //If wlp is enabled and motor limits direction is not_consistent
      //Don't allow move when a limit is active
      if (wlp && (limitsDirState_ == not_consistent) && ((rev_ && limitDisable < 2) ||
         (fwd_ && (!limitDisable || limitDisable == 2)))) {
        //Send wlp axis stop, if haven't already
        wrongLimitStop();
      }
   }
   //If wlp is enabled and motor limits direction is not_consistent
   //Move isn't allowed when a limit is active
   //Set paramList accordingly to update clients
   if (wlp && (limitsDirState_ == not_consistent) && ((rev_ && limitDisable < 2) ||
      (fwd_ && (!limitDisable || limitDisable == 2)))) {
      //Wrong limit protection stopping this motor now
      pC_->setIntegerParam(axisNo_, pC_->GalilWrongLimitProtectionStop_, 1);
   }
   else {
      //Wrong limit protection not stopping this motor now
      pC_->setIntegerParam(axisNo_, pC_->GalilWrongLimitProtectionStop_, 0);
   }
}

//Called by checkMotorLimitConsistency without lock
//Sends motor stop request to pollServices thread, if not done so already
void GalilAxis::wrongLimitStop(void)
{
   char message[MAX_GALIL_STRING_SIZE];	//Stop message

   if (!stopSent_) {
      //Set the stop reason so limit deceleration is applied during stop
      stop_reason_ = MOTOR_STOP_ONWLP;
      //Stop the motor
      pollRequest_.send((void*)&MOTOR_STOP, sizeof(int));
      //Flag the motor has been stopped
      stopSent_ = true;
      //Inform user
      sprintf(message, "Wrong limit protect stop motor %c", axisName_);
      //Set controller error mesg monitor
      pC_->setCtrlError(message);
   }
}

/* C Function which runs the pollServices thread */ 
static void pollServicesThreadC(void *pPvt)
{
  GalilAxis *pC = (GalilAxis*)pPvt;
  pC->pollServices();
}

//Service slow and infrequent requests from poll thread to write to the controller
//We do this in a separate thread so the poll thread is not slowed, and poll thread doesnt have a lock
void GalilAxis::pollServices(void)
{
  char post[MAX_GALIL_STRING_SIZE];	//Motor record post field
  int request = -1; 			//Real service numbers start at 0
  int status;				//Asyn param status

  while (true)
     {
     //Wait for poll to request a service
     pollRequest_.receive(&request, sizeof(int));
     if (pC_->shutdownRequested())
         break;
     //Obtain the lock
     pC_->lock();
     //What did poll request
     switch (request)
        {
        //Poll will make upper layers wait for POST, Sync encoded stepper at stop, and HOMED completion by setting moving true
        //Poll will not make upper layers wait for other services to complete
        case MOTOR_CANCEL_HOME: sprintf(pC_->cmd_, "home%c=0\n", axisName_);
                                epicsThreadSleep(.2);  //Wait as controller may still issue move upto this time after
                                errlogSevPrintf(errlogInfo, "Poll services: MOTOR CANCEL HOME %c\n", axisName_);
                                                       //Setting home to 0 (cancel home)
                                stopSent_ = true;
                                stop_reason_ = MOTOR_STOP_ONSTALL;
                                setIntegerParam(pC_->motorStatusSlip_, 1);
                                //break; Delibrate fall through to MOTOR_STOP
        case MOTOR_STOP: stopInternal(limdc_);
                         std::cerr << "Poll services: STOP " << axisName_ << std::endl;
                         break;
        case MOTOR_POST: status = pC_->getStringParam(axisNo_, pC_->GalilPost_, (int)sizeof(post), post);
                         if (!status) { 
                            //Copy post field into cmd 
                            if (strcmp(post, ""))
                            {                                
                                strcpy(pC_->cmd_, post);
                                //Write command to controller
                                pC_->sync_writeReadController();
                                std::cerr << "Poll services: POST " << axisName_ << " " << post << std::endl;
                            }
                         }
                         //Motor post complete
                         {
                         double lf = getGalilAxisVal("_LF");
                         double lr = getGalilAxisVal("_LR");
                         double sc_code = getGalilAxisVal("_SC");
                         std::cerr << "Motion Complete: _SC" << axisName_ << "=" << sc_code << " [" << lookupStopCode((int)sc_code) << "] " <<
                                 "fwdLS=" << (lf == 0.0 ? "ENGAGED" : "OK") << " revLS=" << (lr == 0.0 ? "ENGAGED" : "OK") << std::endl;
                         }
                         postExecuted_ = true;
                         break;
        case MOTOR_OFF:  //Block auto motor off if again inmotion_
                         if (!inmotion_)
                         {
                            setClosedLoop(false);	//Execute the motor off command
                            std::cerr << "Poll services: MOTOR OFF " << axisName_ << std::endl;
                         }
                         break;
        case MOTOR_BRAKE_ON://Block auto brake on if again inmotion_
                         if (!inmotion_)
                            setBrake(true);//Execute the brake on command
                         break;
        case MOTOR_STEP_SYNC_ATSTOP:
                         //Holds up done status until complete
                         syncPosition();
                         //Sync encoded stepper executed
                         syncEncodedStepperAtStopExecuted_ = true;
                         break;
        case MOTOR_STEP_SYNC_ATENC:
                         //Block sync encoded stepper if again inmotion_
                         if (!inmotion_)
                            syncPosition();
                         //Sync encoded stepper at encoder move completed
                         syncEncodedStepperAtEncSent_ = false;
                         break;
        case MOTOR_HOMED://Perform jog after home if requested
                         errlogSevPrintf(errlogInfo, "Poll services: MOTOR HOMED %c\n", axisName_);
                         jogAfterHome();
                         break;
        default: break;
        }
     //Release the lock
    pC_->unlock();
    }
}

//Execute motor record prem function
//Caller requires lock
void GalilAxis::executePrem(void)
{
  char prem[MAX_GALIL_STRING_SIZE];		//Motor record prem field

  if (pC_->getStringParam(axisNo_, pC_->GalilPrem_, (int)sizeof(prem), prem) == asynSuccess)
     {
     if (strcmp(prem, ""))
        {
        //Copy prem to cmd
        strcpy(pC_->cmd_, prem);
        //Execute the prem string
        std::cerr << "Premove " << prem << std::endl;
        pC_->sync_writeReadController();
        }
     }
}

//Execute auto motor power on
//Caller requires lock
bool GalilAxis::executeAutoOn(void)
{
  int autoonoff;	//Motor power auto on/off setting
  int motoroff;		//Motor amplifier off status

  //Execute Auto power on if activated
  pC_->getIntegerParam(axisNo_, pC_->GalilAutoOnOff_, &autoonoff);
  
  //Execute motor auto on if feature is enabled and motor is off
  if (autoonoff) {
     //Query motor off status direct from controller
     sprintf(pC_->cmd_, "MG _MO%c", axisName_);
     pC_->sync_writeReadController();
     motoroff = atoi(pC_->resp_);
     if (motoroff) {
        //motor on command
        setClosedLoop(true);
        return true;//Did some work
     }
  }

  //Did no work
  return false;
}

//Execute auto brake off
//Caller requires lock
bool GalilAxis::executeAutoBrakeOff(void)
{
  int autobrake;	//Brake auto disable/enable setting
  int brakeport;	//Brake digital out port
  int brakeoff;		//Motor brake off status

  //Retrieve brake attributes from ParamList
  //Auto brake setting
  pC_->getIntegerParam(axisNo_, pC_->GalilAutoBrake_, &autobrake);
  //Retrieve brake digital out port
  pC_->getIntegerParam(axisNo_, pC_->GalilBrakePort_, &brakeport);

  //Execute motor auto brake if feature is enabled and brake is on
  if (autobrake && brakeport > 0) {
     //Query brake status direct from controller
     sprintf(pC_->cmd_, "MG @OUT[%d]", brakeport);
     pC_->sync_writeReadController();
     brakeoff = atoi(pC_->resp_);
     if (!brakeoff) {
        //brake off command
        setBrake(false);
        return true;//Did some work
     }
  }

  //Did no work
  return false;
}

//Execute auto on delay
void GalilAxis::executeAutoOnDelay(void)
{
  double ondelay;	//Motor power on delay

  //Retrieve AutoOn delay from ParamList
  pC_->getDoubleParam(axisNo_, pC_->GalilAutoOnDelay_, &ondelay);

  //Wait required on delay if AutoOn did some work
  if (ondelay >= 0.035) {
     //AutoOn delay long enough to justify releasing lock to other threads
     pC_->unlock();
     epicsThreadSleep(ondelay);
     pC_->lock();
  }
  else //AutoOn delay too short to bother releasing lock to other threads
     epicsThreadSleep(ondelay);
}

/** Execute axis post command
  * Called by poller without lock
  * Executed after this axis dmov becomes true
  * \param[in] dmov The MR dmov of this axis*/
void GalilAxis::executePost(int dmov)
{
  int homing;				//Homing status that includes JAH
  int status = 0;				//Asyn paramlist status

  //Homing status that includes JAH
  status |= pC_->getIntegerParam(axisNo_, pC_->GalilHoming_, &homing);

  //Execute post when required
  if (dmov && !status && !homing_ && !homing && !homedSent_ && !postSent_) {
     //Send the post command
     pollRequest_.send((void*)&MOTOR_POST, sizeof(int));
     //Set post flags
     postSent_ = true;
  }

  //Clear post flags when motor record dmov false
  if (!dmov) {
     postExecuted_ = postSent_ = false;
  }
}

/** Are all related CSAxis done
  * Called by poller without lock
  * \param[out] csdmov The MR dmov of CSAxes this axis is a member of AND'd together */
asynStatus GalilAxis::allCSAxisDoneMoving(bool *csdmov) {
   int status = asynSuccess;	//Return status
   int axisNo;			//CSAxis axis number
   int pcsdmov = 0;		//Param list csaxis dmov
   unsigned i;			//Looping

   //Default csdmov
   *csdmov = true;

   //Are all related CSAxis done ?
   for (i = 0; csaxesList_[i] != '\0'; i++) {
      //Determine axis
      axisNo = csaxesList_[i] - AASCII;
      //Retrieve related csaxis dmov
      status |= pC_->getIntegerParam(axisNo, pC_->GalilDmov_, &pcsdmov);
      //AND CSAxis dmov together
      *csdmov &= (bool)pcsdmov;
   }

   //Return status
   return (asynStatus)status;
}

/** Send motor power off mesg to pollServices thread
  * Called by poller without lock
  * Executed after axis, and related csaxes dmov are all true (all moves done)
  * \param[in] dmov  The axis motor record dmov includes backlash, retries
  * \param[in] csdmov The CSAxes this axis is a member of MR dmov And'd together */
void GalilAxis::executeAutoOff(int dmov, int csdmov)
{
  int autoonoff = 0;	//Motor auto power on/off setting
  int homing;		//Homing status that includes JAH
  double offdelay;	//Motor auto off delay in seconds
  int status;		//Asyn param status

  //Retrieve required params
  status = pC_->getIntegerParam(axisNo_, pC_->GalilAutoOnOff_, &autoonoff);
  status |= pC_->getDoubleParam(axisNo_, pC_->GalilAutoOffDelay_, &offdelay);
  //Homing status that includes JAH
  status |= pC_->getIntegerParam(axisNo_, pC_->GalilHoming_, &homing);

  //Execute motor auto power off if activated
  if (autoonoff && dmov && csdmov && !status && !homing_ && !homing && !homedSent_ &&
      !autooffSent_ && stoppedTime_ >= offdelay) {
     //Send the motor off command
     pollRequest_.send((void*)&MOTOR_OFF, sizeof(int));
     autooffSent_ = true;
  }

  //Clear auto off flags when motor record dmov false
  if (!dmov) {
     autooffSent_ = false;
  }
}

/** Send brake on mesg to pollServices thread
  * Called by poller without lock
  * Executed after axis, and related csaxes dmov are all true (all moves done)
  * \param[in] dmov  The axis motor record dmov includes backlash, retries
  * \param[in] csdmov The CSAxes this axis is a member of MR dmov And'd together */
void GalilAxis::executeAutoBrakeOn(int dmov, int csdmov)
{
  int autobrake;	//Brake auto disable/enable setting
  int homing;		//Homing status that includes JAH
  double ondelay;	//Brake auto on delay in seconds
  int status;		//Asyn param status

  //Retrieve required params
  status = pC_->getIntegerParam(axisNo_, pC_->GalilAutoBrake_, &autobrake);
  status |= pC_->getDoubleParam(axisNo_, pC_->GalilAutoBrakeOnDelay_, &ondelay);
  //Homing status that includes JAH
  status |= pC_->getIntegerParam(axisNo_, pC_->GalilHoming_, &homing);
 
  //Execute auto brake on if activated
  if (autobrake && dmov && csdmov && !status && !homing_ && !homing && !homedSent_ &&
      !autobrakeonSent_ && stoppedTime_ >= ondelay) {
     //Send the brake on command
     pollRequest_.send((void*)&MOTOR_BRAKE_ON, sizeof(int));
     autobrakeonSent_ = true;
  }

  //Clear auto brake off flags when motor record dmov false
  if (!dmov) {
     autobrakeonSent_ = false;
  }
}

/** Starts motion, and delay until it begins or timeout happens
  * Called by move, moveVelocity, home
  * \param[in] caller  Method calling beginMotion
  * \param[in] position The axis position Units=steps
  * \param[in] relative Move relative or absolute - default false
  * \param[in] checkpos Check requested position true/false - default false
  * \param[in] move Really move the motor (or just prepare) true/false - default true*/
asynStatus GalilAxis::beginMotion(const char *caller, double position, bool relative, bool checkpos, bool move)
{
   double readback;	//Calculated readback
   char mesg[MAX_GALIL_STRING_SIZE];	//Controller error mesg if begin fail
   bool autoOn = false;			//Did auto on do any work?
   double estall_time;
   pC_->getDoubleParam(axisNo_, pC_->GalilEStallTime_, &estall_time);
   double begin_timeout = (BEGIN_TIMEOUT < estall_time ? estall_time : BEGIN_TIMEOUT);

   //Execute motor auto on and brake off function
   autoOn = executeAutoOn();
   autoOn |= executeAutoBrakeOff();
   if (autoOn)
      executeAutoOnDelay();

   //Execute motor record prem command
   executePrem();

   //Requested work complete
   if (!move) //success
      return asynSuccess;

   //Check position at last possible moment prior to move
   if (checkpos)
      {
      //Relative moves
      if (relative && position == 0)
         return asynSuccess;//Nothing to do
      //Absolute moves
      if (!relative)
         {
         //Retrieve readback
         readback = (motorIsServo_) ? encoder_position_ : motor_position_;
         //If new position differs from readback, then write new position
         if ((long long) position == (long long) readback)
            return asynSuccess;//Nothing to do
         }
      }

   //Begin the move
   sprintf(pC_->cmd_, "BG%c", axisName_);
   if (pC_->sync_writeReadController() == asynSuccess) {
      //Wait until axis moving true status delivered to mr, or timeout
      pC_->motion_started_.signal(); // to catch a very small move
      axisEventMonitor(beginEvent_);
   }
   else {
      //Controller gave error at begin
      // get last stop code
      double sc_code = getGalilAxisVal("_SC");
      // get axis moving state
      double bg_code = getGalilAxisVal("_BG");
      double bl = getGalilAxisVal("_BL"); // low limit
      double fl = getGalilAxisVal("_FL"); // high limit
      double tp = getGalilAxisVal("_TP"); // current position (from encoder if present)
      double td = getGalilAxisVal("_TD"); // current position (motor steps)
      double rp = getGalilAxisVal("_RP"); // commanded position (motor steps)
      // need to check signal of event? need to check how axisEventMonitor above works
      if (sc_code == 1) {
      epicsSnprintf(mesg, sizeof(mesg), "%s begin timeout axis %c after %f seconds, however this may be an artifact of a very small move as _SC%c=1: _BG%c=%.0f _SC%c=%.0f [%s] _BL%c=%f _FL%c=%f _TP%c=%f _TD%c=%f _RP%c=%f", caller, axisName_, begin_timeout, axisName_, axisName_, bg_code, axisName_, sc_code, lookupStopCode((int)sc_code), axisName_, bl, axisName_, fl, axisName_, tp, axisName_, td, axisName_, rp);
      } else {
      epicsSnprintf(mesg, sizeof(mesg), "%s begin failure axis %c after %f seconds: _BG%c=%.0f _SC%c=%.0f [%s] _BL%c=%f _FL%c=%f _TP%c=%f _TD%c=%f _RP%c=%f", caller, axisName_, begin_timeout, axisName_, bg_code, axisName_, sc_code, lookupStopCode((int)sc_code), axisName_, bl, axisName_, fl, axisName_, tp, axisName_, td, axisName_, rp);
      // getting these a lot, it it moving to somewhere very near current position?
      // comment out sending to errlog for now and send to cerr instead
      //Set controller error mesg monitor
      //pC_->setCtrlError(mesg);
      }
      std::cerr << mesg << std::endl;
      return (sc_code == 1 ? asynSuccess : asynError);
   }

   //Success
   return asynSuccess;
}

//Executes jog after home if requested
//Called by pollServices in response to MOTOR_HOMED unsolicited message from controller
//MOTOR_HOMED message only sent by controller if axis homed successfully
asynStatus GalilAxis::jogAfterHome(void) {
   const char *functionName = "jogAfterHome";
   epicsEventWaitStatus eventCaller;	//Caller signal recieve.  Used to synchronize threads
   int status = asynSuccess;		//Asyn param status
   double mres, eres;			//Motor record mres, eres
   double jahv;				//Jog after home value in egu
   int jah;				//Jog after home feature status
   double off;				//Motor record off
   int dir, dirm;			//Motor record dir, and dirm direction multiplier based on motor record DIR field
   int autoonoff;			//Motor Amp Auto on/off
   double ondelay;			//Motor on delay
   double position;			//Absolute position Units=steps
   int j = 0;				//Ensure while loop doesnt get stuck
   string mesg;				//Controller mesg
   int dmov;				//Axis motor record dmov
   static bool apply_home = ( (getenv("NO_APPLY_HOMEPOS") != NULL ? atoi(getenv("NO_APPLY_HOMEPOS")) : 0) == 0 );

   //Retrieve needed params
   status = pC_->getDoubleParam(axisNo_, pC_->GalilJogAfterHomeValue_, &jahv);
   status |= pC_->getDoubleParam(axisNo_, pC_->motorResolution_, &mres);
   status |= pC_->getIntegerParam(axisNo_, pC_->GalilDirection_, &dir);
   status |= pC_->getDoubleParam(axisNo_, pC_->GalilEncoderResolution_, &eres);
   status |= pC_->getDoubleParam(axisNo_, pC_->GalilUserOffset_, &off);
   status |= pC_->getIntegerParam(axisNo_, pC_->GalilJogAfterHome_, &jah);

   //Motor is stopped
   //Program home registers
   if (!status) {
      //Slight delay before setting position registers
      epicsThreadSleep(.1);
      //Position registers always set to 0 in dial coordinates
      //Use OFF to give correct user position
      //Program motor position register
      if (apply_home) {
          std::cerr << "JogAfterHome:  redefine position to 0 for " << axisName_ << std::endl; 
          sprintf(pC_->cmd_, "DP%c=0", axisName_);
          pC_->sync_writeReadController();
          //Program encoder position register
          if (ueip_ || motorIsServo_) {
             sprintf(pC_->cmd_, "DE%c=0", axisName_);
             pC_->sync_writeReadController();
          }
      } else {
          std::cerr << "JogAfterHome:  not applying home positon for " << axisName_ << std::endl; 
      }
      
      //Give ample time for position register updates to complete
      epicsThreadSleep(.2);
   }

   //Homed pollService completed
   //This will cause moving status to become false
   homedExecuted_ = true;
   homedSent_ = false;

   //Do jog after home move
   if (!status && jah) {

      //Ensure MR dmov true before continuing
      pC_->getIntegerParam(axisNo_, pC_->GalilDmov_, &dmov);
      if (!dmov) {
         //Unlock mutex give chance for sync poller to get the lock
         pC_->unlock();
         while (!dmov) {
            epicsThreadSleep(.001);
            //Give up after 2000 ms
            if (j++ > 2000) {
               mesg = "Jog after home failed, " + string(1, axisName_) + " motor record busy";
               pC_->setCtrlError(mesg);
               status = asynError;
               break;
            }
            //Get updated dmov value
            pC_->getIntegerParam(axisNo_, pC_->GalilDmov_, &dmov);
         }
         //Obtain lock, continue with jog after home
         pC_->lock();
      }

      //Ensure useCSADynamics is false before continuing
      //jogAfterHome is for independant axis home
      //Case where user pressed axis HOMR/HOMF during CSAxis move
      //And GalilCSAxis::clearCSAxisDynamics hasn't had time to execute
      useCSADynamics_ = false;

      //Calculate direction multiplier
      dirm = (dir == 0) ? 1 : -1;
      //Calculate position in steps from jog after home value in user coordinates
      position = (double)((jahv - off)/mres) * dirm;
      //Check all axis settings
      status |= checkAllSettings(functionName, axisName_, position, 100);

      if (!status) {
         //If all settings OK, do the move
         errlogSevPrintf(errlogInfo, "jogging %c after home to raw position %.0f\n", axisName_, position);
         if (!moveThruMotorRecord(position, true)) {
            //Requested move equal or larger than 1 motor step, move success
            //Retrieve AutoOn delay from ParamList
            pC_->getDoubleParam(axisNo_, pC_->GalilAutoOnDelay_, &ondelay);
            //Retrieve Auto on off status from ParamList
            pC_->getIntegerParam(axisNo_, pC_->GalilAutoOnOff_, &autoonoff);
            if (!autoonoff)
               ondelay = 0.0;
            //Unlock mutex so GalilAxis::move is called
            //Also give chance for sync poller to get the lock
            pC_->unlock();
            //Wait for GalilAxis::move method to signal move status true given to MR
            eventCaller = epicsEventWaitWithTimeout(callerEvent_, requestedTimeout_ + ondelay);
            //Check result
            if (eventCaller == epicsEventWaitOK)
               jogAfterHome_ = true;//Jog after home started
            else if (moveThruRecord_) {
               //GalilAxis::move wasn't called
               //Set flag false
               moveThruRecord_ = false;
               //Disable further writes to axis motor record from driver
               pC_->setIntegerParam(axisNo_, pC_->GalilMotorSetValEnable_, 0);
            }
            //Done, move on to next motor
            pC_->lock();
         }
      }
   }//JAH

   //If no jog after home, then homing completed
   if (!jogAfterHome_)
      setIntegerParam(pC_->GalilHoming_, 0);

   return (asynStatus)status;
}

/** Delay until event or timeout
  * May signal interested caller thread if signalCaller_ true and event received successfully
  * \param[in] requestedEvent Requested event (ie. beginEvent_, stopEvent_) */
asynStatus GalilAxis::axisEventMonitor(epicsEventId requestedEvent) {
   asynStatus result = asynSuccess; //Default result

   //Set the requested event
   requestedEvent_ = requestedEvent;
   //Signal event monitor to start monitoring
   epicsEventSignal(eventMonitorStart_);
   //Tell poller to signal when event occurs
   requestedEventSent_ = false;

   //Poller sends the signal we wait on
   //Release lock so sync poller can get lock
   pC_->unlock();
   //Wait for event monitor to finish
   epicsEventWait(eventMonitorDone_);
   //Check result
   if (eventResult_ != epicsEventWaitOK)
      result = asynError; //Timeout, or error
   else {
      //Inform interested caller thread that eventMonitor got event
      if (signalCaller_) {
         //Signal caller if eventMonitor got event
         epicsEventSignal(callerEvent_);
         //Caller has been signaled
         //Set true in moveThruMotorRecord only
         signalCaller_ = false;
      }
   }
   //Retake lock
   pC_->lock();

   //Return result
   return result;
}

/** Send axis events to waiting threads
  * Called by poller without lock after passing
  * motor status to motor record */
void GalilAxis::sendAxisEvents(void) {

   //Check for work
   if (requestedEventSent_) return; //Nothing to do

   //Motion begin event = confirmed moving status true delivered to MR
   if (requestedEvent_ == beginEvent_)
      if (inmotion_) {
         //Axis is really moving
         //Begin event
         epicsEventSignal(beginEvent_);
         //Tell poller we dont want signal when events occur
         requestedEventSent_ = true;
      }

   //Motion stop event = confirmed moving status false delivered to MR
   if (requestedEvent_ == stopEvent_)
      if (!inmotion_) {
         //Axis is really stopped
         //Stop event
         epicsEventSignal(stopEvent_);
         //Tell poller we dont want signal when events occur
         requestedEventSent_ = true;
      }
}

double GalilAxis::getGalilAxisVal(const char* name)
{
    static const char *functionName = "GalilAxis::getGalilAxisVal";
    sprintf(pC_->cmd_, "MG %s%c\n", name, axisName_);
    pC_->sync_writeReadController();
    return atof(pC_->resp_);
}

/** Polls the axis.
  * This function reads the controller position, encoder position, the limit status, the moving status, 
  * and the drive power-on status.  It does not current detect following error, etc. but this could be
  * added.
  * It calls setIntegerParam() and setDoubleParam() for each item that it polls,
  * \param[out] moving A flag that is set indicating that the axis is moving (1) or done (0). */
asynStatus GalilAxis::poller(bool& moving)
{
   //static const char *functionName = "GalilAxis::poll";
   int dmov;			//Motor record dmov
   bool csdmov;			//Related CSAxis dmov And'd together
   int home;			//Home status to give to motorRecord
   int status;			//Communication status with controller
   double stopDelay;		//Delay stop reporting
   int urip = 0;

   //Default communication status
   status = asynError;
   //Default home status
   home = 0;
   //Default moving status
   done_ = 1;
   moving = false;
   
   //Retrieve required params
   status = pC_->getIntegerParam(axisNo_, pC_->GalilUseEncoder_, &ueip_);
   status |= pC_->getIntegerParam(axisNo_, pC_->GalilEncoderTolerance_, &enc_tol_);
   status |= pC_->getIntegerParam(axisNo_, pC_->GalilDmov_, &dmov);
   status |= pC_->getDoubleParam(axisNo_, pC_->GalilStopDelay_, &stopDelay);
   status |= pC_->getIntegerParam(axisNo_, pC_->GalilUseReadback_, &urip);

   //Are all related CSAxis done (includes retries, backlash)
   //Used for auto amp off, and auto brake on for this axis
   status |= allCSAxisDoneMoving(&csdmov);

   //Extract axis motion data from controller datarecord, and load into GalilAxis instance
   status |= getStatus();
   if (status) goto skip;

   //Set poll variables in GalilAxis based on data record info
   setStatus(&moving);

   //Set motor stop time
   setStopTime();

   //Check motor direction/limits consistency
   checkMotorLimitConsistency();

   //Check homing flag
   checkHoming();

   //Execute motor record post if stopped and not homing
   executePost(dmov);

   //Execute auto brake on function if stopped, not homing and no new move coming
   executeAutoBrakeOn(dmov, csdmov);

   //Execute motor auto power off function if stopped, not homing and no new move coming
   executeAutoOff(dmov, csdmov);

   //check for stalled encoders, whilst we are moving
   //stop motors with stalled encoders
   checkEncoder();

   //After encoded stepper stop, copy main encoder to aux
   syncEncodedStepper();

   //check ssi encoder connect status
   set_ssi_connectflag();

   //Check home switch
   if (home_ && !limit_as_home_)
      home = 1;

   /*If Rev switch is on and we are using it as a home, set the appropriate flag*/
   if (rev_ && limit_as_home_)
      home = 1;

   /*If fwd switch is on and we are using it as a home, set the appropriate flag*/
   if (fwd_ && limit_as_home_)
      home = 1;

   //Reset homing status that includes JAH
   if (dmov && jogAfterHome_ && done_ && last_done_) {
      //JAH completed
      jogAfterHome_ = false;
      //Set homing flag that includes JAH to 0
      pC_->setIntegerParam(axisNo_, pC_->GalilHoming_, 0);
   }

   //Clear stop axis flags now dmov true
   if (stopInternal_ && dmov && last_done_ && done_) {
      stopInternal_ = false;
      stopSent_ = false;
      stop_reason_ = MOTOR_OKAY;
   }

    if (first_poll_)
   {
	   smoothed_encoder_position_ = encoder_position_;
	   first_poll_ = false;
   }
   else
   {
       smoothed_encoder_position_ = (1.0 - encoder_smooth_factor_) * encoder_position_ +
                               encoder_smooth_factor_ * smoothed_encoder_position_;
   }
skip:
   //Save encoder position, and done for next poll cycle
   last_encoder_position_ = encoder_position_;
   last_done_ = done_;
   //Save limit status for next poll cycle
   //Used to support checkMotorLimitConsistency and wronglimit protection
   //Can't retrieve from paramList as limit status can be masked/hidden from motor record
   //By this driver during homing
   revlast_ = rev_;
   fwdlast_ = fwd_;

   //Set status
   if (encoder_smooth_factor_ != 0.0)
   {
       //Pass step count/aux encoder info to motorRecord
       setDoubleParam(pC_->motorPosition_, motor_position_);
       //Pass encoder value to motorRecord
	   if (moving)
	   {
           setDoubleParam(pC_->motorEncoderPosition_, encoder_position_);
		   smoothed_encoder_position_ = encoder_position_; // no smoothing during moves, reset series
	   }
	   else if (stoppedTime_ < motor_dly_ || encoderMove_)
	   {
//		   if (!encoderMove_) {
//		       std::cerr << axisName_ << " raw " << encoder_position_ << " smoothed " << smoothed_encoder_position_ << " (" << stopped_time_ << "," << motor_dly_ << "," << encoderMove_ << ")" << std::endl;
//	       }
           setDoubleParam(pC_->motorEncoderPosition_, smoothed_encoder_position_);
	   }
   }
   else if ( !urip || (moving || encoderMove_) ) {
       //Pass step count/aux encoder info to motorRecord
       setDoubleParam(pC_->motorPosition_, motor_position_);
       //Pass encoder value to motorRecord
       setDoubleParam(pC_->motorEncoderPosition_, encoder_position_);
   }
   //Pass home status to motorRecord
   setIntegerParam(pC_->motorStatusAtHome_, home);
   setIntegerParam(pC_->motorStatusHome_, home);
   //Pass direction to motorRecord
   setIntegerParam(pC_->motorStatusDirection_, direction_);
   //Tell upper layers motor is moving whilst
   //sync encoded stepper at stop or
   //homing requests are being executed or
   //GalilAxis has a new position from CSAxis (setPositionIn_ true) or
   //stopDelay has yet to expire
   //This prevents new moves being initiated whilst above functions are being executed
   //Also keeps HOMR and HOMF 1 until homing finished
   //Done late in poll to allow parallel execution with pollServices
   if ((homedSent_ && !homedExecuted_) || homing_ || setPositionIn_ ||
       (syncEncodedStepperAtStopSent_ && !syncEncodedStepperAtStopExecuted_) ||
       (stoppedTime_ < stopDelay && !status)) {
      moving = true;
      done_ = 0;
      //Set setPositionIn_ false, we've acted by setting moving true for 1 cycle
      //Moving true for 1 cycle will cause GalilAxis MR to synchronize drive field to new readback value
      setPositionIn_ = (setPositionIn_) ? false : setPositionIn_;
   }

   //Dont show limits whilst homing otherwise mr may interrupt custom routines
   if (homing_) {
      //Dont show reverse limit when homing
      rev_ = 0;
      //Dont show forward limit when homing
      fwd_ = 0;
   }

   //Pass limit status to motorRecord
   setIntegerParam(pC_->motorStatusLowLimit_, rev_);
   setIntegerParam(pC_->motorStatusHighLimit_, fwd_);
   //Pass moving status to motorRecord
   setIntegerParam(pC_->motorStatusDone_, done_);
   setIntegerParam(pC_->motorStatusMoving_, moving ? 1:0);
   //Pass comms status to motorRecord
   setIntegerParam(pC_->motorStatusCommsError_, status ? 1:0);
   //Update motor status fields in upper layers using asynMotorAxis->callParamCallbacks
   callParamCallbacks();
   //Status delivered to MR, now send events to waiting threads
   sendAxisEvents();
   //Always return success. Dont need more error mesgs
   return asynSuccess;
}

/*-----------------------------------------------------------------------------------*/
/* Returns true if motor is enabled with current digital IO status
*/

bool GalilAxis::motor_enabled(const char *caller, string &mesg)
{
   unsigned mask;				//Mask used to check motor go/no go status
   unsigned i, j;				//General loop counters
   struct Galilmotor_enables *motor_enables = NULL;  //Convenience pointer to GalilController motor_enables[digport]
   unsigned binaryin;				//binary in state

   //Retrieve binary in data for 1st bank from ParamList (ie. bits 0-7)
   pC_->getUIntDigitalParam(0, pC_->GalilBinaryIn_ , &binaryin, 0xFF);

   //Cycle through digital inputs structure looking for current motor
   for (i = 0; i < 8; i++) {
      //Retrieve structure for digital port from controller instance
      motor_enables = (Galilmotor_enables *)&pC_->motor_enables_[i];
      //Scan through motors in the disable list
      for (j = 0; j < strlen(motor_enables->motors); j++) {
         //Is the current GalilAxis found in disable list
         if (motor_enables->motors[j] == axisName_) {
            //motor found
            //Calculate mask
            mask = (1 << (i));
            //Check "no go" status
            if ((binaryin & mask) == mask) {
               /* Motor is "no go", due to digital IO state */
               if (motor_enables->disablestates[j] == 1) {
                  //Set controller error mesg
                  mesg = string(caller) + " failed, " + string(1, axisName_) + " ";
                  mesg += "disabled due to digital input";
                  return(false);
               }
            }
            else {
               /* Motor is "no go", due to digital IO state */
               if (motor_enables->disablestates[j] == 0) {
                  //Set controller error mesg
                  mesg = string(caller) + " failed, " + string(1, axisName_) + " ";
                  mesg += "disabled due to digital input";
                  return(false);
               }
            }				
         }
      } //For
   } //For
	
   // Motor is enabled
   return(true);
}

/*-----------------------------------------------------------------------------------*/
/* Checks ssi encoder connect status and sets connect flag
*/

void GalilAxis::set_ssi_connectflag(void)
{
    double disconnect_val = 0.0;
    long disconnect_valtmp = 0;
    int ssi_connect;			//SSI encoder connect status
    int ssicapable, ssiinput;		//SSI parameters
    int ssitotalbits, ssierrbits;	//SSI parameters
    int ssidataform;
    int i;
    bool even;				//Total number of bits odd or even

    //Retrieve SSI parameters required
    pC_->getIntegerParam(pC_->GalilSSICapable_, &ssicapable);
    pC_->getIntegerParam(axisNo_, pC_->GalilSSIInput_, &ssiinput);
    pC_->getIntegerParam(axisNo_, pC_->GalilSSITotalBits_, &ssitotalbits);
    pC_->getIntegerParam(axisNo_, pC_->GalilSSIErrorBits_, &ssierrbits);
    pC_->getIntegerParam(axisNo_, pC_->GalilSSIData_, &ssidataform);

    if (ssicapable != 0 && ssiinput != 0)
       {
       //work out the value recieved when encoder disconnected
       if (ssidataform == 1)
          {
          //First we do gray code encoders
          //Determine whether total number of bits is odd or even
          even = (ssitotalbits % 2) ? false : true;
          //Calculate disconnect value for gray code encoders
          for (i = 0; i < (ssitotalbits - ssierrbits); i++)
             {
             if (even)
                {
                //Even number of total bits
                if ((i % 2))
                   disconnect_valtmp |= (long) 1 << i;
                }
             else
                {
                //Odd number of total bits
                if (!(i % 2))
                   disconnect_valtmp |= (long) 1 << i;
                }
             }
          if (!(invert_ssi_))
             disconnect_val = (double)disconnect_valtmp;
          else
             disconnect_val = ((1 << (ssitotalbits - ssierrbits)) - 1) - disconnect_valtmp;
          }
       else
          {
          //last we do binary code encoders
          if (!(invert_ssi_))
             disconnect_val = (1 << (ssitotalbits - ssierrbits)) - 1;
          else
             disconnect_val = 0;
          }
	
       //safe default
       ssi_connect = 0;
       //check if encoder readback == value recieved when encoder disconnected
       //set connect flag accordingly
       if ((ssiinput == 1 && !encoderSwapped_) || (ssiinput == 2 && encoderSwapped_))	//Main encoder
          ssi_connect = (encoder_position_ == disconnect_val) ? 0 : 1;
       if ((ssiinput == 2 && !encoderSwapped_ && motorIsServo_) || (ssiinput == 1 && encoderSwapped_ && motorIsServo_))	//Aux encoder
          ssi_connect = (motor_position_ == disconnect_val) ? 0 : 1;
       //Set motorRecord MSTA bit 15 motorStatusHomed_
       //With SSI encoder, just move it where you want it
       setIntegerParam(pC_->motorStatusHomed_, ssi_connect);
       pC_->setIntegerParam(axisNo_, pC_->GalilSSIConnected_, ssi_connect);
       }
    else
       pC_->setIntegerParam(axisNo_, pC_->GalilSSIConnected_, 0);
}

/*-----------------------------------------------------------------------------------*/
/* Get ssi encoder settings from controller
*/

asynStatus GalilAxis::get_ssi(int function, epicsInt32 *value)
{
	asynStatus status;				 //Comms status
	int ssiinput, ssitotalbits, ssisingleturnbits;   //Local copy of ssi parameters
        int ssierrbits, ssitimecode, ssidataform;	 //Local copy of ssi parameters
	//Construct query
	sprintf(pC_->cmd_, "SI%c=?", axisName_);
	//Write query to controller
	if ((status = pC_->sync_writeReadController()) == asynSuccess)
		{
		//Convert response to integers
		sscanf(pC_->resp_, "%d, %d, %d, %d, %d, %d\n",&ssiinput, &ssitotalbits, &ssisingleturnbits, &ssierrbits, &ssitimecode, &ssidataform);
		if (function == pC_->GalilSSIInput_)
			*value = ssiinput;
	        if (function == pC_->GalilSSITotalBits_)
			*value = ssitotalbits;
		if (function == pC_->GalilSSISingleTurnBits_)
			*value = ssisingleturnbits;
		if (function == pC_->GalilSSIErrorBits_)
			*value = ssierrbits;
	        if (function == pC_->GalilSSITime_)
			*value = ssitimecode;
		if (function == pC_->GalilSSIData_)
			*value = ssidataform - 1;
		}
	else    //Comms error, return startup default or last good read value
		{
		pC_->getIntegerParam(axisNo_, function, value);
		}

	return status;
}

/*-----------------------------------------------------------------------------------*/
/* Send ssi encoder settings to controller
*/

asynStatus GalilAxis::set_ssi(void)
{
	char mesg[MAX_GALIL_STRING_SIZE];		//Error mesg
	int ssiinput, ssitotalbits, ssisingleturnbits;  //Local copy of ssi parameters
	int ssierrbits, ssitimecode, ssidataform;	//Local copy of ssi parameters
	asynStatus status;				//Comms status
	int allowed[] = {4,8,10,12,13,24,26};		//Allowed values of p parameter for SSI setting
	bool found;					//Used to validate ssitimecode
	int i;						//General loop variable
	int ssiinput_rbk;				//SSI setting before action

	//Query SSI setting before action
	sprintf(pC_->cmd_, "SI%c=?", axisName_);
	//Write query to controller
	if ((status = pC_->sync_writeReadController()) == asynSuccess)
		sscanf(pC_->resp_, "%d, %d, %d, %d, %d, %d\n",&ssiinput_rbk, &ssitotalbits, &ssisingleturnbits, &ssierrbits, &ssitimecode, &ssidataform);
	
	//Retrieve ssi parameters from ParamList
	pC_->getIntegerParam(axisNo_, pC_->GalilSSIInput_, &ssiinput);
	pC_->getIntegerParam(axisNo_, pC_->GalilSSITotalBits_, &ssitotalbits);
	pC_->getIntegerParam(axisNo_, pC_->GalilSSISingleTurnBits_, &ssisingleturnbits);
	pC_->getIntegerParam(axisNo_, pC_->GalilSSIErrorBits_, &ssierrbits);
	pC_->getIntegerParam(axisNo_, pC_->GalilSSITime_, &ssitimecode);
	pC_->getIntegerParam(axisNo_, pC_->GalilSSIData_, &ssidataform);

	if (!ssiinput && ssiinput_rbk)//User just disabled ssi, unset motorRecord MSTA bit 15 motorStatusHomed_
		setIntegerParam(pC_->motorStatusHomed_, 0);

	//Ensure parameters are valid
	if (ssitotalbits < -31)
		ssitotalbits = -31;
	if (ssitotalbits > 31)
		ssitotalbits = 31;

	if (ssierrbits < -2)
		ssierrbits = -2;
	if (ssierrbits > 2)
		ssierrbits = 2;

	if (ssisingleturnbits < -31)
		ssisingleturnbits = -31;
	if (ssisingleturnbits > 31)
		ssisingleturnbits = 31;

	//validate ssitimecode
	found = false;
	for (i=0;i<7;i++)
		if (ssitimecode == abs(allowed[i]))
			found = true;

	//Could not validate specified ssitimecode, set it to default
	if (found == false)
		ssitimecode = 13;

	//Convert ssidataform from record values to controller values
	ssidataform = (ssidataform == 0) ? 1 : 2;

	//Check if main and auxiliary encoder has been swapped by DFx=1
	sprintf(pC_->cmd_, "MG _DF%c", axisName_);
	pC_->sync_writeReadController();
	encoderSwapped_ = (bool)atoi(pC_->resp_);

	if ((ssiinput == 2 && !encoderSwapped_ && !motorIsServo_) || (ssiinput == 1 && encoderSwapped_ && !motorIsServo_))
		{
		sprintf(mesg, "%c cannot use auxillary encoder for SSI whilst motor is stepper", axisName_);
		pC_->setCtrlError(mesg);
		status = asynError;
		}
	else
		{
		//Update ssi setting on controller
		sprintf(pC_->cmd_, "SI%c=%d,%d,%d,%d<%d>%d", axisName_, ssiinput, ssitotalbits, ssisingleturnbits, ssierrbits, ssitimecode, ssidataform);
		//Write setting to controller
		status = pC_->sync_writeReadController();
		}
	
	return status;
}

/*-----------------------------------------------------------------------------------*/
/* Get BiSS encoder settings from controller
*/

asynStatus GalilAxis::get_biss(int function, epicsInt32 *value)
{
	asynStatus status = asynSuccess;  //Comms status
        int bissInput = 0;
        int bissData1 = 0;
        int bissData2 = 0;
        int bissZeroPadding = 0;
        int bissClockDivider = 0;
	//Construct query
	sprintf(pC_->cmd_, "SS%c=?", axisName_);
	//Write query to controller
	if ((status = pC_->sync_writeReadController()) == asynSuccess)
		{
		//Convert response to integers
		sscanf(pC_->resp_, "%d, %d, %d, %d, %d\n", &bissInput, &bissData1, &bissData2, 
                                                           &bissZeroPadding, &bissClockDivider);

		if (function == pC_->GalilBISSInput_)
			*value = bissInput;
	        if (function == pC_->GalilBISSData1_)
			*value = bissData1;
		if (function == pC_->GalilBISSData2_)
			*value = bissData2;
		if (function == pC_->GalilBISSZP_)
			*value = bissZeroPadding;
	        if (function == pC_->GalilBISSCD_)
			*value = bissClockDivider;
                }
	else    //Comms error, return startup default or last good read value
		{
		pC_->getIntegerParam(axisNo_, function, value);
		}

	return status;
}

/*-----------------------------------------------------------------------------------*/
/* Send BiSS encoder settings to controller
*/

asynStatus GalilAxis::set_biss(void)
{
   char mesg[MAX_GALIL_STRING_SIZE]; //Error mesg
   int motortype = 0;
   bool stepper = false;
   int bissInput = 0;
   int bissData1 = 0;
   int bissData2 = 0;
   int bissZeroPadding = 0;
   int bissClockDivider = 0;
   asynStatus status = asynSuccess; //Comms status
   int bissInput_rbk; //BiSS setting before action
   const char *functionName = "GalilAxis::set_biss";
        
   //Query BiSS setting before action
   sprintf(pC_->cmd_, "SS%c=?", axisName_);
   //Write query to controller
   if ((status = pC_->sync_writeReadController()) == asynSuccess)
      sscanf(pC_->resp_, "%d, %d, %d, %d, %d\n",&bissInput_rbk, &bissData1, &bissData2, 
                                                          &bissZeroPadding, &bissClockDivider);
	
   asynPrint(pC_->pasynUserSelf, ASYN_TRACE_FLOW, 
              "%s Existing BiSS setting on axis %c: %d,%d,%d,%d<%d\n", 
                functionName, axisName_, bissInput_rbk, bissData1, bissData2, bissZeroPadding, bissClockDivider);

   //Retrieve existing BiSS parameters from ParamList
   pC_->getIntegerParam(axisNo_, pC_->GalilBISSInput_, &bissInput);
   pC_->getIntegerParam(axisNo_, pC_->GalilBISSData1_, &bissData1);
   pC_->getIntegerParam(axisNo_, pC_->GalilBISSData2_, &bissData2);
   pC_->getIntegerParam(axisNo_, pC_->GalilBISSZP_, &bissZeroPadding);
   pC_->getIntegerParam(axisNo_, pC_->GalilBISSCD_, &bissClockDivider);
   pC_->getIntegerParam(axisNo_, pC_->GalilMotorType_, &motortype);

   if (!bissInput && bissInput_rbk) {
      //User just disabled BiSS, unset motorRecord MSTA bit 15 motorStatusHomed_
      setIntegerParam(pC_->motorStatusHomed_, 0);
   }

   //Enforce limits
   bissInput = max(BISS_INPUT_MIN, min(bissInput, BISS_INPUT_MAX));
   bissData1 = max(BISS_DATA1_MIN, min(bissData1, BISS_DATA1_MAX));
   bissData2 = max(BISS_DATA2_MIN, min(bissData2, BISS_DATA2_MAX));
   bissZeroPadding = max(BISS_ZP_MIN, min(bissZeroPadding, BISS_ZP_MAX));
   bissClockDivider = max(BISS_CD_MIN, min(bissClockDivider, BISS_CD_MAX));

   //Check if main and auxiliary encoder has been swapped by DFx=1
   sprintf(pC_->cmd_, "MG _DF%c", axisName_);
   pC_->sync_writeReadController();
   encoderSwapped_ = (bool)atoi(pC_->resp_);

   //Figure out if we have a stepper
   stepper = ((motortype >= 2) && (motortype <= 5));

   if ((bissInput == 2 && !encoderSwapped_ && !motorIsServo_ && stepper)
            || (bissInput == 1 && encoderSwapped_ && !motorIsServo_ && stepper)) {
      sprintf(mesg, "%c cannot use auxillary encoder for BiSS whilst motor is stepper", axisName_);
      pC_->setCtrlError(mesg);
      status = asynError;
   }
   else {
      //Update BiSS setting on controller
      sprintf(pC_->cmd_, "SS%c=%d,%d,%d,%d<%d", axisName_, bissInput, bissData1, bissData2, 
              bissZeroPadding, bissClockDivider);
      //Write setting to controller
      status = pC_->sync_writeReadController();
          
      if (status == asynSuccess) {
         asynPrint(pC_->pasynUserSelf, ASYN_TRACE_FLOW, 
                   "%s New BiSS setting on axis %c: %d,%d,%d,%d<%d\n", 
                   functionName, axisName_, bissInput, bissData1, bissData2, bissZeroPadding, bissClockDivider);
         if (bissInput != 0) {
              setIntegerParam(pC_->motorStatusHomed_, 1);
         }
      }
   }

   return status;
}

/* 
 * Read the _SSm operand to get the BiSS status bits
 * Bit 0 - Timeout
 * Bit 1 - CRC status
 * Bit 2 - Error
 * Bit 3 - Warning
 *
 * The BiSS active levels may have to be set using SY command.
 * This function should only be called by the pollServices function.
 */
asynStatus GalilAxis::checkBISSStatusService(void)
{
   static bool errorPrint = true;
   asynStatus status = asynSuccess; //Comms status
   const char *functionName = "GalilAxis::checkBiSSStatus";
   int bissInput = 0;
   int bissStat = 0;

   //Need to check BiSS is enabled as well
   pC_->getIntegerParam(axisNo_, pC_->GalilBISSInput_, &bissInput);
   if (bissInput != 0) {
	  
      //Read the BiSS status bits
      sprintf(pC_->cmd_, "MG _SS%c", axisName_);
      //Write command to controller
      status = pC_->sync_writeReadController(false, false);
          
      if (status == asynSuccess) {
         sscanf(pC_->resp_, "%d", &bissStat);
         setIntegerParam(pC_->GalilBISSStatTimeout_, (bissStat >> BISS_STAT_TIMEOUT) & 0x1);
         setIntegerParam(pC_->GalilBISSStatCRC_,     (bissStat >> BISS_STAT_CRC) & 0x1);
         setIntegerParam(pC_->GalilBISSStatError_,   (bissStat >> BISS_STAT_ERROR) & 0x1);
         setIntegerParam(pC_->GalilBISSStatWarn_,    (bissStat >> BISS_STAT_WARN) & 0x1);
         if (!errorPrint) {
            asynPrint(pC_->pasynUserSelf, ASYN_TRACE_ERROR, 
			   "%s Successfully reading BiSS encoder status bits on controller %s, axis %d.\n", 
			   functionName, pC_->portName, axisNo_);
	        errorPrint = true;
        }
      } else {
        if (errorPrint) {
           asynPrint(pC_->pasynUserSelf, ASYN_TRACE_ERROR, 
              "%s Failed to read BiSS encoder status bits on controller %s, axis %d.\n", 
              functionName, pC_->portName, axisNo_);
	       errorPrint = false;
	    }
	  }
	} // end of if (bissInput != 0)
	
   return status;
}

/* C Function which runs the status thread */ 
static void axisStatusThreadC(void *pPvt)
{
  GalilAxis *pC = (GalilAxis*)pPvt;
  pC->axisStatusThread();
}

/* Function which runs in its own thread to poll axis and encoder status */ 
void GalilAxis::axisStatusThread()
{
  int ssiCapable = 0;
  int bissCapable = 0;
  int bissInput = 0;
  int bissStatPoll = 0;
  double pollDelay = 1;
  epicsEventWaitStatus event = epicsEventWaitTimeout;
  int status = asynSuccess;

  while (true) {
    //Retrieve required parameters
    status = pC_->getIntegerParam(axisNo_, pC_->GalilSSICapable_, &ssiCapable);
    status |= pC_->getIntegerParam(axisNo_, pC_->GalilBISSCapable_, &bissCapable);

    if (event == epicsEventWaitTimeout && !shuttingDown_) {
       if (ssiCapable == 1 && !status && !shuttingDown_) {
          //Polling SSI status
       }
       if (bissCapable == 1 && !status && !shuttingDown_) {
          status = pC_->getIntegerParam(axisNo_, pC_->GalilBISSStatPoll_, &bissStatPoll);
          //Check BiSS is enabled
          status |= pC_->getIntegerParam(axisNo_, pC_->GalilBISSInput_, &bissInput);  
          if (bissInput != 0 && bissStatPoll == 1 && !status) {
             //Grab the lock only when required
             pC_->lock();
             checkBISSStatusService();
             pC_->unlock();
          }
       }
    }
    else {
       //Thread will exit
       epicsEventSignal(axisStatusShutdown_);
       axisStatusRunning_ = false;
       break;
    }
    
    //Retrieve requested pollDelay
    status = pC_->getDoubleParam(axisNo_, pC_->GalilStatusPollDelay_, &pollDelay);
    //Limit pollDelay values
    if (pollDelay < .1)
       pollDelay = .1;
    if (pollDelay > 10)
       pollDelay = 10;

    //Perform interruptable delay
    if (!status && !shuttingDown_) {
       event = epicsEventWaitWithTimeout(axisStatusShutRequest_, pollDelay);
    } 
    else if (!shuttingDown_) {
      epicsThreadSleep(1);
    }
  } //while
}

/* C Function which runs the event monitor thread */ 
static void eventMonitorThreadC(void *pPvt)
{
  GalilAxis *pC = (GalilAxis*)pPvt;
  pC->eventMonitorThread();
}

//Event monitor runs in its own thread
//Used to synchronize threads to events sent by poller
void GalilAxis::eventMonitorThread()
{ 
  while (true) {
    //Wait for request
    epicsEventWait(eventMonitorStart_);
    //Check for shutdown request
    if (shuttingDown_)
       return; //Exit the thread
    //Wait for requested event
    eventResult_ = epicsEventWaitWithTimeout(requestedEvent_, requestedTimeout_);
    if (eventResult_ != epicsEventWaitOK) {
       //Timeout, or error occurred
       //Tell poller we dont want signal when events occur
       requestedEventSent_ = true;
    }
    //Signal waiting thread begin monitor done
    epicsEventSignal(eventMonitorDone_);
  }
}

/*
 * Confirms shutdown of axis status thread
 * Blocks until the thread has finished
 * Required due to controller lock usage in axisStatusThread
 */
void GalilAxis::axisStatusShutdown()
{
  if (axisStatusRunning_) {
    //Request axisStatus thread shutdown
    epicsEventSignal(axisStatusShutRequest_);
    //Wait for axisStatus thread to signal it's shutdown
    epicsEventWait(axisStatusShutdown_);
  }
}

struct StopCode
{
    int sc;
    const char* mess;
};

static const StopCode stopCodes[] = { 
    { 0, "Motors are running, independent mode" }, 
    { 1, "Motors decelerating or stopped at commanded independent position" }, 
    { 2, "Decelerating or stopped by FWD limit switch or soft limit FL" }, 
    { 3, "Decelerating or stopped by REV limit switch or soft limit BL" }, 
    { 4, "Decelerating or stopped by Stop Command (ST)" }, 
    { 6, "Stopped by Abort input" }, 
    { 7, "Stopped by Abort command (AB)" }, 
    { 8, "Decelerating or stopped by Off on Error (OE1)" }, 
    { 9, "Stopped after finding edge (FE)" }, 
    { 10, "Stopped after homing (HM) or Find Index (FI)" }, 
    { 11, "Stopped by selective abort input" }, 
    { 12, "Decelerating or stopped by encoder failure (OA1)" }, //  (For controllers supporting OA/OV/OT)
    { 15, "Amplifier Fault" }, // (For controllers with internal drives)
    { 16, "Stepper position maintenance error" }, 
    { 30, "Running in PVT mode" }, 
    { 31, "PVT mode completed normally" }, 
    { 32, "PVT mode exited because buffer is empty" }, 
    { 50, "Contour Running" }, 
    { 51, "Contour Stopped" }, 
    { 60, "ECAM Running" }, 
    { 61, "ECAM Stopped" }, 
    { 70, "Stopped due to EtherCAT communication failure" }, 
    { 71, "Stopped due to EtherCAT drive fault" }, 
    { 99, "MC timeout" },
    { 100, "Vector Sequence running" },
    { 101, "Vector Sequence stopped" }
};

static const char* lookupStopCode(int sc)
{
    for(int i = 0; i < sizeof(stopCodes) / sizeof(StopCode); ++i)
    {
        if (sc == stopCodes[i].sc)
        {
            return stopCodes[i].mess;
        }
    }
    return "UNKNOWN";
}
