/* Copyright 2018 NXP */


#include "freertos/FreeRTOS.h"

#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"


#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "serial_link.h"
#include "serial.h"
#include "zb_common.h"

#if ENABLE_ZB_MODULE

#define ZB_DEVICE_OTA_IMAGE_FILE_IDENTIFY      0x1EF1EE0B     

// ------------------------------------------------------------------
// External prototype
// ------------------------------------------------------------------

extern tsZbNetworkInfo zbNetworkInfo;

int eGetPermitJoining(void) {
    if (eSL_SendMessage(E_SL_MSG_GET_PERMIT_JOIN, 0, NULL, NULL) != E_SL_OK) {
        return 0;
    }
    return 1;
}



teZcbStatus eSetPermitJoining(uint8_t u8Interval) {

    struct _PermitJoiningMessage {
        uint16_t    u16TargetAddress;
        uint8_t     u8Interval;
        uint8_t     u8TCSignificance;
    } PACKED sPermitJoiningMessage;

    sPermitJoiningMessage.u16TargetAddress  = htons(E_ZB_BROADCAST_ADDRESS_ROUTERS);
    sPermitJoiningMessage.u8Interval        = u8Interval;
    sPermitJoiningMessage.u8TCSignificance  = 0;

    if (eSL_SendMessage(E_SL_MSG_PERMIT_JOINING_REQUEST, sizeof(struct _PermitJoiningMessage),
           &sPermitJoiningMessage, NULL) != E_SL_OK) {
        return E_ZCB_COMMS_FAILED;
    }
    eGetPermitJoining();
    return E_ZCB_OK;
}


teZcbStatus eSetChannelMask(uint32_t u32ChannelMask) {

    struct _SetChannelMaskMessage {
        uint32_t    u32ChannelMask;
    } PACKED sSetChannelMaskMessage;

    sSetChannelMaskMessage.u32ChannelMask  = htonl(u32ChannelMask);

    if (eSL_SendMessage(E_SL_MSG_SET_CHANNELMASK, sizeof(struct _SetChannelMaskMessage),
            &sSetChannelMaskMessage, NULL) != E_SL_OK) {
        return E_ZCB_COMMS_FAILED;
    }
    return E_ZCB_OK;
}


teZcbStatus eSetDeviceType(teModuleMode eModuleMode) {

    struct _SetDeviceTypeMessage {
        uint8_t    u8ModuleMode;
    } PACKED sSetDeviceTypeMessage;

    sSetDeviceTypeMessage.u8ModuleMode = eModuleMode;

    if (eSL_SendMessage(E_SL_MSG_SET_DEVICETYPE, sizeof(struct _SetDeviceTypeMessage),
            &sSetDeviceTypeMessage, NULL) != E_SL_OK) {
        return E_ZCB_COMMS_FAILED;
    }

    return E_ZCB_OK;
}


teZcbStatus eStartNetwork(void) {
    if (eSL_SendMessage(E_SL_MSG_START_NETWORK, 0, NULL, NULL) != E_SL_OK) {
        return E_ZCB_COMMS_FAILED;
    }
    
    return E_ZCB_OK;
}


teZcbStatus eErasePersistentData(void)
{
    teZcbStatus rt = E_ZCB_OK;

    /* 
      @ baudrate = 250000, somehow the Zigbee control brdige's ACK to ERASE_PD_CMD (0x0012) end with 0xFB (should be 0x03)
      For this reason, the normal API eSL_SendMessage() will not able to receive ACK, just Nowait API instead
    */
    if (eSL_SendMessageNoWait(E_SL_MSG_ERASE_PERSISTENT_DATA, 0, NULL, NULL) != E_SL_OK) {
        rt = E_ZCB_COMMS_FAILED;
    } else {
        if (eSL_MessageWait(E_SL_MSG_RESTART_FACTORY_NEW, 2000, NULL, NULL) != E_SL_OK) {
            HAL_Printf("No indication of new factory restart\r\n");
            rt = E_ZCB_COMMS_FAILED;
        }
    }
    
    return rt;
}


teZcbStatus eResetDevice(void)
{
    if (eSL_SendMessage(E_SL_MSG_RESET, 0, NULL, NULL) != E_SL_OK) {
        return E_ZCB_COMMS_FAILED;
    }
    /*  May Need Do Some Blocking Thing for The Device To Reset and Be Ready Again */
    return E_ZCB_OK; 

}


