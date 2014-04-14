///////////////////////////////////////////////////////////////////////////////
// FILE:          PicardStage.cpp
// PROJECT:       Micro-Manager
// SUBSYSTEM:     DeviceAdapters
//-----------------------------------------------------------------------------
// DESCRIPTION:   The drivers required for the Picard Industries USB stages
//
// AUTHORS:       Johannes Schindelin, Luke Stuyvenberg, 2011 - 2014
//
// COPYRIGHT:     Board of Regents of the University of Wisconsin -- Madison,
//					Copyright (C) 2011 - 2014
//
// LICENSE:       This file is distributed under the BSD license.
//                License text is included with the source distribution.
//
//                This file is distributed in the hope that it will be useful,
//                but WITHOUT ANY WARRANTY; without even the implied warranty
//                of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//
//                IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//                CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
//                INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES.

#include <iostream>

#include "../../MMDevice/ModuleInterface.h"
#include "PiUsb.h"

#include "PicardStage.h"

// We have a lot of stub implementations in here...
#pragma warning(disable: 4100)

using namespace std;

// External names used used by the rest of the system
// to load particular device from the "PicardStage.dll" library
const char* g_TwisterDeviceName = "Picard Twister";
const char* g_StageDeviceName = "Picard Z Stage";
const char* g_XYStageDeviceName = "Picard XY Stage";
const char* g_XYAdapterDeviceName = "Picard XY Stage Adapter";
const char* g_Keyword_SerialNumber = "Serial Number";
const char* g_Keyword_SerialNumberX = "Serial Number (X)";
const char* g_Keyword_SerialNumberY = "Serial Number (Y)";
const char* g_Keyword_Min = "Min";
const char* g_Keyword_MinX = "X-Min";
const char* g_Keyword_MinY = "Y-Min";
const char* g_Keyword_Max = "Max";
const char* g_Keyword_MaxX = "X-Max";
const char* g_Keyword_MaxY = "Y-Max";
const char* g_Keyword_Velocity = "Velocity";
const char* g_Keyword_VelocityX = "X-Velocity";
const char* g_Keyword_VelocityY = "Y-Velocity";
const char* g_Keyword_StepSize = "StepSize";
const char* g_Keyword_StepSizeX = "X-StepSize";
const char* g_Keyword_StepSizeY = "Y-StepSize";

#define TO_STRING_INTERNAL(x) #x
#define FIXED_TO_STRING(x) TO_STRING_INTERNAL(x)

#define CLOCKDIFF(now, then) (((double)(now) - (double)(then))/((double)(CLOCKS_PER_SEC)))
#define MAX_WAIT 0.05 // Maximum time to wait for the motors to begin motion, in seconds.

// These constants are per the Picard Industries documentation.
#define TWISTER_STEP_SIZE 1.8 // deg/step
#define TWISTER_LOWER_LIMIT -58980.6 // (-32767 * TWISTER_STEP_SIZE)
#define TWISTER_UPPER_LIMIT 58980.6 // (32767 * TWISTER_STEP_SIZE)

#define MOTOR_STEP_SIZE 1.5 // um/step
#define MOTOR_LOWER_LIMIT 0 // 0 * MOTOR_STEP_SIZE
#define MOTOR_UPPER_LIMIT 9000 // 6000 * MOTOR_STEP_SIZE

// These apply to both motors and twisters.
#define PICARD_MIN_VELOCITY 1
#define PICARD_MAX_VELOCITY 10

#define DEFAULT_SERIAL_UNKNOWN -1 // This is the default serial value, before serial numbers are pinged.
#define MAX_SERIAL_IDX 250 // Highest serial number index to ping.

#define PICARDSTAGE_ERROR_OFFSET 1327 // Error codes are unique to device classes, but MM defines some basic ones (see MMDeviceConstants.h). Make sure we're past them.

inline static char* VarFormat(const char* fmt, ...)
{
	static char buffer[MM::MaxStrLength];

	memset(buffer, 0x00, MM::MaxStrLength);
	va_list va;
	va_start(va, fmt);
	vsnprintf(buffer, MM::MaxStrLength, fmt, va);
	va_end(va);

	return buffer;
}

