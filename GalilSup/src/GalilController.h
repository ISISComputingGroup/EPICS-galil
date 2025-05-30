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

#ifndef GalilController_H
#define GalilController_H

#if defined _WIN32 || _WIN64
#define rint(x) floor((x)+0.5)
#define lrint(x) floor((x)+0.5)
#define finite(x) _finite(x)
#endif /* _WIN32/_WIN64 */

#define BEGIN_TIMEOUT 2.0
#define AASCII 65
#define IASCII 73
#define QASCII 81
#define ZEROASCII 48
#define SCALCARGS 16
//Number of communication retries
#define ALLOWED_TIMEOUTS 3
#define MAX_UPDATE_PERIOD 200
#define MAX_GALIL_STRING_SIZE 256
#define MAX_GALIL_DATAREC_SIZE 768
#define MAX_GALIL_AXES 8
#define MAX_GALIL_LINEAR_INCREMENT 8388607
#define MAX_GALIL_ABSOLUTE_MOVE 2147483647
#define MAX_ENUM_ROWS 16
//Number of parameter tables created
//Meaning records can have address values 0-63
#define MAX_ADDRESS 64
#define MAX_GALIL_CSAXES 8
#define MAX_FILENAME_LEN 2048
#define MAX_LINEAR_SEGMENTS 511
#define MAX_PVT_SEGMENTS 255
#define HEADER_BYTES 4 
#define COORDINATE_SYSTEMS 2
#define ANALOG_PORTS 8
#define BINARYIN_BYTES 3
#define BINARYOUT_WORDS 3
#define LIMIT_CODE_LEN 80000
#define INP_CODE_LEN 80000
#define THREAD_CODE_LEN 80000

//Time base
#define DEFAULT_TIME 1000.0

//EtherCat errors
#define ECAT_DUPLICATEID 180
#define ECAT_TIMEOUT 181
#define ECAT_DRIVENOTFOUND 182
#define ECAT_NODRIVECONFIGURED 187

//Nearest integer
#define NINT(f) (long)((f)>0 ? (f)+0.5 : (f)-0.5)

#include "macLib.h"
#include "GalilAxis.h"
#include "GalilCSAxis.h"
#include "GalilConnector.h"
#include "GalilPoller.h"
#include "epicsMessageQueue.h"

#include <unordered_map> //used for data record features
#include <vector>

// drvInfo strings for extra parameters that the Galil controller supports
#define GalilDriverString		"CONTROLLER_DRIVER"
#define GalilAddressString		"CONTROLLER_ADDRESS"
#define GalilModelString		"CONTROLLER_MODEL"
#define GalilHomeTypeString		"CONTROLLER_HOMETYPE"
#define GalilHomeEdgeString		"CONTROLLER_HOMEEDGE"
#define GalilLimitTypeString		"CONTROLLER_LIMITTYPE"
#define GalilCtrlErrorString		"CONTROLLER_ERROR"
#define GalilCommunicationErrorString	"CONTROLLER_COMMERR"
#define GalilDeferredModeString		"CONTROLLER_DEFERRED_MODE"
#define GalilPVTCapableString		"CONTROLLER_PVTCAPABLE"
#define GalilStartString		"CONTROLLER_START"

#define GalilCoordSysString		"COORDINATE_SYSTEM"
#define GalilCoordSysMotorsString	"COORDINATE_SYSTEM_MOTORS"
#define GalilCoordSysMovingString	"COORDINATE_SYSTEM_MOVING"
#define GalilCoordSysSegmentsString	"COORDINATE_SYSTEM_SEGMENTS"
#define GalilCoordSysMotorsStopString	"COORDINATE_SYSTEM_MOTORS_STOP"
#define GalilCoordSysMotorsGoString	"COORDINATE_SYSTEM_MOTORS_GO"

#define GalilProfileFileString		"GALIL_PROFILE_FILE"
#define GalilProfileMaxVelocityString	"GALIL_PROFILE_MAX_VELOCITY"
#define GalilProfileMaxAccelerationString	"GALIL_PROFILE_MAX_ACCELERATION"
#define GalilProfileMinPositionString	"GALIL_PROFILE_MIN_POSITION"
#define GalilProfileMaxPositionString	"GALIL_PROFILE_MAX_POSITION"
#define GalilProfileMoveModeString	"GALIL_PROFILE_MOVE_MODE"
#define GalilProfileTypeString		"GALIL_PROFILE_TYPE"
#define GalilProfileCalculatedString    "GALIL_PROFILE_CALCULATED"