teZcbStatus eMgmtLeaveRequst(uint16_t u16ShortAddr,
                             uint64_t u64MacAddr, 
                             uint8_t u8Rejoin, 
                             uint8_t u8RmChildren)
{
    struct _MgmtLeaveRequstMessage {
        uint16_t    u16TargetAddress;
        uint64_t    u64ExtendAddress;
        uint8_t     uRejoinOrNot;     //0: Do Not Rejoin
        uint8_t     uRmChildrenOrNot; //0: Leave and Remove the children
    } PACKED sMgmtLeaveRequstMessage;

    sMgmtLeaveRequstMessage.u16TargetAddress = htons(u16ShortAddr);
    sMgmtLeaveRequstMessage.u64ExtendAddress = htond(u64MacAddr);
    sMgmtLeaveRequstMessage.uRejoinOrNot     = u8Rejoin;
    sMgmtLeaveRequstMessage.uRmChildrenOrNot = u8RmChildren;
    
    if (eSL_SendMessage(E_SL_MSG_MANAGEMENT_LEAVE_REQUEST, sizeof(struct _MgmtLeaveRequstMessage),
            &sMgmtLeaveRequstMessage, NULL) != E_SL_OK) {
        return E_ZCB_COMMS_FAILED;
    }

    return E_ZCB_OK;
    
}


teZcbStatus eSetExPANID(uint64_t u64Epid)
{
    struct _SetExPANIDMessage {
        uint64_t    u64ExPANID;
    } PACKED sSetExPANIDMessage;

    sSetExPANIDMessage.u64ExPANID  = htond(u64Epid);

    if (eSL_SendMessage(E_SL_MSG_SET_EXT_PANID, sizeof(struct _SetExPANIDMessage),
            &sSetExPANIDMessage, NULL) != E_SL_OK) {
        return E_ZCB_COMMS_FAILED;
    }
    
    return E_ZCB_OK;
}



teZcbStatus eActiveEndpointRequest(uint16_t u16ShortAddr)
{
    struct _ActiveEndpointRequestMessage {
        uint16_t    u16ShortAddress;
    } PACKED sActiveEndpointRequestMessage;

    sActiveEndpointRequestMessage.u16ShortAddress  =u16ShortAddr; //htons(u16ShortAddr);

    if (eSL_SendMessage(E_SL_MSG_ACTIVE_ENDPOINT_REQUEST, sizeof(struct _ActiveEndpointRequestMessage),
            &sActiveEndpointRequestMessage, NULL) != E_SL_OK) {
        return E_ZCB_COMMS_FAILED;
    }

    return E_ZCB_OK;
}



teZcbStatus eNodeDescriptorRequest(uint16_t u16ShortAddr)    
{
    struct _NodeDescriptorRequestMessage {
        uint16_t    u16ShortAddress;
    } PACKED sNodeDescriptorRequestMessage;

    sNodeDescriptorRequestMessage.u16ShortAddress  = htons(u16ShortAddr);

    if (eSL_SendMessage(E_SL_MSG_NODE_DESCRIPTOR_REQUEST, sizeof(struct _NodeDescriptorRequestMessage),
            &sNodeDescriptorRequestMessage, NULL) != E_SL_OK) {
        return E_ZCB_COMMS_FAILED;
    }

    return E_ZCB_OK;

}



teZcbStatus eSimpleDescriptorRequest(uint16_t u16ShortAddr, uint8_t u8DstEp)
{
    struct _SimpleDescriptorRequestMessage {
        uint16_t    u16ShortAddress;
        uint8_t     u8DestinationEndpoint;
    } PACKED sSimpleDescriptorRequestMessage;

    sSimpleDescriptorRequestMessage.u16ShortAddress       = htons(u16ShortAddr);
    sSimpleDescriptorRequestMessage.u8DestinationEndpoint = u8DstEp;

    if (eSL_SendMessage(E_SL_MSG_SIMPLE_DESCRIPTOR_REQUEST, sizeof(struct _SimpleDescriptorRequestMessage),
            &sSimpleDescriptorRequestMessage, NULL) != E_SL_OK) {
        return E_ZCB_COMMS_FAILED;
    }
    
    return E_ZCB_OK;
}



teZcbStatus eMatchDescriptorRequest(uint16_t u16ShortAddr,
                                    uint16_t u16ProfileId,
                                    uint8_t u8NoInputCluster,
                                    uint16_t u16InClusterId,
                                    uint8_t u8NoOutputCluster,
                                    uint16_t u16OutClusterId)
{
    struct _MatchDescriptorRequestMessage {
        uint16_t    u16ShortAddress;
        uint16_t    u16ProfileIdentification;
        uint8_t     u8NumberInputCluster;
        uint16_t    u16InputClusterId;
        uint8_t     u8NumberOutputCluster;
        uint16_t    u16OutputClusterId;
    } PACKED sMatchDescriptorRequestMessage;

    sMatchDescriptorRequestMessage.u16ShortAddress          = htons(u16ShortAddr);
    sMatchDescriptorRequestMessage.u16ProfileIdentification = htons(u16ProfileId);
    sMatchDescriptorRequestMessage.u8NumberInputCluster     = u8NoInputCluster;
    sMatchDescriptorRequestMessage.u16InputClusterId        = htons(u16InClusterId);
    sMatchDescriptorRequestMessage.u8NumberOutputCluster    = u8NoOutputCluster;
    sMatchDescriptorRequestMessage.u16OutputClusterId       = htons(u16OutClusterId);

    if (eSL_SendMessage(E_SL_MSG_MATCH_DESCRIPTOR_REQUEST, sizeof(struct _MatchDescriptorRequestMessage),
            &sMatchDescriptorRequestMessage, NULL) != E_SL_OK) {
        return E_ZCB_COMMS_FAILED;
    }
    
    return E_ZCB_OK;
}