class CPiDetector
{
	private:
	CPiDetector(MM::Core& core, MM::Device& device)
	{
		core.LogMessage(&device, "Pinging motors...", false);

		m_pMotorList = new int[16];
		m_pTwisterList = new int[4];

		int error = PingDevices(core, device, &piConnectMotor, &piDisconnectMotor, m_pMotorList, 16, &m_iMotorCount);
		if(error > 1)
			core.LogMessage(&device, VarFormat(" Error detecting motors: %d", error), false);

		error = PingDevices(core, device, &piConnectTwister, &piDisconnectTwister, m_pTwisterList, 4, &m_iTwisterCount);
		if(error > 1)
			core.LogMessage(&device, VarFormat(" Error detecting twisters: %d", error), false);

		core.LogMessage(&device, VarFormat("Found %d motors and %d twisters.", m_iMotorCount, m_iTwisterCount), false);
	}

	~CPiDetector()
	{
		delete[] m_pMotorList;
		delete[] m_pTwisterList;
	}

	public:
	int GetMotorSerial(int idx)
	{
		if(idx < m_iMotorCount)
			return m_pMotorList[idx];

		return DEFAULT_SERIAL_UNKNOWN;
	}

	int GetTwisterSerial(int idx)
	{
		if(idx < m_iTwisterCount)
			return m_pTwisterList[idx];

		return DEFAULT_SERIAL_UNKNOWN;
	}

	private:
	int PingDevices(MM::Core& core, MM::Device& device, void* (__stdcall* connfn)(int*, int), void (__stdcall* discfn)(void*), int* pOutArray, const int iMax, int* pOutCount)
	{
		void* handle = NULL;
		int error = 0;
		int count = 0;
		for(int idx = 0; idx < MAX_SERIAL_IDX && count < iMax; ++idx)
		{
			if((handle = (*connfn)(&error, idx)) != NULL && error <= 1)
			{
				pOutArray[count++] = idx;
				(*discfn)(handle);
				handle = NULL;
			}
			else if(error > 1)
			{
				core.LogMessage(&device, VarFormat("Error scanning index %d: %d", idx, error), false);
				*pOutCount = count;
				return error;
			}
		}

		*pOutCount = count;
		return 0;
	}

	int *m_pMotorList;
	int m_iMotorCount;

	int *m_pTwisterList;
	int m_iTwisterCount;

	private:
	static CPiDetector *pPiDetector;

	public:
	static CPiDetector *GetInstance(MM::Core& core, MM::Device& device)
	{
		if(pPiDetector == NULL)
			pPiDetector = new CPiDetector(core, device);

		return pPiDetector;
	}
};

CPiDetector* CPiDetector::pPiDetector;

inline static void GenerateAllowedVelocities(vector<string>& vels)
{
	vels.clear();

	for(int i = PICARD_MIN_VELOCITY; i <= PICARD_MAX_VELOCITY; ++i)
		vels.push_back(VarFormat("%d", i));
}

// This routine handles a very generic sense of the OnVelocity PropertyAction.
// Get/set the velocity to a member variable, and optionally invoke PiUsb routines to change the motor's on-board velocity.
inline static int OnVelocityGeneric(MM::PropertyBase* pProp, MM::ActionType eAct, void* handle, int& velocity, int (__stdcall* pGet)(int*, void*), int (__stdcall* pSet)(int, void*))
{
	if(handle == NULL)
		return eAct == MM::BeforeGet ? DEVICE_OK : DEVICE_ERR;

	switch(eAct)
	{
	case MM::BeforeGet:
		{
			if(pGet != NULL && (*pGet)(&velocity, handle) != PI_NO_ERROR)
				return DEVICE_ERR;

			pProp->Set((long)velocity);

			break;
		}
	case MM::AfterSet:
		{
			long vel_temp = (long) velocity;
			pProp->Get(vel_temp);
			velocity = (int)vel_temp;

			if(pSet != NULL && (*pSet)(velocity, handle) != PI_NO_ERROR)
				return DEVICE_ERR;

			break;
		}
	}

	return DEVICE_OK;
}

// Similar to the above routine, this one handles the OnSerialNumber PropertyAction.
inline static int OnSerialGeneric(MM::PropertyBase* pProp, MM::ActionType eAct, MM::Core& core, MM::Device& self, int& serial, bool twister, int serialidx)
{
	switch(eAct)
	{
	case MM::BeforeGet:
		{
			if(serial == DEFAULT_SERIAL_UNKNOWN)
			{
				if(twister)
					serial = CPiDetector::GetInstance(core, self)->GetTwisterSerial(serialidx);
				else
					serial = CPiDetector::GetInstance(core, self)->GetMotorSerial(serialidx);

				int error = self.Initialize();
				if(error != DEVICE_OK)
					return error;
			}

			pProp->Set((long)serial);
		}
	case MM::AfterSet:
		{
			long serial_temp = (long)serial;
			pProp->Get(serial_temp);
			serial = (int)serial_temp;

			return self.Initialize();
		}
	}

	return DEVICE_OK;
}

