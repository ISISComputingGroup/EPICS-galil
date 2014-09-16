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
#include <float.h>
#include <string.h>
#include <stdlib.h>
#include <Galil.h>   //Galil communication library api
#include <iostream>  //cout
#include <sstream>   //ostringstream istringstream
#include <typeinfo>  //std::bad_typeid
#include <algorithm> //std::remove_if

using namespace std; //cout ostringstream vector string

#include <epicsString.h>
#include <iocsh.h>
#include <epicsThread.h>
#include <epicsExit.h>
#include <errlog.h>
#include <initHooks.h>

#include <asynOctetSyncIO.h>

#include "GalilController.h"
#include <epicsExport.h>

static const char *driverName = "GalilController";

static void GalilProfileThreadC(void *pPvt);

//Single GalilConnector instance for all GalilControllers
GalilConnector *connector;

//Block read functions during Iocinit
//Prevent normal behaviour of output records getting initial value at iocInit from driver
//Instead output records will write their db default or autosave value just after iocInit
//This change in behaviour is anticipated by the asyn record device layer and causes no error mesgs
bool readAllowed_ = false;

//Number of communication retries
#define MAX_RETRIES 1
#define ALLOWED_TIMEOUTS 3

#define MAX_FILENAME_LEN 256
#define MAX_MESSAGE_LEN 256

//Convenience functions
#ifndef MAX
#define MAX(a,b) ((a)>(b)? (a): (b))
#endif
#ifndef MIN
#define MIN(a,b) ((a)<(b)? (a): (b))
#endif

//EPICS exit handler
extern "C" void shutdownCallback(void *pPvt)
{
  GalilController *pC_ = static_cast<GalilController *>(pPvt);

  pC_->lock();
  pC_->shuttingDown_ = 1;
  pC_->unlock();
}

//EPICS iocInit status
extern "C" void myHookFunction(initHookState state)
{
  //Update readAllowed_ status for all GalilController instances
  if (state >= initHookAfterInitDatabase)
	readAllowed_ = true;
}

/** Creates a new GalilController object.
  * \param[in] portName          The name of the asyn port that will be created for this driver
  * \param[in] address      	 The name or address to provide to Galil communication library 
  * \param[in] updatePeriod  	 The time between polls when any axis is moving 
  */
GalilController::GalilController(const char *portName, const char *address, double updatePeriod)
  :  asynMotorController(portName, MAX_GALIL_AXES + MAX_GALIL_CSAXES, NUM_GALIL_PARAMS,	//MAX_GALIL_AXES paramLists are needed for binary IO at all times
                         asynInt32Mask | asynFloat64Mask | asynUInt32DigitalMask, 
                         asynInt32Mask | asynFloat64Mask | asynUInt32DigitalMask,
                         ASYN_CANBLOCK | ASYN_MULTIDEVICE, 
                         1, // autoconnect
                         0, 0)  // Default priority and stack size
{
  struct Galilmotor_enables *motor_enables = NULL;  //Convenience pointer to GalilController motor_enables[digport]
  unsigned i;
  
  // Create controller-specific parameters
  createParam(GalilAddressString, asynParamOctet, &GalilAddress_);
  createParam(GalilHomeTypeString, asynParamInt32, &GalilHomeType_);
  createParam(GalilLimitTypeString, asynParamInt32, &GalilLimitType_);

  createParam(GalilCoordSysString, asynParamInt32, &GalilCoordSys_);
  createParam(GalilCoordSysMotorsString, asynParamOctet, &GalilCoordSysMotors_);
  createParam(GalilCoordSysMovingString, asynParamInt32, &GalilCoordSysMoving_);
  createParam(GalilCoordSysSegmentsString, asynParamInt32, &GalilCoordSysSegments_);
  createParam(GalilCoordSysMotorsStopString, asynParamInt32, &GalilCoordSysMotorsStop_);
  createParam(GalilCoordSysMotorsGoString, asynParamInt32, &GalilCoordSysMotorsGo_);

  createParam(GalilCoordSysVarString, asynParamFloat64, &GalilCoordSysVar_);

  createParam(GalilProfileFileString, asynParamOctet, &GalilProfileFile_);
  createParam(GalilProfileMaxVelocityString, asynParamFloat64, &GalilProfileMaxVelocity_);
  createParam(GalilProfileMaxAccelerationString, asynParamFloat64, &GalilProfileMaxAcceleration_);
  createParam(GalilProfileMinPositionString, asynParamFloat64, &GalilProfileMinPosition_);
  createParam(GalilProfileMaxPositionString, asynParamFloat64, &GalilProfileMaxPosition_);
  createParam(GalilProfileMoveModeString, asynParamInt32, &GalilProfileMoveMode_);

  createParam(GalilOutputCompare1AxisString, asynParamInt32, &GalilOutputCompareAxis_);
  createParam(GalilOutputCompare1StartString, asynParamFloat64, &GalilOutputCompareStart_);
  createParam(GalilOutputCompare1IncrString, asynParamFloat64, &GalilOutputCompareIncr_);
  createParam(GalilOutputCompareMessageString, asynParamOctet, &GalilOutputCompareMessage_);

  createParam(GalilMotorStopGoString, asynParamInt32, &GalilMotorStopGo_);

  createParam(GalilSSIConnectedString, asynParamInt32, &GalilSSIConnected_);	
  createParam(GalilEncoderStallString, asynParamInt32, &GalilEStall_);
  createParam(GalilEncoderStallTimeString, asynParamFloat64, &GalilEStallTime_);

  createParam(GalilStepSmoothString, asynParamFloat64, &GalilStepSmooth_);
  createParam(GalilEncoderDeadBString, asynParamFloat64, &GalilEncoderDeadB_);
  createParam(GalilMotorTypeString, asynParamInt32, &GalilMotorType_);
  createParam(GalilModelString, asynParamOctet, &GalilModel_);

  createParam(GalilMotorOnString, asynParamInt32, &GalilMotorOn_);
  createParam(GalilMotorConnectedString, asynParamInt32, &GalilMotorConnected_);

  createParam(GalilProgramHomeString, asynParamInt32, &GalilProgramHome_);
  createParam(GalilAfterLimitString, asynParamFloat64, &GalilAfterLimit_);
  createParam(GalilHomeValueString, asynParamFloat64, &GalilHomeValue_);
  createParam(GalilHomedString, asynParamInt32, &GalilHomed_);
  createParam(GalilWrongLimitProtectionString, asynParamInt32, &GalilWrongLimitProtection_);
  createParam(GalilWrongLimitProtectionActiveString, asynParamInt32, &GalilWrongLimitProtectionActive_);

  createParam(GalilUserOffsetString, asynParamFloat64, &GalilUserOffset_);
  createParam(GalilEncoderResolutionString, asynParamFloat64, &GalilEncoderResolution_);
  createParam(GalilUseEncoderString, asynParamInt32, &GalilUseEncoder_);

  createParam(GalilMainEncoderString, asynParamInt32, &GalilMainEncoder_);
  createParam(GalilAuxEncoderString, asynParamInt32, &GalilAuxEncoder_);
  createParam(GalilMotorAcclString, asynParamFloat64, &GalilMotorAccl_);
  createParam(GalilMotorVeloString, asynParamFloat64, &GalilMotorVelo_);
  createParam(GalilMotorVmaxString, asynParamFloat64, &GalilMotorVmax_);

  createParam(GalilBinaryInString, asynParamUInt32Digital, &GalilBinaryIn_);
  createParam(GalilBinaryOutString, asynParamUInt32Digital, &GalilBinaryOut_);
  createParam(GalilBinaryOutRBVString, asynParamUInt32Digital, &GalilBinaryOutRBV_);

  createParam(GalilAnalogInString, asynParamFloat64, &GalilAnalogIn_);
  createParam(GalilAnalogOutString, asynParamFloat64, &GalilAnalogOut_);
  createParam(GalilAnalogOutRBVString, asynParamFloat64, &GalilAnalogOutRBV_);

  createParam(GalilStopEventString, asynParamInt32, &GalilStopEvent_);

  createParam(GalilDirectionString, asynParamInt32, &GalilDirection_);
  createParam(GalilSSICapableString, asynParamInt32, &GalilSSICapable_);

  createParam(GalilSSIInputString, asynParamInt32, &GalilSSIInput_);
  createParam(GalilSSITotalBitsString, asynParamInt32, &GalilSSITotalBits_);
  createParam(GalilSSISingleTurnBitsString, asynParamInt32, &GalilSSISingleTurnBits_);
  createParam(GalilSSIErrorBitsString, asynParamInt32, &GalilSSIErrorBits_);
  createParam(GalilSSITimeString, asynParamInt32, &GalilSSITime_);
  createParam(GalilSSIDataString, asynParamInt32, &GalilSSIData_);
  createParam(GalilErrorLimitString, asynParamFloat64, &GalilErrorLimit_);
  createParam(GalilErrorString, asynParamFloat64, &GalilError_);
  createParam(GalilOffOnErrorString, asynParamInt32, &GalilOffOnError_);
  createParam(GalilAxisString, asynParamInt32, &GalilAxis_);
//Add new parameters here

  createParam(GalilCommunicationErrorString, asynParamInt32, &GalilCommunicationError_);

  //Print Galil communication library version
  cout << Galil::libraryVersion() << endl;

  //Store address
  strcpy(address_, address);
  //Default model
  strcpy(model_, "Unknown");
  //Initialize galil communication object
  gco_ = NULL;
  //Code for the controller has not been assembled yet
  code_assembled_ = false;
  //We have not reported any connect failures
  connect_fail_reported_ = false;
  //We have not recieved a timeout yet
  consecutive_timeouts_ = 0;
  //Store period in ms between data records
  updatePeriod_ = updatePeriod;
  //Code generator has not been initialized
  codegen_init_ = false;		
  digitalinput_init_ = false;
  //Deferred moves off at start-up
  movesDeferred_ = false;
  //Allocate memory for code buffers.  
  //We put all code for this controller in these buffers.
  thread_code_ = (char *)calloc(MAX_GALIL_AXES * (THREAD_CODE_LEN),sizeof(char));	
  limit_code_ = (char *)calloc(MAX_GALIL_AXES * (LIMIT_CODE_LEN),sizeof(char));
  digital_code_ = (char *)calloc(MAX_GALIL_AXES * (INP_CODE_LEN),sizeof(char));
  card_code_ = (char *)calloc(MAX_GALIL_AXES * (THREAD_CODE_LEN+LIMIT_CODE_LEN+INP_CODE_LEN),sizeof(char));
  //zero code buffers
  strcpy(thread_code_, "");
  strcpy(limit_code_, "");
  strcpy(digital_code_, "");
  strcpy(card_code_, "");
 
  //Set defaults in Paramlist before connect
  setParamDefaults();

  /* Set an EPICS exit handler that will shut down polling before exit */
  epicsAtExit(shutdownCallback, this);

  //Thread to acquire datarecord for a single GalilController
  //We write our own because epicsEventWaitWithTimeout in asynMotorController::asynMotorPoller calls sleep, we dont want that.
  //One instance per GalilController
  poller_ = new GalilPoller(this);

  //Create separate thread to manage connection to GalilController(s)
  //This thread makes poller sleep/wake, and so GalilPoller must instantiated already
  if (!connector)
	{
	//Create GalilConnector class instance.
	//One instance per driver only
	connector = new GalilConnector();
	//Register for iocInit state updates, so we can keep track of iocInit status
	initHookRegister(myHookFunction);
	}

  //Register this GalilController instance with GalilConnector for connection management
  connector->registerController(this);

  // Create the event that wakes up the thread for profile moves
  profileExecuteEvent_ = epicsEventMustCreate(epicsEventEmpty);
  
  // Create the thread that will execute profile moves
  epicsThreadCreate("GalilProfile", 
                    epicsThreadPriorityLow,
                    epicsThreadGetStackSize(epicsThreadStackMedium),
                    (EPICSTHREADFUNC)GalilProfileThreadC, (void *)this);
  
  //Wait 2 sec for first connect, then keep going
  epicsThreadSleep(2.0);

  //Initialize the motor enables struct in GalilController instance
  for (i=0;i<8;i++)
	{
	//Retrieve structure for digital port from controller instance
	motor_enables = (Galilmotor_enables *)&motor_enables_[i];
	//Initialize motor enables structure in GalilController instance
	strcpy(motor_enables->motors, "");
	strcpy(motor_enables->disablestates, "");
	}
}

//Calls GalilController::connect when needed.
void GalilController::connectManager(void)
{
     bool require_connect;
     //Reset require_connect flag
     require_connect = false;
     //If we have received allowed timeouts
     if (consecutive_timeouts_ > ALLOWED_TIMEOUTS)
	require_connect = true;	
     //If not connected
     if (gco_ == NULL)
	require_connect = true;	
     //disconnect/connect is required
     if (require_connect)
	{
	//Obtain mutex
	lock();
	//Wait until poller is in sleep mode
	//Also stop async records from controller
	poller_->sleepPoller(false);
	//Disconnect if needed, and then attempt connect
	connect();
	//Wake poller,
	poller_->wakePoller();
	unlock();
	}
}

//Disconnection/connection management
void GalilController::connect(void)
{
  static const char *functionName = "connect";

  //Close connection if its open
  if (gco_ != NULL)
	{
	//Delete reference to Galil communication class.  This runs galil communication class destructor
	delete gco_;
	//Stop GalilController class use of Galil communication object
	gco_ = NULL;
	//Print brief disconnection details
	cout << "Disconnected from " << model_ << " at " << address_ << endl;
	}

  //Attempt connection
  try	{
	//Open connection to address provided
	gco_ = new Galil(address_);
	//Success, continue
	//No timeouts have occurred
	consecutive_timeouts_ = 0;
	//A connection fail mesg not issued, because connect succeeded
	connect_fail_reported_ = false;
	}
  catch (string e)
      	{
	//Ensure galil communications object is NULL on connect failure
	gco_ = NULL;
	if (!connect_fail_reported_)
		{
		//Couldnt connect mesg
	      	cout << functionName << ":" << " " << e;
		//Assume 8 threads controller we cannot connect with
		numThreads_ = 8;
		//Connect fail has been reported to iocShell
		connect_fail_reported_ = true;
		}
      	}

  //if connected
  if (gco_ != NULL)
	{
	//Read controller model, firmware, stop all motors and threads
  	connected();
	callParamCallbacks();	//Pass changes to custom records (ai, ao, etc)
	}
}

void GalilController::setParamDefaults(void)
{ 
  unsigned i;
  //Set defaults in Paramlist before connected
  //Pass address string provided by GalilCreateController to upper layers
  setStringParam(GalilAddress_, address_);
  //Set default model string
  setStringParam(GalilModel_, "Unknown");
  //SSI capable
  setIntegerParam(GalilSSICapable_, 0);
  //Communication status
  setIntegerParam(GalilCommunicationError_, 0);
  //Deferred moves off 
  setIntegerParam(motorDeferMoves_, 0);
  //Default coordinate system is S
  setIntegerParam(GalilCoordSys_, 0);
  //Coordinate system S axes list empty
  setStringParam(0, GalilCoordSysMotors_, "");
  //Coordinate system T axes list empty
  setStringParam(1, GalilCoordSysMotors_, "");
  //Put all motors in spmg go mode
  for (i = 0; i < MAX_GALIL_AXES + MAX_GALIL_CSAXES; i++)
	setIntegerParam(i, GalilMotorStopGo_, 3);
  //Output compare is off
  for (i = 0; i < 2; i++)
	setIntegerParam(i, GalilOutputCompareAxis_, 0);
}