teZcbStatus eNwkAddressRequest(uint16_t u16DstShortAddr,
                               uint64_t u64InterestMacAddr,
                               uint8_t u8RstType,
                               uint8_t u8Index)
{
    struct _NetworkAddressRequestMessage {
        uint16_t    u16TargetShortAddress;
        uint64_t    u64InterestMacAddress;
        uint8_t     u8RequestType;
        uint8_t     u8StartIndex;
    } PACKED sNetworkAddressRequestMessage;

    sNetworkAddressRequestMessage.u16TargetShortAddress = htons(u16DstShortAddr);
    sNetworkAddressRequestMessage.u64InterestMacAddress = htond(u64InterestMacAddr);
    sNetworkAddressRequestMessage.u8RequestType         = u8RstType;
    sNetworkAddressRequestMessage.u8StartIndex          = u8Index;

    if (eSL_SendMessage(E_SL_MSG_NETWORK_ADDRESS_REQUEST, sizeof(struct _NetworkAddressRequestMessage),
            &sNetworkAddressRequestMessage, NULL) != E_SL_OK) {
        return E_ZCB_COMMS_FAILED;
    }
    
    return E_ZCB_OK;
}



teZcbStatus eIeeeAddressRequest(uint16_t u16DstShortAddr,
                                uint16_t u16InterestShortAddr,
                                uint8_t u8RstType,
                                uint8_t u8Index)
{
    struct _IeeeAddressRequestMessage {
        uint16_t    u16TargetShortAddress;
        uint16_t    u16InterestShortAddress;
        uint8_t     u8RequestType;
        uint8_t     u8StartIndex;
    } PACKED sIeeeAddressRequestMessage;

    sIeeeAddressRequestMessage.u16TargetShortAddress   = htons(u16DstShortAddr);
    sIeeeAddressRequestMessage.u16InterestShortAddress = htons(u16InterestShortAddr);
    sIeeeAddressRequestMessage.u8RequestType           = u8RstType;
    sIeeeAddressRequestMessage.u8StartIndex            = u8Index;

    if (eSL_SendMessage(E_SL_MSG_IEEE_ADDRESS_REQUEST, sizeof(struct _IeeeAddressRequestMessage),
            &sIeeeAddressRequestMessage, NULL) != E_SL_OK) {
        return E_ZCB_COMMS_FAILED;
    }
    
    return E_ZCB_OK;
}



teZcbStatus eReadAttributeRequest(uint8_t u8AddrMode, 
                                  uint16_t u16Addr, 
                                  uint8_t u8SrcEp, 
                                  uint8_t u8DstEp,
                                  uint16_t u16ClusterId,
                                  uint16_t u16ManuCode,
                                  bool    bIsManuSpecific,
                                  uint8_t u8NumOfAttr,                            
                                  uint16_t* au16AttrList)
{
    uint8_t u8SequenceNo;
    teSL_Status eStatus;

    if (u8NumOfAttr > MAX_NB_READ_ATTRIBUTES) {
        u8NumOfAttr = MAX_NB_READ_ATTRIBUTES;
    }

    tsZDReadAttrReq sReadAttrReq;
    sReadAttrReq.u8AddressMode              = u8AddrMode;
    sReadAttrReq.u16ShortAddress            = htons(u16Addr);
    sReadAttrReq.u8SourceEndPointId         = u8SrcEp;
    sReadAttrReq.u8DestinationEndPointId    = u8DstEp;
    sReadAttrReq.u16ClusterId               = htons(u16ClusterId);
    sReadAttrReq.bDirectionIsServerToClient = SEND_DIR_FROM_CLIENT_TO_SERVER;
    sReadAttrReq.bIsManufacturerSpecific    = bIsManuSpecific;
    sReadAttrReq.u16ManufacturerCode        = htons(u16ManuCode);
    sReadAttrReq.u8NumberOfAttributes       = u8NumOfAttr;


    for (uint8_t i = 0; i < u8NumOfAttr; i++)
    {
        sReadAttrReq.au16AttributeList[i] = htons(au16AttrList[i]);
    }

    HAL_Printf("Send Read Attribute Request to 0x%04x, endpoints: 0x%02x -> 0x%02x\r\n",
        u16Addr, u8SrcEp, u8DstEp);

    eStatus = eSL_SendMessage(E_SL_MSG_READ_ATTRIBUTE_REQUEST,
        sizeof(tsZDReadAttrReq) + sizeof(uint16_t)*(u8NumOfAttr - MAX_NB_READ_ATTRIBUTES), &sReadAttrReq, &u8SequenceNo);
    if (eStatus != E_SL_OK)
    {
        HAL_Printf("Send Read Attribute Request to 0x%04x : Fail (0x%x)\r\n",
            u16Addr, eStatus);
        return E_ZCB_COMMS_FAILED;
    }

    return E_ZCB_OK;
}