///////////////////////////////////////////////////////////////////////////////
// Exported MMDevice API
///////////////////////////////////////////////////////////////////////////////

/**
 * List all supported hardware devices here Do not discover devices at runtime.
 * To avoid warnings about missing DLLs, Micro-Manager maintains a list of
 * supported device (MMDeviceList.txt).  This list is generated using
 * information supplied by this function, so runtime discovery will create
 * problems.
 */
MODULE_API void InitializeModuleData()
{
	RegisterDevice(g_TwisterDeviceName, MM::StageDevice, "Twister");
	RegisterDevice(g_StageDeviceName, MM::StageDevice, "Z stage");
	RegisterDevice(g_XYStageDeviceName, MM::XYStageDevice, "XY stage");
}

MODULE_API MM::Device* CreateDevice(const char* deviceName)
{
	if (deviceName == 0)
		return 0;

	// decide which device class to create based on the deviceName parameter
	if (strcmp(deviceName, g_TwisterDeviceName) == 0)
	{
		// create twister
		return new CSIABTwister();
	}
	else if (strcmp(deviceName, g_StageDeviceName) == 0)
	{
		// create stage
		return new CSIABStage();
	}
	else if (strcmp(deviceName, g_XYStageDeviceName) == 0)
	{
		// create X/Y stage
		return new CSIABXYStage();
	}

	// ...supplied name not recognized
	return 0;
}

MODULE_API void DeleteDevice(MM::Device* pDevice)
{
	delete pDevice;
}

// The twister

CSIABTwister::CSIABTwister()
: serial_(DEFAULT_SERIAL_UNKNOWN), handle_(NULL)
{
	CPropertyAction* pAct = new CPropertyAction (this, &CSIABTwister::OnSerialNumber);
	CreateProperty(g_Keyword_SerialNumber, FIXED_TO_STRING(DEFAULT_SERIAL_UNKNOWN), MM::String, false, pAct, true);
	SetErrorText(1, "Could not initialize twister");

	CreateProperty(g_Keyword_Velocity, FIXED_TO_STRING(MOTOR_MAX_VELOCITY), MM::Integer, false, new CPropertyAction(this, &CSIABTwister::OnVelocity), false);
	vector<string> vels;
	GenerateAllowedVelocities(vels);
	SetAllowedValues(g_Keyword_Velocity, vels);

	CreateProperty(g_Keyword_Min, FIXED_TO_STRING(TWISTER_LOWER_LIMIT), MM::Integer, false, NULL, true);
	CreateProperty(g_Keyword_Max, FIXED_TO_STRING(TWISTER_UPPER_LIMIT), MM::Integer, false, NULL, true);

	CreateProperty(g_Keyword_StepSize, FIXED_TO_STRING(TWISTER_STEP_SIZE), MM::Float, false, NULL, true);
}

CSIABTwister::~CSIABTwister()
{
}

int CSIABTwister::OnSerialNumber(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	// Usually only 1 twister, so expect index 0.
	return OnSerialGeneric(pProp, eAct, *GetCoreCallback(), *this, serial_, true, 0);
}

int CSIABTwister::OnVelocity(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	return OnVelocityGeneric(pProp, eAct, handle_, velocity_, &piGetTwisterVelocity, NULL);
}

bool CSIABTwister::Busy()
{
	if(handle_ == NULL)
		return false;

	BOOL moving;
	if (handle_ && !piGetTwisterMovingStatus(&moving, handle_))
		return moving != 0;
	return false;
}

double CSIABTwister::GetDelayMs() const
{
	return 0;
}

void CSIABTwister::SetDelayMs(double delay)
{
}

bool CSIABTwister::UsesDelay()
{
	return false;
}

int CSIABTwister::Initialize()
{
	int error = -1;
	handle_ = piConnectTwister(&error, serial_);

	if (handle_)
		piGetTwisterVelocity(&velocity_, handle_);
	else
		LogMessage(VarFormat("Could not initialize twister %d (error code %d)", serial_, error), false);

	return handle_ ? 0 : 1;
}