//Anything that should be done once connection established
//Read controller details, stop all motors and threads
void GalilController::connected(void)
{
	static const char *functionName = "connected";
	char RV[] ={0x12,0x16,0x0};    		//Galil command string for model and firmware version query
  	unsigned i;

	//Used for development
	/*
	vector<string> s = gco_->sources();  //create a vector to hold all available sources
	for (i = 0; i < s.size() ; i++)  //step through sources and print every source name
		{
		//Print source description
		cout << gco_->source("Description",s[i]) << endl;
		//Print source name
		cout << s[i] << endl;
		}
	*/

	//Load model, and firmware query into cmd structure
	strcpy(cmd_, RV); 
	//Query model, and firmware version
	writeReadController(functionName);
	//store model, and firmware version in GalilController instance
	strcpy(model_, resp_);
	//Pass model string to ParamList
	setStringParam(GalilModel_, model_);

	//Print brief connection details
	cout << "Connected to " << model_ << " at " << address_ << endl;
	
	//Read max number of axes the controller supports
	strcpy(cmd_, "MG _BV");
	writeReadController(functionName);
	//Store max axes controller supports
	numAxesMax_ = atoi(resp_);

	//adjust numAxesMax_ when model is RIO.
	numAxesMax_ = (strncmp(model_, "RIO",3) == 0)? 0 : numAxesMax_;
	//Determine if controller is SSI capable
	if (strstr(model_, "SI") == NULL)
		setIntegerParam(GalilSSICapable_, 0);
	else
		setIntegerParam(GalilSSICapable_, 1);
	
	//Determine number of threads supported
	//RIO
	numThreads_ = (strncmp(model_,"RIO",3) == 0)? 4 : numThreads_;
	//DMC4 range
	if ((model_[0] == 'D' && model_[3] == '4'))
		numThreads_ = 8;
	//DMC2 range
	numThreads_ = (model_[3] == '2')? 8 : numThreads_;
	//DMC1 range
	numThreads_ = (model_[3] == '1')? 2 : numThreads_;

	//Stop all threads running on the controller
	for (i=0;i<numThreads_;i++)
		{
		sprintf(cmd_, "HX%d",i);
		writeReadController(functionName);
		}

	//Stop all moving motors, and turn all motors off
	for (i=0;i<numAxesMax_;i++)
		{
		//Query moving status
		sprintf(cmd_, "MG _BG%c", (i + AASCII));
		writeReadController(functionName);
		if (atoi(resp_))
			{
			//Stop moving motor
			sprintf(cmd_, "ST%c", (i + AASCII));
			writeReadController(functionName);
			//Allow time for motor stop
		        epicsThreadSleep(1.0);
			//Ensure home process is stopped
			sprintf(cmd_, "home%c=0", (i + AASCII));
			writeReadController(functionName);
			}
		//Turn off motor
		sprintf(cmd_, "MO%c", (i + AASCII));
		writeReadController(functionName);
		}

	//Has code for the GalilController been assembled
	if (code_assembled_)
		{
	        //Deliver and start the code on controller
		GalilStartController(code_file_, eeprom_write_, 0);
		}
	//Use async polling by default
	try	{
		//get controller to send a record at specified rate
		gco_->recordsStart(updatePeriod_);
		//Success, no exception
		async_records_ = true;
		}
	catch  (string e) {
		//Upon any exception, use synchronous poll mechanism at rate specified in GalilCreateController as fall back strategy
		//Fail.  Probably wrong bus.  RS-232 does not support async DR data record 
		async_records_ = false;
		//Print explanation
		cout << "Asynchronous setup failed, swapping to synchronous poll " << model_ << " at " << address_ << endl;
		cout << e << endl;
		}
}

/** Reports on status of the driver
  * \param[in] fp The file pointer on which report information will be written
  * \param[in] level The level of report detail desired
  *
  * If details > 0 then information is printed about each axis.
  * After printing controller-specific information calls asynMotorController::report()
  */
void GalilController::report(FILE *fp, int level)
{
  //int axis;
  //GalilAxis *pAxis;

  fprintf(fp, "Galil motor driver %s, numAxes=%d, moving poll period=%f, idle poll period=%f\n", 
    this->portName, numAxes_, movingPollPeriod_, idlePollPeriod_);
  /*
  if (level > 0) {
    for (axis=0; axis<numAxes_; axis++) {
      pAxis = getAxis(axis);
      fprintf(fp, "  axis %d\n"
              "    pulsesPerUnit_ = %f\n"
              "    encoder position=%f\n"
              "    theory position=%f\n"
              "    limits=0x%x\n"
              "    flags=0x%x\n", 
              pAxis->axisNo_, pAxis->pulsesPerUnit_, 
              pAxis->encoderPosition_, pAxis->theoryPosition_,
              pAxis->currentLimits_, pAxis->currentFlags_);
    }
  }*/

  // Call the base class method
  asynMotorController::report(fp, level);
}

/** Returns a pointer to an GalilMotorAxis object.
  * Returns NULL if the axis number encoded in pasynUser is invalid.
  * \param[in] pasynUser asynUser structure that encodes the axis index number. */
GalilAxis* GalilController::getAxis(asynUser *pasynUser)
{
  //For real motors
  return static_cast<GalilAxis*>(asynMotorController::getAxis(pasynUser));
}

/** Returns a pointer to an GalilMotorAxis object.
  * Returns NULL if the axis number encoded in pasynUser is invalid.
  * \param[in] pasynUser asynUser structure that encodes the axis index number. */
GalilCSAxis* GalilController::getCSAxis(asynUser *pasynUser)
{
  //For coordinate system motors
  return static_cast<GalilCSAxis*>(asynMotorController::getAxis(pasynUser));
}

/** Returns a pointer to an GalilMotorAxis object.
  * Returns NULL if the axis number encoded in pasynUser is invalid.
  * \param[in] axisNo Axis index number. */
GalilAxis* GalilController::getAxis(int axisNo)
{
  //For real motors
  return static_cast<GalilAxis*>(asynMotorController::getAxis(axisNo));
}

/** Returns a pointer to an GalilMotorAxis object.
  * Returns NULL if the axis number encoded in pasynUser is invalid.
  * \param[in] axisNo Axis index number. */
GalilCSAxis* GalilController::getCSAxis(int axisNo)
{
  //For coordinate system motors
  return static_cast<GalilCSAxis*>(asynMotorController::getAxis(axisNo));
}

/** Returns true if any motor in the provided list is moving
  * \param[in] Motor list
  */
bool GalilController::motorsMoving(char *axes)
{
  int moving = 0;	//Moving status
  int axisNo;		//Axis number
  int j;		//Looping

  //Look through motor list, if any moving return true
  for (j = 0; j < (int)strlen(axes); j++)
	{
	//Determine axis number
	axisNo = axes[j] - AASCII;
	getIntegerParam(axisNo, motorStatusMoving_, &moving);
	if (moving) return true;
	}

  //None of the motors were moving
  return false;
}

/** setOutputCompare function.  For turning output compare on/off
  * \param[in] output compare to setup - 0 or 1 for output compare 1 and 2 */
asynStatus GalilController::setOutputCompare(int oc)
{
  static const char *functionName = "setOutputCompare";
  char message[MAX_MESSAGE_LEN];	//Output compare message
  int ueip;				//mr ueip field
  int ocaxis;				//Output compare axis from paramList
  int axis;				//Axis number derived from output compare axis in paramList
  int motor;				//motor type read from controller
  double ocstart;			//Output compare start position from paramList
  double ocincr;			//Output compare incremental distance for repeat pulses from paramList
  double eres;	        		//mr eres
  int mainencoder, auxencoder;		//Main and aux encoder setting
  int encoder_setting;			//Overall encoder setting value
  bool encoders_ok = false;		//Encoder setting good or bad for output compare
  bool setup_ok = false;		//Overall setup status
  int bankAservo, bankEservo;		//Axis that is servo in banks A-D, and E-H
  int comstatus = asynSuccess;		//Status of comms
  int paramstatus = asynSuccess;	//Status of paramList gets
  int i;				//Looping

  //Retrieve axis to use with output compare
  paramstatus = getIntegerParam(oc, GalilOutputCompareAxis_, &ocaxis);

  //Attempt turn on selected output compare
  if (ocaxis && !paramstatus)
	{
	//Convert paramList ocaxis to 0-7 for axis A-H
	axis = (oc) ? (ocaxis - 1 + 4) : (ocaxis - 1);
	//Query motor type
        sprintf(cmd_, "MT%c=?", axis + AASCII);
        comstatus = writeReadController(functionName);
        motor = atoi(resp_);	
	//Retrieve ueip setting for this motor
	paramstatus = getIntegerParam(axis, GalilUseEncoder_, &ueip);
	//Check encoder settings
	paramstatus |= getIntegerParam(axis, GalilMainEncoder_, &mainencoder);
	paramstatus |= getIntegerParam(axis, GalilAuxEncoder_, &auxencoder);
	encoder_setting = mainencoder + auxencoder;
	if ((!encoder_setting || encoder_setting == 5 || encoder_setting == 10 || encoder_setting == 15) && !paramstatus)
		encoders_ok = true;

	if (ueip && (abs(motor) == 1) && encoders_ok && !paramstatus && !comstatus)
		{
		//Passed motor configuration checks.  Motor is servo, ueip = 1, and encoder setting is ok
		//Retrieve output compare start, and increment values
		paramstatus = getDoubleParam(oc, GalilOutputCompareStart_, &ocstart);
		paramstatus |= getDoubleParam(oc, GalilOutputCompareIncr_, &ocincr);
		//Retrieve the select motor's encoder resolution
		paramstatus = getDoubleParam(axis, GalilEncoderResolution_, &eres);
		//Convert start and increment to steps
		ocstart = ocstart / eres;
		ocincr = ocincr / eres;
		//Check start, and increment values
		if (fabs(rint(ocstart)) > 0 && fabs(rint(ocstart)) < 65535 && fabs(rint(ocincr)) > 0 && fabs(rint(ocincr)) < 65535 && !paramstatus)
			{			
			if (!paramstatus)
				{
				sprintf(cmd_, "OC%c=%.0lf,%.0lf", axis + AASCII, rint(ocstart), rint(ocincr));
				comstatus = writeReadController(functionName);
				setup_ok = (!comstatus) ? true : false;
				if (setup_ok)
					{
					sprintf(message, "Output compare %d setup successfully", oc + 1);
					setStringParam(GalilOutputCompareMessage_, message);
					}
				else
					{
					//Reject motor setting if problem
					sprintf(message, "Output compare %d setup failed", oc + 1);
					setStringParam(GalilOutputCompareMessage_, message);
					setIntegerParam(oc, GalilOutputCompareAxis_, 0);
					}
				}
			}
		else
			{
			//Reject motor setting if problem with start or increment
			sprintf(message, "Output compare %d failed due to start/increment out of range", oc + 1);
			setStringParam(GalilOutputCompareMessage_, message);
			setIntegerParam(oc, GalilOutputCompareAxis_, 0);
			}
		}
	else if (!paramstatus && !comstatus)
		{
		//Reject motor setting if the motor has a configuration problem
		sprintf(message, "Output compare %d failed due to configuration problem axis %c", oc + 1, axis + AASCII);
		paramstatus = setStringParam(GalilOutputCompareMessage_, message);
		setIntegerParam(oc, GalilOutputCompareAxis_, 0);
		}
	}

  //Attempt turn off selected output compare
  if (!setup_ok && !paramstatus)
	{
	//Default parameters
	axis = bankAservo = bankEservo = 99;
	//Find a servo in A-D, and E-H
	for (i=0;i<MAX_GALIL_AXES;i++)
		{
		sprintf(cmd_, "MT%c=?", i + AASCII);
		comstatus = writeReadController(functionName);
		motor = atoi(resp_);
		if (abs(motor) == 1 && i<4)
			bankAservo = i;
		if (abs(motor) == 1 && i>3)
			bankEservo = i;
		}
	//Determine correct axis to use to turn of this output compare
	if (!oc && bankAservo != 99)
		axis = bankAservo;
	if (oc && bankEservo != 99)
		axis = bankEservo;
	
	if (axis != 99)
		{
		//A servo was found in the correct bank
		sprintf(cmd_, "OC%c=0,0", axis + AASCII);
		comstatus = writeReadController(functionName);
		if (!ocaxis)
			{
			sprintf(message, "Output compare %d turned off", oc + 1);
			setStringParam(GalilOutputCompareMessage_, message);
			}
		}
	}

  return (asynStatus)comstatus;
}