#define GalilUserArrayUploadString	"CONTROLLER_UARRAY_UPLOAD"
#define GalilUserArrayString		"CONTROLLER_UARRAY"
#define GalilUserArrayNameString	"CONTROLLER_UARRAY_NAME"

#define GalilOutputCompare1AxisString	"OUTPUT_COMPARE_AXIS"
#define GalilOutputCompare1StartString	"OUTPUT_COMPARE_START"
#define GalilOutputCompare1IncrString	"OUTPUT_COMPARE_INCR"
#define GalilOutputCompareMessageString	"OUTPUT_COMPARE_MESSAGE"

#define GalilCSMotorSetPointString	"CSMOTOR_SETPOINT"

#define GalilCSMotorForwardString	"CSMOTOR_FORWARD_TRANSFORM"
#define GalilCSMotorReverseAString	"CSMOTOR_REVERSEA_TRANSFORM"
#define GalilCSMotorReverseBString	"CSMOTOR_REVERSEB_TRANSFORM"
#define GalilCSMotorReverseCString	"CSMOTOR_REVERSEC_TRANSFORM"
#define GalilCSMotorReverseDString	"CSMOTOR_REVERSED_TRANSFORM"
#define GalilCSMotorReverseEString	"CSMOTOR_REVERSEE_TRANSFORM"
#define GalilCSMotorReverseFString	"CSMOTOR_REVERSEF_TRANSFORM"
#define GalilCSMotorReverseGString	"CSMOTOR_REVERSEG_TRANSFORM"
#define GalilCSMotorReverseHString	"CSMOTOR_REVERSEH_TRANSFORM"

#define GalilMotorSetValString		"MOTOR_SET_VAL"
#define GalilMotorSetValEnableString	"MOTOR_SETVAL_ENABLE"
#define GalilMotorSetString		"MOTOR_SET"
#define GalilMotorStopString		"MOTOR_RECORD_STOP"
#define GalilSSIConnectedString		"MOTOR_SSI_CONNECTED"
#define GalilEncoderStallTimeString	"MOTOR_ENCODER_STALL_TIME"
#define GalilStepSmoothString		"MOTOR_STEPSMOOTH"
#define GalilITCSmoothString		"MOTOR_ITCSMOOTH"
#define GalilMotorTypeString		"MOTOR_TYPE"

#define GalilHomingRoutineAString	"HMRNAM_A"
#define GalilHomingRoutineBString	"HMRNAM_B"
#define GalilHomingRoutineCString	"HMRNAM_C"
#define GalilHomingRoutineDString	"HMRNAM_D"
#define GalilHomingRoutineEString	"HMRNAM_E"
#define GalilHomingRoutineFString	"HMRNAM_F"
#define GalilHomingRoutineGString	"HMRNAM_G"
#define GalilHomingRoutineHString	"HMRNAM_H"

#define GalilEtherCatCapableString	"CONTROLLER_ECATCAPABLE"
#define GalilEtherCatNetworkString	"CONTROLLER_ECAT_NETWORK"
#define GalilCtrlEtherCatFaultString	"CONTROLLER_ECAT_FAULT"
#define GalilEtherCatFaultResetString	"MOTOR_ECAT_FAULTRESET"
#define GalilEtherCatAddressString	"MOTOR_ECAT_ADDR"
#define GalilEtherCatFaultString	"MOTOR_ECAT_FAULT"