int CSIABTwister::Shutdown()
{
	if (handle_) {
		piDisconnectTwister(handle_);
		handle_ = NULL;
	}
	return 0;
}

void CSIABTwister::GetName(char* name) const
{
	CDeviceUtils::CopyLimitedString(name, g_TwisterDeviceName);
}

int CSIABTwister::SetPositionUm(double pos)
{
	if(handle_ == NULL)
		return DEVICE_ERR;

	double min = MOTOR_LOWER_LIMIT, max = MOTOR_UPPER_LIMIT;
	int error = DEVICE_OK;

	if((error = GetLimits(min, max)) != DEVICE_OK)
		return error;

	pos = pos < min ? min : (pos > max ? max : pos); // Clamp to min..max

	int to = (int)(pos / GetStepSizeUm());

	int moveret = piRunTwisterToPosition(to, velocity_, handle_);

	int at = 0;
	if(piGetTwisterPosition(&at, handle_) != PI_NO_ERROR)
		return DEVICE_ERR;

	if(at != (int)pos) {
		clock_t start = clock();
		clock_t last = start;
		while(!Busy() && at != (int)pos && CLOCKDIFF(last = clock(), start) < MAX_WAIT) {
			CDeviceUtils::SleepMs(0);

			if(piGetTwisterPosition(&at, handle_) != PI_NO_ERROR)
				return DEVICE_ERR;
		};

		if(CLOCKDIFF(last, start) >= MAX_WAIT)
			LogMessage(VarFormat("Long wait (twister): %d / %d (%d != %d).", last - start, (int)(MAX_WAIT*CLOCKS_PER_SEC), at, (int)pos), true);
	};

	return moveret;
}

int CSIABTwister::Move(double velocity)
{
	velocity_ = (int)velocity;
	return DEVICE_ERR;
}

int CSIABTwister::SetAdapterOriginUm(double d)
{
	return DEVICE_ERR;
}

int CSIABTwister::GetPositionUm(double& pos)
{
	if(handle_ == NULL)
		return DEVICE_ERR;

	int position;
	if (piGetTwisterPosition(&position, handle_))
		return DEVICE_ERR;
	pos = position;
	return DEVICE_OK;
}

double CSIABTwister::GetStepSizeUm()
{
	int error = DEVICE_OK;
	double stepsize = TWISTER_STEP_SIZE;

	if((error = GetProperty(g_Keyword_StepSize, stepsize)) != DEVICE_OK)
		return error;

	// This is technically wrong, since the step size is not in um, but in degrees.
	// MM does not have a concept of a rotational stage, however, so 'overload' this.
	return stepsize;
}

int CSIABTwister::SetPositionSteps(long steps)
{
	return DEVICE_ERR;
}

int CSIABTwister::GetPositionSteps(long& steps)
{
	return DEVICE_ERR;
}

int CSIABTwister::SetOrigin()
{
	return DEVICE_ERR;
}

int CSIABTwister::GetLimits(double& lower, double& upper)
{
	int error = DEVICE_OK;

	if((error = GetProperty(g_Keyword_Min, lower)) != DEVICE_OK)
		return error;

	if((error = GetProperty(g_Keyword_Max, upper)) != DEVICE_OK)
		return error;

	return DEVICE_OK;
}

int CSIABTwister::IsStageSequenceable(bool& isSequenceable) const
{
	isSequenceable = false;
	return DEVICE_OK;
}

int CSIABTwister::GetStageSequenceMaxLength(long& nrEvents) const
{
	nrEvents = 0;
	return DEVICE_OK;
}

int CSIABTwister::StartStageSequence() const
{
	return DEVICE_OK;
}

int CSIABTwister::StopStageSequence() const
{
	return DEVICE_OK;
}

int CSIABTwister::ClearStageSequence()
{
	return DEVICE_OK;
}

int CSIABTwister::AddToStageSequence(double position)
{
	return DEVICE_OK;
}

int CSIABTwister::SendStageSequence() const
{
	return DEVICE_OK;
}

bool CSIABTwister::IsContinuousFocusDrive() const
{
	return false;
}

// The Stage

