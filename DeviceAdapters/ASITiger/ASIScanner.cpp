///////////////////////////////////////////////////////////////////////////////
// FILE:          ASIScanner.cpp
// PROJECT:       Micro-Manager
// SUBSYSTEM:     DeviceAdapters
//-----------------------------------------------------------------------------
// DESCRIPTION:   ASI scanner device adapter
//
// COPYRIGHT:     Applied Scientific Instrumentation, Eugene OR
//
// LICENSE:       This file is distributed under the BSD license.
//
//                This file is distributed in the hope that it will be useful,
//                but WITHOUT ANY WARRANTY; without even the implied warranty
//                of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
//
//                IN NO EVENT SHALL THE COPYRIGHT OWNER OR
//                CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
//                INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES.
//
// AUTHOR:        Jon Daniels (jon@asiimaging.com) 09/2013
//
// BASED ON:      MicroPoint.cpp and others
//

#ifdef WIN32
#define snprintf _snprintf 
#pragma warning(disable: 4355)
#endif

#include "ASIScanner.h"
#include "ASITiger.h"
#include "ASIHub.h"
#include "ASIDevice.h"
#include "../../MMDevice/ModuleInterface.h"
#include "../../MMDevice/DeviceUtils.h"
#include "../../MMDevice/DeviceBase.h"
#include "../../MMDevice/MMDevice.h"
#include <iostream>
#include <cmath>
#include <sstream>
#include <string>

using namespace std;

///////////////////////////////////////////////////////////////////////////////
// CScanner
//
CScanner::CScanner(const char* name) :
   ASIDevice(this,name),
   axisLetterX_(g_EmptyAxisLetterStr),    // value determined by extended name
   axisLetterY_(g_EmptyAxisLetterStr),    // value determined by extended name
   unitMultX_(g_ScannerDefaultUnitMult),  // later will try to read actual setting
   unitMultY_(g_ScannerDefaultUnitMult),  // later will try to read actual setting
   limitX_(0),   // later will try to read actual setting
   limitY_(0),   // later will try to read actual setting
   shutterX_(0), // home position, used to turn beam off
   shutterY_(0), // home position, used to turn beam off
   lastX_(0),    // cached position before blanking, used for SetIlluminationState
   lastY_(0),    // cached position before blanking, used for SetIlluminationState
   illuminationState_(true),
   polygonRepetitions_(0),
   ring_buffer_supported_(false)
{
   if (IsExtendedName(name))  // only set up these properties if we have the required information in the name
   {
      axisLetterX_ = GetAxisLetterFromExtName(name);
      CreateProperty(g_AxisLetterXPropertyName, axisLetterX_.c_str(), MM::String, true);
      axisLetterY_ = GetAxisLetterFromExtName(name,1);
      CreateProperty(g_AxisLetterYPropertyName, axisLetterY_.c_str(), MM::String, true);
   }
}

int CScanner::Initialize()
{
   // call generic Initialize first, this gets hub
   RETURN_ON_MM_ERROR( ASIDevice::Initialize() );

   // read the unit multiplier for X and Y axes
   // ASI's unit multiplier is how many units per degree rotation for the micromirror card
   ostringstream command;
   command.str("");
   command << "UM " << axisLetterX_ << "? ";
   RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(),":") );
   RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(unitMultX_) );
   command.str("");
   command << "UM " << axisLetterY_ << "? ";
   RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(),":") );
   RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(unitMultY_) );

   // read the home position (used for beam shuttering)
   command.str("");
   command << "HM " << axisLetterX_ << "? ";
   RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(),":") );
   RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(shutterX_) );
   command.str("");
   command << "HM " << axisLetterY_ << "? ";
   RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(),":") );
   RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(shutterY_) );

   // set controller card to return positions with 1 decimal places (3 is max allowed currently, units are millidegrees)
   command.str("");
   command << addressChar_ << "VB Z=1";
   RETURN_ON_MM_ERROR ( hub_->QueryCommand(command.str()) );

   // create MM description; this doesn't work during hardware configuration wizard but will work afterwards
   command.str("");
   command << g_ScannerDeviceDescription << " Xaxis=" << axisLetterX_ << " Yaxis=" << axisLetterY_ << " HexAddr=" << addressString_;
   CreateProperty(MM::g_Keyword_Description, command.str().c_str(), MM::String, true);

   // remove for now because of bug in PZINFO, will replace by RDADC command later (Jon 23-Oct-13)
