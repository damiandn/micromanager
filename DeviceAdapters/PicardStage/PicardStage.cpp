///////////////////////////////////////////////////////////////////////////////
// FILE:          PicardStage.cpp
// PROJECT:       Micro-Manager
// SUBSYSTEM:     DeviceAdapters
//-----------------------------------------------------------------------------
// DESCRIPTION:   The drivers required for the Picard Industries USB stages
//
// AUTHOR:        Johannes Schindelin, 2011; Luke Stuyvenberg, 2011 - 2014
//
// COPYRIGHT:     Johannes Schindelin, Copyright (C) 2011 - 2014
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

#include "ModuleInterface.h"
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
const char* g_Keyword_MinX = "X-Min";
const char* g_Keyword_MaxX = "X-Max";
const char* g_Keyword_MinY = "Y-Min";
const char* g_Keyword_MaxY = "Y-Max";
const char* g_Keyword_Velocity = "Velocity";
const char* g_Keyword_VelocityX = "X-Velocity";
const char* g_Keyword_VelocityY = "Y-Velocity";
const char* g_Keyword_StepSize = "StepSize";
const char* g_Keyword_StepSizeX = "X-StepSize";
const char* g_Keyword_StepSizeY = "Y-StepSize";

#define CLOCKDIFF(now, then) (((double)(now) - (double)(then))/((double)(CLOCKS_PER_SEC)))
#define MAX_WAIT 0.05 // Maximum time to wait for the motors to begin motion, in seconds.

#define MAX_IDX 250

#define LOWER_TWISTER_LIMIT -32767
#define UPPER_TWISTER_LIMIT 32767

#define MOTOR_MIN_VELOCITY 1
#define MOTOR_MAX_VELOCITY 10

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

		return -1;
	}

	int GetTwisterSerial(int idx)
	{
		if(idx < m_iTwisterCount)
			return m_pTwisterList[idx];

		return -1;
	}

	private:
	int PingDevices(MM::Core& core, MM::Device& device, void* (__stdcall* connfn)(int*, int), void (__stdcall* discfn)(void*), int* pOutArray, const int iMax, int* pOutCount)
	{
		void* handle = NULL;
		int error = 0;
		int count = 0;
		for(int idx = 0; idx < MAX_IDX && count < iMax; ++idx)
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

	for(int i = MOTOR_MIN_VELOCITY; i <= MOTOR_MAX_VELOCITY; ++i)
		vels.push_back(VarFormat("%d", i));
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

#ifdef _DEBUG
	static bool bMessaged = false;

	if(!bMessaged) {
		std::cout << "WARNING: The PicardStage device adapter has been built in DEBUG MODE. This version should NOT be uploaded to the update site!" << endl;
		bMessaged = true;
	}
#endif
}