CSIABStage::CSIABStage()
: serial_(DEFAULT_SERIAL_UNKNOWN), handle_(NULL)
{
	CreateProperty(g_Keyword_SerialNumber, FIXED_TO_STRING(DEFAULT_SERIAL_UNKNOWN), MM::Integer, false, new CPropertyAction (this, &CSIABStage::OnSerialNumber), true);

	CreateProperty(g_Keyword_Velocity, FIXED_TO_STRING(MOTOR_MAX_VELOCITY), MM::Integer, false, new CPropertyAction (this, &CSIABStage::OnVelocity), false);
	std::vector<std::string> allowed_velocities;
	GenerateAllowedVelocities(allowed_velocities);
	SetAllowedValues(g_Keyword_Velocity, allowed_velocities);

	CreateProperty(g_Keyword_StepSize, FIXED_TO_STRING(MOTOR_STEP_SIZE), MM::Float, false, NULL, true);

	CreateProperty(g_Keyword_Min, FIXED_TO_STRING(MOTOR_LOWER_LIMIT), MM::Integer, false, NULL, true);
	CreateProperty(g_Keyword_Max, FIXED_TO_STRING(MOTOR_UPPER_LIMIT), MM::Integer, false, NULL, true);

	SetErrorText(1, "Could not initialize motor (Z stage)");
}

CSIABStage::~CSIABStage()
{
}

int CSIABStage::OnSerialNumber(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	// Index derived via magic. (The Z stage is presumed to be the 3rd index in numerical order.)
	return OnSerialGeneric(pProp, eAct, *GetCoreCallback(), *this, serial_, false, 2);
}

int CSIABStage::OnVelocity(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	return OnVelocityGeneric(pProp, eAct, handle_, velocity_, &piGetMotorVelocity, &piSetMotorVelocity);
}

bool CSIABStage::Busy()
{
	if(handle_ == NULL)
		return false;

	BOOL moving;
	if (handle_ && !piGetMotorMovingStatus(&moving, handle_))
		return moving != 0;
	return false;
}

double CSIABStage::GetDelayMs() const
{
	return 0;
}

void CSIABStage::SetDelayMs(double delay)
{
}

bool CSIABStage::UsesDelay()
{
	return false;
}

int CSIABStage::Initialize()
{
	if(handle_)
		Shutdown();

	int error = -1;
	handle_ = piConnectMotor(&error, serial_);
	if (handle_)
		piGetMotorVelocity(&velocity_, handle_);
	else
		LogMessage(VarFormat("Could not initialize motor %i (error code %i)", serial_, error));

	return handle_ ? 0 : 1;
}

int CSIABStage::Shutdown()
{
	if (handle_) {
		piDisconnectMotor(handle_);
		handle_ = NULL;
	}
	return 0;
}

void CSIABStage::GetName(char* name) const
{
	CDeviceUtils::CopyLimitedString(name, g_StageDeviceName);
}

double CSIABStage::GetStepSizeUm()
{
	double out = 0;

	if(GetProperty(g_Keyword_StepSize, out) != DEVICE_OK)
		return 0;

	return out;
}

int CSIABStage::SetPositionUm(double pos)
{
	if(handle_ == NULL)
		return DEVICE_ERR;

	double min = MOTOR_LOWER_LIMIT, max = MOTOR_UPPER_LIMIT;
	int error = DEVICE_OK;

	if((error = GetLimits(min, max)) != DEVICE_OK)
		return error;

	pos = pos < min ? min : (pos > max ? max : pos); // Clamp to min..max

	int to = (int)(pos / GetStepSizeUm());

	int moveret = piRunMotorToPosition(to, velocity_, handle_);

	int at = 0;
	if(piGetMotorPosition(&at, handle_) != PI_NO_ERROR)
		return DEVICE_ERR;

	// WORKAROUND: piRunMotorToPosition doesn't wait for the motor to get
	// underway. Wait a bit here.
	if(at != to) {
		clock_t start = clock();
		clock_t last = start;
		while(!Busy() && at != to && CLOCKDIFF(last = clock(), start) < MAX_WAIT) {
			CDeviceUtils::SleepMs(0);

			if(piGetMotorPosition(&at, handle_) != PI_NO_ERROR)
				return DEVICE_ERR;
		};

		if(CLOCKDIFF(last, start) >= MAX_WAIT)
			LogMessage(VarFormat("Long wait (Z stage): %d / %d (%d != %d).", last - start, (int)(MAX_WAIT*CLOCKS_PER_SEC), at, to), true);
	};

	return moveret;
}

int CSIABStage::SetRelativePositionUm(double d)
{
	double position;
	int err = GetPositionUm(position);
	if(err != DEVICE_OK)
		return err;

	return SetPositionUm(position + d);
}