//   // high voltage reading for diagnostics
//   command.str("");
//   command << addressChar_ << "PZINFO";
//   RETURN_ON_MM_ERROR( hub_->QueryCommand(command.str()));
//   vector<string> vReply = hub_->SplitAnswerOnCR();
//   hub_->SetLastSerialAnswer(vReply[0]);  // 1st line has the HV info for micromirror vs. 3rd for piezo
//   command.str("");
//   command << hub_->ParseAnswerAfterColon();
//   CreateProperty(g_CardVoltagePropertyName, command.str().c_str(), MM::Float, true);

   // now create properties
   CPropertyAction* pAct;

   // refresh properties from controller every time; default is false = no refresh (speeds things up by not redoing so much serial comm)
   pAct = new CPropertyAction (this, &CScanner::OnRefreshProperties);
   CreateProperty(g_RefreshPropValsPropertyName, g_NoState, MM::String, false, pAct);
   AddAllowedValue(g_RefreshPropValsPropertyName, g_NoState);
   AddAllowedValue(g_RefreshPropValsPropertyName, g_YesState);

   // save settings to controller if requested
   pAct = new CPropertyAction (this, &CScanner::OnSaveCardSettings);
   CreateProperty(g_SaveSettingsPropertyName, g_SaveSettingsOrig, MM::String, false, pAct);
   AddAllowedValue(g_SaveSettingsPropertyName, g_SaveSettingsX);
   AddAllowedValue(g_SaveSettingsPropertyName, g_SaveSettingsY);
   AddAllowedValue(g_SaveSettingsPropertyName, g_SaveSettingsZ);
   AddAllowedValue(g_SaveSettingsPropertyName, g_SaveSettingsOrig);
   AddAllowedValue(g_SaveSettingsPropertyName, g_SaveSettingsDone);

   // upper and lower limits (SU and SL) (limits not as useful for micromirror as for stage but they work)
   pAct = new CPropertyAction (this, &CScanner::OnLowerLimX);
   CreateProperty(g_ScannerLowerLimXPropertyName, "0", MM::Float, false, pAct);
   UpdateProperty(g_ScannerLowerLimXPropertyName);
   pAct = new CPropertyAction (this, &CScanner::OnLowerLimY);
   CreateProperty(g_ScannerLowerLimYPropertyName, "0", MM::Float, false, pAct);
   UpdateProperty(g_ScannerLowerLimYPropertyName);
   pAct = new CPropertyAction (this, &CScanner::OnUpperLimX);
   CreateProperty(g_ScannerUpperLimXPropertyName, "0", MM::Float, false, pAct);
   UpdateProperty(g_ScannerUpperLimXPropertyName);
   pAct = new CPropertyAction (this, &CScanner::OnUpperLimY);
   CreateProperty(g_ScannerUpperLimYPropertyName, "0", MM::Float, false, pAct);
   UpdateProperty(g_ScannerUpperLimYPropertyName);

   // mode, currently just changes between internal and external input
   pAct = new CPropertyAction (this, &CScanner::OnMode);
   CreateProperty(g_ScannerInputModePropertyName, "0", MM::String, false, pAct);
   UpdateProperty(g_ScannerInputModePropertyName);
   AddAllowedValue(g_ScannerInputModePropertyName, g_ScannerMode_internal);
   AddAllowedValue(g_ScannerInputModePropertyName, g_ScannerMode_external);

   // filter cut-off frequency
   // decided to implement separately for X and Y axes so can have one fast and other slow
   pAct = new CPropertyAction (this, &CScanner::OnCutoffFreqX);
   CreateProperty(g_ScannerCutoffFilterXPropertyName, "0", MM::Float, false, pAct);
   UpdateProperty(g_ScannerCutoffFilterXPropertyName);
   SetPropertyLimits(g_ScannerCutoffFilterXPropertyName, 0.1, 650);
   pAct = new CPropertyAction (this, &CScanner::OnCutoffFreqY);
   CreateProperty(g_ScannerCutoffFilterYPropertyName, "0", MM::Float, false, pAct);
   UpdateProperty(g_ScannerCutoffFilterYPropertyName);
   SetPropertyLimits(g_ScannerCutoffFilterYPropertyName, 0.1, 650);

   // attenuation factor for movement
   pAct = new CPropertyAction (this, &CScanner::OnAttenuateTravelX);
   CreateProperty(g_ScannerAttenuateXPropertyName, "0", MM::Float, false, pAct);
   UpdateProperty(g_ScannerAttenuateXPropertyName);
   SetPropertyLimits(g_ScannerAttenuateXPropertyName, 0, 1);
   pAct = new CPropertyAction (this, &CScanner::OnAttenuateTravelY);
   CreateProperty(g_ScannerAttenuateYPropertyName, "0", MM::Float, false, pAct);
   UpdateProperty(g_ScannerAttenuateYPropertyName);
   SetPropertyLimits(g_ScannerAttenuateYPropertyName, 0, 1);

   // joystick fast speed (JS X=) (per-card, not per-axis)
   pAct = new CPropertyAction (this, &CScanner::OnJoystickFastSpeed);
   CreateProperty(g_JoystickFastSpeedPropertyName, "100", MM::Integer, false, pAct);
   UpdateProperty(g_JoystickFastSpeedPropertyName);
   SetPropertyLimits(g_JoystickFastSpeedPropertyName, 0, 100);

   // joystick slow speed (JS Y=) (per-card, not per-axis)
   pAct = new CPropertyAction (this, &CScanner::OnJoystickSlowSpeed);
   CreateProperty(g_JoystickSlowSpeedPropertyName, "10", MM::Integer, false, pAct);
   UpdateProperty(g_JoystickSlowSpeedPropertyName);
   SetPropertyLimits(g_JoystickSlowSpeedPropertyName, 0, 100);

   // joystick mirror (changes joystick fast/slow speeds to negative) (per-card, not per-axis)
   pAct = new CPropertyAction (this, &CScanner::OnJoystickMirror);
   CreateProperty(g_JoystickMirrorPropertyName, g_NoState, MM::String, false, pAct);
   AddAllowedValue(g_JoystickMirrorPropertyName, g_NoState);
   AddAllowedValue(g_JoystickMirrorPropertyName, g_YesState);
   UpdateProperty(g_JoystickMirrorPropertyName);

   // joystick disable and select which knob
   pAct = new CPropertyAction (this, &CScanner::OnJoystickSelectX);
   CreateProperty(g_JoystickSelectXPropertyName, g_JSCode_0, MM::String, false, pAct);
   AddAllowedValue(g_JoystickSelectXPropertyName, g_JSCode_0);
   AddAllowedValue(g_JoystickSelectXPropertyName, g_JSCode_2);
   AddAllowedValue(g_JoystickSelectXPropertyName, g_JSCode_3);
   AddAllowedValue(g_JoystickSelectXPropertyName, g_JSCode_22);
   AddAllowedValue(g_JoystickSelectXPropertyName, g_JSCode_23);
   UpdateProperty(g_JoystickSelectXPropertyName);
   pAct = new CPropertyAction (this, &CScanner::OnJoystickSelectY);
   CreateProperty(g_JoystickSelectYPropertyName, g_JSCode_0, MM::String, false, pAct);
   AddAllowedValue(g_JoystickSelectYPropertyName, g_JSCode_0);
   AddAllowedValue(g_JoystickSelectYPropertyName, g_JSCode_2);
   AddAllowedValue(g_JoystickSelectYPropertyName, g_JSCode_3);
   AddAllowedValue(g_JoystickSelectYPropertyName, g_JSCode_22);
   AddAllowedValue(g_JoystickSelectYPropertyName, g_JSCode_23);
   UpdateProperty(g_JoystickSelectYPropertyName);

   // turn the beam on and off
   pAct = new CPropertyAction (this, &CScanner::OnBeamEnabled);
   CreateProperty(g_ScannerBeamEnabledPropertyName, g_YesState, MM::String, false, pAct);
   AddAllowedValue(g_ScannerBeamEnabledPropertyName, g_NoState);
   AddAllowedValue(g_ScannerBeamEnabledPropertyName, g_YesState);

   // single-axis mode settings
   // todo fix firmware TTL initialization problem where SAM p=2 triggers by itself 1st time
   pAct = new CPropertyAction (this, &CScanner::OnSAAmplitudeX);
   CreateProperty(g_ScannerSAAmplitudeXPropertyName, "0", MM::Float, false, pAct);
   UpdateProperty(g_ScannerSAAmplitudeXPropertyName);
   pAct = new CPropertyAction (this, &CScanner::OnSAOffsetX);
   CreateProperty(g_ScannerSAOffsetXPropertyName, "0", MM::Float, false, pAct);
   UpdateProperty(g_ScannerSAOffsetXPropertyName);
   pAct = new CPropertyAction (this, &CScanner::OnSAPeriodX);
   CreateProperty(g_SAPeriodXPropertyName, "0", MM::Integer, false, pAct);
   UpdateProperty(g_SAPeriodXPropertyName);
   pAct = new CPropertyAction (this, &CScanner::OnSAModeX);
   CreateProperty(g_SAModeXPropertyName, g_SAMode_0, MM::String, false, pAct);
   AddAllowedValue(g_SAModeXPropertyName, g_SAMode_0);
   AddAllowedValue(g_SAModeXPropertyName, g_SAMode_1);
   AddAllowedValue(g_SAModeXPropertyName, g_SAMode_2);
   AddAllowedValue(g_SAModeXPropertyName, g_SAMode_3);
   UpdateProperty(g_SAModeXPropertyName);
   pAct = new CPropertyAction (this, &CScanner::OnSAPatternX);
   CreateProperty(g_SAPatternXPropertyName, g_SAPattern_0, MM::String, false, pAct);
   AddAllowedValue(g_SAPatternXPropertyName, g_SAPattern_0);
   AddAllowedValue(g_SAPatternXPropertyName, g_SAPattern_1);
   AddAllowedValue(g_SAPatternXPropertyName, g_SAPattern_2);
   UpdateProperty(g_SAPatternXPropertyName);
   pAct = new CPropertyAction (this, &CScanner::OnSAAmplitudeY);
   CreateProperty(g_ScannerSAAmplitudeYPropertyName, "0", MM::Float, false, pAct);
   UpdateProperty(g_ScannerSAAmplitudeYPropertyName);
   pAct = new CPropertyAction (this, &CScanner::OnSAOffsetY);
   CreateProperty(g_ScannerSAOffsetYPropertyName, "0", MM::Float, false, pAct);
   UpdateProperty(g_ScannerSAOffsetYPropertyName);
   pAct = new CPropertyAction (this, &CScanner::OnSAPeriodY);
   CreateProperty(g_SAPeriodYPropertyName, "0", MM::Integer, false, pAct);
   UpdateProperty(g_SAPeriodYPropertyName);
   pAct = new CPropertyAction (this, &CScanner::OnSAModeY);
   CreateProperty(g_SAModeYPropertyName, g_SAMode_0, MM::String, false, pAct);
   AddAllowedValue(g_SAModeYPropertyName, g_SAMode_0);
   AddAllowedValue(g_SAModeYPropertyName, g_SAMode_1);
   AddAllowedValue(g_SAModeYPropertyName, g_SAMode_2);
   AddAllowedValue(g_SAModeYPropertyName, g_SAMode_3);
   UpdateProperty(g_SAModeYPropertyName);
   pAct = new CPropertyAction (this, &CScanner::OnSAPatternY);
   CreateProperty(g_SAPatternYPropertyName, g_SAPattern_0, MM::String, false, pAct);
   AddAllowedValue(g_SAPatternYPropertyName, g_SAPattern_0);
   AddAllowedValue(g_SAPatternYPropertyName, g_SAPattern_1);
   AddAllowedValue(g_SAPatternYPropertyName, g_SAPattern_2);
   UpdateProperty(g_SAPatternYPropertyName);

   // generates a set of additional advanced properties that are rarely used
   pAct = new CPropertyAction (this, &CScanner::OnSAAdvancedX);
   CreateProperty(g_AdvancedSAPropertiesXPropertyName, g_NoState, MM::String, false, pAct);
   AddAllowedValue(g_AdvancedSAPropertiesXPropertyName, g_NoState);
   AddAllowedValue(g_AdvancedSAPropertiesXPropertyName, g_YesState);
   UpdateProperty(g_AdvancedSAPropertiesXPropertyName);
   pAct = new CPropertyAction (this, &CScanner::OnSAAdvancedY);
   CreateProperty(g_AdvancedSAPropertiesYPropertyName, g_NoState, MM::String, false, pAct);
   AddAllowedValue(g_AdvancedSAPropertiesYPropertyName, g_NoState);
   AddAllowedValue(g_AdvancedSAPropertiesYPropertyName, g_YesState);
   UpdateProperty(g_AdvancedSAPropertiesYPropertyName);

   // end now if we are pre-2.8 firmware
   if (firmwareVersion_ < 2.8)
   {
      initialized_ = true;
      return DEVICE_OK;
   }

   // everything below only supported in firmware 2.8 and newer

   // get build info so we can add optional properties
   build_info_type build;
   RETURN_ON_MM_ERROR( hub_->GetBuildInfo(addressChar_, build) );

   // add SPIM properties if SPIM is supported
   if (build.vAxesProps[0] & BIT4)
   {
      pAct = new CPropertyAction (this, &CScanner::OnSPIMScansPerSlice);
      CreateProperty(g_SPIMNumScansPerSlicePropertyName, "1", MM::Integer, false, pAct);
      UpdateProperty(g_SPIMNumScansPerSlicePropertyName);
      SetPropertyLimits(g_SPIMNumScansPerSlicePropertyName, 1, 100);

      pAct = new CPropertyAction (this, &CScanner::OnSPIMNumSlices);
      CreateProperty(g_SPIMNumSlicesPropertyName, "1", MM::Integer, false, pAct);
      UpdateProperty(g_SPIMNumSlicesPropertyName);
      SetPropertyLimits(g_SPIMNumSlicesPropertyName, 1, 100);

      pAct = new CPropertyAction (this, &CScanner::OnSPIMNumRepeats);
      CreateProperty(g_SPIMNumRepeatsPropertyName, "1", MM::Integer, false, pAct);
      UpdateProperty(g_SPIMNumRepeatsPropertyName);
      SetPropertyLimits(g_SPIMNumRepeatsPropertyName, 1, 100);

      pAct = new CPropertyAction (this, &CScanner::OnSPIMNumSides);
      CreateProperty(g_SPIMNumSidesPropertyName, "1", MM::Integer, false, pAct);
      UpdateProperty(g_SPIMNumSidesPropertyName);
      SetPropertyLimits(g_SPIMNumSidesPropertyName, 1, 2);

      pAct = new CPropertyAction (this, &CScanner::OnSPIMFirstSide);
      CreateProperty(g_SPIMFirstSidePropertyName, g_SPIMSideAFirst, MM::String, false, pAct);
      AddAllowedValue(g_SPIMFirstSidePropertyName, g_SPIMSideAFirst);
      AddAllowedValue(g_SPIMFirstSidePropertyName, g_SPIMSideBFirst);
      UpdateProperty(g_SPIMFirstSidePropertyName);

      pAct = new CPropertyAction (this, &CScanner::OnSPIMDelayBeforeSide);
      CreateProperty(g_SPIMDelayBeforeSidePropertyName, "0", MM::Float, false, pAct);
      SetPropertyLimits(g_SPIMDelayBeforeSidePropertyName, 0, 100);
      UpdateProperty(g_SPIMDelayBeforeSidePropertyName);

      pAct = new CPropertyAction (this, &CScanner::OnSPIMDelayBeforeSlice);
      CreateProperty(g_SPIMDelayBeforeSlicePropertyName, "0", MM::Float, false, pAct);
      SetPropertyLimits(g_SPIMDelayBeforeSlicePropertyName, 0, 100);
      UpdateProperty(g_SPIMDelayBeforeSlicePropertyName);

      pAct = new CPropertyAction (this, &CScanner::OnSPIMState);
      CreateProperty(g_SPIMStatePropertyName, g_SPIMStateIdle, MM::String, false, pAct);
      AddAllowedValue(g_SPIMStatePropertyName, g_SPIMStateIdle);
      AddAllowedValue(g_SPIMStatePropertyName, g_SPIMStateArmed);
      AddAllowedValue(g_SPIMStatePropertyName, g_SPIMStateRunning);
      UpdateProperty(g_SPIMStatePropertyName);
   }

   // add ring buffer properties if supported (starting 2.81)
   if ((firmwareVersion_ > 2.8) && (build.vAxesProps[0] & BIT1))
   {
      ring_buffer_supported_ = true;

      pAct = new CPropertyAction (this, &CScanner::OnRBMode);
      CreateProperty(g_RB_ModePropertyName, g_RB_OnePoint_1, MM::String, false, pAct);
      AddAllowedValue(g_RB_ModePropertyName, g_RB_OnePoint_1);
      AddAllowedValue(g_RB_ModePropertyName, g_RB_PlayOnce_2);
      AddAllowedValue(g_RB_ModePropertyName, g_RB_PlayRepeat_3);
      UpdateProperty(g_RB_ModePropertyName);

      pAct = new CPropertyAction (this, &CScanner::OnRBDelayBetweenPoints);
      CreateProperty(g_RB_DelayPropertyName, "0", MM::Integer, false, pAct);
      UpdateProperty(g_RB_DelayPropertyName);

      // "do it" property to do TTL trigger via serial
      pAct = new CPropertyAction (this, &CScanner::OnRBTrigger);
      CreateProperty(g_RB_TriggerPropertyName, g_IdleState, MM::String, false, pAct);
      AddAllowedValue(g_RB_TriggerPropertyName, g_IdleState, 0);
      AddAllowedValue(g_RB_TriggerPropertyName, g_DoItState, 1);
      AddAllowedValue(g_RB_TriggerPropertyName, g_DoneState, 2);
      UpdateProperty(g_RB_TriggerPropertyName);

      pAct = new CPropertyAction (this, &CScanner::OnRBRunning);
      CreateProperty(g_RB_AutoplayRunningPropertyName, g_NoState, MM::String, false, pAct);
      AddAllowedValue(g_RB_AutoplayRunningPropertyName, g_NoState);
      AddAllowedValue(g_RB_AutoplayRunningPropertyName, g_YesState);
      UpdateProperty(g_RB_AutoplayRunningPropertyName);
   }

   initialized_ = true;
   return DEVICE_OK;
}