#define GalilMotorConnectedString	"MOTOR_MCONN"
#define GalilAfterLimitString		"MOTOR_EGUAFTER_LIMIT"
#define GalilWrongLimitProtectionString	"MOTOR_WLP"
#define GalilWrongLimitProtectionStopString	"MOTOR_WLP_STOP"
#define GalilUserOffsetString		"MOTOR_OFF"
#define GalilEncoderResolutionString	"MOTOR_ERES"
#define GalilDirectionString		"MOTOR_DIR"
#define GalilDmovString			"MOTOR_DMOV"
#define GalilUseEncoderString		"MOTOR_UEIP"
#define GalilUseReadbackString		"MOTOR_URIP"
#define GalilStopPauseMoveGoString	"MOTOR_SPMG"
#define GalilPremString			"MOTOR_PREM"
#define GalilPostString			"MOTOR_POST"
#define GalilUseSwitchString 		"MOTOR_USESWITCH"
#define GalilUseIndexString		"MOTOR_USEINDEX"
#define GalilJogAfterHomeString		"MOTOR_JOG_AHOME"
#define GalilJogAfterHomeValueString	"MOTOR_JOG_AHOME_VALUE"
#define GalilAutoOnOffString		"MOTOR_AUTO_ONOFF"
#define GalilAutoOnDelayString		"MOTOR_AUTO_ONDELAY"
#define GalilAutoOffDelayString		"MOTOR_AUTO_OFFDELAY"
#define GalilAutoBrakeString		"MOTOR_AUTO_BRAKE"
#define GalilAutoBrakeOnDelayString     "MOTOR_AUTO_BRAKEONDELAY"
#define GalilBrakePortString		"MOTOR_BRAKEPORT"
#define GalilBrakeString		"MOTOR_BRAKE"
#define GalilHomeAllowedString		"MOTOR_HOME_ALLOWED"
#define GalilStopDelayString		"MOTOR_STOP_DELAY"
#define GalilMicrostepString		"MOTOR_MICROSTEP"
#define GalilAmpGainString		"MOTOR_AMP_GAIN"
#define GalilAmpCurrentLoopGainString	"MOTOR_AMP_CURRENTLOOP_GAIN"
#define GalilAmpLowCurrentString	"MOTOR_AMP_LOWCURRENT"
#define GalilHomingString		"MOTOR_HOMING"
#define GalilHomrString			"MOTOR_HOMR"
#define GalilHomfString			"MOTOR_HOMF"
#define GalilUserDataString		"MOTOR_USER_DATA"
#define GalilUserDataDeadbString	"MOTOR_USER_DATA_DEADB"
#define GalilLimitDisableString		"MOTOR_LIMIT_DISABLE"
#define GalilLimitConsistentString	"MOTOR_LIMIT_CONSISTENT"
#define GalilEncoderToleranceString     "MOTOR_ENCODER_TOLERANCE"

#define GalilBiasVoltageString		"MOTOR_BIASVOLTAGE"
#define GalilPoleString		        "MOTOR_POLE"

#define GalilMainEncoderString		"MOTOR_MAIN_ENCODER"
#define GalilAuxEncoderString		"MOTOR_AUX_ENCODER"

#define GalilMotorAcclString		"MOTOR_ACCL"
#define GalilMotorRdbdString		"MOTOR_RDBD"
#define GalilMotorHvelString		"MOTOR_HVEL"
#define GalilMotorVeloString		"MOTOR_VELO"
#define GalilMotorVmaxString		"MOTOR_VMAX"
#define GalilAnalogInString		"ANALOG_IN"
#define GalilAnalogInDeadbString	"ANALOG_IN_DEADB"
#define GalilAnalogOutString		"ANALOG_OUT"
#define GalilAnalogOutRBVString		"ANALOG_OUTRBV"
#define GalilAnalogOutRBVDeadbString	"ANALOG_OUTRBV_DEADB"

#define GalilBinaryInString		"BINARY_IN"
#define GalilBinaryOutString		"BINARY_OUT"
#define GalilBinaryOutRBVString		"BINARY_OUTRBV"
#define GalilSSICapableString		"CONTROLLER_SSICAPABLE"
#define GalilSSIInputString		"MOTOR_SSIINPUT"
#define GalilSSITotalBitsString		"MOTOR_SSITOTBITS"
#define GalilSSISingleTurnBitsString	"MOTOR_SSISINGLETBITS"
#define GalilSSIErrorBitsString		"MOTOR_SSIERRBITS"
#define GalilSSITimeString		"MOTOR_SSITIME"
#define GalilSSIDataString		"MOTOR_SSIDATA"
#define GalilSSIInvertString		"MOTOR_SSIINVERT"
#define GalilBISSCapableString		"CONTROLLER_BISSCAPABLE"
#define GalilBISSInputString		"MOTOR_BISSINPUT"
#define GalilBISSData1String		"MOTOR_BISSDATA1"
#define GalilBISSData2String		"MOTOR_BISSDATA2"
#define GalilBISSZPString		"MOTOR_BISSZP"
#define GalilBISSCDString		"MOTOR_BISSCD"
#define GalilBISSLevelString		"MOTOR_BISSLEVEL"
#define GalilBISSStatTimeoutString	"MOTOR_BISSSTAT_TIMEOUT"
#define GalilBISSStatCRCString	        "MOTOR_BISSSTAT_CRC"
#define GalilBISSStatErrorString	"MOTOR_BISSSTAT_ERROR"
#define GalilBISSStatWarnString	        "MOTOR_BISSSTAT_WARN"
#define GalilBISSStatPollString	        "MOTOR_BISSSTAT_POLL"

#define GalilErrorLimitString		"MOTOR_ERRLIM"
#define GalilErrorString		"MOTOR_ERR"
#define GalilOffOnErrorString		"MOTOR_OOE"
#define GalilAxisString			"MOTOR_AXIS"
#define GalilMotorVelocityEGUString  	"MOTOR_VELOCITY_EGU"
#define GalilMotorVelocityRAWString  	"MOTOR_VELOCITY_RAW"

