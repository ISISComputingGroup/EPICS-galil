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
// mark.clift@synchrotron.org.au
// 800 Blackburn Road, Clayton, Victoria 3168, Australia.
//

#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <Galil.h>
#include <iostream>  //cout
#include <sstream>   //ostringstream istringstream
#include <typeinfo>  //std::bad_typeid

using namespace std; //cout ostringstream vector string

#include <epicsString.h>
#include <iocsh.h>
#include <epicsThread.h>
#include <errlog.h>

#include <asynOctetSyncIO.h>

#include "GalilController.h"
#include <epicsExport.h>

static void pollServicesThreadC(void *pPvt);

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
    pC_(pC), last_encoder_position_(0), pollRequest_(10, sizeof(int)), encDirOk_(true)
{
  char axis_limit_code[LIMIT_CODE_LEN];   	//Code generated for limits interrupt on this axis
  char axis_digital_code[INP_CODE_LEN];	     	//Code generated for digital interrupt related to this axis
  char axis_thread_code[THREAD_CODE_LEN]; 	//Code generated for the axis (eg. home code, limits response)

  epicsTimeGetCurrent(&stop_begint_);
  stop_nowt_ = stop_begint_;
 
  //Increment internal axis counter
  //Used to check start status of galil thread (on hardware) for this GalilAxis
  pC_->numAxes_++;

  //encoder ratio has not been set yet
  encmratioset_ = false;

  //store axis details
  //Store axis name in axis class instance
  axisName_ = (char)(toupper(axisname[0]));
  
  //store settings, and set defaults
  setDefaults(limit_as_home, enables_string, switch_type);
  //store the motor enable/disable digital IO setup
  store_motors_enable();
  //Generate the code for this axis based on specified settings
  //Initialize the code generator
  initialize_codegen(axis_thread_code, axis_limit_code, axis_digital_code);
  //Generate code for limits interrupt.  Motor behaviour on limits active
  gen_limitcode(axisName_, axis_thread_code, axis_limit_code);
  //Generate code for axis homing routine
  gen_homecode(axisName_, axis_thread_code);
  /* insert motor interlock code into thread A */
  if (axisName_ == 'A')
	{
	sprintf(axis_thread_code,"%sIF (mlock=1)\n",axis_thread_code);
	sprintf(axis_thread_code,"%sII ,,dpon,dvalues\nENDIF\n",axis_thread_code);
	}

  //Insert the final jump statement for the current thread code
  sprintf(axis_thread_code,"%sJP #THREAD%c\n",axis_thread_code, axisName_);
  //Copy this axis code into the controller class code buffers
  strcat(pC->thread_code_, axis_thread_code);
  strcat(pC->limit_code_, axis_limit_code);
  strcat(pC->digital_code_, axis_digital_code);
  
  // Create the thread that will service poll requests
  // To write to the controller
  epicsThreadCreate("pollServices", 
                    epicsThreadPriorityMax,
                    epicsThreadGetStackSize(epicsThreadStackMedium),
                    (EPICSTHREADFUNC)pollServicesThreadC, (void *)this);
  
  // We assume motor with encoder
  setIntegerParam(pC->motorStatusGainSupport_, 1);
  setIntegerParam(pC->motorStatusHasEncoder_, 1);
  //Wrong limit protection not actively stopping motor right now
  setIntegerParam(pC->GalilWrongLimitProtectionActive_, 0);
  //Default stall/following error status
  setIntegerParam(pC_->motorStatusSlip_, 0);
  setIntegerParam(pC_->GalilEStall_, 0);
  callParamCallbacks();
}

/*--------------------------------------------------------------------------------*/
/* Store settings, set defaults for motor */
/*--------------------------------------------------------------------------------*/