bool CScanner::Busy()
{
//   ostringstream command; command.str("");
//   if (firmwareVersion_ > 2.7) // can use more accurate RS <axis>?
//   {
//      command << "RS " << axisLetterX_ << "?";
//      ret_ = hub_->QueryCommandVerify(command.str(),":A");
//      if (ret_ != DEVICE_OK)  // say we aren't busy if we can't communicate
//         return false;
//      if (hub_->LastSerialAnswer().at(3) == 'B')
//         return true;
//      command.str("");
//      command << "RS " << axisLetterY_ << "?";
//      return (hub_->LastSerialAnswer().at(3) == 'B');
//   }
//   else  // use LSB of the status byte as approximate status, not quite equivalent
//   {
//      command << "RS " << axisLetterX_;
//      ret_ = hub_->QueryCommandVerify(command.str(),":A");
//      if (ret_ != DEVICE_OK)  // say we aren't busy if we can't communicate
//         return false;
//      int i = (int) (hub_->ParseAnswerAfterPosition(2));
//      if (i & (int)BIT0)  // mask everything but LSB
//         return true; // don't bother checking other axis
//      command.str("");
//      command << "RS " << axisLetterY_;
//      ret_ = hub_->QueryCommandVerify(command.str(),":A");
//      if (ret_ != DEVICE_OK)  // say we aren't busy if we can't communicate
//         return false;
//      i = (int) (hub_->ParseAnswerAfterPosition(2));
//      return (i & (int)BIT0);  // mask everything but LSB
//   }
   return false;
}

