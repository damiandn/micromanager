///////////////////////////////////////////////////////////////////////////////
// FILE:          OpenSPIM.cpp
// PROJECT:       Micro-Manager
// SUBSYSTEM:     DeviceAdapters
//-----------------------------------------------------------------------------
// DESCRIPTION:   The drivers required for the OpenSPIM project
//                
// AUTHOR:        Johannes Schindelin, 2011
//
// COPYRIGHT:     Johannes Schindelin, 2011
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

#include "OpenSPIM.h"
#include "../../MMDevice/ModuleInterface.h"
#include "../../../3rdpartypublic/picard/PiUsb.h"

using namespace std;

// External names used used by the rest of the system
// to load particular device from the "OpenSPIM.dll" library
const char* g_TwisterDeviceName = "Picard Twister";
const char* g_StageDeviceName = "Picard Z Stage";
const char* g_XYStageDeviceName = "Picard XY Stage";
const char* g_Keyword_SerialNumber = "Serial Number";
const char* g_Keyword_SerialNumberX = "Serial Number (X)";
const char* g_Keyword_SerialNumberY = "Serial Number (Y)";

// windows DLL entry code
#ifdef WIN32
BOOL APIENTRY DllMain( HANDLE /*hModule*/, 
                      DWORD  ul_reason_for_call, 
                      LPVOID /*lpReserved*/
                      )
{
   switch (ul_reason_for_call)
   {
   case DLL_PROCESS_ATTACH:
   case DLL_THREAD_ATTACH:
   case DLL_THREAD_DETACH:
   case DLL_PROCESS_DETACH:
      break;
   }
   return TRUE;
}
#endif





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
   AddAvailableDeviceName(g_TwisterDeviceName, "Twister");
   AddAvailableDeviceName(g_StageDeviceName, "Z stage");
   AddAvailableDeviceName(g_XYStageDeviceName, "XY stage");

   if (DiscoverabilityTest())
   {
      SetDeviceIsDiscoverable(g_TwisterDeviceName, true); 
      SetDeviceIsDiscoverable(g_StageDeviceName, true); 
      SetDeviceIsDiscoverable(g_XYStageDeviceName, true);
   }
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
: serial_(20), handle_(NULL)
{
	CPropertyAction* pAct = new CPropertyAction (this, &CSIABTwister::OnSerialNumber);
	CreateProperty(g_Keyword_SerialNumber, "101", MM::String, false, pAct, true);
	SetErrorText(1, "Could not initialize twister");
}

CSIABTwister::~CSIABTwister()
{
}

int CSIABTwister::OnSerialNumber(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet)
   {
      // instead of relying on stored state we could actually query the device
      pProp->Set((long)serial_);
   }
   else if (eAct == MM::AfterSet)
   {
      long serial;
      pProp->Get(serial);
      serial_ = (int)serial;
   }
   return DEVICE_OK;
}

bool CSIABTwister::Busy()
{
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
	return piRunTwisterToPosition((int)pos, velocity_, handle_);
}

int CSIABTwister::SetRelativePositionUm(double d)
{
	return DEVICE_ERR;
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
	lower = 0;
	upper = 360;
	return 0;
}

int CSIABTwister::IsStageSequenceable(bool& isSequenceable) const
{
	isSequenceable = false;
	return 0;
}

int CSIABTwister::GetStageSequenceMaxLength(long& nrEvents) const
{
	nrEvents = 0;
	return DEVICE_ERR;
}

int CSIABTwister::StartStageSequence() const
{
	return DEVICE_ERR;
}

int CSIABTwister::StopStageSequence() const
{
	return DEVICE_ERR;
}

int CSIABTwister::ClearStageSequence()
{
	return DEVICE_ERR;
}

int CSIABTwister::AddToStageSequence(double position)
{
	return DEVICE_ERR;
}

int CSIABTwister::SendStageSequence() const
{
	return DEVICE_ERR;
}

bool CSIABTwister::IsContinuousFocusDrive() const
{
	return false;
}

// The Stage

CSIABStage::CSIABStage()
: serial_(107), handle_(NULL)
{
	CPropertyAction* pAct = new CPropertyAction (this, &CSIABStage::OnSerialNumber);
	CreateProperty(g_Keyword_SerialNumber, "107", MM::String, false, pAct, true);
	SetErrorText(1, "Could not initialize motor (Z stage)");
}

CSIABStage::~CSIABStage()
{
}

int CSIABStage::OnSerialNumber(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet)
   {
      // instead of relying on stored state we could actually query the device
      pProp->Set((long)serial_);
   }
   else if (eAct == MM::AfterSet)
   {
      long serial;
      pProp->Get(serial);
      serial_ = (int)serial;
   }
   return DEVICE_OK;
}