teZcbStatus eSendBindUnbindCommand(uint64_t u64TargetIeeeAddr,
                                   uint8_t u8TargetEp,
                                   uint16_t u16ClusterId,
                                   bool bBind)
{                            
    struct _BindUnbindReq {
        uint64_t u64SrcAddress;
        uint8_t u8SrcEndpoint;
        uint16_t u16ClusterId;
        uint8_t u8DstAddrMode;
        union {
            struct {
                uint16_t u16DstAddress;
            } PACKED sShort;
            struct {
                uint64_t u64DstAddress;
                uint8_t u8DstEndPoint;
            } PACKED sExtended;
        } PACKED uAddressField;
    } PACKED sBindUnbindReq;

#if 0
    struct _BindUnbindResp {
        uint8_t     u8SequenceNo;
        uint8_t     u8Status;
    } PACKED *psBindUnbindResp = NULL;
#endif
    
    uint16_t u16Length = sizeof(struct _BindUnbindReq);
    uint8_t u8SequenceNo;
    
    memset( (char *)&sBindUnbindReq, 0, u16Length );

    sBindUnbindReq.u64SrcAddress = (u64TargetIeeeAddr); //htond
    sBindUnbindReq.u8SrcEndpoint = u8TargetEp;
    sBindUnbindReq.u16ClusterId  = htons(u16ClusterId);
    sBindUnbindReq.u8DstAddrMode = E_ZD_ADDRESS_MODE_IEEE;
    sBindUnbindReq.uAddressField.sExtended.u64DstAddress = (zbNetworkInfo.u64IeeeAddress);//htond
    sBindUnbindReq.uAddressField.sExtended.u8DstEndPoint = ZB_ENDPOINT_ATTR;
    
    HAL_Printf("Send (Un-)Binding request to 0x%016llX (%d)\r\n", u64TargetIeeeAddr, u16Length);

    if (bBind) 
    {    
        if (eSL_SendMessage( E_SL_MSG_BIND,
                             u16Length, 
                             &sBindUnbindReq, 
                             &u8SequenceNo) != E_SL_OK) 
        {
            HAL_Printf("Sending bind command fail\r\n");
            return E_ZCB_COMMS_FAILED;
        }
        else
        {
            //wait 1s for the bind response
            if (eSL_MessageWait(E_SL_MSG_BIND_RESPONSE, 1000, NULL, NULL) != E_SL_OK)
            {
                HAL_Printf( "No bind response is received\r\n");
                return E_ZCB_COMMS_FAILED;
            }
        }
    }
    else 
    {
        if (eSL_SendMessage( E_SL_MSG_UNBIND,
                             u16Length, 
                             &sBindUnbindReq, 
                             &u8SequenceNo) != E_SL_OK) 
        {
            HAL_Printf("Sending unbind command fail\n");
            return E_ZCB_COMMS_FAILED;
        } 
        else
        {
            //wait 1s for the unbind response
            if (eSL_MessageWait(E_SL_MSG_UNBIND_RESPONSE, 1000, NULL, NULL) != E_SL_OK)
            {
                HAL_Printf("No unbind response is received\r\n");
                return E_ZCB_COMMS_FAILED;
            }
        }

    }
    return E_ZCB_OK;
}