int CScanner::SetPosition(double x, double y)
// will not change the position of an axis unless single-axis functions are inactive
{
   if (!illuminationState_) return DEVICE_OK;  // don't do anything if beam is turned off
   ostringstream command; command.str("");
   char SAModeX[MM::MaxStrLength];
   RETURN_ON_MM_ERROR ( GetProperty(g_SAModeXPropertyName, SAModeX) );
   if (strcmp(SAModeX, g_SAMode_0) == 0)
   {
      command.str("");
      command << "M " << axisLetterX_ << "=" << x*unitMultX_;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(),":A") );
   }
   char SAModeY[MM::MaxStrLength];
   RETURN_ON_MM_ERROR ( GetProperty(g_SAModeYPropertyName, SAModeY) );
   if (strcmp(SAModeY, g_SAMode_0) == 0)
   {
      command.str("");
      command << "M " << axisLetterY_ << "=" << y*unitMultY_;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(),":A") );
   }
   return DEVICE_OK;
}

int CScanner::GetPosition(double& x, double& y)
{
//   // read from card instead of using cached values directly, could be slight mismatch
   ostringstream command; command.str("");
   command << "W " << axisLetterX_;
   RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(),":A") );
   RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterPosition2(x) );
   x = x/unitMultX_;
   command.str("");
   command << "W " << axisLetterY_;
   RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(),":A") );
   RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterPosition2(y) );
   y = y/unitMultY_;
   return DEVICE_OK;
//   ostringstream command; command.str("");
//   hub_->QueryCommand("VB F=1");
//   command << "W " << axisLetterX_;
//   RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(),axisLetterX_) );
//   RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(x) );
//   x = x/unitMultX_;
//   command.str("");
//   command << "W " << axisLetterY_;
//   RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(),axisLetterY_) );
//   RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(y) );
//   y = y/unitMultY_;
//   hub_->QueryCommand("VB F=0");
//   return DEVICE_OK;
}

void CScanner::UpdateIlluminationState()
{
   // no direct way to query the controller if we are in "home" position or not
   // here we make the assumption that if both axes are at upper limits we are at home
   if (firmwareVersion_ > 2.7)  // require version 2.8 to do this
   {
      ostringstream command; command.str("");
      command << "RS " << axisLetterX_ << "-";
      ret_ = hub_->QueryCommandVerify(command.str(),":A");
      if (ret_ != DEVICE_OK)  // don't choke on comm error
         return;
      if (hub_->LastSerialAnswer().at(3) != 'U')
      {
         illuminationState_ = true;
         return;
      }
      command.str("");
      command << "RS " << axisLetterY_ << "-";
      ret_ = hub_->QueryCommandVerify(command.str(),":A");
      if (ret_ != DEVICE_OK)  // don't choke on comm error
         return;
      if (hub_->LastSerialAnswer().at(3) != 'U')
      {
         illuminationState_ = true;
         return;
      }
      // if we made it this far then both axes are at upper limits
      illuminationState_ = false;
      return;
   }
}

int CScanner::SetIlluminationState(bool on)
// we can't turn off beam but we can steer beam to corner where hopefully it is blocked internally
{
   UpdateIlluminationState();
   if (on && !illuminationState_)  // was off, turning on
   {
      illuminationState_ = true;
      return SetPosition(lastX_, lastY_);  // move to where it was when last turned off
   }
   else if (!on && illuminationState_) // was on, turning off
   {
      illuminationState_ = false;
      GetPosition(lastX_, lastY_);  // read and store pre-off position so we can undo
      ostringstream command; command.str("");
      command << "! " << axisLetterX_ << " " << axisLetterY_;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(),":A") );
      SetProperty(g_SAModeXPropertyName, g_SAMode_0);  // scan is stopped by firmware, change property accordingly
      SetProperty(g_SAModeYPropertyName, g_SAMode_0);  // scan is stopped by firmware, change property accordingly
      return DEVICE_OK;
   }
   // if was off, turning off do nothing
   // if was on, turning on do nothing
   return DEVICE_OK;
}

int CScanner::AddPolygonVertex(int polygonIndex, double x, double y)
{
   if (polygons_.size() <  (unsigned) (1 + polygonIndex))
      polygons_.resize(polygonIndex + 1);
   polygons_[polygonIndex].first = x;
   polygons_[polygonIndex].second = y;
   return DEVICE_OK;
}

int CScanner::DeletePolygons()
{
   polygons_.clear();
   return DEVICE_OK;
}

int CScanner::LoadPolygons()
{
   if (ring_buffer_supported_)
   {
      ostringstream command; command.str("");
      command << addressChar_ << "RM X=0";
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
      for (int i=0; i< (int) polygons_.size(); ++i)
      {
         command.str("");
         command << "LD " << axisLetterX_ << "=" << polygons_[i].first*unitMultX_
               << " " << axisLetterY_ << "=" << polygons_[i].second*unitMultY_;
         RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
      }
   }
   else
   {
      // do nothing since device doesn't store polygons in HW
   }
   return DEVICE_OK;
}

int CScanner::SetPolygonRepetitions(int repetitions)
{
   if (ring_buffer_supported_)
   {
      // ring buffer HW does not support having multiple repetitions
      return DEVICE_UNSUPPORTED_COMMAND;
   }
   else
   {
      polygonRepetitions_ = repetitions;
      return DEVICE_OK;
   }
}

int CScanner::RunPolygons()
{
   if (ring_buffer_supported_)
   {
      ostringstream command; command.str("");
      command << addressChar_ << "RM";
      return hub_->QueryCommandVerify(command.str(), ":A");
   }
   else
   {
      // no HW support => have to repeatedly call SetPosition
      for (int j=0; j<polygonRepetitions_; ++j)
         for (int i=0; i< (int) polygons_.size(); ++i)
            SetPosition(polygons_[i].first,polygons_[i].second);
      return DEVICE_OK;
   }
}

int CScanner::GetChannel(char* channelName)
{
   ostringstream command; command.str("");
   command << "Axes_ " << axisLetterX_ << axisLetterY_;
   CDeviceUtils::CopyLimitedString(channelName, command.str().c_str());
   return DEVICE_OK;
}

int CScanner::RunSequence()
{
   if (ring_buffer_supported_)
   {
      // note that this simply sends a trigger, which will also turn it off if it's currently running
      SetProperty(g_RB_TriggerPropertyName, g_DoItState);
      return DEVICE_OK;
   }
   else
   {
      return DEVICE_UNSUPPORTED_COMMAND;
   }
}


////////////////
// action handlers

int CScanner::OnSaveCardSettings(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   string tmpstr;
   ostringstream command; command.str("");
   if (eAct == MM::AfterSet) {
      command << addressChar_ << "SS ";
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_SaveSettingsOrig) == 0)
         return DEVICE_OK;
      if (tmpstr.compare(g_SaveSettingsDone) == 0)
         return DEVICE_OK;
      if (tmpstr.compare(g_SaveSettingsX) == 0)
         command << 'X';
      else if (tmpstr.compare(g_SaveSettingsY) == 0)
         command << 'X';
      else if (tmpstr.compare(g_SaveSettingsZ) == 0)
         command << 'Z';
      RETURN_ON_MM_ERROR (hub_->QueryCommandVerify(command.str(), ":A", (long)200));  // note 200ms delay added
      pProp->Set(g_SaveSettingsDone);
   }
   return DEVICE_OK;
}

int CScanner::OnRefreshProperties(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   string tmpstr;
   if (eAct == MM::AfterSet) {
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_YesState) == 0)
         refreshProps_ = true;
      else
         refreshProps_ = false;
   }
   return DEVICE_OK;
}