#define GalilUserCmdString		"USER_CMD"
#define GalilUserOctetString		"USER_OCTET"
#define GalilUserOctetValString		"USER_OCTET_VAL"
#define GalilUserVarString		"USER_VAR"

#define GalilEthAddrString	  	"CONTROLLER_ETHADDR"
#define GalilSerialNumString	  	"CONTROLLER_SERIALNUM"
#define GalilMoveCommandString	"MOVE_COMMAND"
#define GalilMotorEncoderSyncTolString	"MOTOR_ENC_TOL"
#define GalilEncoderSmoothString	"ENCODER_SMOOTH"
#define GalilMotorDlyString			"MOTOR_DLY"

#define GalilStatusPollDelayString	"MOTOR_STATUS_POLL_DELAY"

//C++ "To String with Precision" static function template
template <typename T>
string tsp(const T a_value, const int n = 6);

/* For each digital input, we maintain a list of motors, and the state the input should be in*/
/* To disable the motor */
struct Galilmotor_enables {
	char motors[MAX_GALIL_AXES];
	char disablestates[MAX_GALIL_AXES];
};

struct Source //each data record source key (e.g. "_RPA") maps to one of these, each of which describes the position and width of the variable within the binary data record
{
	int byte; //byte offset within binary data record
	std::string type; //"SB", "UB", "SW", "UW", "SL", "UL".  Specifies width within binary data record and signed/unsigned.
	int bit; //-1 if not bit field (e.g. RPA).  >= 0 if bit field (e.g. _MOA)
	std::string units; //e.g. "counts"
	std::string description; //e.g. "analog input 1"
	double scale; //e.g. 32768, scale factor:  most sources are 1 except TV, TT, @AN, @AO etc.
	double offset; //needed for analog inputs and outputs

	Source(int byte = 0, std::string type = "Ux", int bit = -1, std::string units = "", std::string description = "", double scale = 1, double offset = 0) :
		byte(byte), type(type), bit(bit), units(units), description(description), scale(scale), offset(offset)
	{ /*ctor just initializes values*/ }
};

// DMC code download status
typedef enum downloadState {
  notRequired = 0,
  downloadSuccess,
  downloadFailed,
} downloadState;

// Motor record spmg state
typedef enum motorRecordState {
  spmgStop = 0,
  spmgPause,
  spmgMove,
  spmgGo
} motorRecordState;

// Amplifier enum information
typedef struct {
  const char *enumString;
  int  enumValue;
} enumStruct_t;

static const enumStruct_t ampGain_43040[] = {
  {"0.40 A/V",   0},
  {"0.70 A/V",   1},
  {"1.00 A/V",   2},
};

static const enumStruct_t ampGain_43140[] = {
  {"0.10 A",   0},
};

static const enumStruct_t microstep_43547[] = {
  {"256",   256},
};

static const enumStruct_t ampGain_44040[] = {
  {"0.50 A",   0},
  {"0.75 A",   1},
  {"1.00 A",   2},
  {"1.40 A",   3},
};

static const enumStruct_t microstep_44040[] = {
  {"1",   1},
  {"2",   2},
  {"4",   4},
  {"16", 16},
};

static const enumStruct_t ampGain_44140[] = {
  {"0.50 A",   0},
  {"1.00 A",   1},
  {"2.00 A",   2},
  {"3.00 A",   3},
};

static const enumStruct_t microstep_44140[] = {
  {"64",   64},
};

class GalilController : public asynMotorController {
public:
  //These variables need to be accessible from static callbacks
  epicsEventId connectEvent_;		//Connection event
  int connected_;			//Is the synchronous communication socket connected according to asyn.  Async UDP is connectionless

  string address_;			//address string
  string model_;			//model string
  static int init_state_;

  //Class constructor
  GalilController(const char *portName, const char *address, double updatePeriod, int quiet_start);

  asynStatus async_writeReadController(const char *output, char *input, size_t maxChars, size_t *nread, double timeout);
  asynStatus async_writeReadController(void);

  asynStatus sync_writeReadController(const char *output, char *input, size_t maxChars, size_t *nread, double timeout);
  asynStatus sync_writeReadController(bool testQuery = false, bool logCommand = true);

  asynStatus sendUnsolicitedMessage(char *mesg);
  bool my_isascii(int c);
  asynStatus arrayUpload(void);
  asynStatus programUpload(string *prog);
  asynStatus programDownload(const string prog);
  asynStatus programValidate(const string prog);
  
