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
#include "PerkinElmerInterface.h"
#include "PerkinElmerDetInfoCtrlObj.h"
#include "PerkinElmerSyncCtrlObj.h"
#include <Acq.h>

using namespace lima;
using namespace lima::PerkinElmer;
Interface *theInterface = NULL;

// CALLBACKS
void CALLBACK lima::PerkinElmer::_OnEndAcqCallback(HANDLE)
{
  theInterface->SetEndAcquisition();
}

void CALLBACK lima::PerkinElmer::_OnEndFrameCallback(HANDLE)
{
  theInterface->newFrameReady();
}

Interface::Interface() :
  m_acq_desc(NULL),
  m_acq_started(false)
{
  DEB_MEMBER_FUNCT();

  unsigned int max_columns,max_rows;
  _InitDetector(max_columns,max_rows);
  m_det_info = new DetInfoCtrlObj(m_acq_desc,max_columns,max_rows);
  m_sync = new SyncCtrlObj(m_acq_desc);
  
  if(Acquisition_SetCallbacksAndMessages(m_acq_desc, NULL, 0,
					 0, _OnEndFrameCallback, _OnEndAcqCallback) != HIS_ALL_OK)
    THROW_HW_ERROR(Error) << "Could not set callback";
  theInterface = this;

  m_cap_list.push_back(HwCap(m_det_info));
  m_cap_list.push_back(HwCap(&m_buffer_ctrl_mgr));
  m_cap_list.push_back(HwCap(m_sync));

}

Interface::~Interface()
{
  Acquisition_Close(m_acq_desc);
  delete m_det_info;
  delete m_sync;
}

void Interface::_InitDetector(unsigned int &max_columns,unsigned int &max_rows)
{
  DEB_MEMBER_FUNCT();

  unsigned int sensorNum;
  if(Acquisition_EnumSensors(&sensorNum,1,0) != HIS_ALL_OK)
    THROW_HW_ERROR(Error) << "No camera found";

  ACQDESCPOS sensorId = 0;
  if(Acquisition_GetNextSensor(&sensorId,&m_acq_desc) != HIS_ALL_OK)
    THROW_HW_ERROR(Error) << "Can't get detector" << DEB_VAR1(sensorId);
  
  int channel_id;
  const char *channel_type;
  if(!get_channel_type_n_id(m_acq_desc,channel_type,channel_id))
    THROW_HW_ERROR(Error) << "Can't get channel type and number";
  DEB_ALWAYS() << "Acquisition board:" << DEB_VAR2(channel_type,channel_id);
  
  //Reset Binning
  if(Acquisition_SetCameraBinningMode(m_acq_desc,1) != HIS_ALL_OK)
    THROW_HW_ERROR(Error) << "Can't reset the Binning";

  //Reset Roi
  if(Acquisition_SetCameraROI(m_acq_desc,0xf) != HIS_ALL_OK)
    THROW_HW_ERROR(Error) << "Can't reset roi";

  unsigned int dwFrames,dwSortFlags,data_type;
  BOOL bEnableIRQ;
  DWORD dwAcqType,system_id,sync_mode,dwHwAccess;
  if(Acquisition_GetConfiguration(m_acq_desc,&dwFrames,
				  &max_rows,&max_columns,&data_type,
				  &dwSortFlags, &bEnableIRQ, &dwAcqType, 
				  &system_id, &sync_mode, &dwHwAccess) != HIS_ALL_OK)
    THROW_HW_ERROR(Error) << "Can't get detector configuration";
  
  DEB_ALWAYS() << "System Id:" << DEB_VAR1(system_id);
  DEB_ALWAYS() << "Detector size (" << max_columns << "," << max_rows << ")";

  if(Acquisition_SetFrameSyncMode(m_acq_desc,HIS_SYNCMODE_INTERNAL_TIMER) != HIS_ALL_OK)
    THROW_HW_ERROR(Error) << "Can't set trigger mode to INTERNAL";
}

void Interface::getCapList(CapList &cap_list) const
{
  cap_list = m_cap_list;
}

void Interface::reset(ResetLevel reset_level)
{
  m_acq_started = false;
}

void Interface::prepareAcq()
{
  DEB_MEMBER_FUNCT();

  StdBufferCbMgr& buffer_mgr = m_buffer_ctrl_mgr.getBuffer();
  int nb_buffers;
  buffer_mgr.getNbBuffers(nb_buffers);
  
  Size image_size;
  m_det_info->getDetectorImageSize(image_size);

  if(Acquisition_DefineDestBuffers(m_acq_desc,
				   (unsigned short*)buffer_mgr.getFrameBufferPtr(0),
				   nb_buffers,
				   image_size.getHeight(),
				   image_size.getWidth()) != HIS_ALL_OK)
    THROW_HW_ERROR(Error) << "Unable to register destination buffer";
  m_acq_frame_nb = 0;
}

void Interface::startAcq()
{
  StdBufferCbMgr& buffer_mgr = m_buffer_ctrl_mgr.getBuffer();
  buffer_mgr.setStartTimestamp(Timestamp::now());
  m_acq_started = true;
  m_sync->startAcq();
}

void Interface::stopAcq()
{
  Acquisition_Abort(m_acq_desc);
}

void Interface::getStatus(StatusType &status)
{
  status.set(m_acq_started ? 
	     HwInterface::StatusType::Exposure : HwInterface::StatusType::Ready);
}

int Interface::getNbHwAcquiredFrames()
{
  return m_acq_frame_nb;
}

void Interface::newFrameReady()
{
  StdBufferCbMgr& buffer_mgr = m_buffer_ctrl_mgr.getBuffer();
  HwFrameInfoType frame_info;
  frame_info.acq_frame_nb = m_acq_frame_nb;
  bool continueAcq = buffer_mgr.newFrameReady(frame_info);
  ++m_acq_frame_nb;
  if(!continueAcq) stopAcq();
}

void Interface::SetEndAcquisition()
{
  m_acq_started = false;
}

/*============================================================================
			   Static Methodes
============================================================================*/
bool Interface::get_channel_type_n_id(HANDLE &acq_desc,
				      const char* &channel_type,
				      int &channel_id)
{
  UINT nChannelType;

  if(Acquisition_GetCommChannel(acq_desc,&nChannelType,&channel_id) != HIS_ALL_OK)
    return false;

  switch(nChannelType)
    {
    case HIS_BOARD_TYPE_ELTEC: 
      channel_type = "XRD-FG or XRD-FGe Frame Grabber";break;
    case HIS_BOARD_TYPE_ELTEC_XRD_FGX:
      channel_type = "XRD-FGX frame grabber";break;
    case HIS_BOARD_TYPE_ELTEC_XRD_FGE_Opto:
      channel_type = "XRD-FGe Opto";break;
    case HIS_BOARD_TYPE_ELTEC_GbIF:
      channel_type = "GigabitEthernet";break;
    default:
      channel_type = "Unknow";break;
    }
  return true;
}