int CScanner::OnLowerLimX(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SL " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << "SL " << axisLetterX_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnLowerLimY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SL " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << "SL " << axisLetterY_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnUpperLimX(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SU " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
      limitX_ = tmp;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << "SU " << axisLetterX_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnUpperLimY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SU " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
      limitY_ = tmp;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << "SU " << axisLetterY_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnMode(MM::PropertyBase* pProp, MM::ActionType eAct)
// assume X axis's mode is for both, and then set mode for both axes together just like XYStage properties
// todo change to using PM for v2.8 and above
{
   ostringstream command; command.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      ostringstream response; response.str("");
      if (firmwareVersion_ > 2.7)
      {
         command << "PM " << axisLetterX_ << "?";
         response << axisLetterX_ << "=";
      }
      else
      {
         command << "MA " << axisLetterX_ << "?";
         response << ":A " << axisLetterX_ << "=";
      }
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      bool success = 0;
      if (firmwareVersion_ > 2.7)  // using PM command
      {
         switch (tmp)
         {
            case 0: success = pProp->Set(g_ScannerMode_internal); break;
            case 1: success = pProp->Set(g_ScannerMode_external); break;
            default: success = 0;                        break;
         }
      }
      else
      {
         switch (tmp)
         {
            case 0: success = pProp->Set(g_ScannerMode_external); break;
            case 1: success = pProp->Set(g_ScannerMode_internal); break;
            default: success = 0;                        break;
         }
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      if (firmwareVersion_ > 2.7)  // using PM command
      {
         if (tmpstr.compare(g_ScannerMode_internal) == 0)
            tmp = 0;
         else if (tmpstr.compare(g_ScannerMode_external) == 0)
            tmp = 1;
         else
            return DEVICE_INVALID_PROPERTY_VALUE;
         command << "PM " << axisLetterX_ << "=" << tmp << " " << axisLetterY_ << "=" << tmp;
         RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
      }
      else
      {
         if (tmpstr.compare(g_ScannerMode_external) == 0)
            tmp = 0;
         else if (tmpstr.compare(g_ScannerMode_internal) == 0)
            tmp = 1;
         else
            return DEVICE_INVALID_PROPERTY_VALUE;
         command << "MA " << axisLetterX_ << "=" << tmp << " " << axisLetterY_ << "=" << tmp;
         RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
      }
   }
   return DEVICE_OK;
}

int CScanner::OnCutoffFreqX(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "B " << axisLetterX_ << "?";
      response << ":" << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << "B " << axisLetterX_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnCutoffFreqY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "B " << axisLetterY_ << "?";
      response << ":" << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << "B " << axisLetterY_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnAttenuateTravelX(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "D " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << "D " << axisLetterX_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnAttenuateTravelY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "D " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << "D " << axisLetterY_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnBeamEnabled(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   string tmpstr;
   if (eAct == MM::BeforeGet)
   {
      // do this one even if refreshProps is turned off
      UpdateIlluminationState();
      bool success;
      if (illuminationState_)
         success = pProp->Set(g_YesState);
      else
         success = pProp->Set(g_NoState);
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      UpdateIlluminationState();
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_YesState) == 0)
         SetIlluminationState(true);
      else
         SetIlluminationState(false);
   }
   return DEVICE_OK;
}

int CScanner::OnSAAdvancedX(MM::PropertyBase* pProp, MM::ActionType eAct)
// special property, when set to "yes" it creates a set of little-used properties that can be manipulated thereafter
{
   if (eAct == MM::BeforeGet)
   {
      return DEVICE_OK; // do nothing
   }
   else if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_YesState) == 0)
      {
         CPropertyAction* pAct;

         pAct = new CPropertyAction (this, &CScanner::OnSAClkSrcX);
         CreateProperty(g_SAClkSrcXPropertyName, g_SAClkSrc_0, MM::String, false, pAct);
         AddAllowedValue(g_SAClkSrcXPropertyName, g_SAClkSrc_0);
         AddAllowedValue(g_SAClkSrcXPropertyName, g_SAClkSrc_1);
         UpdateProperty(g_SAClkSrcXPropertyName);

         pAct = new CPropertyAction (this, &CScanner::OnSAClkPolX);
         CreateProperty(g_SAClkPolXPropertyName, g_SAClkPol_0, MM::String, false, pAct);
         AddAllowedValue(g_SAClkPolXPropertyName, g_SAClkPol_0);
         AddAllowedValue(g_SAClkPolXPropertyName, g_SAClkPol_1);
         UpdateProperty(g_SAClkPolXPropertyName);

         pAct = new CPropertyAction (this, &CScanner::OnSATTLOutX);
         CreateProperty(g_SATTLOutXPropertyName, g_SATTLOut_0, MM::String, false, pAct);
         AddAllowedValue(g_SATTLOutXPropertyName, g_SATTLOut_0);
         AddAllowedValue(g_SATTLOutXPropertyName, g_SATTLOut_1);
         UpdateProperty(g_SATTLOutXPropertyName);

         pAct = new CPropertyAction (this, &CScanner::OnSATTLPolX);
         CreateProperty(g_SATTLPolXPropertyName, g_SATTLPol_0, MM::String, false, pAct);
         AddAllowedValue(g_SATTLPolXPropertyName, g_SATTLPol_0);
         AddAllowedValue(g_SATTLPolXPropertyName, g_SATTLPol_1);
         UpdateProperty(g_SATTLPolXPropertyName);

         pAct = new CPropertyAction (this, &CScanner::OnSAPatternByteX);
         CreateProperty(g_SAPatternModeXPropertyName, "0", MM::Integer, false, pAct);
         UpdateProperty(g_SAPatternModeXPropertyName);
         SetPropertyLimits(g_SAPatternModeXPropertyName, 0, 255);
      }
   }
   return DEVICE_OK;
}

int CScanner::OnSAAdvancedY(MM::PropertyBase* pProp, MM::ActionType eAct)
// special property, when set to "yes" it creates a set of little-used properties that can be manipulated thereafter
{
   if (eAct == MM::BeforeGet)
   {
      return DEVICE_OK; // do nothing
   }
   else if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_YesState) == 0)
      {
         CPropertyAction* pAct;

         pAct = new CPropertyAction (this, &CScanner::OnSAClkSrcY);
         CreateProperty(g_SAClkSrcYPropertyName, g_SAClkSrc_0, MM::String, false, pAct);
         AddAllowedValue(g_SAClkSrcYPropertyName, g_SAClkSrc_0);
         AddAllowedValue(g_SAClkSrcYPropertyName, g_SAClkSrc_1);
         UpdateProperty(g_SAClkSrcYPropertyName);

         pAct = new CPropertyAction (this, &CScanner::OnSAClkPolY);
         CreateProperty(g_SAClkPolYPropertyName, g_SAClkPol_0, MM::String, false, pAct);
         AddAllowedValue(g_SAClkPolYPropertyName, g_SAClkPol_0);
         AddAllowedValue(g_SAClkPolYPropertyName, g_SAClkPol_1);
         UpdateProperty(g_SAClkPolYPropertyName);

         pAct = new CPropertyAction (this, &CScanner::OnSATTLOutY);
         CreateProperty(g_SATTLOutYPropertyName, g_SATTLOut_0, MM::String, false, pAct);
         AddAllowedValue(g_SATTLOutYPropertyName, g_SATTLOut_0);
         AddAllowedValue(g_SATTLOutYPropertyName, g_SATTLOut_1);
         UpdateProperty(g_SATTLOutYPropertyName);

         pAct = new CPropertyAction (this, &CScanner::OnSATTLPolY);
         CreateProperty(g_SATTLPolYPropertyName, g_SATTLPol_0, MM::String, false, pAct);
         AddAllowedValue(g_SATTLPolYPropertyName, g_SATTLPol_0);
         AddAllowedValue(g_SATTLPolYPropertyName, g_SATTLPol_1);
         UpdateProperty(g_SATTLPolYPropertyName);

         pAct = new CPropertyAction (this, &CScanner::OnSAPatternByteY);
         CreateProperty(g_SAPatternModeYPropertyName, "0", MM::Integer, false, pAct);
         UpdateProperty(g_SAPatternModeYPropertyName);
         SetPropertyLimits(g_SAPatternModeYPropertyName, 0, 255);
      }
   }
   return DEVICE_OK;
}