  /* These are the methods that we override from asynMotorController */
  asynStatus setDeferredMoves(bool deferMoves);
  asynStatus writeInt32(asynUser *pasynUser, epicsInt32 value);
  asynStatus writeFloat64(asynUser *pasynUser, epicsFloat64 value);
  asynStatus writeFloat64Array(asynUser *pasynUser, epicsFloat64 *value, size_t nElements);
  asynStatus writeOctet(asynUser *pasynUser, const char*  value,  size_t  nChars,  size_t *  nActual);
  asynStatus readInt32(asynUser *pasynUser, epicsInt32 *value);
  asynStatus readFloat64(asynUser *pasynUser, epicsFloat64 *value);
  asynStatus readEnum(asynUser *pasynUser, char *strings[], int values[], int severities[], size_t nElements, size_t *nIn);
  asynStatus drvUserCreate(asynUser *pasynUser, const char* drvInfo, const char** pptypeName, size_t* psize); 
  asynStatus drvUserDestroy(asynUser *pasynUser);
  void report(FILE *fp, int level);

  //Real motors
  GalilAxis* getAxis(asynUser *pasynUser);
  GalilAxis* getAxis(int axisNo);

  //Coordinate system axis
  GalilCSAxis* getCSAxis(asynUser *pasynUser);
  GalilCSAxis* getCSAxis(int axisNo);

  /* These are the methods that we override from asynPortDriver */
  asynStatus writeUInt32Digital(asynUser *pasynUser, epicsUInt32 value, epicsUInt32 mask);

  /* These are the functions for profile moves */
  asynStatus buildProfile();
  asynStatus buildProfileFile();
  asynStatus checkCSAxisProfiles(string &mesg);
  asynStatus transformCSAxisProfiles();
  asynStatus prepRunProfile(FILE **profFile, int *coordsys, char *coordName, double startp[]);
  asynStatus executeProfile();
  asynStatus abortProfile();
  asynStatus initializeProfile(size_t maxProfilePoints);
  asynStatus beginProfileMotion(int coordsys, char coordName);
  asynStatus beginPVTProfileMotion();
  asynStatus runProfileFile();
  asynStatus runProfile();
  bool anyMotorMoving();
  bool allMotorsMoving();
  bool motorsAtStart(double startp[]);
  asynStatus motorsToProfileStartPosition(double startp[], bool move);
  void profileGetSegsMoving(int profStarted, int coordsys, int *moving, int *segprocessed);

  //asynStatus readbackProfile();

  /* These are the methods that are new to this class */
  asynStatus poller(void);
  int GalilInitializeVariables(bool burn_variables);
  void GalilReplaceHomeCode(char *axis, string filename);
  void GalilAddCode(int section, string filename);
  void GalilStartController(char *code_file, int eeprom_write, int thread_mask);
  void connect(void);
  void disconnect(void);
  void connected(void);
  void acquireDataRecord(void);
  asynStatus readDataRecord(char *input, unsigned bytesize);
  void getStatus(void);
  void setParamDefaults(void);
  void gen_code_headers();
  void gen_rio_code(void);
  void gen_card_codeend(void);
  void gen_motor_enables_code(void);
  void write_gen_codefile(const char* suffix);
  asynStatus read_codefile(const char *code_file);
  asynStatus read_codefile_part(const char *code_file, MAC_HANDLE* mac_handle);
  asynStatus read_codefile_hf(const char *code_files);
  asynStatus get_integer(int function, epicsInt32 *value, int axisNo);
  asynStatus get_double(int function, epicsFloat64 *value, int axisNo);
  void profileThread();
  void arrayUploadThread();
  asynStatus setOutputCompare(int oc);
  asynStatus beginLinearGroupMotion(char coordName, const char *axes);
  asynStatus beginGroupMotion(const char *maxes, const char *paxes = (const char *)"");
  bool axesEventMonitor(const char *axes, unsigned requestedEvent = 0);
  bool csaxesBeginMonitor(const char *axes);
  void clearDeferredMoves(const char *axes);
  //Execute motor record prem function for motor list
  void executePrem(const char *axes);
  //Execute auto motor power on, and brake off 
  void executeAutoOnBrakeOff(const char *axes);
  void processUnsolicitedMesgs(void);
  static std::string extractEthAddr(const char* str);
  void setCtrlError(string mesg);
  //Enable/Disable EtherCat network
  void enableEtherCatNetwork(int value);
  //Check axes are good to go
  asynStatus beginCheck(const char *caller, const char *axes, char *axis);
  //Check axes limits given move request
  asynStatus checkLimits(const char *caller, const char *axes, double npos[], char *axis);
  //Check axes soft limits given move request
  asynStatus checkSoftLimits(const char *caller, const char *axes, double npos[], char *axis);
  //Check motor record status of axes
  asynStatus checkMRSettings(const char *caller, const char *axes, char *axis);
  //Check axes settings given move request
  asynStatus checkAllSettings(const char *caller, const char *axes, double npos[], char *axis);
  //Update amplifer information upon connect
  asynStatus updateAmpInfo();
  //Enum row callback
  void enumRowCallback(unsigned ampNum, int reason, const enumStruct_t *pEnum, size_t nElements);