int CSIABStage::Move(double velocity)
{
	velocity_ = (int)velocity;
	return DEVICE_ERR;
}

int CSIABStage::SetAdapterOriginUm(double d)
{
	return DEVICE_ERR;
}

int CSIABStage::GetPositionUm(double& pos)
{
	if(handle_ == NULL)
		return DEVICE_ERR;

	int position;
	if (piGetMotorPosition(&position, handle_))
		return DEVICE_ERR;
	pos = position * GetStepSizeUm();
	return DEVICE_OK;
}

int CSIABStage::SetPositionSteps(long steps)
{
	return DEVICE_ERR;
}

int CSIABStage::GetPositionSteps(long& steps)
{
	return DEVICE_ERR;
}

int CSIABStage::SetOrigin()
{
	return DEVICE_ERR;
}

int CSIABStage::GetLimits(double& lower, double& upper)
{
	int error = DEVICE_OK;

	if((error = GetProperty(g_Keyword_Min, lower)) != DEVICE_OK)
		return error;

	if((error = GetProperty(g_Keyword_Max, upper)) != DEVICE_OK)
		return error;

	return DEVICE_OK;
}

int CSIABStage::IsStageSequenceable(bool& isSequenceable) const
{
	return false;
}

int CSIABStage::GetStageSequenceMaxLength(long& nrEvents) const
{
	nrEvents = 0;
	return DEVICE_OK;
}

int CSIABStage::StartStageSequence() const
{
	return DEVICE_OK;
}

int CSIABStage::StopStageSequence() const
{
	return DEVICE_OK;
}

int CSIABStage::ClearStageSequence()
{
	return DEVICE_OK;
}

int CSIABStage::AddToStageSequence(double position)
{
	return DEVICE_OK;
}

int CSIABStage::SendStageSequence() const
{
	return DEVICE_OK;
}

bool CSIABStage::IsContinuousFocusDrive() const
{
	return false;
}

// The XY Stage
enum XYSTAGE_ERRORS {
	XYERR_INIT_X = PICARDSTAGE_ERROR_OFFSET,
	XYERR_INIT_Y,
	XYERR_MOVE_X,
	XYERR_MOVE_Y
};

CSIABXYStage::CSIABXYStage()
: serialX_(DEFAULT_SERIAL_UNKNOWN), serialY_(DEFAULT_SERIAL_UNKNOWN), handleX_(NULL), handleY_(NULL)
{
	CreateProperty(g_Keyword_SerialNumberX, FIXED_TO_STRING(DEFAULT_SERIAL_UNKNOWN), MM::Integer, false, new CPropertyAction (this, &CSIABXYStage::OnSerialNumberX), true);
	CreateProperty(g_Keyword_SerialNumberY, FIXED_TO_STRING(DEFAULT_SERIAL_UNKNOWN), MM::Integer, false, new CPropertyAction (this, &CSIABXYStage::OnSerialNumberY), true);

	SetErrorText(XYERR_INIT_X, "Could not initialize motor (X stage)");
	SetErrorText(XYERR_INIT_Y, "Could not initialize motor (Y stage)");
	SetErrorText(XYERR_MOVE_X, "X stage out of range.");
	SetErrorText(XYERR_MOVE_Y, "Y stage out of range.");

	CreateProperty(g_Keyword_VelocityX, FIXED_TO_STRING(MOTOR_MAX_VELOCITY), MM::Integer, false, new CPropertyAction(this, &CSIABXYStage::OnVelocityX), false);
	CreateProperty(g_Keyword_VelocityY, FIXED_TO_STRING(MOTOR_MAX_VELOCITY), MM::Integer, false, new CPropertyAction(this, &CSIABXYStage::OnVelocityY), false);

	std::vector<std::string> allowed_values = std::vector<std::string>();
	GenerateAllowedVelocities(allowed_values);
	SetAllowedValues(g_Keyword_VelocityX, allowed_values);
	SetAllowedValues(g_Keyword_VelocityY, allowed_values);

	CreateProperty(g_Keyword_MinX, FIXED_TO_STRING(MOTOR_LOWER_LIMIT), MM::Integer, false, NULL, true);
	CreateProperty(g_Keyword_MaxX, FIXED_TO_STRING(MOTOR_UPPER_LIMIT), MM::Integer, false, NULL, true);
	CreateProperty(g_Keyword_MinY, FIXED_TO_STRING(MOTOR_LOWER_LIMIT), MM::Integer, false, NULL, true);
	CreateProperty(g_Keyword_MaxY, FIXED_TO_STRING(MOTOR_UPPER_LIMIT), MM::Integer, false, NULL, true);

	CreateProperty(g_Keyword_StepSizeX, FIXED_TO_STRING(MOTOR_STEP_SIZE), MM::Float, false, NULL, true);
	CreateProperty(g_Keyword_StepSizeY, FIXED_TO_STRING(MOTOR_STEP_SIZE), MM::Float, false, NULL, true);
}