//Creates a profile data file suitable for use with linear interpolation mode
asynStatus GalilController::buildLinearProfile()
{
  GalilAxis *pAxis;				//GalilAxis instance
  int nPoints;					//Number of points in profile
  double velocity[MAX_GALIL_AXES];		//Motor velocity
  double maxAllowedVelocity[MAX_GALIL_AXES];    //Derived from MR VMAX to ensure motor velocities are within limits
  double maxProfileVelocity[MAX_GALIL_AXES];    //The highest velocity for each motor in the profile data
  double maxProfilePosition[MAX_GALIL_AXES];	//Maximum profile position in absolute mode
  double minProfilePosition[MAX_GALIL_AXES];	//Minimum profile position in absolute mode
  double maxProfileAcceleration[MAX_GALIL_AXES];//Maximum profile acceleration in any mode
  double vectorVelocity;			//Segment vector velocity
  double incmove;				//Motor incremental move distance
  double firstmove[MAX_GALIL_AXES];		//Used to normalize moves to relative, and prevent big jumps at profile start			
  double apos[MAX_GALIL_AXES];			//Accumulated profile position calculated from integer rounded units (ie. steps/counts)
  double aerr[MAX_GALIL_AXES];			//Accumulated error
  int i, j;					//Loop counters
  int zm_count;					//Zero segment move counter
  int num_motors;				//Number of motors in trajectory
  char message[MAX_MESSAGE_LEN];		//Profile build message
  int useAxis[MAX_GALIL_AXES];  		//Use axis flag for profile moves
  int moveMode[MAX_GALIL_AXES];  		//Move mode absolute or relative
  char moves[MAX_MESSAGE_LEN];			//Segment move command assembled for controller
  char axes[MAX_GALIL_AXES];    		//Motors involved in profile move
  char startp[MAX_MESSAGE_LEN];			//Profile start positions written to file
  char fileName[MAX_FILENAME_LEN];		//Filename to write profile data to
  FILE *profFile;				//File handle for above file
  bool buildOK=true;				//Was the trajectory built successfully
  double mres;					//Motor resolution
  
  //No axis included yet
  strcpy(axes, "");

  //Start position list for all motors in the profile
  strcpy(startp, "");

  // Retrieve required attributes from ParamList
  getStringParam(GalilProfileFile_, (int)sizeof(fileName), fileName);
  getIntegerParam(profileNumPoints_, &nPoints);

  //Check provided fileName
  if (strcmp(fileName, "") == 0)
	{
	strcpy(message, "Bad trajectory file name");
	return asynError;
	}

  /* Create the profile file */
  profFile =  fopen(fileName, "w");

  //Write profile type
  fprintf(profFile,"LINEAR\n");

  //Zero variables, contruct axes, start position, and maxVelocity lists 
  for (j=0; j<MAX_GALIL_AXES; j++)
	{
	//Retrieve GalilAxis
	pAxis = getAxis(j);
	//Retrieve profileUseAxis_ from ParamList
	getIntegerParam(j, profileUseAxis_, &useAxis[j]);
	//Decide to process this axis, or skip
    	if (!useAxis[j] || !pAxis) continue;
	//Initialize accumulated position, and error
	apos[j] = aerr[j] = velocity[j] = 0;
	//Construct axis list
	sprintf(axes,"%s%c", axes, (char)(j + AASCII));
	//Construct start positions list
	sprintf(startp,"%s%.0lf,", startp, rint(pAxis->profilePositions_[0]));
	//Retrieve the motor maxVelocity in egu
	getDoubleParam(j, GalilMotorVmax_, &maxAllowedVelocity[j]);
	//Retrieve motor resolution
	getDoubleParam(j, motorResolution_, &mres);
	//Calculate velocity in steps
	maxAllowedVelocity[j] = maxAllowedVelocity[j] / mres;
	//Retrieve GalilProfileMoveMode_ from ParamList
	getIntegerParam(j, GalilProfileMoveMode_, &moveMode[j]);
	//Initialize max profile velocity, position, and acceleration
	maxProfileVelocity[j] = maxProfilePosition[j] = maxProfileAcceleration[j] = 0;
	//Initialize min profile position
	minProfilePosition[j] = DBL_MAX;
	}

  //Write axes list
  fprintf(profFile,"%s\n", axes);

  //Write start positions list
  fprintf(profFile,"%s\n", startp);

  //Determine number of motors in profile move
  num_motors = strlen(axes);

  //Calculate motor segment velocities from profile positions, and common time base
  for (i=0; i<nPoints; i++)
  	{
	//No controller moves assembled yet for this segment
  	strcpy(moves, "");
	//velocity for this segment
	vectorVelocity = 0.0;
	//motors with zero moves for this segment
	zm_count = 0;
	//Calculate motor incremental move distance, and velocity
    	for (j=0; j<MAX_GALIL_AXES; j++)
		{
		//Retrieve GalilAxis
		pAxis = getAxis(j);
		//Retrieve profileUseAxis_ from ParamList
		getIntegerParam(j, profileUseAxis_, &useAxis[j]);
		//Decide to process this axis, or skip
    		if (!useAxis[j] || !pAxis)
			{
			if (j < MAX_GALIL_AXES - 1)
				sprintf(moves,  "%s,", moves);	 //Add axis relative move separator character ',' as needed
			//Skip the rest, this axis is not in the profile move
			continue;
			}

		if (i==0)
			{
			//First segment incremental move distance
			firstmove[j] = pAxis->profilePositions_[i];
			//Velocity set to 0 for first increment
			incmove = velocity[j] = 0.0;
			}
		else
			{
			//Segment incremental move distance
			if (i == 1)
				incmove = pAxis->profilePositions_[i] - firstmove[j];
			else
				incmove = (pAxis->profilePositions_[i] - firstmove[j]) - (pAxis->profilePositions_[i-1] - firstmove[j]);
			//Accumulated position calculated using integer rounded positions (units=steps/counts)
			apos[j] += rint(incmove);
			//Accumulated error caused by integer rounding
			aerr[j] = apos[j] - (pAxis->profilePositions_[i] - firstmove[j]);
			//If accumlated error due to rounding greater than 1 step/count, apply correction
			if (fabs(aerr[j]) > 1)
				{
				//Apply correction to segment incremental move distance
				incmove = incmove - aerr[j];
				//Apply correction to accumulated position calculated using integer rounded positions
				apos[j] = apos[j] - aerr[j];
				}
			//Calculate required velocity for this motor given move distance and time
			velocity[j] = incmove / profileTimes_[i];
			}

		//Check profile velocity less than mr vmax for this motor
		if (fabs(velocity[j]) > maxAllowedVelocity[j])
			{
			sprintf(message, "Velocity too high, increase time, or check profile loaded");
			buildOK = false;
			}

		//Retrieve motor resolution
		getDoubleParam(j, motorResolution_, &mres);

		//Find max profile velocity for this motor
		if (fabs(velocity[j]*mres) > maxProfileVelocity[j])
			maxProfileVelocity[j] = fabs(velocity[j]*mres);

		//Find max profile position for this motor
		if ((pAxis->profilePositions_[i]*mres) > maxProfilePosition[j])
			 maxProfilePosition[j] = pAxis->profilePositions_[i]*mres;

		//Find min profile position for this motor
		if ((pAxis->profilePositions_[i]*mres) < minProfilePosition[j])
			 minProfilePosition[j] = pAxis->profilePositions_[i]*mres;

		//Find max profile acceleration for this motor
		if (fabs(velocity[j]*mres)/profileTimes_[i] > maxProfileAcceleration[j])
			maxProfileAcceleration[j] = fabs(velocity[j]*mres)/profileTimes_[i];

		//Check position against software limits
		if (pAxis->profilePositions_[i] < pAxis->lowLimit_ || 
		    pAxis->profilePositions_[i] > pAxis->highLimit_)
			{
			//Only if move mode = Absolute
			if (moveMode[j])
				{
				sprintf(message, "Motor %c position beyond soft limits in segment %d", pAxis->axisName_, i);
				buildOK = false;
				}
			}

		//Add this motors' contribution to vector velocity for this segment
		vectorVelocity += pow(velocity[j], 2);

		//Store motor incremental move distance for this segment
		sprintf(moves, "%s%.0lf", moves, rint(incmove));
		
		//Detect zero moves in this segment
                zm_count =  (rint(incmove) == 0) ? zm_count+1 : zm_count;

		if (j < MAX_GALIL_AXES - 1)
			sprintf(moves,  "%s,", moves);	 //Add axis relative move separator character ',' as needed
    		}

	//Determine vector velocity for this segment
        vectorVelocity = sqrt(vectorVelocity);

        //Check for segment too short error
	if ((rint(vectorVelocity) == 0 && i != 0) || (zm_count == num_motors && i != 0))
		{
		sprintf(message, "Velocity zero, reduce time, add motors, and ensure profile loaded");
		buildOK = false;
		}

        //Trim trailing ',' characters from moves string
	for (j=strlen(moves)-1; j>0; j--)
		{
		if (moves[j] != ',')
			{
			//Terminate moves string
			moves[j+1] = '\0';
			break;
			}
		}

	//Add segment velocity
	sprintf(moves, "%s<%.0lf", moves, rint(vectorVelocity));
	//Add second segment and above
	//First segment is the "relative" offset or start position for the profile
	//This is done to prevent jumps in the motor
	if (i > 0)
		{
		//Write the segment command to profile file
		fprintf(profFile,"%s\n", moves);
		}
  	}
  
  //Profile written to file now close the file
  fclose(profFile);

  //Build failed.  
  if (!buildOK)
	{
	//Delete profile file because its not valid
	remove(fileName);
	//Update build message
  	setStringParam(profileBuildMessage_, message);
	return asynError;
	}
  else
	{
	//Update profile ParamList attributes if buildOK
	for (j=0; j<MAX_GALIL_AXES; j++)
		{
		//Retrieve GalilAxis
		pAxis = getAxis(j);
		//Decide to process this axis, or skip
    		if (!useAxis[j] || !pAxis) continue;
		pAxis->setDoubleParam(GalilProfileMinPosition_, minProfilePosition[j]);
    		pAxis->setDoubleParam(GalilProfileMaxPosition_, maxProfilePosition[j]);
    		pAxis->setDoubleParam(GalilProfileMaxVelocity_, maxProfileVelocity[j]);
    		pAxis->setDoubleParam(GalilProfileMaxAcceleration_, maxProfileAcceleration[j]);
		pAxis->callParamCallbacks();
		}
	}

  return asynSuccess;
}

/* Function to build, install and verify trajectory */ 
asynStatus GalilController::buildProfile()
{
  int status;				//asynStatus
  char message[MAX_MESSAGE_LEN];	//Profile build message
  static const char *functionName = "buildProfile";

  asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW,
            "%s:%s: entry\n",
            driverName, functionName);
            
  //Call the base class method which will build the time array if needed
  asynMotorController::buildProfile();

  //Update profile build status
  strcpy(message, "");
  setStringParam(profileBuildMessage_, message);
  setIntegerParam(profileBuildState_, PROFILE_BUILD_BUSY);
  setIntegerParam(profileBuildStatus_, PROFILE_STATUS_UNDEFINED);
  callParamCallbacks();

  //Short delay showing status busy so user knows work actually happened
  epicsThreadSleep(.1);
  
  //Build profile data for use with linear interpolation mode
  status = buildLinearProfile();

  //Update profile build state
  setIntegerParam(profileBuildState_, PROFILE_BUILD_DONE);
  //Update profile build status
  if (status)
	setIntegerParam(profileBuildStatus_, PROFILE_STATUS_FAILURE);
  else
	setIntegerParam(profileBuildStatus_, PROFILE_STATUS_SUCCESS);
  callParamCallbacks();

  return asynSuccess;
}

/* Function to execute trajectory */ 
asynStatus GalilController::executeProfile()
{
  epicsEventSignal(profileExecuteEvent_);
  return asynSuccess;
}

/* C Function which runs the profile thread */ 
static void GalilProfileThreadC(void *pPvt)
{
  GalilController *pC = (GalilController*)pPvt;
  pC->profileThread();
}

/* Function which runs in its own thread to execute profiles */ 
void GalilController::profileThread()
{
  while (true) {
    epicsEventWait(profileExecuteEvent_);
    runProfile();
  }
}

asynStatus GalilController::abortProfile()
{
  int status;
  int moving;
  int coordsys;
  static const char *functionName = "abortProfile";

  //Retrieve currently selected coordinate system 
  getIntegerParam(GalilCoordSys_, &coordsys);
  //Coordsys moving status
  getIntegerParam(coordsys, GalilCoordSysMoving_, &moving);

  //Request the thread that buffers/executes the profile to abort the process
  profileAbort_ = true;

  //Stop the coordinate system if its moving
  if (moving)
	{
        //Stop the coordinate system  
	sprintf(cmd_, "ST %c", (coordsys == 0) ? 'S' : 'T');
	//Write setting to controller
	status = writeReadController(functionName);
        }
  return asynSuccess;
}

/* For profile moves.  Convenience function to move motors to start or stop them moving to start
*/
asynStatus GalilController::motorsToProfileStartPosition(FILE *profFile, char *axes, bool move = true)
{
  GalilAxis *pAxis;			//GalilAxis
  int j;				//Axis looping
  int axisNo;				//Axis number
  int moveMode[MAX_GALIL_AXES];  	//Move mode absolute or relative
  double startp[MAX_GALIL_AXES];	//Motor start positions from file
  double atime, velo, mres;		//Required mr attributes
  double velocity, acceleration;	//Used to move motors to start
  char message[MAX_MESSAGE_LEN];	//Profile execute message
  int status = asynSuccess;

  if (move)
	{
	//Update status
	strcpy(message, "Moving motors to start position and buffering profile data...");
	setStringParam(profileExecuteMessage_, message);
	callParamCallbacks();
	}

  //If mode absolute, send motors to start position or stop them moving to start position
  for (j = 0; j < (int)strlen(axes); j++)
  	{
	//Determine the axis number mentioned in profFile
	axisNo = axes[j] - AASCII;
	//Retrieve GalilProfileMoveMode_ from ParamList
	getIntegerParam(axisNo, GalilProfileMoveMode_, &moveMode[axisNo]);
	if (move) //Read profile start positions from file
		fscanf(profFile, "%lf,", &startp[axisNo]);
	//If moveMode = Absolute
	if (!moveMode[axisNo]) continue;
	//Retrieve axis instance
	pAxis = getAxis(axisNo);
	//Skip axis if not instantiated
	if (!pAxis) continue;
	//Retrieve needed mr attributes
	getDoubleParam(axisNo, GalilMotorAccl_, &atime);
	getDoubleParam(axisNo, GalilMotorVelo_, &velo);
	getDoubleParam(axisNo, motorResolution_, &mres);
	//Calculate velocity and acceleration in steps
	velocity = velo/mres;
	acceleration = velocity/atime;
	if (move) //Move to first position in profile if moveMode = Absolute
		status = pAxis->move(startp[axisNo], 0, 0, velocity, acceleration);
	else      //Stop motor moving to start
		status = pAxis->stop(acceleration);
	if (status)
		{
		//Store message in paramList
		strcpy(message, "Move motors to start failed...");
		setStringParam(profileExecuteMessage_, message);
		}
	}

  if (move)
	{
	//Move file pointer down a line
	fscanf(profFile, "\n");
	}

  return (asynStatus)status;
}

/* Convenience function to begin linear profile using specified coordinate system
   Called after filling the linear buffer
*/
asynStatus GalilController::startLinearProfileCoordsys(char coordName)
{
  static const char *functionName = "startProfileCoordsys";
  char message[MAX_MESSAGE_LEN];	//Profile execute message
  int status = asynSuccess;		//Start status
 
  //Start profile execution
  sprintf(cmd_, "BG %c \n", coordName);
  status |= writeReadController(functionName);
  if (status)
	{
	strcpy(message, "Profile start failed...");		//Update message
	//Store message in ParamList
	setStringParam(profileExecuteMessage_, message);
	}

  //start fail
  if (status)
	return asynError;	//Return error

  //Start success
  setIntegerParam(profileExecuteState_, PROFILE_EXECUTE_EXECUTING);
  strcpy(message, "Profile executing...");
  //Store message in ParamList
  setStringParam(profileExecuteMessage_, message);
  callParamCallbacks();
  //Return success
  return asynSuccess;
}

/* Function to run trajectory.  It runs in a dedicated thread, so it's OK to block.
 * It needs to lock and unlock when it accesses class data. */ 