  void InitializeDataRecord(void);
  double sourceValue(const std::vector<char>& record, const std::string& source);
  void Init30010(bool dmc31010);
  void Init4000(int axes);
  void Init2103(int axes);
  void InitRio(bool rio3);
  void InitRio3_24Ex(void);
  void InitRioSer(bool rio3);
  void aq_analog(int byte, int input_num);
  string ax(string prefix, int axis, string suffix);
  void input_bits(int byte, int num);
  void output_bits(int byte, int num);
  void dq_analog(int byte, int input_num);

  /* Deferred moves functions.*/
  asynStatus beginSyncStartStopMove(int coordsys, const char *axes, const char *moves, double acceleration, double velocity);
  asynStatus beginSyncStartOnlyMove(const char *axes);
  asynStatus prepSyncStartStopMoves(void);
  asynStatus prepSyncStartOnlyMoves(void);

  void shutdownController();
  bool shutdownRequested() { return shutdown_requested_; }
  virtual ~GalilController();

protected:
  #define FIRST_GALIL_PARAM GalilDriver_
  int GalilDriver_;
  int GalilAddress_;
  int GalilModel_;
  int GalilHomeType_;
  int GalilHomeEdge_;
  int GalilLimitType_;
  int GalilCtrlError_;
  int GalilDeferredMode_;
  int GalilPVTCapable_;
  int GalilStart_;
  int GalilCoordSys_;
  int GalilCoordSysMotors_;
  int GalilCoordSysMoving_;
  int GalilCoordSysSegments_;
  int GalilCoordSysMotorsStop_;
  int GalilCoordSysMotorsGo_;

  int GalilProfileFile_;
  int GalilProfileMaxVelocity_;
  int GalilProfileMaxAcceleration_;
  int GalilProfileMinPosition_;
  int GalilProfileMaxPosition_;
  int GalilProfileMoveMode_;
  int GalilProfileType_;
  int GalilProfileCalculated_;

  int GalilUserArrayUpload_;
  int GalilUserArray_;
  int GalilUserArrayName_;

  int GalilOutputCompareAxis_;
  int GalilOutputCompareStart_;
  int GalilOutputCompareIncr_;
  int GalilOutputCompareMessage_;

  int GalilCSMotorSetPoint_;
  int GalilCSMotorForward_;
  int GalilCSMotorReverseA_;
  int GalilCSMotorReverseB_;
  int GalilCSMotorReverseC_;
  int GalilCSMotorReverseD_;
  int GalilCSMotorReverseE_;
  int GalilCSMotorReverseF_;
  int GalilCSMotorReverseG_;
  int GalilCSMotorReverseH_;

  int GalilMotorRecordStop_;
  int GalilMotorSetVal_;
  int GalilMotorSetValEnable_;
  int GalilMotorSet_;
  int GalilEStallTime_;
  int GalilStepSmooth_;
  int GalilMotorType_;

  int GalilEtherCatCapable_;
  int GalilEtherCatNetwork_;
  int GalilCtrlEtherCatFault_;
  int GalilEtherCatFaultReset_;
  int GalilEtherCatAddress_;
  int GalilEtherCatFault_;

  int GalilMotorConnected_;
  int GalilAfterLimit_;
  int GalilWrongLimitProtection_;
  int GalilWrongLimitProtectionStop_;
  int GalilUserOffset_;
  int GalilEncoderResolution_;
  int GalilUseEncoder_;
  int GalilStopPauseMoveGo_;
  int GalilPrem_;
  int GalilPost_;
  int GalilUseSwitch_;
  int GalilUseIndex_;
  int GalilJogAfterHome_;
  int GalilJogAfterHomeValue_;
  int GalilAutoOnOff_;
  int GalilAutoOnDelay_;
  int GalilAutoOffDelay_;
  int GalilAutoBrake_;
  int GalilAutoBrakeOnDelay_;
  int GalilBrakePort_;
  int GalilBrake_;
  int GalilHomeAllowed_;
  int GalilStopDelay_;
  int GalilMicrostep_;
  int GalilAmpGain_;
  int GalilAmpCurrentLoopGain_;
  int GalilAmpLowCurrent_;
  int GalilHoming_;
  int GalilHomr_;
  int GalilHomf_;
  int GalilUserData_;
  int GalilUserDataDeadb_;
  int GalilLimitDisable_;
  int GalilLimitConsistent_;
  int GalilEncoderTolerance_;