int CScanner::OnSAAmplitudeX(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SAA " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      tmp = tmp/unitMultX_;
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << "SAA " << axisLetterX_ << "=" << tmp*unitMultX_;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSAOffsetX(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SAO " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      tmp = tmp/unitMultX_;
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << "SAO " << axisLetterX_ << "=" << tmp*unitMultX_;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSAPeriodX(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SAF " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << "SAF " << axisLetterX_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSAModeX(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   static bool justSet = false;
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_ && !justSet)
         return DEVICE_OK;
      command << "SAM " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      bool success;
      switch (tmp)
      {
         case 0: success = pProp->Set(g_SAMode_0); break;
         case 1: success = pProp->Set(g_SAMode_1); break;
         case 2: success = pProp->Set(g_SAMode_2); break;
         case 3: success = pProp->Set(g_SAMode_3); break;
         default:success = 0;                      break;
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
      justSet = false;
   }
   else if (eAct == MM::AfterSet) {
      if (!illuminationState_)  // don't do anything if beam is turned off
      {
         pProp->Set(g_SAMode_0);
         return DEVICE_OK;
      }
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_SAMode_0) == 0)
         tmp = 0;
      else if (tmpstr.compare(g_SAMode_1) == 0)
         tmp = 1;
      else if (tmpstr.compare(g_SAMode_2) == 0)
         tmp = 2;
      else if (tmpstr.compare(g_SAMode_3) == 0)
         tmp = 3;
      else
         return DEVICE_INVALID_PROPERTY_VALUE;
      command << "SAM " << axisLetterX_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
      // get the updated value right away
      justSet = true;
      return OnSAModeX(pProp, MM::BeforeGet);
   }
   return DEVICE_OK;
}

int CScanner::OnSAPatternX(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SAP " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));

      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );

      bool success;
      tmp = tmp & ((long)(BIT2|BIT1|BIT0));  // zero all but the lowest 3 bits
      switch (tmp)
      {
         case 0: success = pProp->Set(g_SAPattern_0); break;
         case 1: success = pProp->Set(g_SAPattern_1); break;
         case 2: success = pProp->Set(g_SAPattern_2); break;
         default:success = 0;                      break;
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      if (!illuminationState_)  // don't do anything if beam is turned off
      {
         pProp->Set(g_SAMode_0);
         return DEVICE_OK;
      }
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_SAPattern_0) == 0)
         tmp = 0;
      else if (tmpstr.compare(g_SAPattern_1) == 0)
         tmp = 1;
      else if (tmpstr.compare(g_SAPattern_2) == 0)
         tmp = 2;
      else
         return DEVICE_INVALID_PROPERTY_VALUE;
      // have to get current settings and then modify bits 0-2 from there
      command << "SAP " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      long current;
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(current) );
      current = current & (~(long)(BIT2|BIT1|BIT0));  // set lowest 3 bits to zero
      tmp += current;
      command.str("");
      command << "SAP " << axisLetterX_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSAAmplitudeY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SAA " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      tmp = tmp/unitMultX_;
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << "SAA " << axisLetterY_ << "=" << tmp*unitMultY_;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSAOffsetY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SAO " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      tmp = tmp/unitMultX_;
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << "SAO " << axisLetterY_ << "=" << tmp*unitMultY_;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSAPeriodY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SAF " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << "SAF " << axisLetterY_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSAModeY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   static bool justSet = false;
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_ && !justSet)
         return DEVICE_OK;
      command << "SAM " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      bool success;
      switch (tmp)
      {
         case 0: success = pProp->Set(g_SAMode_0); break;
         case 1: success = pProp->Set(g_SAMode_1); break;
         case 2: success = pProp->Set(g_SAMode_2); break;
         case 3: success = pProp->Set(g_SAMode_3); break;
         default:success = 0;                      break;
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
      justSet = false;
   }
   else if (eAct == MM::AfterSet) {
      if (!illuminationState_)  // don't do anything if beam is turned off
      {
         pProp->Set(g_SAMode_0);
         return DEVICE_OK;
      }
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_SAMode_0) == 0)
         tmp = 0;
      else if (tmpstr.compare(g_SAMode_1) == 0)
         tmp = 1;
      else if (tmpstr.compare(g_SAMode_2) == 0)
         tmp = 2;
      else if (tmpstr.compare(g_SAMode_3) == 0)
         tmp = 3;
      else
         return DEVICE_INVALID_PROPERTY_VALUE;
      command << "SAM " << axisLetterY_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
      // get the updated value right away
      justSet = true;
      return OnSAModeY(pProp, MM::BeforeGet);
   }
   return DEVICE_OK;
}

int CScanner::OnSAPatternY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SAP " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      bool success;
      tmp = tmp & ((long)(BIT2|BIT1|BIT0));  // zero all but the lowest 3 bits
      switch (tmp)
      {
         case 0: success = pProp->Set(g_SAPattern_0); break;
         case 1: success = pProp->Set(g_SAPattern_1); break;
         case 2: success = pProp->Set(g_SAPattern_2); break;
         default:success = 0;                      break;
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_SAPattern_0) == 0)
         tmp = 0;
      else if (tmpstr.compare(g_SAPattern_1) == 0)
         tmp = 1;
      else if (tmpstr.compare(g_SAPattern_2) == 0)
         tmp = 2;
      else
         return DEVICE_INVALID_PROPERTY_VALUE;
      // have to get current settings and then modify bits 0-2 from there
      command << "SAP " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      long current;
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(current) );
      current = current & (~(long)(BIT2|BIT1|BIT0));  // set lowest 3 bits to zero
      tmp += current;
      command.str("");
      command << "SAP " << axisLetterY_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSAPatternByteX(MM::PropertyBase* pProp, MM::ActionType eAct)
// get every single time
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      command << "SAP " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << "SAP " << axisLetterX_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSAPatternByteY(MM::PropertyBase* pProp, MM::ActionType eAct)
// get every single time
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      command << "SAP " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << "SAP " << axisLetterY_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSAClkSrcX(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SAP " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      bool success;
      tmp = tmp & ((long)(BIT7));  // zero all but bit 7
      switch (tmp)
      {
         case 0: success = pProp->Set(g_SAClkSrc_0); break;
         case BIT7: success = pProp->Set(g_SAClkSrc_1); break;
         default:success = 0;                      break;
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_SAClkSrc_0) == 0)
         tmp = 0;
      else if (tmpstr.compare(g_SAClkSrc_1) == 0)
         tmp = BIT7;
      else
         return DEVICE_INVALID_PROPERTY_VALUE;
      // have to get current settings and then modify bit 7 from there
      command << "SAP " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      long current;
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(current) );
      current = current & (~(long)(BIT7));  // clear bit 7
      tmp += current;
      command.str("");
      command << "SAP " << axisLetterX_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSAClkSrcY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SAP " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));

      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );

      bool success;
      tmp = tmp & ((long)(BIT7));  // zero all but bit 7
      switch (tmp)
      {
         case 0: success = pProp->Set(g_SAClkSrc_0); break;
         case BIT7: success = pProp->Set(g_SAClkSrc_1); break;
         default:success = 0;                      break;
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_SAClkSrc_0) == 0)
         tmp = 0;
      else if (tmpstr.compare(g_SAClkSrc_1) == 0)
         tmp = BIT7;
      else
         return DEVICE_INVALID_PROPERTY_VALUE;
      // have to get current settings and then modify bit 7 from there
      command << "SAP " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      long current;
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(current) );
      current = current & (~(long)(BIT7));  // clear bit 7
      tmp += current;
      command.str("");
      command << "SAP " << axisLetterY_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSAClkPolX(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SAP " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      bool success;
      tmp = tmp & ((long)(BIT6));  // zero all but bit 6
      switch (tmp)
      {
         case 0: success = pProp->Set(g_SAClkPol_0); break;
         case BIT6: success = pProp->Set(g_SAClkPol_1); break;
         default:success = 0;                      break;
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_SAClkPol_0) == 0)
         tmp = 0;
      else if (tmpstr.compare(g_SAClkPol_1) == 0)
         tmp = BIT6;
      else
         return DEVICE_INVALID_PROPERTY_VALUE;
      // have to get current settings and then modify bit 6 from there
      command << "SAP " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      long current;
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(current) );
      current = current & (~(long)(BIT6));  // clear bit 6
      tmp += current;
      command.str("");
      command << "SAP " << axisLetterX_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSAClkPolY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SAP " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      bool success;
      tmp = tmp & ((long)(BIT6));  // zero all but bit 6
      switch (tmp)
      {
         case 0: success = pProp->Set(g_SAClkPol_0); break;
         case BIT6: success = pProp->Set(g_SAClkPol_1); break;
         default:success = 0;                      break;
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_SAClkPol_0) == 0)
         tmp = 0;
      else if (tmpstr.compare(g_SAClkPol_1) == 0)
         tmp = BIT6;
      else
         return DEVICE_INVALID_PROPERTY_VALUE;
      // have to get current settings and then modify bit 6 from there
      command << "SAP " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      long current;
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(current) );
      current = current & (~(long)(BIT6));  // clear bit 6
      tmp += current;
      command.str("");
      command << "SAP " << axisLetterY_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSATTLOutX(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SAP " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      bool success;
      tmp = tmp & ((long)(BIT5));  // zero all but bit 5
      switch (tmp)
      {
         case 0: success = pProp->Set(g_SATTLOut_0); break;
         case BIT5: success = pProp->Set(g_SATTLOut_1); break;
         default:success = 0;                      break;
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_SATTLOut_0) == 0)
         tmp = 0;
      else if (tmpstr.compare(g_SATTLOut_1) == 0)
         tmp = BIT5;
      else
         return DEVICE_INVALID_PROPERTY_VALUE;
      // have to get current settings and then modify bit 5 from there
      command << "SAP " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      long current;
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(current) );
      current = current & (~(long)(BIT5));  // clear bit 5
      tmp += current;
      command.str("");
      command << "SAP " << axisLetterX_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSATTLOutY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SAP " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      bool success;
      tmp = tmp & ((long)(BIT5));  // zero all but bit 5
      switch (tmp)
      {
         case 0: success = pProp->Set(g_SATTLOut_0); break;
         case BIT5: success = pProp->Set(g_SATTLOut_1); break;
         default:success = 0;                      break;
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_SATTLOut_0) == 0)
         tmp = 0;
      else if (tmpstr.compare(g_SATTLOut_1) == 0)
         tmp = BIT5;
      else
         return DEVICE_INVALID_PROPERTY_VALUE;
      // have to get current settings and then modify bit 5 from there
      command << "SAP " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      long current;
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(current) );
      current = current & (~(long)(BIT5));  // clear bit 5
      tmp += current;
      command.str("");
      command << "SAP " << axisLetterY_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSATTLPolX(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SAP " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      bool success;
      tmp = tmp & ((long)(BIT4));  // zero all but bit 4
      switch (tmp)
      {
         case 0: success = pProp->Set(g_SATTLPol_0); break;
         case BIT4: success = pProp->Set(g_SATTLPol_1); break;
         default:success = 0;                      break;
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_SATTLPol_0) == 0)
         tmp = 0;
      else if (tmpstr.compare(g_SATTLPol_1) == 0)
         tmp = BIT4;
      else
         return DEVICE_INVALID_PROPERTY_VALUE;
      // have to get current settings and then modify bit 4 from there
      command << "SAP " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      long current;
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(current) );
      current = current & (~(long)(BIT4));  // clear bit 4
      tmp += current;
      command.str("");
      command << "SAP " << axisLetterX_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSATTLPolY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "SAP " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      bool success;
      tmp = tmp & ((long)(BIT4));  // zero all but bit 4
      switch (tmp)
      {
         case 0: success = pProp->Set(g_SATTLPol_0); break;
         case BIT4: success = pProp->Set(g_SATTLPol_1); break;
         default:success = 0;                      break;
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_SATTLPol_0) == 0)
         tmp = 0;
      else if (tmpstr.compare(g_SATTLPol_1) == 0)
         tmp = BIT4;
      else
         return DEVICE_INVALID_PROPERTY_VALUE;
      // have to get current settings and then modify bit 4 from there
      command << "SAP " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      long current;
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(current) );
      current = current & (~(long)(BIT4));  // clear bit 4
      tmp += current;
      command.str("");
      command << "SAP " << axisLetterY_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}