teZcbStatus eConfigureReportingCommand(uint8_t u8AddrMode, 
                                       uint16_t u16Addr, 
                                       uint8_t u8SrcEp, 
                                       uint8_t u8DstEp,
                                       uint16_t u16ClusterId,                                       
                                       uint16_t u16ManuCode, 
                                       uint8_t u8AttributeType,
                                       uint16_t u16AttributeID,
                                       uint16_t u16Min,
                                       uint16_t u16Max,
                                       uint8_t u8Change)
{                        
    struct _AttributeReportingConfigurationRequest
    {
        uint8_t     u8TargetAddrMode;
        uint16_t    u16TargetAddress;
        uint8_t     u8SrcEndpoint;
        uint8_t     u8DstEndpoint;
        uint16_t    u16ClusterID;       
        uint8_t     bDirection;
        uint8_t     bManuSpecific; 
        uint16_t    u16ManuCode;
        uint8_t     u8AttrCount;
        uint8_t     u8AttrDir;
        uint8_t     u8DataType;
        uint16_t    u16AttributeId;     
        uint16_t    u16MinInterval;
        uint16_t    u16MaxInterval;
        uint16_t    u16Timeout;
        uint8_t     u8AttrChange;
    } PACKED sAttributeReportingConfigurationRequest;

#if 0
    struct _AttributeReportingConfigurationResponse
    {        
        uint8_t     u8SequenceNo;
        uint8_t     u8Status;
    } PACKED *psAttributeReportingConfigurationResponse = NULL;
#endif
    
    uint16_t u16Length = sizeof(struct _AttributeReportingConfigurationRequest);
    uint8_t u8SequenceNo;
    
    HAL_Printf("Send Reporting Configuration request to 0x%04X\r\n", u16Addr);
    
    sAttributeReportingConfigurationRequest.u8TargetAddrMode = E_ZB_ADDRESS_MODE_SHORT;
    sAttributeReportingConfigurationRequest.u16TargetAddress = htons(u16Addr);
    sAttributeReportingConfigurationRequest.u8SrcEndpoint    = u8SrcEp;
    sAttributeReportingConfigurationRequest.u8DstEndpoint    = u8DstEp;    
    sAttributeReportingConfigurationRequest.u16ClusterID     = htons(u16ClusterId);    
    sAttributeReportingConfigurationRequest.bDirection       = SEND_DIR_FROM_CLIENT_TO_SERVER;
    sAttributeReportingConfigurationRequest.bManuSpecific    = MANUFACTURER_SPECIFIC_FALSE;
    sAttributeReportingConfigurationRequest.u16ManuCode      = htons(u16ManuCode);
    sAttributeReportingConfigurationRequest.u8AttrCount      = 1;
    sAttributeReportingConfigurationRequest.u8AttrDir        = ATTRIBUTE_DIR_TX_SERVER;
    
    sAttributeReportingConfigurationRequest.u8DataType       = u8AttributeType;
    sAttributeReportingConfigurationRequest.u16AttributeId   = htons(u16AttributeID);
    sAttributeReportingConfigurationRequest.u16MinInterval   = htons(u16Min);
    sAttributeReportingConfigurationRequest.u16MaxInterval   = htons(u16Max);
    sAttributeReportingConfigurationRequest.u16Timeout       = 0;
    sAttributeReportingConfigurationRequest.u8AttrChange     = u8Change;

    if (eSL_SendMessage( E_SL_MSG_CONFIG_REPORTING_REQUEST,
                         u16Length, 
                         &sAttributeReportingConfigurationRequest, 
                         &u8SequenceNo) != E_SL_OK)
    {
        HAL_Printf("Sending configure report command fail\r\n");
        return E_ZCB_COMMS_FAILED;    
    }
    return E_ZCB_OK;
}