CSIABXYStage::~CSIABXYStage()
{
}

int CSIABXYStage::InitStage(void** handleptr, int newserial)
{
	int* velptr = NULL;

	if(handleptr == &handleX_) {
		serialX_ = newserial;
		velptr = &velocityX_;
	} else if(handleptr == &handleY_) {
		serialY_ = newserial;
		velptr = &velocityY_;
	} else {
		return DEVICE_ERR;
	};

	if(*handleptr != NULL)
		ShutdownStage(handleptr);

	int error = -1;
	*handleptr = piConnectMotor(&error, newserial); // assignment intentional

	if(*handleptr != NULL) {
		piGetMotorVelocity(velptr, *handleptr);
	} else {
		LogMessage(VarFormat("Could not initialize motor %d (error code %d)\n", newserial, error));
		return DEVICE_ERR;
	}

	return DEVICE_OK;
}

void CSIABXYStage::ShutdownStage(void** handleptr)
{
	if(*handleptr != NULL)
		piDisconnectMotor(*handleptr);

	*handleptr = NULL;
}

int CSIABXYStage::OnSerialNumberX(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	return OnSerialGeneric(pProp, eAct, *GetCoreCallback(), *this, serialX_, false, 0); // X is (usually) the first stage serial.
}

int CSIABXYStage::OnSerialNumberY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	return OnSerialGeneric(pProp, eAct, *GetCoreCallback(), *this, serialY_, false, 1); // And Y is (usually) the second stage serial.
}

int CSIABXYStage::OnVelocityX(MM::PropertyBase *pProp, MM::ActionType eAct)
{
	return OnVelocityGeneric(pProp, eAct, handleX_, velocityX_, &piGetMotorVelocity, &piSetMotorVelocity);
}

int CSIABXYStage::OnVelocityY(MM::PropertyBase *pProp, MM::ActionType eAct)
{
	return OnVelocityGeneric(pProp, eAct, handleY_, velocityY_, &piGetMotorVelocity, &piSetMotorVelocity);
}

bool CSIABXYStage::Busy()
{
	BOOL movingX = FALSE, movingY = FALSE;
	if (handleX_)
		piGetMotorMovingStatus(&movingX, handleX_);
	if (handleY_)
		piGetMotorMovingStatus(&movingY, handleY_);
	return movingX != FALSE || movingY != FALSE;
}

double CSIABXYStage::GetDelayMs() const
{
	return 0;
}

void CSIABXYStage::SetDelayMs(double delay)
{
}

bool CSIABXYStage::UsesDelay()
{
	return false;
}

int CSIABXYStage::Initialize()
{
	if(serialX_ != DEFAULT_SERIAL_UNKNOWN)
		if(InitStage(&handleX_, serialX_) != DEVICE_OK)
			return XYERR_INIT_X;

	if(serialY_ != DEFAULT_SERIAL_UNKNOWN)
		if(InitStage(&handleY_, serialY_) != DEVICE_OK)
			return XYERR_INIT_Y;

	return DEVICE_OK;
}

int CSIABXYStage::Shutdown()
{
	ShutdownStage(&handleX_);
	ShutdownStage(&handleY_);
	return 0;
}

void CSIABXYStage::GetName(char* name) const
{
	CDeviceUtils::CopyLimitedString(name, g_XYStageDeviceName);
}