int CScanner::OnJoystickFastSpeed(MM::PropertyBase* pProp, MM::ActionType eAct)
// ASI controller mirrors by having negative speed, but here we have separate property for mirroring
//   and for speed (which is strictly positive)... that makes this code a bit odd
// note that this setting is per-card, not per-axis
// todo fix firmware so joystick speed works
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << addressChar_ << "JS X?";
      response << ":A X=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      tmp = abs(tmp);
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
     char joystickMirror[MM::MaxStrLength];
      RETURN_ON_MM_ERROR ( GetProperty(g_JoystickMirrorPropertyName, joystickMirror) );
      if (strcmp(joystickMirror, g_YesState) == 0)
         command << addressChar_ << "JS X=-" << tmp;
      else
         command << addressChar_ << "JS X=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnJoystickSlowSpeed(MM::PropertyBase* pProp, MM::ActionType eAct)
// ASI controller mirrors by having negative speed, but here we have separate property for mirroring
//   and for speed (which is strictly positive)... that makes this code a bit odd
// note that this setting is per-card, not per-axis
// todo fix firmware so joystick speed works
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << addressChar_ << "JS Y?";
      response << ":A Y=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      tmp = abs(tmp);
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
     char joystickMirror[MM::MaxStrLength];
      RETURN_ON_MM_ERROR ( GetProperty(g_JoystickMirrorPropertyName, joystickMirror) );
      if (strcmp(joystickMirror, g_YesState) == 0)
         command << addressChar_ << "JS Y=-" << tmp;
      else
         command << addressChar_ << "JS Y=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnJoystickMirror(MM::PropertyBase* pProp, MM::ActionType eAct)
// ASI controller mirrors by having negative speed, but here we have separate property for mirroring
//   and for speed (which is strictly positive)... that makes this code a bit odd
// note that this setting is per-card, not per-axis
// todo fix firmware so joystick speed works
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << addressChar_ << "JS X?";  // query only the fast setting to see if already mirrored
      response << ":A X=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      bool success = 0;
      if (tmp < 0) // speed negative <=> mirrored
         success = pProp->Set(g_YesState);
      else
         success = pProp->Set(g_NoState);
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      double joystickFast = 0.0;
      RETURN_ON_MM_ERROR ( GetProperty(g_JoystickFastSpeedPropertyName, joystickFast) );
      double joystickSlow = 0.0;
      RETURN_ON_MM_ERROR ( GetProperty(g_JoystickSlowSpeedPropertyName, joystickSlow) );
      if (tmpstr.compare(g_YesState) == 0)
         command << addressChar_ << "JS X=-" << joystickFast << " Y=-" << joystickSlow;
      else
         command << addressChar_ << "JS X=" << joystickFast << " Y=" << joystickSlow;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnJoystickSelectX(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "J " << axisLetterX_ << "?";
      response << ":A " << axisLetterX_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      bool success = 0;
      switch (tmp)
      {
         case 0: success = pProp->Set(g_JSCode_0); break;
         case 1: success = pProp->Set(g_JSCode_1); break;
         case 2: success = pProp->Set(g_JSCode_2); break;
         case 3: success = pProp->Set(g_JSCode_3); break;
         case 22: success = pProp->Set(g_JSCode_22); break;
         case 23: success = pProp->Set(g_JSCode_23); break;
         default: success=0;
      }
      // don't complain if value is unsupported, just leave as-is
   }
   else if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_JSCode_0) == 0)
         tmp = 0;
      else if (tmpstr.compare(g_JSCode_1) == 0)
         tmp = 1;
      else if (tmpstr.compare(g_JSCode_2) == 0)
         tmp = 2;
      else if (tmpstr.compare(g_JSCode_3) == 0)
         tmp = 3;
      else if (tmpstr.compare(g_JSCode_22) == 0)
         tmp = 22;
      else if (tmpstr.compare(g_JSCode_23) == 0)
         tmp = 23;
      else
         return DEVICE_INVALID_PROPERTY_VALUE;
      command << "J " << axisLetterX_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnJoystickSelectY(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   ostringstream response; response.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << "J " << axisLetterY_ << "?";
      response << ":A " << axisLetterY_ << "=";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), response.str()));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      bool success = 0;
      switch (tmp)
      {
         case 0: success = pProp->Set(g_JSCode_0); break;
         case 1: success = pProp->Set(g_JSCode_1); break;
         case 2: success = pProp->Set(g_JSCode_2); break;
         case 3: success = pProp->Set(g_JSCode_3); break;
         case 22: success = pProp->Set(g_JSCode_22); break;
         case 23: success = pProp->Set(g_JSCode_23); break;
         default: success=0;
      }
      // don't complain if value is unsupported, just leave as-is
   }
   else if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_JSCode_0) == 0)
         tmp = 0;
      else if (tmpstr.compare(g_JSCode_1) == 0)
         tmp = 1;
      else if (tmpstr.compare(g_JSCode_2) == 0)
         tmp = 2;
      else if (tmpstr.compare(g_JSCode_3) == 0)
         tmp = 3;
      else if (tmpstr.compare(g_JSCode_22) == 0)
         tmp = 22;
      else if (tmpstr.compare(g_JSCode_23) == 0)
         tmp = 23;
      else
         return DEVICE_INVALID_PROPERTY_VALUE;
      command << "J " << axisLetterY_ << "=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSPIMScansPerSlice(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << addressChar_ << "NR X?";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A X="));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << addressChar_ << "NR X=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSPIMNumSlices(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << addressChar_ << "NR Y?";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A Y="));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << addressChar_ << "NR Y=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSPIMNumSides(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << addressChar_ << "NR Z?";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A Z="));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      if (tmp==3)   tmp = 2;  // 3 means two-sided but opposite side
      if (tmp==0)   tmp = 1;  // 0 means one-sided but opposite side
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      char FirstSideVal[MM::MaxStrLength];
      pProp->Get(tmp);
      RETURN_ON_MM_ERROR ( GetProperty(g_SPIMFirstSidePropertyName, FirstSideVal) );
      if (strcmp(FirstSideVal, g_SPIMSideBFirst) == 0)
      {
         if (tmp==1)   tmp = 0;
         if (tmp==2)   tmp = 3;
      }
      command << addressChar_ << "NR Z=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSPIMFirstSide(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   long tmp = 0;
   bool success;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << addressChar_ << "NR Z?";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A Z="));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      if (tmp==3 || tmp==0)  // if opposite side
      {
         success = pProp->Set(g_SPIMSideBFirst);
      }
      else
      {
         success = pProp->Set(g_SPIMSideAFirst);
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      long NumSides = 1;
      string tmpstr;
      pProp->Get(tmpstr);
      RETURN_ON_MM_ERROR ( GetProperty(g_SPIMNumSidesPropertyName, NumSides) );
      if (tmpstr.compare(g_SPIMSideAFirst) == 0)
      {
         tmp = NumSides;
      }
      else
      {
         if (NumSides==1)   tmp = 0;
         if (NumSides==2)   tmp = 3;
      }
      command << addressChar_ << "NR Z=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSPIMNumRepeats(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << addressChar_ << "NR F?";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A F="));
      RETURN_ON_MM_ERROR ( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << addressChar_ << "NR F=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSPIMDelayBeforeSide(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << addressChar_ << "NV Y?";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A Y="));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << addressChar_ << "NV Y=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSPIMDelayBeforeSlice(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   double tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << addressChar_ << "NV X?";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A X="));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << addressChar_ << "NV X=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}