asynStatus GalilController::runLinearProfile(FILE *profFile)
{
  static const char *functionName = "runLinearProfile";
  long maxAcceleration;			//Max acceleration for this controller
  int segsent;				//Segments loaded to controller so far
  char moves[MAX_MESSAGE_LEN];		//Segment move command assembled for controller
  char message[MAX_MESSAGE_LEN];	//Profile execute message
  char axes[MAX_GALIL_AXES];    	//Motors involved in profile move
  int coordsys;				//Coordinate system S(0) or T(1)
  int coordName;			//Coordinate system S or T
  bool profStarted = false;		//Has profile execution started
  int segprocessed;			//Segments processed by coordsys
  int moving;				//Moving status of coordinate system
  int retval;				//Return value from file read
  bool bufferNext = true;		//Controller ready to buffer next segment
  asynStatus status;			//Error status

  lock();

  //Retrieve currently selected coordinate system 
  getIntegerParam(GalilCoordSys_, &coordsys);

  //Selected coordinate system name
  coordName = (coordsys == 0 ) ? 'S' : 'T';

  //Clear any segments in the coordsys buffer
  sprintf(cmd_, "CS %c", coordName);
  writeReadController(functionName);

  //Set vector acceleration/decceleration
  maxAcceleration = 67107840;
  if (model_[0] == 'D' && model_[3] == '4')
	maxAcceleration = 1073740800;

  //Set vector acceleration/decceleration
  sprintf(cmd_, "VA%c=%ld;VD%c=%ld", coordName, maxAcceleration, coordName, maxAcceleration);
  writeReadController(functionName);

  //Read profFile and determine which motors are involved
  retval = fscanf(profFile, "%s\n", axes);
  //Update coordinate system motor list at record layer
  setStringParam(coordsys, GalilCoordSysMotors_, axes);

  //No segments sent to controller just yet
  segsent = 0;
  //Number of segments processed by coordsys
  segprocessed = 0;
  //Profile has not been aborted
  profileAbort_ = false;

  //Set linear interpolation mode and include motor list provided
  sprintf(cmd_, "LM %s", axes);
  writeReadController(functionName);

  //Move motors to start position
  status = motorsToProfileStartPosition(profFile, axes);

  unlock();

  //Execute the profile
  //Loop till file downloaded to buffer, or error, or abort
  while (retval != EOF && !status && !profileAbort_)
	{
	if (bufferNext)		//Read the segment
		retval = fscanf(profFile, "%s\n", moves);
	lock();
	//Process segment if didnt hit EOF
	if (retval != EOF)
		{
		//Set bufferNext true for this loop
		bufferNext = true;
		//Segments processed
		getIntegerParam(coordsys, GalilCoordSysSegments_, &segprocessed);
		//Coordsys moving status
		getIntegerParam(coordsys, GalilCoordSysMoving_, &moving);

		//Case where profile has started, but then stopped
		if (profStarted && !moving)
			{
			unlock();
			//Give other threads a chance to get the lock
			epicsThreadSleep(0.001);
			break;	//break from loop
			}

		//Check buffer, and abort status
		if ((segsent - segprocessed) >= MAX_SEGMENTS && !status && !profileAbort_)
			{
			//Segment buffer is full, and user has not pressed abort

			//Slow frequency of loop	
			//Give time for motors to arrive at start, or
			//Give time for controller to process a segment
			if ((motorsMoving(axes) && !profStarted) || profStarted)
				{
				unlock();
				epicsThreadSleep(.01);
				bufferNext = false;
				lock();
				}

			//Case where motors were moving to start position, and now complete, profile is not started.
			if (!profStarted && !motorsMoving(axes) && !status && !profileAbort_)
				{
				//Start the profile
				status = startLinearProfileCoordsys(coordName);
				profStarted = (status) ? false : true;
				//Pause until at least 1 segment has been processed
				if (!status)
					{
					unlock();
					while (segprocessed < 1 && !profileAbort_)
						{
						epicsThreadSleep(.01);
						//Segments processed
						getIntegerParam(coordsys, GalilCoordSysSegments_, &segprocessed);
						}
					bufferNext = true;
					lock();
					}
				}
			}

		//Buffer next segment if no error, user hasnt pressed abort, there is buffer space
		if (!status && !profileAbort_ && bufferNext)
			{
			//Proceed to send next segment to controller
			sprintf(cmd_, "LI %s\n", moves);
			status = writeReadController(functionName);
			if (status)
				{
				strcpy(message, "Error downloading segment");
				setStringParam(profileExecuteMessage_, message);
				}
			else	//Increment segments sent to controller
				segsent++;
			}

		//Ensure segs are being sent faster than can be processed by controller
                if (((segsent - segprocessed) <= 5) && profStarted && !status && !profileAbort_)
			{
			strcpy(message, "Profile time base too fast\n");
			setStringParam(profileExecuteMessage_, message);
			//break loop
			status = asynError;
			}
		}

	unlock();
	//Give other threads a chance to get the lock
	epicsThreadSleep(0.001);
	}

  lock();

  //All segments have been sent to controller
  //End linear interpolation mode
  strcpy(cmd_, "LE");
  writeReadController(functionName);

  //Check if motors still moving to start position
  if (!profStarted && motorsMoving(axes) && !status && !profileAbort_)
	{
	//Pause till motors stop
	while (motorsMoving(axes))
		{
		unlock();
		epicsThreadSleep(.01);
		lock();
		}
	}

  //Start short profiles that fit entirely in the controller buffer <= MAX_SEGMENTS
  if (!profStarted && !motorsMoving(axes) && !status && !profileAbort_)
  	{
	status = startLinearProfileCoordsys(coordName);
	profStarted = (status) ? false : true;
	//Pause until at least 1 segment has been processed
	if (!status)
		{
		while (segprocessed < 1)
			{
			unlock();
			epicsThreadSleep(.01);
			lock();
			//Segments processed
			getIntegerParam(coordsys, GalilCoordSysSegments_, &segprocessed);
			}
		}
	}

  //User aborted while motors still moving to start
  if (profileAbort_ && !profStarted && motorsMoving(axes))
  	status = motorsToProfileStartPosition(profFile, axes, false);  //Stop motors moving to start position

  //Finish up
  if (!status)
	{
	if (profStarted)
		{
		moving = 1;
		//Loop until coordinate system stops
		while (moving)
			{
			getIntegerParam(coordsys, GalilCoordSysMoving_, &moving);
			//Restrict loop frequency
			unlock();
			epicsThreadSleep(.01);
			lock();
			}
		}

	//Segments processed
	getIntegerParam(coordsys, GalilCoordSysSegments_, &segprocessed);

	//Were all segments processed by controller
	if (segprocessed == segsent)
		{
		setIntegerParam(profileExecuteStatus_, PROFILE_STATUS_SUCCESS);
		strcpy(message, "Profile completed successfully");
		setStringParam(profileExecuteMessage_, message);
		}
	else
		{
		//Not all segments were processed
		setIntegerParam(profileExecuteStatus_, PROFILE_STATUS_FAILURE);
		if (profileAbort_)
			strcpy(message, "Profile was stopped by user");
		else
			strcpy(message, "Profile was stopped by limit switch or other motor/encoder problem");
		setStringParam(profileExecuteMessage_, message);
		}
	}
  else  //Coordinate system didnt start, or aborted by user
	setIntegerParam(profileExecuteStatus_, PROFILE_STATUS_FAILURE);

  //Update status
  setIntegerParam(profileExecuteState_, PROFILE_EXECUTE_DONE);
  callParamCallbacks();
  unlock();

  return asynSuccess;
}

/* Function to run trajectory.  It runs in a dedicated thread, so it's OK to block.
 * It needs to lock and unlock when it accesses class data. */ 
asynStatus GalilController::runProfile()
{
  int status = asynError;		//Execute status
  char fileName[MAX_FILENAME_LEN];	//Filename to read profile data from
  char profType[MAX_MESSAGE_LEN];	//Segment move command assembled for controller
  char message[MAX_MESSAGE_LEN];	//Profile run message
  FILE *profFile;			//File handle for above file

  //Retrieve required attributes from ParamList
  getStringParam(GalilProfileFile_, (int)sizeof(fileName), fileName);

  //Update execute profile status
  setIntegerParam(profileExecuteState_, PROFILE_EXECUTE_MOVE_START);
  setIntegerParam(profileExecuteStatus_, PROFILE_STATUS_UNDEFINED);
  callParamCallbacks();
    
  //Open the profile file
  profFile =  fopen(fileName, "r");

  if (profFile != NULL)
	{
	//Read file header
	//LINEAR or PVT type
	fscanf(profFile, "%s\n", profType);
	//Call appropriate method to handle this profile type
	if (strcmp(profType, "LINEAR")==0)
		status = runLinearProfile(profFile);
	//Done, close the file
	fclose(profFile);
	}
  else
	{
	strcpy(message, "Can't open trajectory file\n");
	setStringParam(profileExecuteMessage_, message);
	setIntegerParam(profileExecuteStatus_, PROFILE_STATUS_FAILURE);
	setIntegerParam(profileExecuteState_, PROFILE_EXECUTE_DONE);
	callParamCallbacks();
	}

  return (asynStatus)status;
}

/**
 * Perform a deferred move (a coordinated group move) on all the axes in a group.
 * @param coordsys - Coordinate system to use
 * @param axes - The list of axis/motors in the coordinate system (eg. "ABCD")
 * @param moves - The comma separated list of the relative moves for each axis/motor in the coordinate system (eg 1000,,-1000,1000)
 * @param acceleration - lowest acceleration amongst all motors in the specified coordsys
 * @param velocity - lowest velocity amongst all motors in the specified coordsys
 * @return motor driver status code.
 */
asynStatus GalilController::processDeferredMovesInGroup(int coordsys, char *axes, char *moves, double acceleration, double velocity)
{
  const char *functionName = "GalilController::processDeferredMovesInGroup";
  GalilAxis *pAxis;		//GalilAxis pointer
  char coordName;		//Coordinate system name
  asynStatus status;		//Result
  unsigned index;		//looping

  //Selected coordinate system name
  coordName = (coordsys == 0 ) ? 'S' : 'T';

  //Clear any segments in the coordsys buffer
  sprintf(cmd_, "CS %c", coordName);
  writeReadController(functionName);

  //Update coordinate system motor list at record layer
  setStringParam(coordsys, GalilCoordSysMotors_, axes);

  //Set linear interpolation mode and include motor list provided
  sprintf(cmd_, "LM %s", axes);
  writeReadController(functionName);

  //Set vector acceleration/decceleration
  sprintf(cmd_, "VA%c=%.0lf;VD%c=%.0lf", coordName, acceleration, coordName, acceleration);
  writeReadController(functionName);

  //Set vector velocity
  sprintf(cmd_, "VS%c=%.0lf", coordName, velocity);
  writeReadController(functionName);
 
  //Specify 1 segment
  sprintf(cmd_, "LI %s", moves);
  writeReadController(functionName);

  //End linear mode
  sprintf(cmd_, "LE");
  writeReadController(functionName);

  //move the coordinate system
  sprintf(cmd_, "BG %c", coordName);
  status = writeReadController(functionName);

  //Loop through the axes list for this coordinate system
  //Turn off deferredMove_ for these axis as coordinated move has been started
  for (index = 0; index < strlen(axes); index++)
	{
	//Retrieve axis specified in axes list
	pAxis = getAxis(axes[index] - AASCII);
	//Set flag
	pAxis->deferredMove_ = false;
	}

  return status;
}

/**
 * Process deferred moves for a controller and groups.
 * This function calculates which unique groups in the controller
 * and passes the controller pointer and group name to processDeferredMovesInGroup.
 * @return motor driver status code.
 */