int CSIABXYStage::SetPositionUm(double x, double y)
{
	if(handleX_ == NULL || handleY_ == NULL)
		return DEVICE_ERR;

	double minX = MOTOR_LOWER_LIMIT, maxX = MOTOR_UPPER_LIMIT, minY = MOTOR_LOWER_LIMIT, maxY = MOTOR_UPPER_LIMIT;
	int error = DEVICE_OK;

	if((error = GetLimitsUm(minX, maxX, minY, maxY)) != DEVICE_OK)
		return error;

	x = x < minX ? minX : (x > maxX ? maxX : x);
	y = y < minY ? minY : (y > maxY ? maxY : y);

	int toX = (int)(x / GetStepSizeXUm());
	int toY = (int)(y / GetStepSizeYUm());

	int moveX = piRunMotorToPosition(toX, velocityX_, handleX_);
	int moveY = piRunMotorToPosition(toY, velocityY_, handleY_) << 1;

	int atX, atY;

	if(piGetMotorPosition(&atX, handleX_) != PI_NO_ERROR || piGetMotorPosition(&atY, handleY_) != PI_NO_ERROR)
		return DEVICE_ERR;

	if(atX != toX || atY != toY) {
		clock_t start = clock();
		clock_t last = start;
		while(!Busy() && (atX != toX || atY != toY) && CLOCKDIFF(last = clock(), start) < MAX_WAIT) {
			CDeviceUtils::SleepMs(0);

			if(piGetMotorPosition(&atX, handleX_) != PI_NO_ERROR || piGetMotorPosition(&atY, handleY_) != PI_NO_ERROR)
				return DEVICE_ERR;
		};

		if(CLOCKDIFF(last, start) >= MAX_WAIT)
			LogMessage(VarFormat("Long wait (XY): %d / %d (%d != %d || %d != %d).", last - start, (int)(MAX_WAIT*CLOCKS_PER_SEC), atX, toX, atY, toY), true);
	};

	return moveX | moveY;
}

int CSIABXYStage::SetRelativePositionUm(double dx, double dy)
{
	double positionX, positionY;
	int err = GetPositionUm(positionX, positionY);
	if(err != DEVICE_OK)
		return err;

	return SetPositionUm(positionX + dx, positionY + dy);
}

int CSIABXYStage::SetAdapterOriginUm(double x, double y)
{
	return 0;
}

int CSIABXYStage::GetPositionUm(double& x, double& y)
{
	if(handleX_ == NULL || handleY_ == NULL)
		return DEVICE_ERR;

	int positionX, positionY;
	if (piGetMotorPosition(&positionX, handleX_) ||
			piGetMotorPosition(&positionY, handleY_))
		return DEVICE_ERR;

	x = positionX * GetStepSizeXUm();
	y = positionY * GetStepSizeYUm();

	return DEVICE_OK;
}

int CSIABXYStage::GetLimitsUm(double& xMin, double& xMax, double& yMin, double& yMax)
{
	int error = DEVICE_OK;

	if((error = GetProperty(g_Keyword_MinX, xMin)) != DEVICE_OK)
		return error;

	if((error = GetProperty(g_Keyword_MaxX, xMax)) != DEVICE_OK)
		return error;

	if((error = GetProperty(g_Keyword_MinY, yMin)) != DEVICE_OK)
		return error;

	if((error = GetProperty(g_Keyword_MaxY, yMax)) != DEVICE_OK)
		return error;

	return DEVICE_OK;
}

int CSIABXYStage::Move(double vx, double vy)
{
	velocityX_ = (int)vx;
	velocityY_ = (int)vy;
	return 0;
}

int CSIABXYStage::SetPositionSteps(long x, long y)
{
	return DEVICE_ERR;
}

int CSIABXYStage::GetPositionSteps(long& x, long& y)
{
	return DEVICE_ERR;
}

int CSIABXYStage::SetRelativePositionSteps(long x, long y)
{
	return DEVICE_ERR;
}

int CSIABXYStage::Home()
{
	return DEVICE_ERR;
}

int CSIABXYStage::Stop()
{
	return DEVICE_ERR;
}

int CSIABXYStage::SetOrigin()
{
	return DEVICE_ERR;
}

int CSIABXYStage::GetStepLimits(long& xMin, long& xMax, long& yMin, long& yMax)
{
	return DEVICE_ERR;
}

double CSIABXYStage::GetStepSizeXUm()
{
	double out = 0;
	if(GetProperty(g_Keyword_StepSizeX, out) != DEVICE_OK)
		return 0;
	return out;
}

double CSIABXYStage::GetStepSizeYUm()
{
	double out = 0;
	if(GetProperty(g_Keyword_StepSizeY, out) != DEVICE_OK)
		return 0;
	return out;
}

int CSIABXYStage::IsXYStageSequenceable(bool& isSequenceable) const
{
	isSequenceable = false;
	return DEVICE_OK;
}