  int GalilMainEncoder_;
  int GalilAuxEncoder_;
  int GalilMotorAccl_;
  int GalilMotorRdbd_;
  int GalilMotorHvel_;
  int GalilMotorVelo_;
  int GalilMotorVmax_;
  int GalilAnalogIn_;
  int GalilAnalogInDeadb_;
  int GalilAnalogOut_;
  int GalilAnalogOutRBV_;
  int GalilAnalogOutRBVDeadb_;
  int GalilBinaryIn_;
  int GalilBinaryOut_;
  int GalilBinaryOutRBV_;
  int GalilDirection_;
  int GalilDmov_;
  int GalilSSIConnected_;
  int GalilSSICapable_;
  int GalilSSIInput_;
  int GalilSSITotalBits_;
  int GalilSSISingleTurnBits_;
  int GalilSSIErrorBits_;
  int GalilSSITime_;
  int GalilSSIData_;
  int GalilSSIInvert_;
  int GalilBISSCapable_;
  int GalilBISSInput_;
  int GalilBISSData1_;
  int GalilBISSData2_;
  int GalilBISSZP_;
  int GalilBISSCD_;
  int GalilBISSLevel_;
  int GalilBISSStatTimeout_;
  int GalilBISSStatCRC_;
  int GalilBISSStatError_;
  int GalilBISSStatWarn_;
  int GalilBISSStatPoll_;
  int GalilErrorLimit_;
  int GalilError_;
  int GalilOffOnError_;
  int GalilAxis_;
  int GalilMotorVelocityEGU_;
  int GalilMotorVelocityRAW_;
  int GalilUserCmd_;
  int GalilUserOctet_;
  int GalilUserOctetVal_;
  int GalilUserVar_;
  int GalilEthAddr_;
  int GalilSerialNum_;
  int GalilStatusPollDelay_;
//Add new parameters here
  int GalilMoveCommand_;
  int GalilMotorEncoderSyncTol_;
  int GalilITCSmooth_;
  int GalilBiasVoltage_;
  int GalilPole_;
  int GalilUseReadback_;
  int GalilEncoderSmooth_;
  int GalilMotorDly_;

  int GalilCommunicationError_;
  #define LAST_GALIL_PARAM GalilCommunicationError_
  int GalilHomingRoutineA_;
  int GalilHomingRoutineB_;
  int GalilHomingRoutineC_;
  int GalilHomingRoutineD_;
  int GalilHomingRoutineE_;
  int GalilHomingRoutineF_;
  int GalilHomingRoutineG_;
  int GalilHomingRoutineH_;

private:

  std::unordered_map<std::string, Source> map; //data structure for data record

  char cmd_[MAX_GALIL_STRING_SIZE];	//holds the assembled Galil cmd string
  char resp_[MAX_GALIL_DATAREC_SIZE];	//Response from Galil controller

  GalilPoller *poller_;			//GalilPoller to acquire a datarecord
  GalilConnector *connector_;		//GalilConnector to manage connection status flags

  bool shuttingDown_;			//IOC exit in progress

  double timeMultiplier_;		//Controller time base divided by default time base

  int hswact_;                          //Value of axis _HM when home switch active
  int hswiact_;                         //Value of axis _HM when home switch inactive

  int digports_;			//Digital ports used in motor enable/disable
  int digvalues_;			//Digital port interrupt values
  bool digInitialUpdate_;		//Digital port initial update

  int ampModel_[2];                     // Model number of amplifier on channels A-D [0] and A-H [1]

  bool rio_;				//Is controller a RIO
  char code_file_[MAX_FILENAME_LEN];	//Code file(s) that user gave to GalilStartController

  int burn_program_;			//Burn program options that user gave to GalilStartController
					
  bool connect_fail_reported_;		//Has initial connection failure been reported to iocShell
  int consecutive_acquire_timeouts_;		//Used for connection management
  int consecutive_read_timeouts_;		//Used for connection management
  bool code_assembled_;			//Has code for the GalilController hardware been assembled (ie. is card_code_ all set to send)
  double updatePeriod_;			//Period between data records in ms
  bool async_records_;			//Are the data records obtained async(DR), or sync (QR)
  bool try_async_;			//Should we even try async udp (DR) before going to synchronous tcp (QR) mode