bool CSIABStage::Busy()
{
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
	int error = -1;
	handle_ = piConnectMotor(&error, serial_);
	if (handle_)
		piGetMotorVelocity(&velocity_, handle_);
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

int CSIABStage::SetPositionUm(double pos)
{
	return piRunMotorToPosition((int)pos, velocity_, handle_);
}

int CSIABStage::SetRelativePositionUm(double d)
{
	return 0;
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
	int position;
	if (piGetMotorPosition(&position, handle_))
		return DEVICE_ERR;
	pos = position;
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
	upper = 2000;
	return 0;
}

int CSIABStage::IsStageSequenceable(bool& isSequenceable) const
{
	return false;
}

int CSIABStage::GetStageSequenceMaxLength(long& nrEvents) const
{
	return DEVICE_ERR;
}

int CSIABStage::StartStageSequence() const
{
	return DEVICE_ERR;
}

int CSIABStage::StopStageSequence() const
{
	return DEVICE_ERR;
}

int CSIABStage::ClearStageSequence()
{
	return DEVICE_ERR;
}

int CSIABStage::AddToStageSequence(double position)
{
	return DEVICE_ERR;
}

int CSIABStage::SendStageSequence() const
{
	return DEVICE_ERR;
}

bool CSIABStage::IsContinuousFocusDrive() const
{
	return false;
}

// The XY Stage

CSIABXYStage::CSIABXYStage()
: serialX_(105), serialY_(106), handleX_(NULL), handleY_(NULL)
{
	CPropertyAction* pActX = new CPropertyAction (this, &CSIABXYStage::OnSerialNumberX);
	CPropertyAction* pActY = new CPropertyAction (this, &CSIABXYStage::OnSerialNumberY);
	CreateProperty(g_Keyword_SerialNumberX, "105", MM::String, false, pActX, true);
	CreateProperty(g_Keyword_SerialNumberY, "106", MM::String, false, pActY, true);
	SetErrorText(1, "Could not initialize motor (X stage)");
	SetErrorText(2, "Could not initialize motor (Y stage)");
}

CSIABXYStage::~CSIABXYStage()
{
}

int CSIABXYStage::OnSerialNumberX(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet)
   {
      // instead of relying on stored state we could actually query the device
      pProp->Set((long)serialX_);
   }
   else if (eAct == MM::AfterSet)
   {
      long serial;
      pProp->Get(serial);
      serialX_ = (int)serial;
   }
   return DEVICE_OK;
}

int CSIABXYStage::OnSerialNumberY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   if (eAct == MM::BeforeGet)
   {
      // instead of relying on stored state we could actually query the device
      pProp->Set((long)serialY_);
   }
   else if (eAct == MM::AfterSet)
   {
      long serial;
      pProp->Get(serial);
      serialY_ = (int)serial;
   }
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
	int errorX = -1, errorY = -1;
	handleX_ = piConnectMotor(&errorX, serialX_);
	if (handleX_)
		piGetMotorVelocity(&velocityX_, handleX_);
	handleY_ = piConnectMotor(&errorY, serialY_);
	if (handleY_)
		piGetMotorVelocity(&velocityY_, handleY_);
	return handleX_ ? (handleY_ ? 0 : 2) : 1;
}

int CSIABXYStage::Shutdown()
{
	if (handleX_) {
		piDisconnectMotor(handleX_);
		handleX_ = NULL;
	}
	if (handleY_) {
		piDisconnectMotor(handleY_);
		handleY_ = NULL;
	}
	return 0;
}

void CSIABXYStage::GetName(char* name) const
{
	CDeviceUtils::CopyLimitedString(name, g_XYStageDeviceName);
}

int CSIABXYStage::SetPositionUm(double x, double y)
{
	return piRunMotorToPosition((int)x, velocityX_, handleX_) ||
		piRunMotorToPosition((int)y, velocityY_, handleY_);
}

int CSIABXYStage::SetRelativePositionUm(double dx, double dy)
{
	return 0;
}

int CSIABXYStage::SetAdapterOriginUm(double x, double y)
{
	return 0;
}

int CSIABXYStage::GetPositionUm(double& x, double& y)
{
	int positionX, positionY;
	if (piGetMotorPosition(&positionX, handleX_) ||
			piGetMotorPosition(&positionY, handleY_))
		return DEVICE_ERR;
	x = positionX;
	y = positionY;
	return DEVICE_OK;
}

int CSIABXYStage::GetLimitsUm(double& xMin, double& xMax, double& yMin, double& yMax)
{
	xMin = yMin = 1;
	xMin = yMin = 2000;
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
	return DEVICE_ERR;
}

double CSIABXYStage::GetStepSizeYUm()
{
	return DEVICE_ERR;
}