asynStatus GalilAxis::setDefaults(int limit_as_home, char *enables_string, int switch_type)
{
	//const char *functionName = "GalilAxis::setDefaults";
	
	//Store limits as home setting						       
	limit_as_home_ = (limit_as_home > 0) ? 1 : 0;

	//Store switch type setting for motor enable/disable function			       
	switch_type_ = (switch_type > 0) ? 1 : 0;

	//Store motor enable/disable string
	enables_string_ = (char *)calloc(strlen(enables_string), sizeof(char));
	strcpy(enables_string_, enables_string);
        
	//Invert ssi flag
	invert_ssi_ = 0;

	//Possible encoder stall not detected
	pestall_detected_ = false;

	//Set encoder stall flag to false
	setIntegerParam(pC_->GalilEStall_, 0);

	//This axis is not performing a deferred move
	deferredMove_ = false;

	//Store axis in ParamList
	setIntegerParam(pC_->GalilAxis_, axisNo_);

	//Give default readback values for positions, movement direction
	motor_position_ = 0;
	encoder_position_ = 0;
	direction_ = 1;

	//Motor not homing now
	homing_ = false;

	//Motor stop mesg not sent to pollServices thread for stall or wrong limit
	stopSent_ = false;

	//Motor record post mesg not sent to pollServicess thread after stop yet  
	postExecuted_ = postSent_ = false;

	premExecuted_ = false;

	//Homed mesg not sent to pollServices thread
	homedExecuted_ = homedSent_ = false;

	autooffExecuted_ = autooffSent_ = false;

	//AutoOn delay not in progress so autooff allowed
	autooffAllowed_ = true;

	//Axis not ready until autosave restore complete.  Motor on is delibrately last
	//So we use setClosedLoop to set this true
	axisReady_ = false;

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
	while (digport[k] != 0)
		{
		//Did user specify interlock function
		if (digport[k]>0 && digport[k]<9)
			{
			//Retrieve structure for digital port from controller instance
			motor_enables = (Galilmotor_enables *)&pC_->motor_enables_[digport[k]-1];
			motors_index = (int)strlen(motor_enables->motors);
			//Add motor, and digital IO state provided into motor_enables structure
			add_motor = 1;
			// Check to make sure motor is not already in list
			for (i=0;i<motors_index;i++)
				{
				if (motor_enables->motors[i] == axisName_)
					add_motor = 0;
				}
			if (add_motor)
				{
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
		}
}

/*--------------------------------------------------------------------------------*/
/* Initialize code buffers, insert program labels, set generator variables */

void GalilAxis::initialize_codegen(char axis_thread_code[],
			  	   char axis_limit_code[],
			  	   char axis_digital_code[])
{
	//Program label for digital input interrupt program
	char axis_digital_label[10]="#ININT\n";
	
	//Does codegen need initialization for this controller ?
	if (pC_->codegen_init_ == false)	
		{
		//setup #AUTO label
		strcpy(pC_->card_code_,"#AUTO\n");
		
		//setup #LIMSWI label	 
		strcpy(pC_->limit_code_,"#LIMSWI\n");

		//Code generator has now been initialized
		pC_->codegen_init_ = true;
		}
	
	/*Empty code buffers for this axis*/
	strcpy(axis_thread_code, "");
	strcpy(axis_limit_code, "");
		
	//Insert code to start motor thread that will be constucted
	//thread 0 (motor A) is auto starting
	if (axisName_ != 'A')
		sprintf(pC_->card_code_,"%sXQ #THREAD%c,%d;",pC_->card_code_, axisName_, axisNo_);

        //Insert code to send axis homed status at startup using unsolicited mesg
        sprintf(pC_->card_code_,"%sMG \"homed%c\",homed%c\n",pC_->card_code_, axisName_, axisName_);
	
	//Insert label for motor thread we are constructing	
	sprintf(axis_thread_code,"%s#THREAD%c\n",axis_thread_code, axisName_);

	//Setup ININT program label for digital input interrupts.  Used for motor enable/disable.
	if (pC_->digitalinput_init_ == false && strcmp(enables_string_, "") != 0)
		{
		//Insert digital input program label #ININT
		strcpy(axis_digital_code, axis_digital_label);
		// Insert code to initialize dpoff (digital ports off) used for motor interlocks management
		sprintf(axis_digital_code,"%sdpoff=dpon\n",axis_digital_code);
		//Digital input label has been inserted
		pC_->digitalinput_init_ = true;
		}
	else	//Empty digital input code buffer
		strcpy(axis_digital_code,"");
}

/*--------------------------------------------------------------------------------*/
/* Generate the required limit code for this axis */

void GalilAxis::gen_limitcode(char c,			 //GalilAxis::axisName_ used very often
			      char axis_thread_code[],
			      char axis_limit_code[])
{
	const char *functionName = "GalilAxis::gen_limitcode";
	
	//Setup the LIMSWI interrupt routine. The Galil Code Below, is called once per limit activate on ANY axis **
	//Determine axis that requires stop based on stop code and moving status
	//Use user desired deceleration, stop motor, then put deceleration back to that for normal moves
        if (!limit_as_home_)	//Hitting limit when homing to home switch is a fail, cancel home process
		sprintf(axis_limit_code,"%sIF (((_SC%c=2) | (_SC%c=3)) & (_BG%c=1))\noldecel%c=_DC%c;DC%c=limdc%c;WT2;ST%c;DC%c=oldecel%c;home%c=0;MG \"home%c\",home%c;ENDIF\n",axis_limit_code,c,c,c,c,c,c,c,c,c,c,c,c,c);	
	else			//Hitting limit when homing to limit switch is normal
		sprintf(axis_limit_code,"%sIF (((_SC%c=2) | (_SC%c=3)) & (_BG%c=1))\noldecel%c=_DC%c;DC%c=limdc%c;WT2;ST%c;DC%c=oldecel%c;ENDIF\n",axis_limit_code,c,c,c,c,c,c,c,c,c,c);	
	
	/*provide sensible default for limdc (limit deceleration) value*/
	sprintf(pC_->cmd_, "limdc%c=67107840\n", c);
	pC_->writeReadController(functionName);
}

/*--------------------------------------------------------------------------------*/
/* Generate home code.*/

void GalilAxis::gen_homecode(char c,			//GalilAxis::axisName_ used very often
			     char axis_thread_code[])
{
	const char *functionName = "gen_limitcode";

	sprintf(axis_thread_code,"%sIF ((home%c=1))\n",axis_thread_code,c);
	
	//Setup home code
	if (limit_as_home_)
		{
		/*hjog%c=1 we have found limit switch outer edge*/
		/*hjog%c=2 we have found limit switch inner edge*/
		/*hjog%c=3 we have found our final home pos*/
		//Code to jog off limit
		sprintf(axis_thread_code,"%sIF ((home%c=1) & (_MO%c=0) & (hjog%c=0) & (_BG%c=0) & ((_LR%c=0) | (_LF%c=0)))\nspeed%c=_SP%c;DC%c=hjgdc%c;JG%c=hjgsp%c;WT10;BG%c;hjog%c=1;ENDIF\n",axis_thread_code,c,c,c,c,c,c,c,c,c,c,c,c,c,c);
		//Stop motor once off limit
		sprintf(axis_thread_code,"%sIF ((_LR%c=1) & (_LF%c=1) & (hjog%c=1) & (_BG%c=1))\nST%c;ENDIF\n",axis_thread_code,c,c,c,c,c);
		//Find encoder index 
		sprintf(axis_thread_code,"%sIF ((_LR%c=1) & (_LF%c=1) & (hjog%c=1) & (_BG%c=0))\nIF ((home%c=1) & (_MO%c=0) & (ueip%c=1) & (ui%c=1))\nSP%c=speed%c;DC%c=67107840;FI%c;WT10;BG%c;hjog%c=2\nELSE\n",axis_thread_code,c,c,c,c,c,c,c,c,c,c,c,c,c,c);
		}	
	else
		{
		//Stop motor once home activated
		sprintf(axis_thread_code,"%sIF ((_HM%c=hswact%c) & (hjog%c=0) & (_BG%c=1))\nST%c;ENDIF\n",axis_thread_code,c,c,c,c,c);
		//Code to jog off home
		sprintf(axis_thread_code,"%sIF ((home%c=1) & (_MO%c=0) & (_HM%c=hswact%c) & (hjog%c=0) & (_BG%c=0))\nspeed%c=_SP%c;DC%c=hjgdc%c;JG%c=hjgsp%c;WT10;BG%c;hjog%c=1;ENDIF\n",axis_thread_code,c,c,c,c,c,c,c,c,c,c,c,c,c,c);
		//Stop motor once off home
		sprintf(axis_thread_code,"%sIF ((_HM%c=hswiact%c) & (hjog%c=1) & (_BG%c=1))\nST%c;ENDIF\n",axis_thread_code,c,c,c,c,c);
		//Find encoder index
		sprintf(axis_thread_code,"%sIF ((_HM%c=hswiact%c) & (hjog%c=1) & (_BG%c=0))\nIF ((home%c=1) & (_MO%c=0) & (ueip%c=1) & (ui%c=1))\nSP%c=speed%c;DC%c=67107840;FI%c;WT10;BG%c;hjog%c=2\nELSE\n",axis_thread_code,c,c,c,c,c,c,c,c,c,c,c,c,c,c);
		}

	//Common homing code regardless of homing to limit or home switch
	//If no encoder we are home already
	sprintf(axis_thread_code,"%shjog%c=3;ENDIF;ENDIF\n",axis_thread_code,c);
	//If encoder index complete we are home
	sprintf(axis_thread_code,"%sIF ((hjog%c=2) & (_BG%c=0))\nhjog%c=3;ENDIF\n",axis_thread_code,c,c,c);
	//Unset home flag
	if (limit_as_home_)
		sprintf(axis_thread_code,"%sIF ((_LR%c=1) & (_LF%c=1) & (hjog%c=3) & (_BG%c=0))\n",axis_thread_code,c,c,c,c);
	else
		sprintf(axis_thread_code,"%sIF ((_HM%c=hswiact%c) & (hjog%c=3) & (_BG%c=0))\n",axis_thread_code,c,c,c,c);
	//Common homing code regardless of homing to limit or home switch
	//Flag homing complete
	sprintf(axis_thread_code,"%sWT10;hjog%c=0;SP%c=speed%c;home%c=0\n", axis_thread_code,c,c,c,c);
	//Send unsolicited messages to epics informing home and homed status
	sprintf(axis_thread_code,"%shomed%c=1;MG \"homed%c\",homed%c;MG \"home%c\",home%c;ENDIF\nENDIF\n", axis_thread_code,c,c,c,c,c);
	
	//Initialize home related parameters on controller
	//initialise home variable for this axis, set to not homming just yet.  Set to homming only when doing a home
	sprintf(pC_->cmd_, "home%c=0\n", c);
	pC_->writeReadController(functionName);

	//Ensure homed variable is defined on controller
	sprintf(pC_->cmd_, "homed%c=?\n", c);
	if (pC_->writeReadController(functionName) != asynSuccess)
		{
		//Controller doesnt know homed variable.  Give it initial value only in this case
		sprintf(pC_->cmd_, "homed%c=0\n", c);
		pC_->writeReadController(functionName);
		}
	else
		{
		//Controller does know homed variable
		//Extract its current value
		int value = atoi(pC_->resp_);
		//Set homed status for this axis
		setIntegerParam(pC_->GalilHomed_, value);
		//Set motorRecord MSTA bit 15 motorStatusHomed_ too
		//Homed is not part of Galil data record, we support it using Galil code and unsolicited messages over tcp instead
		//We must use asynMotorAxis version of setIntegerParam to set MSTA bits for this MotorAxis
		setIntegerParam(pC_->motorStatusHomed_, value);
		callParamCallbacks();
		}
	
	//Initialize home switch active value*/
	sprintf(pC_->cmd_, "hswact%c=0", c);
	pC_->writeReadController(functionName);
	
	//Initialize home switch inactive value*/
	sprintf(pC_->cmd_, "hswiact%c=1", c);
	pC_->writeReadController(functionName);

	/*initialise home jogoff variable*/
	sprintf(pC_->cmd_, "hjog%c=0\n", c);
	pC_->writeReadController(functionName);
	
	//Add code that counts cpu cycles through thread 0
	if (axisName_ == 'A')
		{
		sprintf(axis_thread_code,"%scounter=counter+1\n",axis_thread_code);

		//initialise counter variable
		sprintf(pC_->cmd_, "counter=0\n");
		pC_->writeReadController(functionName);
		}
}

/*  Sets deceleration used when limit is activated for this axis
  * \param[in] velocity Units=steps/sec.*/
asynStatus GalilAxis::setLimitDecel(double velocity)
{
   static const char *functionName = "GalilAxis::setLimitDecel";
   double mres;			//MotorRecord mres
   double egu_after_limit;	//Egu after limit parameter
   double distance;	//Used for kinematic calcs
   long decceleration;		//limits decel final value
   double deccel;		//double version of above
   asynStatus status;		//Comms status

   //Retrieve required values from paramList
   pC_->getDoubleParam(axisNo_, pC_->motorResolution_, &mres);
   pC_->getDoubleParam(axisNo_, pC_->GalilAfterLimit_, &egu_after_limit);	
   //recalculate limit deceleration given velocity and allowed steps after home/limit activation
   distance = (egu_after_limit < mres) ? mres : egu_after_limit; 
   //suvat equation for acceleration
   deccel = (velocity * velocity)/((distance/mres) * 2.0);
   //Find closest hardware setting
   decceleration = (long)(lrint(deccel/1024.0) * 1024);
   sprintf(pC_->cmd_, "limdc%c=%ld\n", axisName_, decceleration);
   status = pC_->writeReadController(functionName);
   return status;
}

/** Move the motor to an absolute location or by a relative amount.
  * \param[in] position  The absolute position to move to (if relative=0) or the relative distance to move 
  * by (if relative=1). Units=steps.
  * \param[in] relative  Flag indicating relative move (1) or absolute move (0).
  * \param[in] minVelocity The initial velocity, often called the base velocity. Units=steps/sec.
  * \param[in] maxVelocity The maximum velocity, often called the slew velocity. Units=steps/sec.
  * \param[in] acceleration The acceleration value. Units=steps/sec/sec. */
asynStatus GalilAxis::move(double position, int relative, double minVelocity, double maxVelocity, double acceleration)
{
  static const char *functionName = "move";
  long accel;					//Acceleration/Deceleration when limit not active
  char mesg[MAX_MESSAGE_LEN];			//Error mesg
  bool pos_ok = false;				//Is the requested position ok
  double readback = motor_position_;		//For step motors controller uses motor_position_ for positioning
  char move_command[128]; // at least enough to take a stringout record, but extra in case of a waveform
  
    //Check velocity and wlp protection
  if (beginCheck(functionName, maxVelocity))
     return asynSuccess;  //Nothing to do

  //If motor is servo and ueip_ = 1 then controller uses encoder_position_ for positioning
  if (ueip_ && (motorType_ == 0 || motorType_ == 1))
     readback = encoder_position_;
  
  std::cerr << "MOVE: axis " << axisName_ << " to " << position << (relative != 0 ? " (relative) " : " (absolute)") << " readback " << readback << std::endl;

  //Ensure home flag is 0
  sprintf(pC_->cmd_, "home%c=0", axisName_);
  pC_->writeReadController(functionName);
  homing_ = false;
		
  //recalculate limit deceleration given velo/slew velocity
  setLimitDecel(maxVelocity);

  //Are moves to be deferred ?
  if (pC_->movesDeferred_ != 0)
	{
	//convert all moves to relative
	deferredPosition_ = (relative) ? position : position - readback;
	//Store required parameters for deferred move in GalilAxis
	pC_->getIntegerParam(0, pC_->GalilCoordSys_, &deferredCoordsys_);
	deferredVelocity_ = maxVelocity;
	deferredAcceleration_ = acceleration;
	deferredMove_ = true;
	}
  else
	{
	//Moves are not deferred
	//Motor must be enabled to allow move
	if (motor_enabled())
 		{
		//Set speed
		sprintf(pC_->cmd_, "SP%c=%.0lf", axisName_, maxVelocity);
		pC_->writeReadController(functionName);

		//Set acceleration and deceleration for when limit not active
		accel = (long)lrint(acceleration/1024.0) * 1024;
		sprintf(pC_->cmd_, "AC%c=%ld;DC%c=%ld", axisName_, accel, axisName_, accel);
		pC_->writeReadController(functionName);

		//Set absolute or relative move
		move_command[0] = '\0';
        if ( (pC_->getStringParam(axisNo_, pC_->GalilMoveCommand_, sizeof(move_command), move_command) == asynSuccess) && (move_command[0] != '\0' && move_command[0] != ' ') )
		{
		    move_command[sizeof(move_command)-1] = '\0';
			if (epicsSnprintf(pC_->cmd_, sizeof(pC_->cmd_), move_command, position) > 0)
			{
			    pos_ok = true;
			    pC_->writeReadController(functionName);
			}
		}
		else if (relative)
		  	{
			//Check position
			if (position != 0)
				{
				pos_ok = true;
				//Set the relative move
				sprintf(pC_->cmd_, "PR%c=%.0f", axisName_, position);
				pC_->writeReadController(functionName);
				}
			}
		else   
			{
			//Check position
			if (position != readback)
				{
				pos_ok = true;
				//Set the absolute move
				sprintf(pC_->cmd_, "PA%c=%.0f", axisName_, position);
				pC_->writeReadController(functionName);
				}
			}

		//Check position
		if (!pos_ok)
			{
			//Dont start if wlp is on, and its been activated
			sprintf(mesg, "%s failed, bad %s position %.0f requested for axis %c with readback %.0f", 
			        functionName, (relative != 0 ? "relative" : "absolute"), position, axisName_, readback);
			//Set controller error mesg monitor
			pC_->setCtrlError(mesg);
			return asynSuccess;  //Nothing to do 
			}

		//Execute motor auto on function
		executeAutoOn();

		//Execute motor record prem command
		executePrem();

		//Begin the move
		beginMotion(functionName);
		}
	}

  //Always return success. Dont need more error mesgs
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
  int home_direction;		//Muliplier to change direction of the jog off home switch
  double hjgsp;			//home jog speed
  long hjgdc;			//home jog decel
  double hvel;			//home velocity in counts per sec calculated from maxVelocity
  double mres;			//Motor record mres
  double egu_after_limit;
  double deccel;
  double distance;
  long accel;			//Acceleration/Deceleration when limit not active
  int ssiinput;			//SSI encoder register

  pC_->getIntegerParam(pC_->GalilSSIInput_, &ssiinput);

  //Homing not supported for absolute encoders, just move it where you want
  if (ssiinput)
     return asynSuccess;  //Nothing to do

  // check homing thread is available
  if ( !pC_->checkGalilThreads() )
  {
  errlogPrintf("Galil home threads are not running. Attempting to restart homing threads.\n");
  sprintf(pC_->cmd_, "HX0;HX1");
  pC_->writeReadController(functionName);
  sprintf(pC_->cmd_, "XQ 0,0");
  pC_->writeReadController(functionName);
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
  
  //Check velocity and wlp protection
  if (beginCheck(functionName, maxVelocity))
     return asynSuccess;  //Nothing to do

  //Home only if interlock ok
  if (motor_enabled())
  	{
	//Calculate direction of jog off home switch
	home_direction = (forwards == 0) ? 1 : -1;

	//Retrieve needed parameters
	pC_->getDoubleParam(axisNo_, pC_->motorResolution_, &mres);
	pC_->getDoubleParam(axisNo_, pC_->GalilAfterLimit_, &egu_after_limit);

	//recalculate home/limit switch deceleration for switch jog off given hvel
	distance = (egu_after_limit < mres) ? mres : egu_after_limit;
	//suvat equation for acceleration
	deccel = (maxVelocity * maxVelocity)/((distance/mres) * 2.0);
	//Find closest hardware setting
	hjgdc = (long)(lrint(deccel/1024.0) * 1024);
	sprintf(pC_->cmd_, "hjgdc%c=%ld\n", axisName_, hjgdc);
	pC_->writeReadController(functionName);

	//Calculate home jog off speed to use hvel
        hjgsp = maxVelocity * home_direction;

	//Home jog off speed
	sprintf(pC_->cmd_, "hjgsp%c=%.0lf\n", axisName_, hjgsp);
	pC_->writeReadController(functionName);

	//Set Homed status to false
	sprintf(pC_->cmd_, "homed%c=0\n", axisName_);
	pC_->writeReadController(functionName);

	//Set homed status for this axis
	setIntegerParam(pC_->GalilHomed_, 0);
	//Set motorRecord MSTA bit 15 motorStatusHomed_ too
	//Homed is not part of Galil data record, we support it using Galil code and unsolicited messages over tcp instead
	//We must use asynMotorAxis version of setIntegerParam to set MSTA bits for this MotorAxis
        setIntegerParam(pC_->motorStatusHomed_, 0);

	//calculate home velocity speed in motor steps per sec
	//we need to do this because we use jog command
	//SP command does not affect jog

	hvel = maxVelocity * home_direction * -1;
			
//	sprintf(pC_->cmd_, "JG%c=%.0lf", axisName_, hvel);
//	pC_->writeReadController(functionName);

	//recalculate limit deceleration given hvel (instead of velo) and allowed steps after home/limit activation
	setLimitDecel(hvel);	

	//Set acceleration and deceleration for when limit not active
	accel = (long)lrint(acceleration/1024.0) * 1024;
	sprintf(pC_->cmd_, "AC%c=%ld;DC%c=%ld", axisName_, accel, axisName_, accel);
	pC_->writeReadController(functionName);

	//Execute motor auto on function
	executeAutoOn();

	//Execute motor record prem command
	executePrem();

        //Begin the move
//	if (!beginMotion(functionName))
//		{
		homing_ = true;  //Start was successful
                cancelHomeSent_ = false;  //Homing has not been cancelled yet
		//tell controller which axis we are doing a home on
		//We do this last so home algorithm doesn't cancel home jog in incase motor
		//is sitting on opposite limit to which we are homing
		sprintf(pC_->cmd_, "home%c=1\n", axisName_);
		pC_->writeReadController(functionName);
		std::cerr << "Home started axis " << axisName_ << std::endl;
//		}
	}

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

//Check velocity and wlp protection
asynStatus GalilAxis::beginCheck(const char *functionName, double maxVelocity)
{
  int wlp, wlpactive;			//Wrong limit protection.  When motor hits wrong limit
  char mesg[MAX_MESSAGE_LEN];

  //Retrieve wrong limit protection setting
  pC_->getIntegerParam(axisNo_, pC_->GalilWrongLimitProtection_, &wlp);
  pC_->getIntegerParam(axisNo_, pC_->GalilWrongLimitProtectionActive_, &wlpactive);

  if (!axisReady_)
	{
	sprintf(mesg, "%s failed, autosave still restoring axis %c", functionName, axisName_);
	//Set controller error mesg monitor
	pC_->setCtrlError(mesg);
	return asynError;  //Nothing to do 
	}

  //Dont start if wlp is on, and its been activated
  if (wlp && wlpactive)
	{
	sprintf(mesg, "%s failed, wlp active for axis %c", functionName, axisName_);
	//Set controller error mesg monitor
	pC_->setCtrlError(mesg);
	return asynError;  //Nothing to do 
	}

  //Dont start if velocity 0
  if (lrint(maxVelocity) == 0)
	{
	sprintf(mesg, "%s failed, velocity 0 for axis %c", functionName, axisName_);
	//Set controller error mesg monitor
	pC_->setCtrlError(mesg);
	return asynError;  //Nothing to do 
	}
	
   if (!checkEncoderMotorSync(true))
   {
        pC_->setCtrlError("Encoder and motor registers out of sync - you may need to rehome");
	    // return asynError;  // should we stop move attempts? 
   }

  //Everything ok so far
  return asynSuccess;
}


/** Move the motor at a fixed velocity until told to stop.
  * \param[in] minVelocity The initial velocity, often called the base velocity. Units=steps/sec.
  * \param[in] maxVelocity The maximum velocity, often called the slew velocity. Units=steps/sec.
  * \param[in] acceleration The acceleration value. Units=steps/sec/sec. */
asynStatus GalilAxis::moveVelocity(double minVelocity, double maxVelocity, double acceleration)
{
  static const char *functionName = "moveVelocity";
  long accel;

  //Check velocity and wlp protection
  if (beginCheck(functionName, maxVelocity))
     return asynSuccess;  //Nothing to do

  //Check interlock status before allowing move
  if (motor_enabled())
  	{
	//Ensure home flag is 0
	sprintf(pC_->cmd_, "home%c=0", axisName_);
	pC_->writeReadController(functionName);
	homing_ = false;

	//Set acceleration and deceleration
	accel = (long)lrint(acceleration/1024.0) * 1024;
	sprintf(pC_->cmd_, "AC%c=%ld;DC%c=%ld", axisName_, accel, axisName_, accel);
	pC_->writeReadController(functionName);

	//recalculate limit deceleration given velo/slew velocity
	setLimitDecel(maxVelocity);
		  
	//Give jog speed and direction
	sprintf(pC_->cmd_, "JG%c=%.0lf\n", axisName_, maxVelocity);
	pC_->writeReadController(functionName);

	//Execute motor auto on function
	executeAutoOn();

	//Execute motor record prem command
	executePrem();
					
	//Begin the move
	beginMotion(functionName);
	}
   
  //Always return success. Dont need more error mesgs
  return asynSuccess;
}


/** Stop the motor.
  * \param[in] acceleration The acceleration value. Units=steps/sec/sec. */
asynStatus GalilAxis::stop(double acceleration)
{
  static const char *functionName = "GalilAxis::stop";

  //cancel any home operations that may be underway
  sprintf(pC_->cmd_, "home%c=0\n", axisName_);
  pC_->writeReadController(functionName);
  homing_ = false;

  //cancel any home switch jog off operations that may be underway
  // GH 05/10/2015 home%c=0 should be sufficient. Galil controls how state completes.
  // sprintf(pC_->cmd_, "hjog%c=0\n", axisName_);
  // pC_->writeReadController(functionName);

  //Stop the axis
  sprintf(pC_->cmd_, "ST%c\n", axisName_);
  pC_->writeReadController(functionName);

  /* Clear defer move flag for this axis. */
  deferredMove_ = false;

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

/** Set the current position of the motor.
  * \param[in] position The new absolute motor position that should be set in the hardware. Units=steps.*/
asynStatus GalilAxis::setPosition(double position)
{
  const char *functionName = "GalilAxis::setPosition";
  double mres, eres;				//MotorRecord mres, and eres
  double enc_pos;				//Calculated encoder position
  int motor;

  //Retrieve motor setting direct from controller rather than ParamList as IocInit may be in progress
  sprintf(pC_->cmd_, "MT%c=?", axisName_);
  pC_->writeReadController(functionName);
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

  //output motor position (step count) to aux encoder register on controller
  //DP and DE command function is different depending on motor type
  if (abs(motor) == 1)
	sprintf(pC_->cmd_, "DE%c=%.0f", axisName_, position);  //Servo motor, use aux register for step count
  else
	sprintf(pC_->cmd_, "DP%c=%.0f", axisName_, position);  //Stepper motor, aux register for step count
  pC_->writeReadController(functionName);
  
  //Set encoder position
  setEncoderPosition(enc_pos);

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

// return true if in sync
// only needed for stepper motors
bool GalilAxis::checkEncoderMotorSync(bool correct_motor)
{
    const char *functionName = "GalilAxis::checkEncoderMotorSync";
    double posdiff_tol = 0.0;
	asynStatus status = pC_->getDoubleParam(axisNo_, pC_->GalilMotorEncoderSyncTol_, &posdiff_tol);
    sprintf(pC_->cmd_, "MT%c=?", axisName_);
    pC_->writeReadController(functionName);
    int motor = atoi(pC_->resp_); // servo is -1.5, -1, 1, or 1.5 so abs(int(motor)) is 1 
	if ( status != asynSuccess || abs(motor) == 1 || !ueip_ || posdiff_tol <= 0.0 )
	{
		return true;
	}
    double mres = 0.0, eres = 0.0;				// MotorRecord mres, and eres
	pC_->getDoubleParam(axisNo_, pC_->GalilEncoderResolution_, &eres);
	pC_->getDoubleParam(axisNo_, pC_->motorResolution_, &mres);
	double posdiff_egu = motor_position_ * mres - encoder_position_ * eres;
	if (fabs(posdiff_egu) < posdiff_tol)
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
	std::cerr << "Raw motor position corrected from " << motor_position_ << " to " << new_motor_pos << " using encoder" << std::endl;
	if (abs(motor) == 1)
	{
		sprintf(pC_->cmd_, "DE%c=%.0f", axisName_, new_motor_pos);  //Servo motor, use aux register for step count
	}
	else
	{
		sprintf(pC_->cmd_, "DP%c=%.0f", axisName_, new_motor_pos);  //Stepper motor, main register for step count
	}
	pC_->writeReadController(functionName);
	return true;		
}

/** Set the current position of the encoder.
  * \param[in] position The new absolute encoder position that should be set in the hardware. Units=steps.*/
asynStatus GalilAxis::setEncoderPosition(double position)
{
  const char *functionName = "GalilAxis::setEncoderPosition";
  asynStatus status;
  int motor;

  //Retrieve motor setting direct from controller rather than ParamList as IocInit may be in progress
  sprintf(pC_->cmd_, "MT%c=?", axisName_);
  pC_->writeReadController(functionName);
  motor = atoi(pC_->resp_);

  //output encoder counts to main encoder register on controller
  //DP and DE command function is different depending on motor type
  if (abs(motor) == 1)
  	sprintf(pC_->cmd_, "DP%c=%.0f", axisName_, position);   //Servo motor, encoder is main register
  else
	sprintf(pC_->cmd_, "DE%c=%.0f", axisName_, position);   //Stepper motor, encoder is main register

  status = pC_->writeReadController(functionName);

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

/** Set the motor encoder ratio. 
  * \param[in] ratio The new encoder ratio */
asynStatus GalilAxis::setEncoderRatio(double ratio)
{
  //const char *functionName = "GalilAxis::setEncoderRatio";

  //setEncoder is called during IocInit/Autosave for position restore, and when user changes 
  //eres, mres, ueip or position is changed
  //Store the ratio in GalilAxis instance
  encmratio_ = ratio;
  encmratioset_ = true;

  return asynSuccess;
}

/** Set the high limit position of the motor.
  * \param[in] highLimit The new high limit position that should be set in the hardware. Units=steps.*/
asynStatus GalilAxis::setHighLimit(double highLimit)
{
  const char *functionName = "GalilAxis::setHighLimit";
  //this gets called at init for every mR
  //Assemble Galil Set High Limit, forward limit in Galil language
  highLimit_ = (highLimit > 2147483647.0) ? 2147483647.0 : highLimit; 
  sprintf(pC_->cmd_, "FL%c=%lf", axisName_, highLimit_);
  pC_->writeReadController(functionName);

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

/** Set the low limit position of the motor.
  * \param[in] lowLimit The new low limit position that should be set in the hardware. Units=steps.*/
asynStatus GalilAxis::setLowLimit(double lowLimit)
{
  const char *functionName = "GalilAxis::setLowLimit";
  //this gets called at init for every mR
  //Assemble Galil Set High Limit, forward limit in Galil language
  lowLimit_ = (lowLimit < -2147483648.0) ? -2147483648.0 : lowLimit;
  sprintf(pC_->cmd_, "BL%c=%lf", axisName_, lowLimit_);
  pC_->writeReadController(functionName);

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}


/** Set the proportional gain of the motor.
  * \param[in] pGain The new proportional gain. */
asynStatus GalilAxis::setPGain(double pGain)
{
  const char *functionName = "GalilAxis::setPGain";
  //Parse pGain value
  pGain = fabs(pGain);
  pGain = pGain * KPMAX;
  pGain = (pGain > KPMAX) ? KPMAX : pGain;
  //Assemble KP command
  sprintf(pC_->cmd_, "KP%c=%lf",axisName_, pGain);
  pC_->writeReadController(functionName);
  
  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

/** Set the integral gain of the motor.
  * \param[in] iGain The new integral gain. */
asynStatus GalilAxis::setIGain(double iGain)
{
  const char *functionName = "GalilAxis::setIGain";
  double kimax;					//Maximum integral gain depends on controller model 
  //Model 21X3 has different kimax from 41x3, and 40x0
  kimax = (pC_->model_[3] == '2') ? 2047.992 : 255.999;
  //Parse iGain value
  iGain = fabs(iGain);
  iGain = iGain * kimax;
  iGain = (iGain > kimax) ? kimax : iGain;
  //Assemble KI command
  sprintf(pC_->cmd_, "KI%c=%lf",axisName_, iGain);
  pC_->writeReadController(functionName);

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

/** Set the derivative gain of the motor.
  * \param[in] dGain The new derivative gain. */
asynStatus GalilAxis::setDGain(double dGain)
{
  const char *functionName = "GalilAxis::setDGain";
  //Parse dGain value
  dGain = fabs(dGain);
  dGain = dGain * KDMAX;
  dGain = (dGain > KDMAX) ? KDMAX : dGain;
  //Assemble KD command
  sprintf(pC_->cmd_, "KD%c=%lf",axisName_, dGain);
  pC_->writeReadController(functionName);

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

/** Set the motor closed loop status. 
  * \param[in] closedLoop true = close loop, false = open loop. */
asynStatus GalilAxis::setClosedLoop(bool closedLoop)
{
  const char *functionName = "GalilAxis::setClosedLoop";
  asynStatus status;

  //Enable or disable motor amplifier
  if (closedLoop)
	sprintf(pC_->cmd_, "SH%c", axisName_);
  else
	sprintf(pC_->cmd_, "MO%c", axisName_);
  //Write setting to controller
  status = pC_->writeReadController(functionName);

  return status;
}

//Extract axis data from GalilController data record and
//store in GalilAxis (motorRecord attributes) or asyn ParamList (other record attributes)
//Return status of GalilController data record acquisition
asynStatus GalilAxis::getStatus(void)
{
   const char *functionName="GalilAxis::getStatus";
   char src[MAX_GALIL_STRING_SIZE]="\0";    //data source to retrieve
   int offonerror, motoron;                 //paramList items to update
   int connected;                           //paramList items to update
   double error;                            //paramList items to update  

   //If data record query success in GalilController::acquireDataRecord
   if (pC_->recstatus_ == asynSuccess)
	{
	try	{
		//extract relevant axis data from GalilController data-record, store in GalilAxis
		//If connected, then proceed
		if (pC_->gco_ != NULL)
			{
			//aux encoder data
			sprintf(src, "_TD%c", axisName_);
			motor_position_ = pC_->gco_->sourceValue(pC_->recdata_, src);
			//main encoder data
			sprintf(src, "_TP%c", axisName_);
			encoder_position_ = pC_->gco_->sourceValue(pC_->recdata_, src);

			//moving status
			sprintf(src, "_BG%c", axisName_);
			inmotion_ = (bool)(pC_->gco_->sourceValue(pC_->recdata_, src) == 1) ? 1 : 0;

			//reverse limit
			sprintf(src, "_LR%c", axisName_);
			rev_ = (bool)(pC_->gco_->sourceValue(pC_->recdata_, src) == 1) ? 0 : 1;
			//forward limit
			sprintf(src, "_LF%c", axisName_);
			fwd_ = (bool)(pC_->gco_->sourceValue(pC_->recdata_, src) == 1) ? 0 : 1;
			//home switch
			sprintf(src, "_HM%c", axisName_);
			home_ = (bool)(pC_->gco_->sourceValue(pC_->recdata_, src) == 1) ? 0 : 1;
			//direction
			sprintf(src, "JG%c-", axisName_);
			direction_ = (bool)(pC_->gco_->sourceValue(pC_->recdata_, src) == 1) ? 0 : 1;

			//extract relevant axis data from GalilController record, store in asynParamList
			//motor connected status
			connected = (rev_ && fwd_) ? 0 : 1;
			setIntegerParam(pC_->GalilMotorConnected_, connected);
			//Off on error
			sprintf(src, "_OE%c", axisName_);
			offonerror = (int)pC_->gco_->sourceValue(pC_->recdata_, src);
			setIntegerParam(pC_->GalilOffOnError_, offonerror);
			//Motor on
			sprintf(src, "_MO%c", axisName_);
			motoron = (pC_->gco_->sourceValue(pC_->recdata_, src) == 1) ? 0 : 1;
			//Set motorRecord status
			setIntegerParam(pC_->motorStatusPowerOn_, motoron);
			//Set galil motor on status
			setIntegerParam(pC_->GalilMotorOn_, motoron);
			//Motor error
			sprintf(src, "_TE%c", axisName_);
			error = pC_->gco_->sourceValue(pC_->recdata_, src);
			setDoubleParam(pC_->GalilError_, error);
			}
		}
	catch (string e) 
		{
		//Print exception mesg
		errlogSevPrintf(errlogMajor, "%s: %s\n", functionName, e.c_str());
		}
	//Forgiveness is cheap
	//Allows us to poll without lock
	catch (const std::bad_typeid& e)
		{
		cout << "Caught bad_typeid GalilAxis::getStatus" << e.what() << endl;
		}
	}
  return pC_->recstatus_;
}

//Set poller internal status variables based on data record info
//Called by poll
void GalilAxis::setStatus(bool *moving)
{
  double eres, edel;		//motorRecord eres, and GalilEncoderDeadB_ (edel) Param
  int encoder_direction;	//Determined encoder move direction

  //Encoder move status
  encoderMove_ = false;
  if (ueip_)
     {
     //Retrieve needed parameters
     pC_->getDoubleParam(axisNo_, pC_->GalilEncoderResolution_, &eres);
     pC_->getDoubleParam(axisNo_, pC_->GalilEncoderDeadB_, &edel);
     //Check encoder move
     if (last_encoder_position_ > (encoder_position_ + (edel/eres)))
        {
        encoder_direction = 0;
        encoderMove_ = true;
        }
     if (last_encoder_position_ < (encoder_position_ - (edel/eres)))
        {
        encoder_direction = 1;
        encoderMove_ = true;
        }
     //Encoder direction ok flag - only update if encoder is moving, else encoder_direction is uninitialised
	 if (encoderMove_)
	     {
         encDirOk_ = (encoder_direction == direction_) ? true : false;
	     }
     }

   //Determine move status
   //Motors with deferred moves pending set to status moving
   if (inmotion_ || encoderMove_ || deferredMove_)
      {
      *moving = true;		//set flag for moving
      done_ = 0;
      //Motor record post not sent as motor is moving
      postExecuted_ = postSent_ = false;
      //Motor auto off not yet sent as motor is moving
      autooffExecuted_ = autooffSent_ = false;
      }
}

//Called by poll without lock
//When encoder problem detected
//May stop motor via pollServices thread
void GalilAxis::checkEncoder(void)
{
   char message[MAX_MESSAGE_LEN];	//Safety stop message
   double estall_time;			//Allowed encoder stall time specified by user
   double pestall_time;			//Possible encoder stall has been happening for this many secs
   const char *functionName = "GalilAxis::checkEncoder";

   if ((ueip_ && !done_ && (!encoderMove_ || !encDirOk_)))
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
            setIntegerParam(pC_->GalilEStall_, 1);
            //stop the motor
            pollRequest_.send((void*)&MOTOR_STOP, sizeof(int));
            //Flag the motor has been stopped
            stopSent_ = true;
            //Inform user
            sprintf(message, "Encoder stall stop motor %c", axisName_);
            //Set controller error mesg monitor
			pC_->setCtrlError(message);
			std::cerr << "STALL: pestall_time=" << pestall_time << " (>" << estall_time << ") encoderMove_=" << encoderMove_ << " encDirOk_=" << encDirOk_ << " _SC" << axisName_ << "=" << sc_code << " _BG" << axisName_ << "=" << bg_code << std::endl;
            }
         }
      }
  else if ((ueip_ && !done_ && encoderMove_ && encDirOk_ && !stopSent_) || (!done_ && !ueip_ && !stopSent_))
      {
      //Reset stalled encoder flag when moving ok
      //Pass stall status to higher layers
      setIntegerParam(pC_->motorStatusSlip_, 0);
      setIntegerParam(pC_->GalilEStall_, 0);
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
//Detects activation of incorrect limit and
//May stop motor via pollServices thread
void GalilAxis::wrongLimitProtection(void)
{
   char message[MAX_MESSAGE_LEN];	//Safety stop message
   int wlp;				//Wrong limit protection.  When motor hits wrong limit

   //Retrieve wrong limit protection setting
   pC_->getIntegerParam(axisNo_, pC_->GalilWrongLimitProtection_, &wlp);
   if (wlp)
      {
      if ((!done_ && direction_ && rev_) || (!done_ && !direction_ && fwd_))
         {
         if (!stopSent_)
            {
            //Wrong limit protection actively stopping this motor now
            setIntegerParam(pC_->GalilWrongLimitProtectionActive_, 1);
            //Stop the motor if the wrong limit is active, AND wlp protection active
            pollRequest_.send((void*)&MOTOR_STOP, sizeof(int));
            //Flag the motor has been stopped
            stopSent_ = true;
            //Inform user
            sprintf(message, "Wrong limit protect stop motor %c", axisName_);
            //Set controller error mesg monitor
	        pC_->setCtrlError(message);
            }
         }
      else if (!done_)
         {
         //Wrong limit protection is NOT actively stopping this motor now
  	 setIntegerParam(pC_->GalilWrongLimitProtectionActive_, 0);
         }
      }
   else
      setIntegerParam(pC_->GalilWrongLimitProtectionActive_, 0); //NOT actively stopping this motor now
}

//Called by poll
//Returns time motor has been stopped for
void GalilAxis::setStopTime(void)
{   
   //Default stopped time
   stopped_time_ = 0.0;

   if (done_ && !last_done_)
      {
      //Get time stop first detected
      epicsTimeGetCurrent(&stop_begint_);
      //Not moving, so reset stopSent_ flag
      stopSent_ = false;
      }
   if (done_ && last_done_)
      {
      //Get time stopped for
      epicsTimeGetCurrent(&stop_nowt_);
      //Result
      stopped_time_ = epicsTimeDiffInSeconds(&stop_nowt_, &stop_begint_);
      }
}

//Called by poll thread
//Check for soft limit violation
//Check for homing that has just stopped for some reason
//Cancel home if either true
void GalilAxis::checkHoming(void)
{
   const char *functionName = "GalilAxis::checkHoming";
   char message[MAX_GALIL_STRING_SIZE];
   double readback = motor_position_;	//For step motors controller uses motor_position_ for positioning
   double estall_time;
   pC_->getDoubleParam(axisNo_, pC_->GalilEStallTime_, &estall_time);
   double homing_timeout = (HOMING_TIMEOUT < estall_time ? estall_time : HOMING_TIMEOUT);

   //If motor is servo and ueip_ = 1 then controller uses encoder_position_ for positioning
   if (ueip_ && (motorType_ == 0 || motorType_ == 1))
      readback = encoder_position_;

//   GH - Changed IF statement to remove highLimit and lowLimit checks for the home.
//   if ((homing_ && (stopped_time_ >= homing_timeout) && !cancelHomeSent_) ||
//       ((readback > highLimit_ || readback < lowLimit_) && homing_ && !cancelHomeSent_ && done_))
   if (homing_ && (stopped_time_ >= homing_timeout) && !cancelHomeSent_)
      {
	    // get last stop code
	    sprintf(pC_->cmd_, "MG _SC%c\n", axisName_);
        pC_->writeReadController(functionName);
        double sc_code = atof(pC_->resp_);

	    // get axis moving state
	    sprintf(pC_->cmd_, "MG _BG%c\n", axisName_);
        pC_->writeReadController(functionName);
        double bg_code = atof(pC_->resp_);

	  sprintf(message, "Homing timed out after %f: BG%c=%f SC%c=%f ", homing_timeout, axisName_, bg_code, axisName_, sc_code);
	  pC_->setCtrlError(message);
	  
      //Cancel home
      pollRequest_.send((void*)&MOTOR_CANCEL_HOME, sizeof(int));
      //Flag home has been cancelled
      cancelHomeSent_ = true;
      //Inform user
      if (stopped_time_ >= homing_timeout)
         sprintf(message, "%c Homing timed out", axisName_);
      else
         sprintf(message, "%c Homing violated soft limits", axisName_);
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
  static const char *functionName = "GalilAxis::pollServices";
  char post[MAX_GALIL_STRING_SIZE];	//Motor record post field
  int request = -1; 			//Real service numbers start at 0
  int jah;				//Jog after home feature status
  double jahv;				//Jog after home value in egu
  double homeval;			//User programmed home value in egu
  double enhmval, mrhmval;		//Encoder, and motor register home value in steps
  int dir, dirm = 1;			//Motor record dir, and dirm direction multiplier based on motor record DIR field
  double accl;				//Motor record accl
  double velo;				//Motor record velo
  double mres, eres;			//Motor record mres, eres
  double off;				//Motor record off
  int ueip;				//Motor record ueip
  double acceleration;			//Acceleration Units=Steps/Sec/Sec
  double velocity;			//Velocity Units=Steps/Sec
  double position;			//Absolute position Units=steps
  double readback;			//Controller positioning readback
  int status = asynSuccess;		//Asyn param status

  while (true)
     {
     //Wait for poll to request a service
     pollRequest_.receive(&request, sizeof(int));
     //Obtain the lock
     pC_->lock();
     //What did poll request
     switch (request)
        {
        //Poll will make upper layers wait for POST, HOMED and OFF completion by setting moving true
        //Poll will not make upper layers wait for STOP, or CANCEL_HOME completion
        case MOTOR_CANCEL_HOME: sprintf(pC_->cmd_, "home%c=0\n", axisName_);
								pC_->writeReadController(functionName);
                                epicsThreadSleep(.2);  //Wait as controller may still issue move upto this time after
                                                       //Setting home to 0 (cancel home)
						 errlogSevPrintf(errlogInfo, "Poll services: MOTOR CANCEL HOME %c\n", axisName_);
                                //break; Delibrate fall through to MOTOR_STOP
        case MOTOR_STOP: stop(1);
						 std::cerr << "Poll services: STOP " << axisName_ << std::endl;
                         break;
        case MOTOR_POST: if (pC_->getStringParam(axisNo_, pC_->GalilPost_, (int)sizeof(post), post) == asynSuccess)
                            {
                            //Copy post field into cmd 
                            strcpy(pC_->cmd_, post);
                            //Write command to controller
                            pC_->writeReadController(functionName);
							std::cerr << "Poll services: POST " << axisName_ << " " << post << std::endl;
                            postExecuted_ = true;
                            }
                         break;
        case MOTOR_OFF:  //Block auto off if again inmotion_ or auto on delay active
                         if (!inmotion_ && autooffAllowed_)
                            {
                            //Execute the motor off command
							std::cerr << "Poll services: MOTOR OFF " << axisName_ << std::endl;
                            setClosedLoop(false);
                            autooffExecuted_ = true;
                            }
                         break;
        case MOTOR_HOMED://Retrieve needed params
						 errlogSevPrintf(errlogInfo, "Poll services: MOTOR HOMED %c\n", axisName_);
                         status = pC_->getDoubleParam(axisNo_, pC_->GalilJogAfterHomeValue_, &jahv);
                         status |= pC_->getDoubleParam(axisNo_, pC_->GalilMotorAccl_, &accl);
                         status |= pC_->getDoubleParam(axisNo_, pC_->GalilMotorVelo_, &velo);
                         status |= pC_->getDoubleParam(axisNo_, pC_->motorResolution_, &mres);
                         status |= pC_->getIntegerParam(axisNo_, pC_->GalilDirection_, &dir);
                         status |= pC_->getDoubleParam(axisNo_, pC_->GalilEncoderResolution_, &eres);
                         status |= pC_->getDoubleParam(axisNo_, pC_->GalilUserOffset_, &off);
                         status |= pC_->getDoubleParam(axisNo_, pC_->GalilHomeValue_, &homeval);
                         status |= pC_->getIntegerParam(axisNo_, pC_->GalilJogAfterHome_, &jah);
                         status |= pC_->getIntegerParam(axisNo_, pC_->GalilUseEncoder_, &ueip);

                         //Program home registers
                         if (!status)
                            {
                            //Calculate polarity of encoder, step register home value
                            dirm = (dir == 0) ? 1 : -1;
                            //Calculate the encoder home value and mtr home value each in steps
                            if (homeval != 0.0000)
                               {
                               enhmval = (double)((homeval - off)/eres) * dirm;
                               mrhmval = (double)((homeval - off)/mres) * dirm;
                               }
                            else
                               {
                               enhmval = 0.0;
                               mrhmval = 0.0;
                               }
                            //Program motor position register
						    errlogSevPrintf(errlogInfo, "Poll services: applying motor %c raw home position %.0f\n", axisName_, mrhmval);
                            sprintf(pC_->cmd_, "DP%c=%.0f\n", axisName_, mrhmval);
                            pC_->writeReadController(functionName);
                            //Program encoder position register
                            if (ueip)
                               {
						       errlogSevPrintf(errlogInfo, "Poll services: applying encoder %c raw home position %.0f\n", axisName_, enhmval);
                               sprintf(pC_->cmd_, "DE%c=%.0f\n", axisName_, enhmval);
                               pC_->writeReadController(functionName);
                               }
                            //Give ample time for position register updates to complete
                            epicsThreadSleep(.2);
                            }

                         //Do jog after home move
                         if (!status && jah)
                            {
                            //Calculate position, velocity (velo not hvel) and acceleration
                            velocity = velo/mres;
                            acceleration = velocity/accl;
                            position = (double)((jahv - off)/mres) * dirm;
                            readback = motor_position_;//For step motors controller uses motor_position_ for positioning
                            //If motor is servo and ueip_ = 1 then controller uses encoder_position_ for positioning
                            if (ueip_ && (motorType_ == 0 || motorType_ == 1))
                               readback = encoder_position_;
                            //Do the move
                            if (position != readback)
                            	move(position, 0, 0, velocity, acceleration);
                            }

                         //Homed pollService completed
                         homedExecuted_ = true;
                         homedSent_ = false;
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
  static const char *functionName = "GalilAxis::executePrem";
  char prem[MAX_GALIL_STRING_SIZE];		//Motor record prem field
  premExecuted_ = true; // need to set this even if no PREM as it is used by POST

  if (pC_->getStringParam(axisNo_, pC_->GalilPrem_, (int)sizeof(prem), prem) == asynSuccess)
     {
     if (strcmp(prem, ""))
        {
        //Copy prem to cmd
        strcpy(pC_->cmd_, prem);
        //Execute the prem string
		std::cerr << "Premove " << prem << std::endl;
        pC_->writeReadController(functionName);
        }
     }
}

//Execute motor auto power on
//Caller requires lock
void GalilAxis::executeAutoOn(void)
{
  static const char *functionName = "GalilAxis::executeAutoOn";
  int autoonoff;	//Motor power auto on/off setting
  double ondelay;	//Motor power on delay
  int motoroff;		//Motor amplifier off status

  //Execute Auto power on if activated
  pC_->getIntegerParam(axisNo_, pC_->GalilAutoOnOff_, &autoonoff);
  //Query motor off status direct from controller
  sprintf(pC_->cmd_, "MG _MO%c\n", axisName_);
  pC_->writeReadController(functionName);
  motoroff = atoi(pC_->resp_);
  epicsTimeGetCurrent(&stop_nowt_);
  //Execute motor auto on if feature is enabled and motor is off
  if (autoonoff && motoroff)
     {
     //motor on command
     setClosedLoop(true);
     //Wait user specified time after turning motor on
     pC_->getDoubleParam(axisNo_, pC_->GalilAutoOnDelay_, &ondelay);
     if (ondelay >= 0.035)
        {
        //AutoOn delay long enough to justify releasing lock to other threads
        //Case where autoon delay greater than autooff delay.  Dont wont motor turning off
        //Whilst still waiting AutoOn delay
        //Block autooff whilst lock released for AutoOn delay
        autooffAllowed_ = false;
        pC_->unlock();
        epicsThreadSleep(ondelay);
        pC_->lock();
        //Reset stop timer for auto off
        stop_begint_ = stop_nowt_;
        //Autooff now allowed as we have lock now anyway
        autooffAllowed_ = true;
        }
     else //AutoOn delay too short to bother releasing lock to other threads
        epicsThreadSleep(ondelay);
     }
  //Reset stop timer for auto off
  stop_begint_ = stop_nowt_;
  // allow a motor off to be sent at a later time
  autooffSent_ = false;
}

//Send motor record post mesg to pollServices thread
//Called by poll thread without lock
void GalilAxis::executePost(void)
{
  char post[MAX_GALIL_STRING_SIZE];	//Motor record post field

  if (!homing_ && !homedSent_ && done_ && premExecuted_ && !postSent_)
  {
      premExecuted_ = false;
      //Process motor record post field
      if ( (pC_->getStringParam(axisNo_, pC_->GalilPost_, (int)sizeof(post), post) == asynSuccess) && strcmp(post, "") )
      {
        //Send the post command
        pollRequest_.send((void*)&MOTOR_POST, sizeof(int));
        postSent_ = true;
        }
     }
}

//Send motor auto power off mesg to pollServices thread
//Called by poll thread without lock
void GalilAxis::executeAutoOff(void)
{
  int autoonoff;	//Motor auto power on/off setting
  double offdelay;	//Motor auto off delay in seconds

  //Execute motor auto power off if activated
  if ((pC_->getIntegerParam(axisNo_, pC_->GalilAutoOnOff_, &autoonoff) == asynSuccess) &&
      (pC_->getDoubleParam(axisNo_, pC_->GalilAutoOffDelay_, &offdelay) == asynSuccess))
     if (autoonoff && autooffAllowed_ && !homing_ && !homedSent_ && !autooffSent_ && stopped_time_ >= offdelay)
        {
        //Send the motor off command
        pollRequest_.send((void*)&MOTOR_OFF, sizeof(int));
        autooffSent_ = true;
        }
}

//Starts motion, and delay until it begins or timeout happens
//Called by move, moveVelocity, home
asynStatus GalilAxis::beginMotion(const char *caller)
{
   double begin_time;	//Time taken for motion to begin
   static const char *functionName = "GalilAxis::beginMotion";
   char mesg[MAX_MESSAGE_LEN];	//Controller error mesg if begin fail
   double estall_time;
   pC_->getDoubleParam(axisNo_, pC_->GalilEStallTime_, &estall_time);
   double begin_timeout = (BEGIN_TIMEOUT < estall_time ? estall_time : BEGIN_TIMEOUT);

   //Begin the move
   //Get time when attempt motor begin
   epicsTimeGetCurrent(&begin_begint_);
   sprintf(pC_->cmd_, "BG%c", axisName_);
   if (pC_->writeReadController(functionName) == asynSuccess)
      {
      while (!inmotion_) //Allow time for motion to begin
         {
         epicsThreadSleep(.001);
         epicsTimeGetCurrent(&begin_nowt_);
         //Calculate time begin has taken so far
         begin_time = epicsTimeDiffInSeconds(&begin_nowt_, &begin_begint_);
         if (begin_time > begin_timeout)
            {
			// get last stop code
            double sc_code = getGalilAxisVal("_SC");
	        // get axis moving state
            double bg_code = getGalilAxisVal("_BG");
            double bl = getGalilAxisVal("_BL"); // low limit
            double fl = getGalilAxisVal("_FL"); // high limit
            double tp = getGalilAxisVal("_TP"); // current position (from encoder if present)
            double td = getGalilAxisVal("_TD"); // current position (motor steps)
            double rp = getGalilAxisVal("_RP"); // commanded position (motor steps)
            sprintf(mesg, "%s begin failure axis %c after %f seconds: _BG%c=%f _SC%c=%f [%s] _BL%c=%f _FL%c=%f _TP%c=%f _TD%c=%f _RP%c=%f", caller, axisName_, begin_time, axisName_, bg_code, axisName_, sc_code, lookupStopCode((int)sc_code), axisName_, bl, axisName_, fl, axisName_, tp, axisName_, td, axisName_, rp);
			// getting these a lot, it it moving to somewhere very near current position?
			// comment out sending to errlog for now and send to cerr instead
            //Set controller error mesg monitor
            pC_->setCtrlError(mesg);
//			std::cerr << mesg << std::endl;
            return asynError;
            }
         }
      }
   else
      return asynError; //Controller complained at begin.

   //Success
   return asynSuccess;
}

  double GalilAxis::getGalilAxisVal(const char* name)
  {
      static const char *functionName = "GalilAxis::getGalilAxisVal";
	  sprintf(pC_->cmd_, "MG %s%c\n", name, axisName_);
      pC_->writeReadController(functionName);
	  return atof(pC_->resp_);
  }

/** Polls the axis.
  * This function reads the controller position, encoder position, the limit status, the moving status, 
  * and the drive power-on status.  It does not current detect following error, etc. but this could be
  * added.
  * callParamCallbacks is now called in GalilPoller
  * It calls setIntegerParam() and setDoubleParam() for each item that it polls,
  * \param[out] moving A flag that is set indicating that the axis is moving (1) or done (0). */
asynStatus GalilAxis::poll(bool *moving)
{
   //static const char *functionName = "GalilAxis::poll";
   int home;			//Home status to give to motorRecord
   int status;			//Communication status with controller

   //Default communication status
   status = asynError;
   //Default home status
   home = 0;
   //Default moving status
   done_ = 1;
   *moving = false;
   
   //Retrieve the motorRecord use encoder if present (ueip) from ParamList
   status = pC_->getIntegerParam(axisNo_, pC_->GalilUseEncoder_, &ueip_);
   //Retrieve motor type from paramList
   status |= pC_->getIntegerParam(axisNo_, pC_->GalilMotorType_, &motorType_);

   //Extract axis motion data from controller datarecord, and load into GalilAxis instance
   status |= getStatus();
   if (status) goto skip;

   //Set poll variables in GalilAxis based on data record info
   setStatus(moving);

   //Set motor stop time
   setStopTime();

   //Check homing flag
   checkHoming();

   //Execute motor record post if stopped and not homing
   executePost();

   //Execute motor auto power on/off function if stopped, not homing and no new moving comming
   executeAutoOff();

   //check for stalled encoders, whilst we are moving
   //stop motors with stalled encoders
   checkEncoder();

   //check ssi encoder connect status
   set_ssi_connectflag();

   //Enforce wrong limit protection if enabled
   wrongLimitProtection();

   //Check home switch
   if (home_ && !limit_as_home_)
      home = 1;

   /*if Rev switch is on and we are using it as a home, set the appropriate flag*/
   if (rev_ && limit_as_home_)
      home = 1;

   /*if fwd switch is on and we are using it as a home, set the appropriate flag*/
   if (fwd_ && limit_as_home_)
      home = 1;

   // Save encoder position, and done for next poll cycle
   // only update last_encoder_position_ if we think encoder has moved, otherwise if we move 
   // slower than "edel/eres" per poll it will not register as a move and might cause a stall
   if (encoderMove_)
   {
       last_encoder_position_ = encoder_position_;
   }
   last_done_ = done_;

skip:
   //Set status
   //Pass step count/aux encoder info to motorRecord
   setDoubleParam(pC_->motorPosition_, motor_position_);
   //Pass encoder value to motorRecord
   setDoubleParam(pC_->motorEncoderPosition_, encoder_position_);
   //Pass home status to motorRecord
   setIntegerParam(pC_->motorStatusAtHome_, home);
   setIntegerParam(pC_->motorStatusHome_, home);
   //Pass direction to motorRecord
   setIntegerParam(pC_->motorStatusDirection_, direction_);
   //Tell upper layers motor is moving whilst post, and homing requests are being executed
   //This prevents new moves being initiated whilst post, homing requests are being executed
   //Also keeps HOMR and HOMF 1 until homing finished
   //Done late in poll to allow parallel execution with pollServices
   if ((postSent_ && !postExecuted_) || (homedSent_ && !homedExecuted_) || homing_)
      {
      *moving = true;
      done_ = 0;
      if (homing_)
         {
         fwd_ = 0;
         rev_ = 0;
         }
      }
   //Pass limit status to motorRecord
   setIntegerParam(pC_->motorStatusLowLimit_, rev_);
   setIntegerParam(pC_->motorStatusHighLimit_, fwd_);
   //Pass moving status to motorRecord
   setIntegerParam(pC_->motorStatusDone_, done_);
   setIntegerParam(pC_->motorStatusMoving_, *moving);
   //Pass comms status to motorRecord
   setIntegerParam(pC_->motorStatusCommsError_, status ? 1:0);
   //Update motor status fields in upper layers using asynMotorAxis->callParamCallbacks
   callParamCallbacks();   
   //Always return success. Dont need more error mesgs
   return asynSuccess;
}

/*-----------------------------------------------------------------------------------*/
/* Returns true if motor is enabled with current digital IO status
*/

bool GalilAxis::motor_enabled(void)
{
    unsigned mask;				 //Mask used to check motor go/no go status
    unsigned i, j;				 //General loop counters
    struct Galilmotor_enables *motor_enables = NULL;  //Convenience pointer to GalilController motor_enables[digport]
    unsigned binaryin;				 //binary in state
    char mesg[MAX_MESSAGE_LEN];			//To inform user when disabled

    //Retrieve binary in data for 1st bank from ParamList (ie. bits 0-7)
    pC_->getUIntDigitalParam(0, pC_->GalilBinaryIn_ , &binaryin, 0xFF);

    //Cycle through digital inputs structure looking for current motor
    for (i=0;i<8;i++)
	{
        //Retrieve structure for digital port from controller instance
	motor_enables = (Galilmotor_enables *)&pC_->motor_enables_[i];
	//Scan through motors in the disable list
	for (j=0;j<strlen(motor_enables->motors);j++)
		{
		//Is the current GalilAxis found in disable list
		if (motor_enables->motors[j] == axisName_)
			{
			//motor found
			//Calculate mask
			mask = (1 << (i));
			//Check "no go" status
			if ((binaryin & mask) == mask)
				{
				/* Motor is "no go", due to digital IO state */
				if (motor_enables->disablestates[j] == 1)
					{
					sprintf(mesg, "%c disabled due to digital input", axisName_);
					//Set controller error mesg monitor
					pC_->setCtrlError(mesg);
					return(false);
					}
				}
			else
				{
				/* Motor is "no go", due to digital IO state */
				if (motor_enables->disablestates[j] == 0)
					{
					sprintf(mesg, "%c disabled due to digital input", axisName_);
					//Set controller error mesg monitor
					pC_->setCtrlError(mesg);
					return(false);
					}
				}				
			}
		}
	}
	
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
    int ueip;				//MotorRecord use encoder if present
    int ssi_connect;			//SSI encoder connect status
    int ssicapable, ssiinput;		//SSI parameters
    int ssitotalbits, ssierrbits;	//SSI parameters
    int ssidataform;
    int i;
   
    //Retrieve the motorRecord use encoder if present (ueip) from ParamList
    pC_->getIntegerParam(axisNo_, pC_->GalilUseEncoder_, &ueip);
    //Retrieve SSI parameters required
    pC_->getIntegerParam(pC_->GalilSSICapable_, &ssicapable);
    pC_->getIntegerParam(pC_->GalilSSIInput_, &ssiinput);
    pC_->getIntegerParam(axisNo_, pC_->GalilSSITotalBits_, &ssitotalbits);
    pC_->getIntegerParam(axisNo_, pC_->GalilSSIErrorBits_, &ssierrbits);
    pC_->getIntegerParam(axisNo_, pC_->GalilSSIData_, &ssidataform);

    if (ueip != 0 && ssicapable !=0 && ssiinput !=0)
    	{
	//work out the value recieved when encoder disconnected
	if (ssidataform == 2)
		{
		//First we do gray code encoders
		for (i=0;i<(ssitotalbits - ssierrbits);i++)
			{
			if (i % 2)
				disconnect_valtmp |= (long)pow(2.0,i);
			
			}
		if (!(invert_ssi_))
			disconnect_val = (double)disconnect_valtmp;
		else
			disconnect_val = (pow(2.0,ssitotalbits - ssierrbits) - 1) - disconnect_valtmp;
		}
	else
		{
		//last we do binary code encoders
		if (!(invert_ssi_))
			disconnect_val = (pow(2.0,ssitotalbits - ssierrbits) - 1);
		else
			disconnect_val = 0;
		}
		
	//check if encoder_position_ == value recieved when encoder disconnected
	//set connect flag accordingly
	ssi_connect = (encoder_position_ == disconnect_val) ? 0 : 1;
	setIntegerParam(pC_->GalilSSIConnected_, ssi_connect);
	}
    else
	setIntegerParam(pC_->GalilSSIConnected_, 0);
}

/*-----------------------------------------------------------------------------------*/
/* Get ssi encoder settings from controller
*/

asynStatus GalilAxis::get_ssi(int function, epicsInt32 *value)
{
	static const char *functionName = "GalilAxis::get_ssi";
	asynStatus status;				 //Comms status
	int ssiinput, ssitotalbits, ssisingleturnbits;   //Local copy of ssi parameters
        int ssierrbits, ssitimecode, ssidataform;	 //Local copy of ssi parameters
	//Construct query
	sprintf(pC_->cmd_, "SI%c=?\n", axisName_);
	//Write query to controller
	if ((status = pC_->writeReadController(functionName)) == asynSuccess)
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
	static const char *functionName = "GalilAxis::set_ssi";
	int ssiinput, ssitotalbits, ssisingleturnbits;   //Local copy of ssi parameters
        int ssierrbits, ssitimecode, ssidataform;	 //Local copy of ssi parameters
	asynStatus status;				 //Comms status
	int allowed[] = {4,8,10,12,13,24,26};		 //Allowed values of p parameter for SSI setting
	bool found;					 //Used to validate ssitimecode
	int i;						 //General loop variable
	
	//Retrieve ssi parameters from ParamList
	pC_->getIntegerParam(axisNo_, pC_->GalilSSIInput_, &ssiinput);
	pC_->getIntegerParam(axisNo_, pC_->GalilSSITotalBits_, &ssitotalbits);
	pC_->getIntegerParam(axisNo_, pC_->GalilSSISingleTurnBits_, &ssisingleturnbits);
	pC_->getIntegerParam(axisNo_, pC_->GalilSSIErrorBits_, &ssierrbits);
	pC_->getIntegerParam(axisNo_, pC_->GalilSSITime_, &ssitimecode);
	pC_->getIntegerParam(axisNo_, pC_->GalilSSIData_, &ssidataform);

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
		
	ssidataform = (ssidataform == 0) ? 1 : 2;

	//Update ssi setting on controller
	sprintf(pC_->cmd_, "SI%c=%d,%d,%d,%d<%d>%d", axisName_, ssiinput, ssitotalbits, ssisingleturnbits, ssierrbits, ssitimecode, ssidataform);
	//Write setting to controller
	status = pC_->writeReadController(functionName);
	
	return status;
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