int CScanner::OnSPIMState(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << addressChar_ << "SN X?";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A"));
      bool success;
      char c;
      RETURN_ON_MM_ERROR( hub_->GetAnswerCharAtPosition3(c) );
      switch ( c )
      {
         case g_SPIMStateCode_Idle:  success = pProp->Set(g_SPIMStateIdle); break;
         case g_SPIMStateCode_Arm:   success = pProp->Set(g_SPIMStateArmed); break;
         case g_SPIMStateCode_Armed: success = pProp->Set(g_SPIMStateArmed); break;
         default:                    success = pProp->Set(g_SPIMStateRunning); break;  // a bunch of different letter codes are possible while running
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      char c;
      if (tmpstr.compare(g_SPIMStateIdle) == 0)
      {
         // check status and stop if it's not idle already
         command << addressChar_ << "SN X?";
         RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A"));
         RETURN_ON_MM_ERROR( hub_->GetAnswerCharAtPosition3(c) );
         if (c!=g_SPIMStateCode_Idle)
         {
            // this will stop state machine if it's running, if we do SN without args we run the risk of it stopping itself before we send the next command
            // after we stop it, it will automatically go to idle state
            command.str("");
            command << addressChar_ << "SN X=" << (int)g_SPIMStateCode_Stop;
            RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A"));
         }
      }
      else if (tmpstr.compare(g_SPIMStateArmed) == 0)
      {
         // stop it if we need to, then change to armed state
         command << addressChar_ << "SN X?";
         RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A"));
         RETURN_ON_MM_ERROR( hub_->GetAnswerCharAtPosition3(c) );
         if (c==g_SPIMStateCode_Idle)
         {
            // this will stop state machine if it's running, if we do SN without args we run the risk of it stopping itself (e.g. finishing) before we send the next command
            command.str("");
            command << addressChar_ << "SN X=" << (int)g_SPIMStateCode_Stop;
            RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A"));
         }
         // now change to armed state
         command.str("");
         command << addressChar_ << "SN X=" << (int)g_SPIMStateCode_Arm;
         RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A"));
      }
      else if (tmpstr.compare(g_SPIMStateRunning) == 0)
      {
         // check status and start if it's idle or armed
         command << addressChar_ << "SN X?";
         RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A"));
         RETURN_ON_MM_ERROR( hub_->GetAnswerCharAtPosition3(c) );
         if ((c==g_SPIMStateCode_Idle) || (c==g_SPIMStateCode_Armed))
         {
            // if we are idle or armed then start it
            // assume that nothing else could have started it since our query moments ago
            command.str("");
            command << addressChar_ << "SN";
            RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A"));
         }
      }
      else
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   return DEVICE_OK;
}

int CScanner::OnRBMode(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   long tmp;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << addressChar_ << "RM X?";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A X="));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      if (tmp >= 128)
      {
         tmp -= 128;  // remove the "running now" code if present
      }
      bool success;
      switch ( tmp )
      {
         case 1: success = pProp->Set(g_RB_OnePoint_1); break;
         case 2: success = pProp->Set(g_RB_PlayOnce_2); break;
         case 3: success = pProp->Set(g_RB_PlayRepeat_3); break;
         default: success = false;
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {

      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_RB_OnePoint_1) == 0)
         tmp = 1;
      else if (tmpstr.compare(g_RB_PlayOnce_2) == 0)
         tmp = 2;
      else if (tmpstr.compare(g_RB_PlayRepeat_3) == 0)
         tmp = 3;
      else
         return DEVICE_INVALID_PROPERTY_VALUE;
      command << addressChar_ << "RM X=" << tmp;
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A"));
   }
   return DEVICE_OK;
}

int CScanner::OnRBTrigger(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   if (eAct == MM::BeforeGet) {
      pProp->Set(g_IdleState);
   }
   else  if (eAct == MM::AfterSet) {
      string tmpstr;
      pProp->Get(tmpstr);
      if (tmpstr.compare(g_DoItState) == 0)
      {
         command << addressChar_ << "RM";
         RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
         pProp->Set(g_DoneState);
      }
   }
   return DEVICE_OK;
}

int CScanner::OnRBRunning(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   long tmp = 0;
   static bool updateAgain;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_ && !updateAgain)
         return DEVICE_OK;
      command << addressChar_ << "RM X?";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A X="));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      bool success;
      if (tmp >= 128)
      {
         success = pProp->Set(g_YesState);
      }
      else
      {
         success = pProp->Set(g_NoState);
      }
      if (!success)
         return DEVICE_INVALID_PROPERTY_VALUE;
      updateAgain = false;
   }
   else if (eAct == MM::AfterSet)
   {
      updateAgain = true;
      return OnRBRunning(pProp, MM::BeforeGet);
   }
   return DEVICE_OK;
}

int CScanner::OnRBDelayBetweenPoints(MM::PropertyBase* pProp, MM::ActionType eAct)
{
   ostringstream command; command.str("");
   long tmp = 0;
   if (eAct == MM::BeforeGet)
   {
      if (!refreshProps_ && initialized_)
         return DEVICE_OK;
      command << addressChar_ << "RT Z?";
      RETURN_ON_MM_ERROR( hub_->QueryCommandVerify(command.str(), ":A Z="));
      RETURN_ON_MM_ERROR( hub_->ParseAnswerAfterEquals(tmp) );
      if (!pProp->Set(tmp))
         return DEVICE_INVALID_PROPERTY_VALUE;
   }
   else if (eAct == MM::AfterSet) {
      pProp->Get(tmp);
      command << addressChar_ << "RT Z=" << tmp;
      RETURN_ON_MM_ERROR ( hub_->QueryCommandVerify(command.str(), ":A") );
   }
   return DEVICE_OK;
}