asynStatus GalilController::setDeferredMoves(bool deferMoves)
{
  //const char *functionName = "GalilController::setDeferredMoves";
  GalilAxis *pAxis;			//GalilAxis pointer
  asynStatus status = asynError;	//Result
  int coordsys;				//Coordinate system looping
  unsigned axis;			//Axis looping
  char axes[MAX_GALIL_AXES];		//Constructed list of axis in the coordinate system
  char moves[MAX_MESSAGE_LEN];		//Constructed comma list of axis relative moves
  double vectorAcceleration;		//Coordinate system acceleration
  double vectorVelocity;		//Coordinate system velocity

  // If we are not ending deferred moves then return
  if (deferMoves || !movesDeferred_)
	{
	movesDeferred_ = true;
	return asynSuccess;
  	}

  //callParamCallbacks();

  //We are ending deferred moves.  So process them
  asynPrint(this->pasynUserSelf, ASYN_TRACE_FLOW, 
              "Processing deferred moves on Galil: %s\n", this->portName);

  //Loop through coordinate systems, looking for work to perform
  for (coordsys = 0; coordsys < COORDINATE_SYSTEMS; coordsys++) 
	{
	//No work found yet in this coordsys
	strcpy(axes, "");
	strcpy(moves, "");
	//velocity for this segment
	vectorVelocity = 0.0;
	vectorAcceleration = 0.0;
	//Loop through the axis looking for deferredMoves in this coordsys
	for (axis = 0; axis < MAX_GALIL_AXES; axis++)
		{
		pAxis = getAxis(axis);
		if (pAxis)
			if (pAxis->deferredCoordsys_ == coordsys && pAxis->deferredMove_)
				{
				//Deferred move found
				//Store axis in coordinate system axes list
				sprintf(axes, "%s%c", axes, pAxis->axisName_);
				//Store axis relative move
				sprintf(moves, "%s%.0lf", moves, pAxis->deferredPosition_);
				//Add this motors' contribution to vector acceleration for this segment
				vectorAcceleration += pow(pAxis->deferredAcceleration_, 2);
				//Add this motors' contribution to vector velocity for this segment
				vectorVelocity += pow(pAxis->deferredVelocity_, 2);
				}
		if (axis < MAX_GALIL_AXES - 1)
			{
			//Add axis relative move separator character ',' as needed
			sprintf(moves,  "%s,", moves);
			}
		}
	//If at least one axis was found with a deferred move in this coordinate system
	//then process coordsys
	if (strcmp(axes, ""))
		{
		//Calculate final vectorVelocity and vectorAcceleration
		vectorVelocity = sqrt(vectorVelocity);
		vectorAcceleration = sqrt(vectorAcceleration);
		vectorVelocity = lrint(vectorVelocity/2.0) * 2;
		vectorAcceleration = lrint(vectorAcceleration/1024.0) * 1024;
		//Start the move
		status = processDeferredMovesInGroup(coordsys, axes, moves, vectorAcceleration, vectorVelocity);
		}
	}

  //Deferred moves have been started
  movesDeferred_ = false;
  
  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

/** Sets GalilCommunicationError_ param when comms problem occurs
  * \param[in] reqd_comms - did function that set status require comms
  * \param[in] status - asynStatus set by function  */
void GalilController::check_comms(bool reqd_comms, asynStatus status)
{
   int b4_status;
   //Set comms flag only if function reqd comms
   if (reqd_comms)
        {
        //Retrieve current GalilCommunicationError_
	getIntegerParam(GalilCommunicationError_, &b4_status);
	//Write to GalilCommunicationError_ parameter only on change
        //Note GalilCommunicationError_ record is boolean
	if (b4_status != (bool)status)
		{
		setIntegerParam(GalilCommunicationError_, status ? 1:0);
		/* Do callbacks so higher layers see any changes */
	        callParamCallbacks();
		}
	}
}

/** Attempts to read value from controller, returns last value set if fails.  
  ** Called by GaLilController::readInt32()
  * \param[in] cmd to send to controller
  * \param[out] value Address of the value to read. 
  * \param[in] axisNo is asyn Param list number 0 - 7.  Controller wide values use list 0 */
asynStatus GalilController::get_integer(int function, epicsInt32 *value, int axisNo = 0)
{
	const char *functionName = "GalilController::get_integer";
	asynStatus status;				 //Communication status.
	
	if ((status = writeReadController(functionName)) == asynSuccess)
		*value = (epicsInt32)atoi(resp_);
	else    //Comms error, return last ParamList value set using setIntegerParam
		getIntegerParam(axisNo, function, value);
	return status;
}

/** Called when asyn clients call pasynInt32->read().
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[out] value Address of the value to read. */
asynStatus GalilController::readInt32(asynUser *pasynUser, epicsInt32 *value)
{
    int function = pasynUser->reason;		 //function requested
    asynStatus status;				 //Used to work out communication_error_ status.  asynSuccess always returned
    GalilAxis *pAxis = getAxis(pasynUser);	 //Retrieve the axis instance
    const char *functionName = "GalilController::readInt32";
    bool reqd_comms;				 //Check for comms error only when function reqd comms

    //If provided addr does not return an GalilAxis instance, then return asynError
    if (!pAxis) return asynError;

    //We dont retrieve values for records at iocInit.  
    //For output records autosave, or db defaults are pushed to hardware instead
    if (!readAllowed_) return asynError;

    //Most functions require comms
    reqd_comms = true;
    
    if (function == GalilHomeType_)
        {
	//Read home type from controller
        strcpy(cmd_, "MG _CN1");
	status = get_integer(GalilHomeType_, value);
	if (!status)
		*value = (*value > 0) ? 1 : 0;
	}
    else if (function == GalilLimitType_)
	{
	//Read limit type from controller
	strcpy(cmd_, "MG _CN0");
	status = get_integer(GalilLimitType_, value);
	if (!status)
		*value = (*value > 0) ? 1 : 0;
	}
   else if (function == GalilMainEncoder_ || function == GalilAuxEncoder_)
	{
        unsigned setting;
	int main, aux;
	
	sprintf(cmd_ , "CE%c=?", pAxis->axisName_);
	if ((status = writeReadController(functionName)) == asynSuccess)
		{
		setting = (unsigned)atoi(resp_);
		//Separate setting into main and aux
		main = setting & 3;
		aux = setting & 12;
		*value = (function == GalilMainEncoder_) ? main : aux;
		}
	else
		{
		//Comms error, return last ParamList value set using setIntegerParam
		if (function == GalilMainEncoder_)
			{
			getIntegerParam(pAxis->axisNo_, GalilMainEncoder_, &main);
			*value = main;
			}
		else
			{
			getIntegerParam(pAxis->axisNo_, GalilAuxEncoder_, &aux);
			*value = aux;
			}
		}
	}
   else if (function == GalilMotorType_)
	{
	float motorType;
	sprintf(cmd_, "MG _MT%c", pAxis->axisName_);
	if ((status = writeReadController(functionName)) == asynSuccess)
		{
		motorType = (epicsInt32)atof(resp_);
		//Upscale by factor 10 to create integer representing motor type 
		*value = (int)(motorType * 10.0);
		//Translate motor type into 0-5 value for mbbi record
		switch (*value)
			{
			case 10:   *value = 0;
				   break;
			case -10:  *value = 1;
				   break;
			case -20:  *value = 2;
				   break;
			case 20:   *value = 3;
				   break;
			case -25:  *value = 4;
				   break;
			case 25:   *value = 5;
				   break;
			default:   break;
			}
		}
	else    //Comms error, return last ParamList value set using setIntegerParam
		getIntegerParam(pAxis->axisNo_, function, value);
	}
   else if (function == GalilProgramHome_)
	{
	sprintf(cmd_, "MG phreg%c", pAxis->axisName_);
	status = get_integer(GalilProgramHome_, value, pAxis->axisNo_);
	}
  else if (function == GalilHomed_)
	{
	sprintf(cmd_, "MG homed%c", pAxis->axisName_);
	status = get_integer(GalilHomed_, value, pAxis->axisNo_);
	//Set motorRecord MSTA bit 15 motorStatusHomed_ too
	//Homed is not part of Galil data record, we support it in galil code instead and must poll it
	//We do it here so we don't slow GalilPoller thread in async mode
	//We must use asynMotorAxis version of setIntegerParam to set MSTA bits for this MotorAxis
	pAxis->setIntegerParam(motorStatusHomed_, (int)*value);
	callParamCallbacks();
	}
  else if (function >= GalilSSIInput_ && function <= GalilSSIData_)
	{
	int ssicapable;	//Local copy of GalilSSICapable_
	//Retrieve GalilSSICapable_ param
	getIntegerParam(GalilSSICapable_, &ssicapable);
	if (ssicapable)
		status = pAxis->get_ssi(function, value);
	else
		{
		status = asynSuccess;
		reqd_comms = false;
		}
	}
  else if (function == GalilCoordSys_)
	{
	//Read active coordinate system
	sprintf(cmd_, "MG _CA");
	status = get_integer(GalilCoordSys_, value);
	if (!status)
		*value = (*value > 0) ? 1 : 0;
        //Set any external changes in coordsys in paramList
	setIntegerParam(0, GalilCoordSys_, *value);
	}
   else 
	{
	status = asynPortDriver::readInt32(pasynUser, value);
	reqd_comms = false;
	}

   //Flag comms error only if function reqd comms
   check_comms(reqd_comms, status);

   //Always return success. Dont need more error mesgs
   return asynSuccess;	
}

/** Attempts to read value from controller, returns last good or default if fails.  
  ** Called by GaLilController::readFloat64()
  * \param[in] cmd to send to controller
  * \param[in] asyn Param function number
  * \param[out] value Address of the value to read. 
  * \param[in] axisNo is asyn Param list number 0 - 7.  Controller wide values use list 0 */
asynStatus GalilController::get_double(int function, epicsFloat64 *value, int axisNo = 0)
{
	const char *functionName = "GalilController::get_double";
	asynStatus status;				 //Communication status.

	if ((status = writeReadController(functionName)) == asynSuccess)
		*value = (epicsFloat64)atof(resp_);
	else    //Comms error, return last ParamList value set using setDoubleParam
		getDoubleParam(axisNo, function, value);
	return status;
}

/** Called when asyn clients call pasynFloat64->read().
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Address of the value to read. */
asynStatus GalilController::readFloat64(asynUser *pasynUser, epicsFloat64 *value)
{
    int function = pasynUser->reason;		 //function requested
    asynStatus status;				 //Used to work out communication_error_ status.  asynSuccess always returned
    GalilAxis *pAxis = getAxis(pasynUser);	 //Retrieve the axis instance
    bool reqd_comms;				 //Check for comms error only when function reqd comms

    if (!pAxis) return asynError;

    //We dont retrieve values for records at iocInit.  
    //For output records autosave, or db defaults are pushed to hardware instead
    if (!readAllowed_) return asynError;

    //Most functions require comms
    reqd_comms = true;
    
    if (function == GalilStepSmooth_)
	{
	sprintf(cmd_, "MG _KS%c", pAxis->axisName_);
	status = get_double(GalilStepSmooth_, value, pAxis->axisNo_);
	}
    else if (function == GalilErrorLimit_)
	{
	sprintf(cmd_, "MG _ER%c", pAxis->axisName_);
	status = get_double(GalilErrorLimit_, value, pAxis->axisNo_);
	}
    else
	{
	status = asynPortDriver::readFloat64(pasynUser, value);
	reqd_comms = false;
	}
    
    //Flag comms error only if function reqd comms
    check_comms(reqd_comms, status);
  
    //Always return success. Dont need more error mesgs
    return asynSuccess;	
}

/** Called when asyn clients call pasynUInt32Digital->write().
  * \param[in] pasynUser pasynUser structure that encodes the reason and address.
  * \param[in] value Value to write.
  * \param[in] mask Mask value to use when writinging the value. */
asynStatus GalilController::writeUInt32Digital(asynUser *pasynUser, epicsUInt32 value, epicsUInt32 mask)
{
    int function = pasynUser->reason;
    int addr=0;
    asynStatus status = asynSuccess;
    const char* functionName = "writeUInt32Digital";
    int obwpa;			//How many output bits are supported per addr at the record layer for this controller type
    epicsUInt32 maxmask;	//Maximum binary out mask supported at record layer for this controller
    int i;

    //Retrieve record address.  Which is byte, or word here.
    status = getAddress(pasynUser, &addr); if (status != asynSuccess) return(status);

    /* Set the parameter in the parameter library. */
    status = (asynStatus) setUIntDigitalParam(addr, function, value, mask);

    if (function == GalilBinaryOut_)
  	{
	//Ensure record mask is within range
	maxmask = strncmp(model_,"RIO",3) != 0 ? 0x8000 : 0x80;
	if (mask > maxmask)
		{
		printf("%s model %s mask too high @ > %x addr %d mask %x\n", functionName, model_, maxmask, addr, mask);
		return asynSuccess;
		}
		
	//Determine bit i from mask
	for (i=0;i<16;i++)
		{
		if (pow(2,i) == mask)
			{
			//Bit numbering is different on DMC compared to RIO controllers
			if (strncmp(model_,"RIO",3) != 0)
				{
				obwpa = 16;	//Binary out records for DMC support 16 bits per addr
				i++;		//First bit on motor controllers is bit 1
				}
			else
				obwpa = 8; 	//Binary out records for RIO support 8 bits per addr
			//Set or clear bit as required
			if (value == mask)
				sprintf(cmd_, "SB %d\n", (addr * obwpa) + i);
			else
				sprintf(cmd_, "CB %d\n", (addr * obwpa) + i);
			//Write setting to controller
			writeReadController(functionName);
			//We found the correct bit, so break from loop
			break;
			}
		}
	}

    //Always return success. Dont need more error mesgs
    return asynSuccess;
}

/** Called when asyn clients call pasynInt32->write().
  * Extracts the function and axis number from pasynUser.
  * Sets the value in the parameter library.
  * If the function is motorSetClosedLoop_ then it turns the drive power on or off.
  * For all other functions it calls asynMotorController::writeInt32.
  * Calls any registered callbacks for this pasynUser->reason and address.  
  * \param[in] pasynUser asynUser structure that encodes the reason and address.
  * \param[in] value     Value to write. */
asynStatus GalilController::writeInt32(asynUser *pasynUser, epicsInt32 value)
{
  const char *functionName = "GalilController::writeInt32";
  int function = pasynUser->reason;		//Function requested
  int addr=0;					//Address requested
  asynStatus status;				//Used to work out communication_error_ status.  asynSuccess always returned
  GalilAxis *pAxis = getAxis(pasynUser);	//Retrieve the axis instance
  int hometype, limittype;			//The home, and limit switch type
  int mainencoder, auxencoder, encoder_setting; //Main, aux encoder setting
  bool reqd_comms;				//Check for comms error only when function reqd comms
  char coordinate_system;			//Coordinate system S or T
  char axes[MAX_GALIL_AXES];			//Coordinate system axis list
  double eres, mres;				//mr eres, and mres
  int motor;					//Motor type.  May use Galil numbering, or database numbering, so be aware
  unsigned i;					//Looping

  status = getAddress(pasynUser, &addr); 
  if (status != asynSuccess) return(status);

  if (!pAxis) return asynError;

  //Most functions require comms
  reqd_comms = true;
   
  /* Set the parameter and readback in the parameter library.  This may be overwritten when we read back the
   * status at the end, but that's OK */
  status = setIntegerParam(pAxis->axisNo_, function, value);

  if (function == GalilHomeType_ || function == GalilLimitType_)
  	{
	//Retrieve the limit type and home type
	//Must ensure getIntegerParam successful, as some parameters may not be set yet due to record default mechanism
	if ((getIntegerParam(GalilLimitType_, &limittype) == asynSuccess) && (getIntegerParam(GalilHomeType_, &hometype) == asynSuccess))
		{
		//Convert Param syntax of these params to controller syntax
		limittype = (limittype > 0) ? 1 : -1;
		hometype = (hometype > 0) ? 1 : -1;
		//Assemble cmd string 
		sprintf(cmd_, "CN %d,%d,-1,0,1", limittype, hometype);
		//printf("GalilLimitType cmd:%s\n", cmd_);
		//Write setting to controller
		status = writeReadController(functionName);
		}
	else
		reqd_comms = false;
  	}
  else if (function == GalilAuxEncoder_ || function == GalilMainEncoder_)	
	{
	//Retrieve main and aux encoder setting
	//Must ensure getIntegerParam successful, as some parameters may not be set yet due to record default mechanism
	if ((getIntegerParam(pAxis->axisNo_, GalilMainEncoder_, &mainencoder) == asynSuccess) && (getIntegerParam(pAxis->axisNo_, GalilAuxEncoder_, &auxencoder) == asynSuccess))
		{
		//Assemble cmd string 
		encoder_setting = mainencoder + auxencoder;
		sprintf(cmd_, "CE%c=%d", pAxis->axisName_, encoder_setting);
		//printf("GalilMainEncoder cmd:%s value=%d\n", cmd_, value);
		//Write setting to controller
		status = writeReadController(functionName);
		}
	else
		reqd_comms = false;
	}
  else if (function == GalilMotorOn_)
	{
	if (value)
		status = pAxis->setClosedLoop(true);
	else
		status = pAxis->setClosedLoop(false);
	//printf("GalilMotorOn_ cmd:%s value=%d\n", cmd_, value);
	}
  else if (function == GalilMotorType_)
	{
	char buf[10];

	//Query motor type before changing setting
        sprintf(cmd_, "MT%c=?", pAxis->axisName_);
        writeReadController(functionName);
        motor = atof(resp_);

	//Assemble command to change motor type
	switch (value)
		{
		case 0: strcpy(buf, "1");
			break;
		case 1: strcpy(buf, "-1");
			break;
		case 2: strcpy(buf, "-2");
			break;
		case 3: strcpy(buf, "2");
			break;
		case 4: strcpy(buf, "-2.5");
			break;
		case 5: strcpy(buf, "2.5");
			break;
		default:break;
		}

	//Change motor type
	sprintf(cmd_, "MT%c=%s", pAxis->axisName_, buf);
        //printf("GalilMotorType_ cmd:%s value %d\n", cmd_, value);
	//Write setting to controller
	status = writeReadController(functionName);

	//IF motor was servo, and now stepper
        //Galil hardware MAY push main encoder to aux encoder (stepper count reg)
	//We re-do this, but apply encoder/motor scaling
        if (abs(motor) == 1 && value > 1)
		{
		getDoubleParam(pAxis->axisNo_, GalilEncoderResolution_, &eres);
		getDoubleParam(pAxis->axisNo_, motorResolution_, &mres);
		//Calculate step count from existing encoder_position, construct mesg to controller_
		sprintf(cmd_, "DP%c=%.0lf", pAxis->axisName_, pAxis->encoder_position_ * (eres/mres));
		//Write setting to controller
		status = writeReadController(functionName);
		}

	//IF motor was stepper, and now servo
	//Set reference position equal to main encoder, which sets initial error to 0
	if (abs(motor) > 1 && value < 2)
		{
		//Calculate step count from existing encoder_position, construct mesg to controller_
		sprintf(cmd_, "DP%c=%.0lf", pAxis->axisName_, pAxis->encoder_position_);
		//Write setting to controller
		status = writeReadController(functionName);
		}
	}
  else if (function == GalilProgramHome_)
	{
	sprintf(cmd_, "phreg%c=%d", pAxis->axisName_, value);
	//printf("GalilProgramHome_ cmd:%s value %d\n", cmd_, value);
	//Write setting to controller
	status = writeReadController(functionName);
	}
  else if (function == GalilUseEncoder_)
	{
	sprintf(cmd_, "ueip%c=%d", pAxis->axisName_, value);
        //printf("GalilUseEncoder_ %s value %d\n", cmd_, value);
	//Write setting to controller
	status = writeReadController(functionName);
	}
  else if (function >= GalilSSIInput_ && function <= GalilSSIData_)
	{
	int ssicapable;	//Local copy of GalilSSICapable_
	//Retrieve GalilSSICapable_ from ParamList
	getIntegerParam(pAxis->axisNo_, GalilSSICapable_, &ssicapable);
	
	//Only if controller is SSI capable
	if (ssicapable)
		status = pAxis->set_ssi();
	else
		reqd_comms = false;
	}
  else if (function == GalilOffOnError_)
	{
	sprintf(cmd_, "OE%c=%d", pAxis->axisName_, value);
	//printf("GalilOffOnError_ cmd:%s value %d\n", cmd, value);
	//Write setting to controller
	status = writeReadController(functionName);
	}
  else if (function == GalilCoordSys_)
	{
	coordinate_system = (value == 0) ? 'S' : 'T';
	sprintf(cmd_, "CA %c", coordinate_system);
	//printf("GalilCoordSys_ cmd:%s value %d\n", cmd, value);
	//Write setting to controller
	status = writeReadController(functionName);
	}
  else if (function == GalilCoordSysMotorsStop_ || function == GalilCoordSysMotorsGo_)
	{
	//Decide Stop or Go function
	int motor_spmg = (function == GalilCoordSysMotorsStop_) ? 0 : 3;
	//If Stop function, then stop coordsys
        if (function == GalilCoordSysMotorsStop_)
		{
		sprintf(cmd_, "ST %c", (addr == 0) ? 'S' : 'T');
		//Write setting to controller
		status = writeReadController(functionName);
		}
	//Retrieve coordSys axes list
        getStringParam(addr, GalilCoordSysMotors_, MAX_GALIL_AXES, axes);
	//Stop/Go all motors in the list
	//This is done to stop motor backlash correction after coordsys stop
	for (i=0;i<strlen(axes);i++)
		{
		//Stop/go the motor
		setIntegerParam(axes[i] - AASCII, GalilMotorStopGo_, motor_spmg);
		}
	callParamCallbacks();
	}
  else if (function == GalilOutputCompareAxis_)
	{
	status = setOutputCompare(addr);
	}
  else 
  	{
    	/* Call base class method */
    	status = asynMotorController::writeInt32(pasynUser, value);
	reqd_comms = false;
  	}

  //Flag comms error only if function reqd comms
  check_comms(reqd_comms, status);

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

/** Called when asyn clients call pasynFloat64->write().
  * Extracts the function and axis number from pasynUser.
  * Sets the value in the parameter library.
  * Calls any registered callbacks for this pasynUser->reason and address.  
  * For all other functions it calls asynMotorController::writeFloat64.
  * \param[in] pasynUser asynUser structure that encodes the reason and address.
  * \param[in] value Value to write. */
asynStatus GalilController::writeFloat64(asynUser *pasynUser, epicsFloat64 value)
{
  int function = pasynUser->reason;		//Function requested
  asynStatus status;				//Used to work out communication_error_ status.  asynSuccess always returned
  GalilAxis *pAxis = getAxis(pasynUser);	//Retrieve the axis instance
  const char *functionName = "GalilController::writeFloat64";
  bool reqd_comms;				//Check for comms error only when function reqd comms
  int addr=0;					//Address requested

  //Retrieve address.  Used for analog IO
  status = getAddress(pasynUser, &addr); 
  if (status != asynSuccess) return(status);

  //Most functions require comms
  reqd_comms = true;

  /* Set the parameter and readback in the parameter library. */
  if (pAxis)
  	status = setDoubleParam(pAxis->axisNo_, function, value);	//DMC (digital motor controller) axis
  else
	status = setDoubleParam(addr, function, value);			//Rio analog output when no axis are defined

  if (function == GalilStepSmooth_)
        {
	if (pAxis)
		{
		//Write new stepper smoothing factor to GalilController
		sprintf(cmd_, "KS%c=%lf",pAxis->axisName_, value);
		//printf("GalilStepSmooth_ cmd:%s value %lf\n", cmd, value);
		status = writeReadController(functionName);
		}
	}
  else if (function == GalilErrorLimit_)
	{
	if (pAxis)
		{
		//Write new error limit to GalilController
		sprintf(cmd_, "ER%c=%lf",pAxis->axisName_, value);
		//printf("GalilErrorLimit_ cmd:%s value %lf\n", cmd, value);
		status = writeReadController(functionName);
		}
	}
  else if (function == GalilAnalogOut_)
	{
	//Write new analog value to specified output (addr)
	sprintf(cmd_, "AO %d, %f", addr, value);
	//printf("GalilAnalogOut_ cmd:%s value %lf\n", cmd, value);
	status = writeReadController(functionName);
	}
  else if (function == GalilOutputCompareStart_ || function == GalilOutputCompareIncr_)
	{
	status = setOutputCompare(addr);
	}
  else
	{
        /* Call base class method */
	status = asynMotorController::writeFloat64(pasynUser, value);
	reqd_comms = false;
	}
    
  //Flag comms error only if function reqd comms
  check_comms(reqd_comms, status);

  //Always return success. Dont need more error mesgs
  return asynSuccess;
}

//Extract controller data from GalilController data record
//Return status of GalilController data record acquisition
void GalilController::getStatus(void)
{
   const char *functionName="GalilController::getStatus";
   char src[MAX_GALIL_STRING_SIZE]="\0";    //data source to retrieve
   int addr;				    //addr or byte of binary IO
   int coordsys;			    //Coordinate system currently selected
   int profstate;			    //Profile running state
   double paramDouble;			    //For passing asynFloat64 to ParamList
   unsigned paramUDig;		    	    //For passing UInt32Digital to ParamList
  
   //If data record query success in GalilController::acquireDataRecord
   if (recstatus_ == asynSuccess)
	{
	try	{
		//extract relevant controller data from GalilController record, store in GalilController
		//If connected, then proceed
		if (gco_ != NULL)
			{
			//digital inputs in banks of 8 bits
			for (addr=0;addr<BINARYIN_BYTES;addr++)
				{
				sprintf(src, "_TI%d", addr);
				paramUDig = (unsigned)gco_->sourceValue(recdata_, src);
				//ValueMask = 0xFF because a byte is 8 bits
				setUIntDigitalParam(addr, GalilBinaryIn_, paramUDig, 0xFF );
				}
			//data record has digital outputs in banks of 16 bits for dmc, 8 bits for rio
			for (addr=0;addr<BINARYOUT_WORDS;addr++)
				{
				sprintf(src, "_OP%d", addr);
				paramUDig = (unsigned)gco_->sourceValue(recdata_, src);
				//ValueMask = 0xFFFF because a word is 16 bits
				setUIntDigitalParam(addr, GalilBinaryOutRBV_, paramUDig, 0xFFFF );
				}
			//Analog ports on rio
			for (addr=0;addr<ANALOG_PORTS;addr++)
				{
				//Analog inputs
				sprintf(src, "@AN[%d]", addr);
				paramDouble = (double)gco_->sourceValue(recdata_, src);
				setDoubleParam(addr, GalilAnalogIn_, paramDouble);
				//Analog outputs
				sprintf(src, "@AO[%d]", addr);
				paramDouble = (double)gco_->sourceValue(recdata_, src);
				setDoubleParam(addr, GalilAnalogOutRBV_, paramDouble);
				}

			//Retrieve currently selected coordinate system 
			getIntegerParam(GalilCoordSys_, &coordsys);

                        //Retrieve profile execute status
  			getIntegerParam(profileExecuteState_, &profstate);

                        //Coordinate system status
			for (addr=0;addr<COORDINATE_SYSTEMS;addr++)
				{
				//Move/done status
				sprintf(src, "_BG%c", (addr) ? 'T' : 'S');
				setIntegerParam(addr, GalilCoordSysMoving_, (int)gco_->sourceValue(recdata_, src));

				//Segment count
				sprintf(src, "_CS%c", (addr) ? 'T' : 'S');
				setIntegerParam(addr, GalilCoordSysSegments_, (int)gco_->sourceValue(recdata_, src));

				//Update profile current point in ParamList
				if ((addr == coordsys) && (profstate))
					{
					//Update profile current point in ParamList
					setIntegerParam(0, profileCurrentPoint_, (int)gco_->sourceValue(recdata_, src));
					}
				}
			}
		}
	catch (string e) 
		{
		//Print exception mesg
		cout << functionName << ":" << e;
		}
	//Forgiveness is cheap
	//Allows us to poll without lock
	catch (const std::bad_typeid& e)
		{
		cout << "Caught bad_typeid GalilController::getStatus" << endl;
		}
	}
}

//Override asynMotorController::poll
//Acquire a data record from controller, store in GalilController instance
//Called by GalilPoller::run
asynStatus GalilController::poll(void)
{
	double time_taken;

	//Read current time
	epicsTimeGetCurrent(&pollnowt_);
	//Calculate cycle time
	time_taken = epicsTimeDiffInSeconds(&pollnowt_, &polllastt_);
	//Store current time for next cycle
	polllastt_.secPastEpoch = pollnowt_.secPastEpoch;
	polllastt_.nsec = pollnowt_.nsec;
	
	//if (time_taken > 0.01)
	//	{
	//	printf("%s GalilController::poll %2.3lfs\n", model_, time_taken);
	//	}
	//

	//Acquire a data record
	if (async_records_)
		acquireDataRecord("DR");  //Asynchronous, record already in ram so get a copy of it
	else
		acquireDataRecord("QR");  //Synchronous, poll controller for record

	//Extract controller data from data record, store in GalilController, and ParamList
	getStatus();

	//Return value is not monitored by asynMotorController
	return asynSuccess;
}

//Acquire data record from controller
asynStatus GalilController::acquireDataRecord(string cmd)
{
	const char *functionName="acquireDataRecord";
	epicsTimeStamp endt_;		//Used for debugging, and tracking overall performance
	epicsTimeStamp startt_;		//Used for debugging, and tracking overall performance
	double time_taken;		//Used for debugging, and tracking overall performance

	try {
	    //If connected, then ask controller for a data record
	    if (gco_ != NULL)
	        {
		//Get acquisition start time
		epicsTimeGetCurrent(&startt_);
		//Get the data record
		recdata_ = gco_->record(cmd);
 		//Get acquisition end time
		epicsTimeGetCurrent(&endt_);
		//Calculate acquistion time
		time_taken = epicsTimeDiffInSeconds(&endt_, &startt_);
	
		//if (time_taken > 0.01)
		//	{
			//printf("%s GalilController::acquire %2.3lfs\n", model_, time_taken);
		//	}
		//
	
		//Success.  Set status in GalilController instance
		recstatus_ = asynSuccess;
	        }
	    else //Failure.  Not connected
		{
	        recstatus_ = asynError;
		}
	    }
	catch (string e)
	    {
	    //Failure.  Print exception mesg
	    cout << functionName << ":" << model_ << ":" << address_ << ":" << e;
	    //Failure.  Set status in GalilController instance
	    recstatus_ = asynError;
	    //Check for timeout errors
	    if (strstr(e.c_str(), "TIMEOUT") != NULL)
		{
		//Increment consecutive timeout counter
		//GalilController::connectManager thread watches this 
		consecutive_timeouts_++;
		}
	    }
	 //Forgiveness is cheap.
	 //Allows us to poll without lock
         catch (const std::bad_typeid& e)
		{
		cout << "Caught bad_typeid acquireDataRecord" << endl;
		}
	
	//Return value is not monitored
	return asynSuccess;
}

/** Writes a string to the Galil controller and reads a response.
  * \param[in] caller functionName
  * \param[in] cmd to write to controller
  * \param[out] resp from controller.*/
asynStatus GalilController::writeReadController(const char *caller)
{
  const char *functionName="GalilController::writeReadController";
  string strcmd = cmd_;
  bool done = false;
  asynStatus status;
  int ex_count;

  //Count the number of exceptions
  ex_count = 0;
  
  //loop until success or reached max allowed exceptions
  while (!done)
	{
  	try	{
		//If connected, then do the command
		if (gco_ != NULL)
		       	{
		 	//send the command, and get the response
			strcpy(resp_, gco_->command(strcmd).c_str()); 
			//No exception = success
			done = true;
		 	consecutive_timeouts_ = 0;
			status = asynSuccess;
			}
		else 	//Not connected
		       	return asynError;
		}
	catch (string e) 
		{
		//Print exception mesg
		cout << caller << ":" << functionName << ":" << cmd_ << ":" << e;
		ex_count++;
		//flag error
		status = asynError;
		//Burn program always causes timeout error
		//Motor jumper incorrect
		//Timeout for any reason
		//Or reached max allowed errors
		if (ex_count > MAX_RETRIES || strncmp(cmd_, "BP",2)==0 || strncmp(cmd_, "MT",2) == 0 || strstr(e.c_str(), "TIMEOUT") != NULL)
			  {
			  //Check for timeout errors
			  if (strstr(e.c_str(), "TIMEOUT") != NULL)
				{
				//Increment consecutive timeout counter
				//GalilController::connectManager watches this 
				consecutive_timeouts_++;
				if (consecutive_timeouts_ > ALLOWED_TIMEOUTS)
					{
					//Give connect thread chance to obtain lock in case disconnect is required
					unlock();
					epicsThreadSleep(0.001);
					lock();
					}
				}
			  //Give up
			  done = true;
			  strcpy(resp_, "");
			  //Reset status to asynSucess if burn program caused time out
			  status = strncmp(cmd_, "BP",2) == 0 ? asynSuccess : asynError;
			  }
		}
      }
  return status;
}

/*--------------------------------------------------------------*/
/* Start the card requested by user   */
/*--------------------------------------------------------------*/
void GalilController::GalilStartController(char *code_file, int eeprom_write, int display_code)
{
	const char *functionName = "GalilStartController";
	unsigned i;					 //General purpose looping
	bool start_ok = true;				 //Have the controller threads started ok
	bool download_ok = true;			 //Was user specified code delivered successfully
	string uc;					 //Uploaded code from controller
	string dc;					 //Code to download to controller

	//Backup parameters used by developer for later re-start attempts of this controller
	//This allows full recovery after disconnect of controller
	strcpy(code_file_, code_file);
	eeprom_write_ = eeprom_write;

	//Assemble code for download to controller.  This is generated, or user specified code.
        if (!code_assembled_)
		{
		//Assemble the code generated by GalilAxis, if we havent already
		//Assemble code for motor controllers only, not rio
		if (strncmp(model_,"RIO",3) != 0)
			{
			/*First add termination code to end of code generated for this card*/
			gen_card_codeend();
	
			/*Assemble the sections of generated code for this card */
			strcat(card_code_, thread_code_);
			strcat(card_code_, limit_code_);
			strcat(card_code_, digital_code_);
	
			//Dump generated code to file
			write_gen_codefile();
			}

		//load up code file specified by user (ie. generated, or generated & user edited, or complete user code)
		read_codefile(code_file);
		}

	/*print out the generated/user code for the controller*/
	if ((display_code == 1) || (display_code == 3))
		{
		printf("\nGenerated/User code is\n\n");
		cout << card_code_ << endl;
		}

	//If connected, then proceed
	//to check the program on dmc, and download if needed
	if (gco_ != NULL)
		{
		//Put, and wait until poller is in sleep mode
		//Also stop async records from controller
		poller_->sleepPoller();			
		/*Upload code currently in controller for comparison to generated code */
		try	{
			uc = gco_->programUpload();
			//Remove the \r characters
			uc.erase (std::remove(uc.begin(), uc.end(), '\r'), uc.end());
			//Remove ' characters
			uc.erase (std::remove(uc.begin(), uc.end(), '\''), uc.end());
			}
		catch (string e)
		      	{
			//Upload failed
		      	cout << "GalilStartController:Upload failed:" << " " << e;
		      	}

		if ((display_code == 2) || (display_code == 3))
			{
			//print out the uploaded code from the controller
			printf("\nUploaded code is\n\n");
			cout << uc << endl;
			}

		//Copy card_code_ into download code buffer
		dc = card_code_;
		//Remove the \r characters
		dc.erase (std::remove(dc.begin(), dc.end(), '\r'), dc.end());
		//Remove ' characters
		dc.erase (std::remove(dc.begin(), dc.end(), '\''), dc.end());

		/*If generated code differs from controller current code then download generated code*/
		if (dc.compare(uc) != 0 && dc.compare("") != 0)
			{
			printf("\nTransferring code to model %s, address %s\n",model_, address_);
			try	{
				//Do the download
				gco_->programDownload(dc);
				}
			catch (string e)
				{
				//Donwload failed
				errlogPrintf("\nError downloading code model %s, address %s msg %s\n",model_, address_, e.c_str());
				download_ok = false;
				}
			
			if (download_ok)
				{
				printf("Code transfer successful to model %s, address %s\n",model_, address_);	
				/*burn code to eeprom if eeprom_write is 1*/
				if (eeprom_write == 1)
					{
					/*Burn program to EEPROM*/
					sprintf(cmd_, "BP");
					if (writeReadController(functionName) != asynSuccess)
						errlogPrintf("Error burning code to EEPROM model %s, address %s\n",model_, address_);
					else
						errlogPrintf("Burning code to EEPROM model %s, address %s\n",model_, address_);
				
					epicsThreadSleep(3);
			
					/*Burn parameters to EEPROM*/
					sprintf(cmd_, "BN");
					if (writeReadController(functionName) != asynSuccess)
						errlogPrintf("Error burning parameters to EEPROM model %s, address %s\n",model_, address_);
					else
						errlogPrintf("Burning parameters to EEPROM model %s, address %s\n",model_, address_);
				
					epicsThreadSleep(3);
				
					/*Burn variables to EEPROM*/
					sprintf(cmd_, "BV");
					if (writeReadController(functionName) != asynSuccess)
						errlogPrintf("Error burning variables to EEPROM model %s, address %s\n",model_, address_);
					else
						errlogPrintf("Burning variables to EEPROM model %s, address %s\n",model_, address_);
				
					epicsThreadSleep(3);
					}
				}
			}

		//Start code on motor controllers, not rio
		if (strncmp(model_, "RIO", 3) != 0)
			{
			//last thing, we start thread 0
			//Note that thread 0 starts any other required threads on the motor controller
			sprintf(cmd_, "XQ 0,0");
			if (writeReadController(functionName) != asynSuccess)
				errlogPrintf("Thread 0 failed to start on model %s address %s\n\n",model_, address_);
					
			epicsThreadSleep(1);
		
			//Check code is running for all created GalilAxis
			if (numAxes_ > 0)
				{
				for (i=0;i<numAxes_;i++)
					{		
					/*check that code is running*/
					sprintf(cmd_, "MG _XQ%d\n",i);
					if (writeReadController(functionName) == asynSuccess)
						{
						if (atoi(resp_) == -1)
							{
							start_ok = 0;
							errlogPrintf("\nThread %d failed to start on model %s, address %s\n",i, model_, address_);
							}
						}
					}
				}
		
			if (start_ok == 0)
				{
				/*stop all motors on the crashed controller*/
				sprintf(cmd_, "AB 1\n");
				if (writeReadController(functionName) != asynSuccess)
					errlogPrintf("\nError aborting all motion on controller\n");
				else	
					errlogPrintf("\nStopped all motion on crashed controller\n");
				}
			else
				errlogPrintf("Code started successfully on model %s, address %s\n",model_, address_);
			}

		//Wake poller, and re-start async records if needed
		poller_->wakePoller();

		}//connected_

	//Code is assembled.  Free RAM, and set flag accordingly
	//Keep card_code_ for re-connection/re-start capability
        //Keep card_code_ so we can call GalilStartController internally for re-start
	if (!code_assembled_)
		{
		//free RAM
		free(thread_code_);
		free(limit_code_);
		free(digital_code_);
		//The GalilController code is fully assembled, and stored in GalilController::card_code_
		code_assembled_ = true;
		}
}

/*--------------------------------------------------------------------------------*/
/* Generate code end, and controller wide code eg. error recovery*/

void GalilController::gen_card_codeend(void)
{
	const char *functionName = "gen_card_codeend";
	int digports=0,digport,digvalues=0;
	int i;

        //Ensure motor interlock function is initially disabled
	sprintf(cmd_, "mlock=0\n");
	writeReadController(functionName);

	/* Calculate the digports and digvalues required for motor interlock function */
	for (i=0;i<8;i++)
		{
		if (strlen(motor_enables_->motors) > 0)
			{
			digport = i + 1;  	//digital port number counting from 1 to 8
			digports = digports | (1 << (digport-1)); 
			digvalues = digvalues | (motor_enables_->disablestates[0] << (digport-1));
			}
		}
	
	/* Activate input interrupts.  Two cases are EPS home, away function AND motor interlock function */
	if (digitalinput_init_ == true)
		{
		if (digports==0)
			{
			/* EPS home and away function */
			strcat(card_code_,"II 1,8\n");		/*code to enable dig input interrupt must be internal to G21X3*/
			}
		else
			{
			/*motor interlock.  output variable values to activate code embedded in thread A*/
			sprintf(cmd_, "dpon=%d;dvalues=%d;mlock=1\n", digports, digvalues);
			writeReadController(functionName);
			}
		}
	
	//generate code end, only if axis are defined	
	if (numAxes_ != 0)
		{
		// Add galil program termination code
		if (digitalinput_init_ == true)
			{
			strcat(limit_code_, "RE 1\n");	/*we have written limit code, and we are done with LIMSWI but not prog end*/
			//Add controller wide motor interlock code to #ININT
			if (digports != 0)
				gen_motor_enables_code();
	
			// Add code to end digital port interrupt routine, and end the prog
			strcat(digital_code_, "RI 1\nEN\n");	
			}
		else
			strcat(limit_code_, "RE 1\nEN\n");   /*we have written limit code, and we are done with LIMSWI and is prog end*/
					
		//Add command error handler
		strcat(thread_code_, "#CMDERR\n");
		strcat(thread_code_, "errstr=_ED\n");
		strcat(thread_code_, "errcde=_TC\n");
		strcat(thread_code_, "cmderr=cmderr+1\n");
		//restart crashed thread, skipping the faulty line that caused the fault
		strcat(thread_code_, "XQ _ED3,_ED1,1\n");
		strcat(thread_code_, "EN\n");
		
		//Set cmderr counter to 0
		sprintf(cmd_, "cmderr=0");
		writeReadController(functionName);
		}
}

/*--------------------------------------------------------------------------------*/
/* Generate code to stop motors if disabled via digital IO transition*/
/* Generates code for #ININT */ 
/*
   See also GalilAxis::gen_digitalcode, it too generates #ININT code
*/

void GalilController::gen_motor_enables_code(void)
{
	int i,j;
	struct Galilmotor_enables *motor_enables=NULL;  //Convenience pointer to GalilController motor_enables[digport]
	bool any;

	//Assume no digital interlock specified
	any = false;
	
	//Add motor inhibit code for first 8 digital inputs
	for (i=0;i<8;i++)
		{
		//Retrieve structure for digital port from controller instance
		motor_enables = (Galilmotor_enables *)&motor_enables_[i];
		// Generate if statement for this digital port
		if (strlen(motor_enables->motors) > 0)
			{
			any = true;
			sprintf(digital_code_,"%sIF ((@IN[%d]=%d)\n", digital_code_, (i + 1), (int)motor_enables->disablestates[0]);
			// Scan through all motors associated with the port
			for (j=0;j<(int)strlen(motor_enables->motors);j++)
				{
				//Add code to stop the motors when digital input state matches that specified
				if (j == (int)strlen(motor_enables->motors) - 1)
					sprintf(digital_code_,"%sST%c\n", digital_code_, motor_enables->motors[j]);
				else
					sprintf(digital_code_,"%sST%c;", digital_code_, motor_enables->motors[j]);
				}
			//Manipulate interrupt flag to turn off the interrupt on this port for one threadA cycle
			sprintf(digital_code_,"%sdpoff=dpoff-%d\nENDIF\n", digital_code_, (int)pow(2,i));
			}
		}
	/* Re-enable input interrupt for all except the digital port(s) just serviced during interrupt routine*/
	/* ThreadA will re-enable the interrupt for the serviced digital ports(s) after 1 cycle */
	if (any)
		strcat(digital_code_,"II ,,dpoff,dvalues\n");
}

/*-----------------------------------------------------------------------------------*/
/*  Dump galil code generated for this controller to file
*/

void GalilController::write_gen_codefile(void)
{
	FILE *fp;
	int i = 0;
	char filename[100];
	
	sprintf(filename,"./%s.gmc",address_);
	
	fp = fopen(filename,"w");

	if (fp != NULL)
		{
		//Dump generated galil code from the GalilController instance
		while (card_code_[i]!='\0')
			{
			fputc(card_code_[i],fp);
			i++;
			}
		fclose(fp);
		}
	else
		errlogPrintf("Could not open %s",filename);
}

/*-----------------------------------------------------------------------------------*/
/*  Load the galil code specified into the controller class
*/

void GalilController::read_codefile(const char *code_file)
{
	int i = 0;
	char user_code[MAX_GALIL_AXES*(THREAD_CODE_LEN+LIMIT_CODE_LEN+INP_CODE_LEN)];
	char file[MAX_FILENAME_LEN];
	FILE *fp;

	if (strcmp(code_file,"")!=0)
		{
		strcpy(file, code_file);

		fp = fopen(file,"r");

		if (fp != NULL)
			{
			//Read the specified galil code
			while (!feof(fp))
				{
				user_code[i] = fgetc(fp);
				i++;
				}
			fclose(fp);
			user_code[i] = '\0';
	
			//Filter code
			for (i=0;i<(int)strlen(user_code);i++)
				{
				//Filter out any REM lines
				if (user_code[i]=='R' && user_code[i+1]=='E' && user_code[i+2]=='M')
					{
					while (user_code[i]!='\n' && user_code[i]!=EOF)
						i++;
					}
				}
	
			//Terminate the code buffer, we dont want the EOF character
			user_code[i-1] = '\0';
			//Load galil code into the GalilController instance
			strcpy(card_code_, user_code);
			}
		else
			errlogPrintf("\ngalil_read_codefile: Can't open user code file, using generated code\n\n");
		}
}

/** Find kinematic variables Q-X and substitute them for variable in range A-P for sCalcPerform
  * \param[in] axes    	   - List of real axis 
  * \param[in] csaxes	   - List of coordinate system axis
  * \param[in] equation    - List kinematic transform equations we search through
  * \param[in] variables   - List variables found in the equation
  * \param[in] substitutes - List substitutes that replaced the variables
  */
asynStatus GalilController::findVariableSubstitutes(char *axes, char *csaxes, char **equations, char **variables, char **substitutes)

{
  unsigned i, j, k, l;			//Looping/indexing
  bool findarg;				//Do we need to find an arg position for this variable, or have we done so already
  unsigned arg_status[SCALCARGS];	//sCalcPerform argument used status
  char *equation;			//The kinematic equation we are processing.  For convenience
  char *vars;				//The kinematic equation variables
  char *subs;				//The kine

  //Sanity check
  if (strlen(axes) != strlen(csaxes))
	{
	printf("Problem with kinetmatic expression.  CSAxis count should equal real axis count\n");
	return asynError;
	}

  //Scan through axes list
  for (i = 0; i < strlen(axes); i++)
	{
	//Assign conveniance variables
	equation = equations[i];
	vars = variables[i];
	subs = substitutes[i];
	//Initialize variables and substitutes
	strcpy(vars, "");
	strcpy(subs, "");

  	//Default sCalcPerform arg status.  No args used
	for (j = 0; j < SCALCARGS; j++)
		arg_status[j] = false;

	//Flag args for all specified axis as used
	for (j = 0; j < strlen(axes); j++)
		arg_status[axes[j] - AASCII] = true;

	//Flag args for all specified csaxis as used
	for (j = 0; j < strlen(csaxes); j++)
		arg_status[csaxes[j] - AASCII] = true;

	k = 0;
  	//Now find variables in range Q-Z, and substitute them for variable in range A-P
  	for (j = 0; j < strlen(equation); j++)
		{
		if ((toupper(equation[j]) >= 'Q' && toupper(equation[j]) <= 'Z' && !isalpha(equation[j+1]) && !j) ||
	    	(j && toupper(equation[j]) >= 'Q' && toupper(equation[j]) <= 'Z' && !isalpha(equation[j+1]) && !isalpha(equation[j-1])))
			{
			findarg = true;
			//Check if we have found an arg position for this variable already
			for (l = 0; l < strlen(vars); l++)
				if (vars[l] == toupper(equation[j]))
					{
					findarg = false;
					break;
					}

			if (findarg)
				{
				//We need to find an argument position
				//Store the found variable
				vars[k] = toupper(equation[j]);
				vars[k + 1] = '\0'; 
				//Find free argument position
				for (l = 0; l < SCALCARGS; l++)
					{
					if (!arg_status[l])
						{
						//Found free arg position, use it
						arg_status[l] = true;
						//Calculate letter A-P which corresponds to this arg position
						subs[k] = l + AASCII;
						subs[k+1] = '\0';
						break;
						}
					//Did we find a free arg position for this kinematic variable ?
					if (l == SCALCARGS)
						{
						printf("Problem with kinetmatic expression.  Cannot find free argument position for all arguments\n");
						return asynError;
						}
					}
				//Substitute the variable
				equation[j] = subs[k];
				k++;
				}
			else
				{
				//Substitute the variable
				equation[j] = subs[k];
				}
			}
		}
	}

  return asynSuccess;
}

/** Break a comma separated list of kinematic transform equations in raw form 
  * forward 
  *   I=(A+B)/2,J=B-A
  * reverse
  *   A=I-J/2,B=I+J/2
  * into an axes list, and corresponding tranform list and return them in axes, and equations
  *
  * \param[in] raw    	   - Raw comma separated list of transform equations   
  * \param[in] axes        - List of axes extracted from raw
  * \param[in] equations   - List of equations extracted from raw for the axes
  * \param[in] variables   - List of kinematic variables Q-Z found in each equation
  * \param[in] substitutes - List of substitutes variables in range A-P used in actual calc in place of variables Q-Z
  */
asynStatus GalilController::breakupTransform(char *raw, char *axes, char **equations)
{
  char *charstr;			//The current token
  int status;				//Result
  bool expectAxis = true;		//Token to expect next is axis, or equation
  unsigned i = 0, j = 0;		//For counting number of axis, and equations respectively

  status = asynSuccess;

  //Break raw transform up into tokens
  charstr = strtok(raw, "=");
  while (charstr != NULL)
	{
        if (expectAxis)
		{
		//Token is an axis name
		axes[i] = toupper(charstr[0]);
		//Increment axis count
		i++;
		//Keep parsing the same string
		charstr = strtok(NULL, ",");
		}
	else
		{
		//Token is a kinematic transform equation
		strcpy(equations[j], charstr);
		//Increment equation count
		j++;
		//Keep parsing the same string
		charstr = strtok(NULL, "=");
		}
	//Toggle expectAxis
	expectAxis = (expectAxis) ? false : true;
	}
   return (asynStatus)status;
}

//IocShell functions

/** Creates a new GalilController object.
  * Configuration command, called directly or from iocsh
  * \param[in] portName          The name of the asyn port that will be created for this driver
  * \param[in] address      	 The name or address to provide to Galil communication library
  * \param[in] updatePeriod	 The time in ms between datarecords.  Async if controller + bus supports it, otherwise is polled/synchronous.
  */
extern "C" int GalilCreateController(const char *portName, const char *address, int updatePeriod)
{
  GalilController *pGalilController = new GalilController(portName, address, updatePeriod);
  pGalilController = NULL;
  return(asynSuccess);
}

/** Creates a new GalilAxis object.
  * Configuration command, called directly or from iocsh
  * \param[in] portName          The name of the asyn port that has already been created for this driver
  * \param[in] axisname      	 The name motor A-H 
  * \param[in] limits_as_home    Home routine will use limit switches for homing, and limits appear to motor record as home switch
  * \param[in] enables_string	 Comma separated list of digital IO ports used to enable/disable the motor
  * \param[in] switch_type	 Switch type attached to digital bits for enable/disable motor
  */
extern "C" asynStatus GalilCreateAxis(const char *portName,        	/*specify which controller by port name */
                         	      char *axisname,                  	/*axis name A-H */
				      int limit_as_home,		/*0=no, 1=yes. Using a limit switch as home*/
				      char *enables_string,		/*digital input(s) to use to enable/inhibit motor*/
				      int switch_type)		  	/*digital input switch type for enable/inhbit function*/
{
  GalilController *pC;
  GalilAxis *pAxis;
  static const char *functionName = "GalilCreateAxis";

  //Retrieve the asynPort specified
  pC = (GalilController*) findAsynPortDriver(portName);

  if (!pC) {
    printf("%s:%s: Error port %s not found\n",
           driverName, functionName, portName);
    return asynError;
  }
  
  pC->lock();

  pAxis = new GalilAxis(pC, axisname, limit_as_home, enables_string, switch_type);

  pAxis = NULL;
  pC->unlock();
  return asynSuccess;
}

/** Creates multiple GalilCSAxis objects.  Coordinate system axis
  * Configuration command, called directly or from iocsh
  * \param[in] portName          The name of the asyn port that has already been created for this driver
  * \param[in] forward           Comma separated list of forward kinematic expressions eg. I=(A+B)/2,J=B-A
  * \param[in] reverse           Comma separated list of reverse kinematic expressions eg. A=I-J/2,B=I+J/2
  */
extern "C" asynStatus GalilCreateCSAxes(const char *portName,     //specify which controller by port name
				        char *forward,		  //Comma separated list of forward kinematic expressions eg. I=(A+B)/2,J=B-A
				        char *reverse)		  //Comma separated list of reverse kinematic expressions eg. A=I-J/2,B=I+J/2
{
  GalilController *pC;			//The GalilController
  GalilCSAxis *pAxis;			//Galil coordinate system axis
  char *fwd[MAX_GALIL_CSAXES];		//Forward transforms for coordinate system axis as specified in the forward transform
  char *rev[MAX_GALIL_AXES];		//Reverse transforms for the individual real axis as specified in the reverse transform
  char *csaxes = NULL;			//List of coordinate system axis as specified in the forward transform
  char *axes = NULL;			//List of real axis involved as specified in the reverse transform
  char *fwdvars[MAX_GALIL_VARS];	//List of kinematic variables in range Q-Z for forward transform
  char *revvars[MAX_GALIL_VARS];	//List of kinematic variables in range Q-Z for reverse transform
  char *fwdsubs[MAX_GALIL_VARS]; 	//List of kinematic variable substitutes in range A-P for forward transform
  char *revsubs[MAX_GALIL_VARS]; 	//List of kinematic variable substitutes in range A-P for reverse transform
  unsigned i;				//For creating multiple GalilCSAxis coordinate system axis
  int status;				//Result
  static const char *functionName = "GalilCreateCSAxes";

  //Retrieve the asynPort specified
  pC = (GalilController*) findAsynPortDriver(portName);

  if (!pC) {
    printf("%s:%s: Error port %s not found\n",
           driverName, functionName, portName);
    return asynError;
  }

  //Make room for axes list
  axes = (char *)calloc(MAX_GALIL_AXES, sizeof(char));

  //Make room for coordinate system axes list
  csaxes = (char *)calloc(MAX_GALIL_CSAXES, sizeof(char));

  //Make room for forward transform variables list
  for (i = 0; i < MAX_GALIL_VARS; i++)
  	fwdvars[i] = (char *)calloc(MAX_MESSAGE_LEN, sizeof(char));

  //Make room for reverse transform variables list
  for (i = 0; i < MAX_GALIL_VARS; i++)
  	revvars[i] = (char *)calloc(MAX_MESSAGE_LEN, sizeof(char));

  //Make room for forward transform variable substitutes list
  for (i = 0; i < MAX_GALIL_VARS; i++)
  	fwdsubs[i] = (char *)calloc(MAX_MESSAGE_LEN, sizeof(char));

  //Make room for reverse transform variable substitutes list
  for (i = 0; i < MAX_GALIL_VARS; i++)
  	revsubs[i] = (char *)calloc(MAX_MESSAGE_LEN, sizeof(char));

  //Make room for individual forward transforms
  for (i = 0; i < MAX_GALIL_CSAXES; i++)
	fwd[i] = (char *)calloc(MAX_MESSAGE_LEN, sizeof(char));

  //Make room for individual reverse transforms
  for (i = 0; i < MAX_GALIL_AXES; i++)
	rev[i] = (char *)calloc(MAX_MESSAGE_LEN, sizeof(char));

  //break up the forward transforms
  status = pC->breakupTransform(forward, csaxes, fwd);

  //break up the reverse transforms
  status |= pC->breakupTransform(reverse, axes, rev);

  //Given the axes, and transforms, find and substitute variables in Q-Z range with variables in A-P range
  //We do this because sCalcPerform only supports 16 arguments
  status |= pC->findVariableSubstitutes(axes, csaxes, fwd, fwdvars, fwdsubs);
  status |= pC->findVariableSubstitutes(axes, csaxes, rev, revvars, revsubs);

  //Print results
  //for (i = 0; i < strlen(axes); i++)
	//{
	//printf("i %d csaxes %c fwd %s vars %s subs %s\n", i, csaxes[i], fwd[i], fwdvars[i], fwdsubs[i]);
	//printf("i %d axes %c rev %s vars %s subs %s\n", i, axes[i], rev[i], revvars[i], revsubs[i]);
	//}

  pC->lock();

  if (!status)
	{
	//Create the GalilCSAxis specified in the forward transform
	for (i = 0; i < strlen(csaxes); i++)
		pAxis = new GalilCSAxis(pC, csaxes[i], csaxes, fwd[i], fwdvars[i], fwdsubs[i], axes, rev, revvars, revsubs);
	}

  pAxis = NULL;

  pC->unlock();

  //Free the ram we used to get started
  free(csaxes);
  free(axes);
  for (i = 0; i < MAX_GALIL_CSAXES; i++)
        free(fwd[i]);
  for (i = 0; i < MAX_GALIL_AXES; i++)
	{
        free(rev[i]);
        free(fwdvars[i]);
        free(revvars[i]);
	free(fwdsubs[i]);
        free(revsubs[i]);
	}

  return asynSuccess;
}

/** Starts a GalilController hardware.  Delivers dmc code, and starts it.
  * Configuration command, called directly or from iocsh
  * \param[in] portName          The name of the asyn port that has already been created for this driver
  * \param[in] code_file      	 Code file to deliver to hardware
  * \param[in] eeprom_write      EEPROM write options
  * \param[in] display_code	 Display code options
  */
extern "C" asynStatus GalilStartController(const char *portName,        	//specify which controller by port name
					   const char *code_file,
					   int eeprom_write,
					   int display_code)
{
  GalilController *pC;
  static const char *functionName = "GalilStartController";

  //Retrieve the asynPort specified
  pC = (GalilController*) findAsynPortDriver(portName);

  if (!pC) {
    printf("%s:%s: Error port %s not found\n",
           driverName, functionName, portName);
    return asynError;
  }
  pC->lock();
  //Call GalilController::GalilStartController to do the work
  pC->GalilStartController((char *)code_file, eeprom_write, display_code);
  pC->unlock();
  return asynSuccess;
}

extern "C" asynStatus GalilCreateProfile(const char *portName,         /* specify which controller by port name */
                            		 int maxPoints)                /* maximum number of profile points */
{
  GalilController *pC;
  static const char *functionName = "GalilCreateProfile";

  //Retrieve the asynPort specified
  pC = (GalilController*) findAsynPortDriver(portName);
  if (!pC) {
    printf("%s:%s: Error port %s not found\n",
           driverName, functionName, portName);
    return asynError;
  }
  pC->lock();
  pC->initializeProfile(maxPoints);
  pC->unlock();
  return asynSuccess;
}

//Register the above IocShell functions
//GalilCreateController iocsh function
static const iocshArg GalilCreateControllerArg0 = {"Controller Port name", iocshArgString};
static const iocshArg GalilCreateControllerArg1 = {"IP address", iocshArgString};
static const iocshArg GalilCreateControllerArg2 = {"update period (ms)", iocshArgInt};
static const iocshArg * const GalilCreateControllerArgs[] = {&GalilCreateControllerArg0,
                                                             &GalilCreateControllerArg1,
                                                             &GalilCreateControllerArg2};
                                                             
static const iocshFuncDef GalilCreateControllerDef = {"GalilCreateController", 3, GalilCreateControllerArgs};

static void GalilCreateContollerCallFunc(const iocshArgBuf *args)
{
  GalilCreateController(args[0].sval, args[1].sval, args[2].ival);
}

//GalilCreateAxis iocsh function
static const iocshArg GalilCreateAxisArg0 = {"Controller Port name", iocshArgString};
static const iocshArg GalilCreateAxisArg1 = {"Specified Axis Name", iocshArgString};
static const iocshArg GalilCreateAxisArg2 = {"Limit switch as home", iocshArgInt};
static const iocshArg GalilCreateAxisArg3 = {"Motor enable string", iocshArgString};
static const iocshArg GalilCreateAxisArg4 = {"Motor enable switch type", iocshArgInt};

static const iocshArg * const GalilCreateAxisArgs[] =  {&GalilCreateAxisArg0,
                                                        &GalilCreateAxisArg1,
							&GalilCreateAxisArg2,
							&GalilCreateAxisArg3,
							&GalilCreateAxisArg4};

static const iocshFuncDef GalilCreateAxisDef = {"GalilCreateAxis", 5, GalilCreateAxisArgs};

static void GalilCreateAxisCallFunc(const iocshArgBuf *args)
{
  GalilCreateAxis(args[0].sval, args[1].sval, args[2].ival, args[3].sval, args[4].ival);
}

//GalilCreateVAxis iocsh function
static const iocshArg GalilCreateCSAxesArg0 = {"Controller Port name", iocshArgString};
static const iocshArg GalilCreateCSAxesArg1 = {"Forward transform", iocshArgString};
static const iocshArg GalilCreateCSAxesArg2 = {"Reverse transform", iocshArgString};

static const iocshArg * const GalilCreateCSAxesArgs[] =  {&GalilCreateCSAxesArg0,
                                                          &GalilCreateCSAxesArg1,
							  &GalilCreateCSAxesArg2};

static const iocshFuncDef GalilCreateCSAxesDef = {"GalilCreateCSAxes", 3, GalilCreateCSAxesArgs};

static void GalilCreateCSAxesCallFunc(const iocshArgBuf *args)
{
  GalilCreateCSAxes(args[0].sval, args[1].sval, args[2].sval);
}

//GalilCreateProfile iocsh function
static const iocshArg GalilCreateProfileArg0 = {"Controller Port name", iocshArgString};
static const iocshArg GalilCreateProfileArg1 = {"Code file", iocshArgInt};
static const iocshArg * const GalilCreateProfileArgs[] = {&GalilCreateProfileArg0,
                                                          &GalilCreateProfileArg1};
                                                             
static const iocshFuncDef GalilCreateProfileDef = {"GalilCreateProfile", 2, GalilCreateProfileArgs};

static void GalilCreateProfileCallFunc(const iocshArgBuf *args)
{
  GalilCreateProfile(args[0].sval, args[1].ival);
}

//GalilStartController iocsh function
static const iocshArg GalilStartControllerArg0 = {"Controller Port name", iocshArgString};
static const iocshArg GalilStartControllerArg1 = {"Code file", iocshArgString};
static const iocshArg GalilStartControllerArg2 = {"EEPROM write", iocshArgInt};
static const iocshArg GalilStartControllerArg3 = {"Display code", iocshArgInt};
static const iocshArg * const GalilStartControllerArgs[] = {&GalilStartControllerArg0,
                                                            &GalilStartControllerArg1,
                                                            &GalilStartControllerArg2,
                                                            &GalilStartControllerArg3};
                                                             
static const iocshFuncDef GalilStartControllerDef = {"GalilStartController", 4, GalilStartControllerArgs};

static void GalilStartControllerCallFunc(const iocshArgBuf *args)
{
  GalilStartController(args[0].sval, args[1].sval, args[2].ival, args[3].ival);
}

//Construct GalilController iocsh function register
static void GalilSupportRegister(void)
{
  iocshRegister(&GalilCreateControllerDef, GalilCreateContollerCallFunc);
  iocshRegister(&GalilCreateAxisDef, GalilCreateAxisCallFunc);
  iocshRegister(&GalilCreateCSAxesDef, GalilCreateCSAxesCallFunc);
  iocshRegister(&GalilCreateProfileDef, GalilCreateProfileCallFunc);
  iocshRegister(&GalilStartControllerDef, GalilStartControllerCallFunc);
}

//Finally do the registration
extern "C" {
epicsExportRegistrar(GalilSupportRegister);
}