teZcbStatus eOtaLoadNewImage(const char * image, bool bCoordUpgrade)
{
    if (image == NULL) {
        HAL_Printf( "Input Ota Image is invalid\r\n");
        return E_ZCB_ERROR;
    }
    
    struct _OtaLoadNewImagePayload
    {
        uint8_t     u8TargetAddrMode;
        uint16_t    u16TargetAddress; 
        uint32_t    u32FileIdendify;
        uint16_t    u16HeaderVersion;
        uint16_t    u16HeaderLength;
        uint16_t    u16HeaderControlField;
        uint16_t    u16ManufacturerCode;
        uint16_t    u16ImageType;
        uint32_t    u32FileVersion;
        uint16_t    u16StackVersion;
        uint8_t     auOtaHeaderString[32];
        uint32_t    u32ImageSize;
        uint8_t     u8Security;
        uint64_t    u64UpgradeFileDest;
        uint16_t    u16MinHWVersion;
        uint16_t    u16MaxHWVersion;        
    } PACKED sOtaLoadNewImagePayload;
    
    uint16_t u16Length = sizeof(struct _OtaLoadNewImagePayload);
    memset(&sOtaLoadNewImagePayload.u8TargetAddrMode, 0, u16Length);
    uint8_t u8SequenceNo;
    uint8_t * p_image = (uint8_t *)image;

    sOtaLoadNewImagePayload.u8TargetAddrMode = E_ZB_ADDRESS_MODE_SHORT;
    sOtaLoadNewImagePayload.u16TargetAddress = htons(ZB_COORDINATOR_SHORT_ADDRESS);
    
    uint32_t u32FileId;
    memcpy(&u32FileId, p_image, sizeof(uint32_t));
    sOtaLoadNewImagePayload.u32FileIdendify = htonl(u32FileId);
    if (sOtaLoadNewImagePayload.u32FileIdendify != ZB_DEVICE_OTA_IMAGE_FILE_IDENTIFY) {
        HAL_Printf("invalid ota header file id = 0x%04X\r\n", sOtaLoadNewImagePayload.u32FileIdendify);
        return E_ZCB_INVALID_VALUE;
    }             
    p_image += sizeof(uint32_t);

    uint16_t u16HeaderVer;
    memcpy(&u16HeaderVer, p_image, sizeof(uint16_t));
    sOtaLoadNewImagePayload.u16HeaderVersion = htons(u16HeaderVer);
    p_image += sizeof(uint16_t);

    uint16_t u16HeaderLen;
    memcpy(&u16HeaderLen, p_image, sizeof(uint16_t));
    sOtaLoadNewImagePayload.u16HeaderLength = htons(u16HeaderLen);
    p_image += sizeof(uint16_t);

    uint16_t u16HeaderCtrFd;
    memcpy(&u16HeaderCtrFd, p_image, sizeof(uint16_t));
    sOtaLoadNewImagePayload.u16HeaderControlField = htons(u16HeaderCtrFd);
    p_image += sizeof(uint16_t);

    uint16_t u16ManuCode;
    memcpy(&u16ManuCode, p_image, sizeof(uint16_t));
    sOtaLoadNewImagePayload.u16ManufacturerCode = htons(u16ManuCode);
    p_image += sizeof(uint16_t);

    uint16_t u16ImageTp;
    memcpy(&u16ImageTp, p_image, sizeof(uint16_t));
    sOtaLoadNewImagePayload.u16ImageType = htons(u16ImageTp);
    p_image += sizeof(uint16_t);

    uint32_t u32FileVer;
    memcpy(&u32FileVer, p_image, sizeof(uint32_t));
    sOtaLoadNewImagePayload.u32FileVersion = htonl(u32FileVer);
    p_image += sizeof(uint32_t);

    uint16_t u16StackVer;
    memcpy(&u16StackVer, p_image, sizeof(uint16_t));
    sOtaLoadNewImagePayload.u16StackVersion = htons(u16StackVer);
    p_image += sizeof(uint16_t);
    
    memcpy(&sOtaLoadNewImagePayload.auOtaHeaderString, p_image, sizeof(uint8_t) * 32);
    p_image += 32;

    uint32_t u32ImageSz;
    memcpy(&u32ImageSz, p_image, sizeof(uint32_t));
    sOtaLoadNewImagePayload.u32ImageSize = htonl(u32ImageSz);
    p_image += sizeof(uint32_t);
    
    memcpy(&sOtaLoadNewImagePayload.u8Security, p_image, sizeof(uint8_t));
    p_image += sizeof(uint8_t);

    uint64_t u64UpgradeFD;
    memcpy(&u64UpgradeFD, p_image, sizeof(uint64_t));
    sOtaLoadNewImagePayload.u64UpgradeFileDest = htond(u64UpgradeFD);
    p_image += sizeof(uint64_t);

    uint16_t u16MinHWVer;
    memcpy(&u16MinHWVer, p_image, sizeof(uint16_t));
    sOtaLoadNewImagePayload.u16MinHWVersion = htons(u16MinHWVer);
    p_image += sizeof(uint16_t);

    uint16_t u16MaxHWVer;
    memcpy(&u16MaxHWVer, p_image, sizeof(uint16_t));
    sOtaLoadNewImagePayload.u16MaxHWVersion = htons(u16MaxHWVer);
    p_image += sizeof(uint16_t);

    if (eSL_SendMessage(E_SL_MSG_LOAD_NEW_IMAGE,
                        u16Length, 
                        &sOtaLoadNewImagePayload, 
                        &u8SequenceNo) != E_SL_OK)
    {
        HAL_Printf("Sending load new image fail\r\n");
        return E_ZCB_COMMS_FAILED;    
    }
    return E_ZCB_OK;     
}