MODULE_API MM::Device* CreateDevice(const char* deviceName)
{
	if (deviceName == 0)
		return 0;

	// decide which device class to create based on the deviceName parameter
	if (strcmp(deviceName, g_TwisterDeviceName) == 0)
	{
		// create stage
		return new CSIABTwister();
	}
	else if (strcmp(deviceName, g_StageDeviceName) == 0)
	{
		// create stage
	return new CSIABStage();
	}
	else if (strcmp(deviceName, g_XYStageDeviceName) == 0)
	{
		// create stage
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
: serial_(-1), handle_(NULL)
{
	char buf[16];
	_itoa(serial_, buf, 10);

	CPropertyAction* pAct = new CPropertyAction (this, &CSIABTwister::OnSerialNumber);
	CreateProperty(g_Keyword_SerialNumber, buf, MM::String, false, pAct, true);
	SetErrorText(1, "Could not initialize twister");
}

CSIABTwister::~CSIABTwister()
{
}

int CSIABTwister::OnSerialNumber(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
	{
		if(serial_ < 0)
		{
			serial_ = CPiDetector::GetInstance(*GetCoreCallback(), *this)->GetTwisterSerial(0); // Usually only 1 twister.

			int error = Initialize();
			if(error != DEVICE_OK)
				return error;
		}

		pProp->Set((long)serial_);
	}
	else if (eAct == MM::AfterSet)
	{
		long serial;
		pProp->Get(serial);
		serial_ = (int)serial;

		return Initialize();
	}

	return DEVICE_OK;
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

	int moveret = piRunTwisterToPosition((int)pos, velocity_, handle_);

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
	lower = LOWER_TWISTER_LIMIT;
	upper = UPPER_TWISTER_LIMIT;
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
: serial_(-1), handle_(NULL)
{
	char buf[16];
	_itoa(serial_, buf, 10);

	CreateProperty(g_Keyword_SerialNumber, buf, MM::Integer, false, new CPropertyAction (this, &CSIABStage::OnSerialNumber), true);

	CreateProperty(g_Keyword_Velocity, "10", MM::Integer, false, new CPropertyAction (this, &CSIABStage::OnVelocity), false);
	std::vector<std::string> allowed_velocities ();
	GenerateAllowedVelocities(allowed_velocities);
	SetAllowedValues(g_Keyword_Velocity, allowed_velocities);

	CreateProperty(g_Keyword_StepSize, "1.5", MM::Float, false);

	SetErrorText(1, "Could not initialize motor (Z stage)");
}

CSIABStage::~CSIABStage()
{
}

int CSIABStage::OnSerialNumber(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
	{
		if(serial_ < 0)
		{
			// Index derived via magic. (The Z stage is presumed to be the 3rd index in numerical order.)
			serial_ = CPiDetector::GetInstance(*GetCoreCallback(), *this)->GetMotorSerial(2);

			int error = Initialize();
			if(error != DEVICE_OK)
				return error;
		}

		// instead of relying on stored state we could actually query the device
		pProp->Set((long)serial_);
	}
	else if (eAct == MM::AfterSet)
	{
		long serial;
		pProp->Get(serial);
		serial_ = (int)serial;

		return Initialize();
	}

	return DEVICE_OK;
}

int CSIABStage::OnVelocity(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
	{
		// instead of relying on stored state we could actually query the device
		pProp->Set((long)velocity_);
	}
	else if (eAct == MM::AfterSet)
	{
		long velocity;
		pProp->Get(velocity);
		velocity_ = (int)velocity;
	}
	return DEVICE_OK;
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
	lower = 1;
	// TODO: make this a property; the USB Motor I has an upper limit of 2000
	upper = 8000;
	return 0;
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
: serialX_(-1), serialY_(-1),
  handleX_(NULL), handleY_(NULL), minX_(1), minY_(1), maxX_(8000), maxY_(8000)
{
	char buf[16];

	_itoa(serialX_, buf, 10);
	CreateProperty(g_Keyword_SerialNumberX, buf, MM::Integer, false, new CPropertyAction (this, &CSIABXYStage::OnSerialNumberX), true);

	_itoa(serialY_, buf, 10);
	CreateProperty(g_Keyword_SerialNumberY, buf, MM::Integer, false, new CPropertyAction (this, &CSIABXYStage::OnSerialNumberY), true);

	SetErrorText(XYERR_INIT_X, "Could not initialize motor (X stage)");
	SetErrorText(XYERR_INIT_Y, "Could not initialize motor (Y stage)");
	SetErrorText(XYERR_MOVE_X, "X stage out of range.");
	SetErrorText(XYERR_MOVE_Y, "Y stage out of range.");

	CreateProperty(g_Keyword_VelocityX, "10", MM::Integer, false, new CPropertyAction(this, &CSIABXYStage::OnVelocityX), false);
	CreateProperty(g_Keyword_VelocityY, "10", MM::Integer, false, new CPropertyAction(this, &CSIABXYStage::OnVelocityY), false);

	std::vector<std::string> allowed_values = std::vector<std::string>();
	GenerateAllowedVelocities(allowed_values);
	SetAllowedValues(g_Keyword_VelocityX, allowed_values);
	SetAllowedValues(g_Keyword_VelocityY, allowed_values);

	CreateProperty(g_Keyword_MinX, "0", MM::Integer, false, new CPropertyAction(this, &CSIABXYStage::OnMinX), true);
	CreateProperty(g_Keyword_MaxX, "8000", MM::Integer, false, new CPropertyAction(this, &CSIABXYStage::OnMaxX), true);
	CreateProperty(g_Keyword_MinY, "0", MM::Integer, false, new CPropertyAction(this, &CSIABXYStage::OnMinY), true);
	CreateProperty(g_Keyword_MaxY, "8000", MM::Integer, false, new CPropertyAction(this, &CSIABXYStage::OnMaxY), true);

	CreateProperty(g_Keyword_StepSizeX, "1.5", MM::Float, false);
	CreateProperty(g_Keyword_StepSizeY, "1.5", MM::Float, false);
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
	if (eAct == MM::BeforeGet)
	{
		if(serialX_ < 0)
		{
			serialX_ = CPiDetector::GetInstance(*GetCoreCallback(), *this)->GetMotorSerial(0); // X is (usually) the first stage serial.

			if(InitStage(&handleX_, serialX_) != DEVICE_OK)
				return XYERR_INIT_X;
		}

		pProp->Set((long)serialX_);
	}
	else if (eAct == MM::AfterSet)
	{
		long serial;
		pProp->Get(serial);

		if(InitStage(&handleX_, serial) != DEVICE_OK)
			return XYERR_INIT_X;
	}
	return DEVICE_OK;
}

int CSIABXYStage::OnSerialNumberY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
	{
		if(serialY_ < 0)
		{
			serialY_ = CPiDetector::GetInstance(*GetCoreCallback(), *this)->GetMotorSerial(1); // And Y is (usually) the second stage serial.

			if(InitStage(&handleY_, serialY_) != DEVICE_OK)
				return XYERR_INIT_Y;
		}

		pProp->Set((long)serialY_);
	}
	else if (eAct == MM::AfterSet)
	{
		long serial;
		pProp->Get(serial);
		serialY_ = (int)serial;

		if(InitStage(&handleY_, serial) != DEVICE_OK)
			return XYERR_INIT_Y;
	}
	return DEVICE_OK;
}

int CSIABXYStage::OnMinX(MM::PropertyBase *pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
		pProp->Set((long)minX_);
	else if (eAct == MM::AfterSet)
		pProp->Get((long&)minX_);

	return DEVICE_OK;
}

int CSIABXYStage::OnMaxX(MM::PropertyBase *pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
		pProp->Set((long)maxX_);
	else if (eAct == MM::AfterSet)
		pProp->Get((long&)maxX_);

	return DEVICE_OK;
}

int CSIABXYStage::OnMinY(MM::PropertyBase *pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
		pProp->Set((long)minY_);
	else if (eAct == MM::AfterSet)
		pProp->Get((long&)minY_);

	return DEVICE_OK;
}

int CSIABXYStage::OnMaxY(MM::PropertyBase *pProp, MM::ActionType eAct)
{
	if (eAct == MM::BeforeGet)
		pProp->Set((long)maxY_);
	else if (eAct == MM::AfterSet)
		pProp->Get((long&)maxY_);

	return DEVICE_OK;
}

int CSIABXYStage::OnVelocityX(MM::PropertyBase *pProp, MM::ActionType eAct)
{
	if(handleX_ == NULL)
		return (eAct == MM::BeforeGet ? DEVICE_OK : DEVICE_ERR);

	if(eAct == MM::BeforeGet)
	{
		if(piGetMotorVelocity(&velocityX_, handleX_) != 0)
			return DEVICE_ERR;

		pProp->Set((long)velocityX_);
	}
	else if(eAct == MM::AfterSet)
	{
		pProp->Get((long&)velocityX_);

		return piSetMotorVelocity(velocityX_, handleX_);
	};

	return DEVICE_OK;
}

int CSIABXYStage::OnVelocityY(MM::PropertyBase *pProp, MM::ActionType eAct)
{
	if(handleY_ == NULL)
		return (eAct == MM::BeforeGet ? DEVICE_OK : DEVICE_ERR);

	if(eAct == MM::BeforeGet)
	{
		if(piGetMotorVelocity(&velocityY_, handleY_) != 0)
			return DEVICE_ERR;

		pProp->Set((long)velocityY_);
	}
	else if(eAct == MM::AfterSet)
	{
		pProp->Get((long&)velocityY_);

		return piSetMotorVelocity(velocityY_, handleY_);
	};

	return DEVICE_OK;
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
	InitStage(&handleX_, serialX_);
	InitStage(&handleY_, serialY_);

	return handleX_ ? (handleY_ ? DEVICE_OK : XYERR_INIT_Y) : XYERR_INIT_X;
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

void CSIABXYStage::GetOrientation(bool& mirrorX, bool& mirrorY)
{
	mirrorX = false;
	mirrorY = false;
}

int CSIABXYStage::SetPositionUm(double x, double y)
{
	if(handleX_ == NULL || handleY_ == NULL)
		return DEVICE_ERR;

	bool flipX, flipY;
	GetOrientation(flipX, flipY);

	if(x < minX_ || x > maxX_)
		x = min(maxX_, max(x, minX_));
	if(y < minY_ || y > maxY_)
		y = min(maxY_, max(y, minY_));

	int toX = (int)((flipX ? (maxX_ - x) + minX_ : x) / GetStepSizeXUm());
	int toY = (int)((flipY ? (maxY_ - y) + minY_ : y) / GetStepSizeYUm());

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

	bool flipX, flipY;
	GetOrientation(flipX, flipY);

	int positionX, positionY;
	if (piGetMotorPosition(&positionX, handleX_) ||
			piGetMotorPosition(&positionY, handleY_))
		return DEVICE_ERR;

	x = flipX ? (maxX_ - positionX) + minX_ : positionX;
	y = flipY ? (maxY_ - positionY) + minY_ : positionY;

	x = (x < minX_ ? minX_ : (x > maxX_ ? maxX_ : x)) * GetStepSizeXUm();
	y = (y < minY_ ? minY_ : (y > maxY_ ? maxY_ : y)) * GetStepSizeYUm();

	return DEVICE_OK;
}

int CSIABXYStage::GetLimitsUm(double& xMin, double& xMax, double& yMin, double& yMax)
{
	xMin = minX_;
	xMax = maxX_;
	yMin = minY_;
	yMax = maxY_;

	return 0;
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
