//###########################################################################
// This file is part of LImA, a Library for Image Acquisition
//
// Copyright (C) : 2009-2011
// European Synchrotron Radiation Facility
// BP 220, Grenoble 38043
// FRANCE
//
// This is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 3 of the License, or
// (at your option) any later version.
//
// This software is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//###########################################################################
#include <cmath>
#include "PerkinElmerSyncCtrlObj.h"
#include <Acq.h>

using namespace lima;
using namespace lima::PerkinElmer;

const double MIN_EXPO_TIME = 33.2e-3;

SyncCtrlObj::SyncCtrlObj(HANDLE &acq_desc) :
  m_acq_desc(acq_desc),
  m_trig_mode(IntTrig),
  m_offset_data(NULL),
  m_gain_data(NULL),
  m_expo_time(-1.),
  m_acq_nb_frames(1),
  m_corr_expo_time(-1.),
  m_keep_first_image(false)
{
  invalidateCorrectionImage();
}

SyncCtrlObj::~SyncCtrlObj()
{
  if(m_offset_data)
    _aligned_free(m_offset_data);
  if(m_gain_data)
    _aligned_free(m_gain_data);
}

bool SyncCtrlObj::checkTrigMode(TrigMode trig_mode)
{
  bool valid_mode = false;
  switch (trig_mode)
    {
    case IntTrig:
    case ExtStartStop:
    case ExtTrigReadout:
      valid_mode = true;
      break;

    default:
      valid_mode = false;
      break;
    }
  return valid_mode;
}

void SyncCtrlObj::setTrigMode(TrigMode trig_mode)
{
  DEB_MEMBER_FUNCT();
  DWORD trigMode;
  switch(trig_mode)
    {
    case ExtTrigReadout:
    case ExtStartStop:
      trigMode = HIS_SYNCMODE_EXTERNAL_TRIGGER;break;
    default:
      trigMode = HIS_SYNCMODE_INTERNAL_TIMER;break;
    }
  if(Acquisition_SetFrameSyncMode(m_acq_desc,trigMode) != HIS_ALL_OK)
    THROW_HW_ERROR(Error) << "Can't set trigger mode to:" << DEB_VAR1(trig_mode);

  m_trig_mode = trig_mode;
}

void SyncCtrlObj::getTrigMode(TrigMode& trig_mode)
{
  trig_mode = m_trig_mode;
}

void SyncCtrlObj::setExpTime(double exp_time)
{
  _setExpTime(exp_time,!m_keep_first_image);
}
void SyncCtrlObj::_setExpTime(double exp_time,bool send_to_hard)
{
  DEB_MEMBER_FUNCT();
  if(m_trig_mode == IntTrig && send_to_hard)
    {						
      DWORD expTime = DWORD(exp_time * 1e6);
      if(Acquisition_SetTimerSync(m_acq_desc,&expTime) != HIS_ALL_OK)
	THROW_HW_ERROR(Error) << "Can't change exposition time";
      m_expo_time = expTime * 1e-6;
    }
  else
    m_expo_time = exp_time;

  DEB_TRACE() << DEB_VAR2(m_expo_time,m_corr_expo_time);
  if(fabs(m_expo_time - m_corr_expo_time) > 1e-6)
    invalidateCorrectionImage();
}
void SyncCtrlObj::_setExpTimeToMin()
{
  if(m_trig_mode == IntTrig && m_keep_first_image)
    {
      DWORD expTime = DWORD(MIN_EXPO_TIME * 1e6);
      Acquisition_SetTimerSync(m_acq_desc,&expTime);
    }
}
void SyncCtrlObj::getExpTime(double& exp_time)
{
  exp_time = m_expo_time;
}

void SyncCtrlObj::setLatTime(double lat_time)
{
  //Not managed
}

void SyncCtrlObj::getLatTime(double& lat_time)
{
  lat_time = 0;		// Readout
}

void SyncCtrlObj::setNbHwFrames(int nb_frames)
{
  DEB_MEMBER_FUNCT();
  DEB_PARAM() << DEB_VAR1(nb_frames);

  m_acq_nb_frames = nb_frames;
}

void SyncCtrlObj::getNbHwFrames(int& nb_frames)
{
  nb_frames = m_acq_nb_frames;
}

void SyncCtrlObj::getValidRanges(ValidRangesType& valid_ranges)
{
  valid_ranges.min_exp_time = MIN_EXPO_TIME;
  valid_ranges.max_exp_time = 5.;
  valid_ranges.min_lat_time = 0.;
  valid_ranges.max_lat_time = 0.;
}

void SyncCtrlObj::startAcq()
{
  DEB_MEMBER_FUNCT();
  DEB_TRACE() << DEB_VAR3(m_corr_mode,m_offset_data,m_gain_data);
  unsigned short* offset_data;
  if(m_corr_mode != Interface::No)
    offset_data = m_offset_data;
  else
    offset_data = NULL;

  DWORD *gain_data;
  if(m_corr_mode == Interface::OffsetAndGain)
    gain_data = m_gain_data;
  else
    gain_data = NULL;

  if(Acquisition_Acquire_Image(m_acq_desc,2,0,
			       HIS_SEQ_CONTINUOUS, 
			       offset_data,
			       gain_data,
			       NULL)!= HIS_ALL_OK)
    THROW_HW_ERROR(Error) << "Could not start the acquisition";
  
}

void SyncCtrlObj::reallocOffset(const Size &aSize)
{
  DEB_MEMBER_FUNCT();

  if(m_gain_data)		// Invalidate gain data
    {
      _aligned_free(m_gain_data);
      m_gain_data = NULL;
    }
  if(m_offset_data)
    _aligned_free(m_offset_data);
  m_offset_data = (unsigned short*)_aligned_malloc(aSize.getWidth() * aSize.getHeight() * sizeof(unsigned short),16);
  DEB_TRACE() << DEB_VAR1(m_offset_data);
}

void SyncCtrlObj::reallocGain(const Size &aSize)
{
  DEB_MEMBER_FUNCT();

  if(m_gain_data)
    _aligned_free(m_gain_data);
  m_gain_data = (DWORD*)_aligned_malloc(aSize.getWidth() * aSize.getHeight() * sizeof(DWORD),16);
  DEB_TRACE() << DEB_VAR1(m_gain_data);
}

void SyncCtrlObj::invalidateCorrectionImage()
{
  if(m_gain_data)
    {
      _aligned_free(m_gain_data);
      m_gain_data = NULL;
    }
  if(m_offset_data)
    {
      _aligned_free(m_offset_data);
      m_offset_data = NULL;
    }
  m_corr_mode = Interface::No;
  m_corr_expo_time = -1.;
}