teZcbStatus eOtaImageBlockSend(uint8_t u8AddrMode, 
                               uint16_t u16Addr, 
                               uint8_t u8SrcEp, 
                               uint8_t u8DstEp,
                               uint8_t u8SequenceNumber,
                               uint8_t u8Status,
                               uint32_t u32FileOffset,
                               uint32_t u32FileVersion,
                               uint16_t u16ImageType,
                               uint16_t u16ManuCode,
                               uint8_t u8DataSize,
                               uint8_t *au8Data)
{
    if (u8DataSize > 64) {
        HAL_Printf("Ota block data size exceeds capcity\r\n");
        return E_ZCB_REQUEST_NOT_ACTIONED;
    }
    
    struct _OtaImageBlockSendPayload
    {
        uint8_t     u8TargetAddrMode;
        uint16_t    u16TargetAddress;
        uint8_t     u8SrcEndpoint;
        uint8_t     u8DstEndpoint;
        uint8_t     u8SequenceNumber;
        uint8_t     u8Status;
        uint32_t    u32FileOffset;
        uint32_t    u32FileVersion;
        uint16_t    u16ImageType;
        uint16_t    u16ManufacturerCode;
        uint8_t     u8DataSize;
        uint8_t     au8Payload[64];
    } PACKED sOtaImageBlockSendPayload;
    
    uint16_t u16Length = sizeof(struct _OtaImageBlockSendPayload) + sizeof(uint8_t) * (u8DataSize - 64);
    uint8_t u8SequenceNo;

    sOtaImageBlockSendPayload.u8TargetAddrMode    = u8AddrMode;
    sOtaImageBlockSendPayload.u16TargetAddress    = htons(u16Addr);
    sOtaImageBlockSendPayload.u8SrcEndpoint       = u8SrcEp;
    sOtaImageBlockSendPayload.u8DstEndpoint       = u8DstEp;
    sOtaImageBlockSendPayload.u8SequenceNumber    = u8SequenceNumber;
    sOtaImageBlockSendPayload.u8Status            = u8Status;
    sOtaImageBlockSendPayload.u32FileOffset       = htonl(u32FileOffset);
    sOtaImageBlockSendPayload.u32FileVersion      = htonl(u32FileVersion);
    sOtaImageBlockSendPayload.u16ImageType        = htons(u16ImageType);
    sOtaImageBlockSendPayload.u16ManufacturerCode = htons(u16ManuCode);
    sOtaImageBlockSendPayload.u8DataSize          = u8DataSize;
    memcpy(sOtaImageBlockSendPayload.au8Payload, au8Data, sizeof(uint8_t) * u8DataSize);
    
    //if (eSL_SendMessage(E_SL_MSG_BLOCK_SEND,
    if (eSL_SendMessageNoWait(E_SL_MSG_BLOCK_SEND,
                        u16Length, 
                        &sOtaImageBlockSendPayload, 
                        &u8SequenceNo) != E_SL_OK)
    {
        HAL_Printf("Sending image block fail\r\n");
        return E_ZCB_COMMS_FAILED;    
    }
    return E_ZCB_OK;   
}



teZcbStatus eOtaUpgradeEndResponse(uint8_t u8AddrMode, 
                                   uint16_t u16Addr, 
                                   uint8_t u8SrcEp, 
                                   uint8_t u8DstEp,
                                   uint8_t u8SequenceNumber,
                                   uint32_t u32UpgradeTime,
                                   uint32_t u32CurrentTime,
                                   uint32_t u32FileVersion,
                                   uint16_t u16ImageType,
                                   uint16_t u16ManuCode)
{
    struct _OtaUpgradeEndResponse
    {
        uint8_t     u8TargetAddrMode;
        uint16_t    u16TargetAddress;
        uint8_t     u8SrcEndpoint;
        uint8_t     u8DstEndpoint;
        uint8_t     u8SequenceNumber;
        uint32_t    u32UpgradeTime;
        uint32_t    u32CurrentTime;
        uint32_t    u32FileVersion;
        uint16_t    u16ImageType;
        uint16_t    u16ManufacturerCode;
    } PACKED sOtaUpgradeEndResponse;
    
    uint16_t u16Length = sizeof(struct _OtaUpgradeEndResponse);
    uint8_t u8SequenceNo;
    
    sOtaUpgradeEndResponse.u8TargetAddrMode    = u8AddrMode;
    sOtaUpgradeEndResponse.u16TargetAddress    = htons(u16Addr);
    sOtaUpgradeEndResponse.u8SrcEndpoint       = u8SrcEp;
    sOtaUpgradeEndResponse.u8DstEndpoint       = u8DstEp;
    sOtaUpgradeEndResponse.u8SequenceNumber    = u8SequenceNumber;
    sOtaUpgradeEndResponse.u32UpgradeTime      = htonl(u32UpgradeTime);
    sOtaUpgradeEndResponse.u32CurrentTime      = htonl(u32CurrentTime);
    sOtaUpgradeEndResponse.u32FileVersion      = htonl(u32FileVersion);
    sOtaUpgradeEndResponse.u16ImageType        = htons(u16ImageType);
    sOtaUpgradeEndResponse.u16ManufacturerCode = htons(u16ManuCode);

    if (eSL_SendMessage(E_SL_MSG_UPGRADE_END_RESPONSE,
                        u16Length, 
                        &sOtaUpgradeEndResponse, 
                        &u8SequenceNo) != E_SL_OK)
    {
        HAL_Printf("Sending ota upgrade end response fail\r\n");
        return E_ZCB_COMMS_FAILED;    
    }
    return E_ZCB_OK;   
}