  bool movesDeferred_;			//Should moves be deferred for this controller

  double analogIndeadb_[ANALOG_PORTS+2];//Analog input dead bands
  double analogOutRBVdeadb_[ANALOG_PORTS+2]; //Analog output rbv dead bands

  double analogInPosted_[ANALOG_PORTS+2];//Analog input values posted to upper layers		
  double analogOutRbvPosted_[ANALOG_PORTS+2]; //Analog output rbv values posted to upper layers

  long maxAcceleration_;		//Maximum acceleration on this model

  epicsEventId profileExecuteEvent_;	//Event for executing motion profiles
  bool profileAbort_;			//Abort profile request flag.  Aborts profile when set true
  char profileAxes_[MAX_GALIL_AXES+1];	//Running profile axes list
  int profileType_;			//Running profile type 0=linear, 1=pvt

  vector<string> userVariables_;	//User defined variables
  vector<int> userVariableAddresses_;	//Address of record that relates to the user defined variable

  epicsEventId arrayUploadEvent_;	//Event for uploading user arrays from controller

  int thread_mask_;			//Mask detailing which threads are expected to be running after program download Bit 0 = thread 0 etc

  vector<char> recdata_;		//Data record from controller
  asynStatus recstatus_;		//Status of last record acquisition
  unsigned numAxesMax_;			//Number of axes actually supported by the controller
  unsigned numAxes_;			//Number of axes requested by developer
  char axisList_[MAX_GALIL_AXES+1];	//Axes requested by developer

  unsigned numThreads_;			//Number of threads the controller supports
  bool codegen_init_;			//Has the code generator been initialised for this controller
  bool digitalinput_init_;		//Has the digital input label #ININT been included for this controller

  string thread_code_ = "";
  string limit_code_ = "";
  string digital_code_ = "";
  string card_code_ = "";
  string user_code_ = "";
  
  static int compareCode(std::string dc, std::string uc);
  static void compressCode(std::string& s);
  static void findReplace(std::string& s, const std::string& toFind, const std::string& toReplace);
  
  void stopThreads();
  void stopAxes();
  int quiet_start_;
  bool shutdown_requested_;
  epicsEvent motion_started_;
  epicsMutex pollLock_;

  char asynccmd_[MAX_GALIL_STRING_SIZE];	//holds the assembled Galil cmd string
  char asyncresp_[MAX_GALIL_DATAREC_SIZE];	//For asynchronous messages including datarecord

  int timeout_;				//Timeout for communications
  int controller_number_;		//The controller number as counted in GalilCreateController
  epicsMessageQueue unsolicitedQueue_;	//Unsolicted messages recieved are placed here initially

  unsigned datarecsize_;			//Calculated size of controller datarecord based on response from QZ command
  
  char syncPort_[MAX_GALIL_STRING_SIZE];	//The name of the asynPort created for synchronous communication with controller
  char syncHandle_;				//Handle on controller used for synchronous communication (ie. tcp or serial)
  asynUser *pasynUserSyncGalil_;		//Asyn user for synchronous communication
  asynCommon *pasynCommon_;			//asynCommon interface for synchronous communication
  void *pcommonPvt_;				//asynCommon drvPvt for synchronous communication
  asynOctet *pSyncOctet_;			//Asyn octet interface for synchronous communication
  void *pSyncOctetPvt_;				//Asyn octet private data for synchronous communication

  char asyncPort_[MAX_GALIL_STRING_SIZE];	//The name of the asynPort created for asynchronous communication with controller
  char udpHandle_;				//Handle on controller used for udp
  asynUser *pasynUserAsyncGalil_;		//Asyn user for asynchronous communication
  asynOctet *pAsyncOctet_;			//Asyn octet interface for asynchronous communication
  void *pAsyncOctetPvt_;			//Asyn octet private data for asynchronous communication

  struct Galilmotor_enables motor_enables_[MAX_GALIL_AXES];//Stores the motor enable disable interlock digital IO setup, only first 8 digital in ports supported

  friend class GalilAxis;
  friend class GalilCSAxis;
  friend class GalilPoller;
  
  bool checkGalilThreads();
  bool checkGalilThread(int thread);
  friend class GalilConnector;
  friend void connectCallback(asynUser *pasynUser, asynException exception);
};
#define NUM_GALIL_PARAMS (&LAST_GALIL_PARAM - &FIRST_GALIL_PARAM + 1)
#endif  // GalilController_H