teZcbStatus eOtaImageNotify(uint8_t u8AddrMode, 
                            uint16_t u16Addr, 
                            uint8_t u8SrcEp, 
                            uint8_t u8DstEp,
                            teOTA_ImageNotifyPayloadType ePayloadType,                                       
                            uint32_t u32FileVersion, 
                            uint16_t u16ImageType,
                            uint16_t u16ManuCode,
                            uint8_t u8QueryJitter)
{
    struct _OtaImageNotifyCommand
    {
        uint8_t     u8TargetAddrMode;
        uint16_t    u16TargetAddress;
        uint8_t     u8SrcEndpoint;
        uint8_t     u8DstEndpoint;        
        teOTA_ImageNotifyPayloadType ePayloadType;
        uint32_t    u32NewFileVersion;
        uint16_t    u16ImageType;
        uint16_t    u16ManufacturerCode;
        uint8_t     u8QueryJitter;
    } PACKED sOtaImageNotifyCommand;
    
    uint16_t u16Length = sizeof(struct _OtaImageNotifyCommand);
    uint8_t u8SequenceNo;

    sOtaImageNotifyCommand.u8TargetAddrMode    = u8AddrMode;
    sOtaImageNotifyCommand.u16TargetAddress    = htons(u16Addr);
    sOtaImageNotifyCommand.u8SrcEndpoint       = u8SrcEp;
    sOtaImageNotifyCommand.u8DstEndpoint       = u8DstEp;
    sOtaImageNotifyCommand.ePayloadType        = ePayloadType;
    sOtaImageNotifyCommand.u32NewFileVersion   = htonl(u32FileVersion);
    sOtaImageNotifyCommand.u16ImageType        = htons(u16ImageType);
    sOtaImageNotifyCommand.u16ManufacturerCode = htons(u16ManuCode);
    sOtaImageNotifyCommand.u8QueryJitter       = u8QueryJitter;

    if (eSL_SendMessage(E_SL_MSG_IMAGE_NOTIFY,
                        u16Length, 
                        &sOtaImageNotifyCommand, 
                        &u8SequenceNo) != E_SL_OK)
    {
       HAL_Printf("Sending image notify command fail\r\n");
        return E_ZCB_COMMS_FAILED;    
    }
    return E_ZCB_OK;
}



teZcbStatus eOtaSendWaitForDataParams(uint8_t u8AddrMode, 
                                      uint16_t u16Addr, 
                                      uint8_t u8SrcEp, 
                                      uint8_t u8DstEp,
                                      uint8_t u8SequenceNumber,
                                      uint8_t u8Status,
                                      uint32_t u32UpgradeTime,
                                      uint32_t u32CurrentTime,
                                      uint16_t u16DelayMs)
{
    struct _OtaSendWaitForDataParams
    {
        uint8_t     u8TargetAddrMode;
        uint16_t    u16TargetAddress;
        uint8_t     u8SrcEndpoint;
        uint8_t     u8DstEndpoint;
        uint8_t     u8SequenceNumber;
        uint8_t     u8Status;
        uint32_t    u32UpgradeTime;
        uint32_t    u32CurrentTime;
        uint16_t    u16BlockRequestDelayMs;
    } PACKED sOtaSendWaitForDataParams;
    
    uint16_t u16Length = sizeof(struct _OtaSendWaitForDataParams);
    uint8_t u8SequenceNo;
    
    sOtaSendWaitForDataParams.u8TargetAddrMode       = u8AddrMode;
    sOtaSendWaitForDataParams.u16TargetAddress       = htons(u16Addr);
    sOtaSendWaitForDataParams.u8SrcEndpoint          = u8SrcEp;
    sOtaSendWaitForDataParams.u8DstEndpoint          = u8DstEp;
    sOtaSendWaitForDataParams.u8SequenceNumber       = u8SequenceNumber;
    sOtaSendWaitForDataParams.u8Status               = u8Status;
    sOtaSendWaitForDataParams.u32UpgradeTime         = htonl(u32UpgradeTime);
    sOtaSendWaitForDataParams.u32CurrentTime         = htonl(u32CurrentTime);
    sOtaSendWaitForDataParams.u16BlockRequestDelayMs = htons(u16DelayMs);

    if (eSL_SendMessage(E_SL_MSG_SEND_WAIT_FOR_DATA_PARAMS,
                        u16Length, 
                        &sOtaSendWaitForDataParams, 
                        &u8SequenceNo) != E_SL_OK)
    {
        HAL_Printf( "Sending wait for data params fail\r\n");
        return E_ZCB_COMMS_FAILED;    
    }
    return E_ZCB_OK;    
}

#endif
// ------------------------------------------------------------------
// End of file
// ------------------------------------------------------------------

