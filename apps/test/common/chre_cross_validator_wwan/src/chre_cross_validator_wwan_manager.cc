/* * Copyright (C) 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "chre_cross_validator_wwan_manager.h"

#include <stdio.h>
#include <algorithm>
#include <cinttypes>
#include <cstring>

#include "chre/util/nanoapp/assert.h"
#include "chre/util/nanoapp/callbacks.h"
#include "chre/util/nanoapp/log.h"
#include "chre_api/chre.h"
#include "chre_api/chre/wwan.h"
#include "chre_cross_validation_wwan.nanopb.h"
#include "chre_test_common.nanopb.h"
#include "send_message.h"

namespace chre {

namespace cross_validator_wwan {

void Manager::handleEvent(uint32_t senderInstanceId, uint16_t eventType,
                          const void *eventData) {
  switch (eventType) {
    case CHRE_EVENT_MESSAGE_FROM_HOST:
      handleMessageFromHost(
          senderInstanceId,
          static_cast<const chreMessageFromHostData *>(eventData));
      break;
    case CHRE_EVENT_WWAN_CELL_INFO_RESULT:
      handleWwanCellInfoResult(
          static_cast<const chreWwanCellInfoResult *>(eventData));
      break;
    default:
      LOGE("Unknown message type %" PRIu16 "received when handling event",
           eventType);
  }
}

void Manager::handleMessageFromHost(uint32_t senderInstanceId,
                                    const chreMessageFromHostData *hostData) {
  LOGI("Received message from host");
  if (senderInstanceId != CHRE_INSTANCE_ID) {
    LOGE("Incorrect sender instance id: %" PRIu32, senderInstanceId);
    return;
  }

  switch (hostData->messageType) {
    case chre_cross_validation_wwan_MessageType_WWAN_CAPABILITIES_REQUEST:
      mHostEndpoint = hostData->hostEndpoint;
      sendCapabilitiesToHost();
      break;
    case chre_cross_validation_wwan_MessageType_WWAN_CELL_INFO_REQUEST:
      LOGI("Received WWAN_CELL_INFO_REQUEST, calling chreWwanGetCellInfoAsync");
      if (!chreWwanGetCellInfoAsync(/*cookie=*/nullptr)) {
        LOGE("chreWwanGetCellInfoAsync() failed");
        test_shared::sendTestResultWithMsgToHost(
            mHostEndpoint,
            chre_cross_validation_wwan_MessageType_WWAN_NANOAPP_ERROR,
            /*success=*/false,
            /*errMessage=*/"chreWwanGetCellInfoAsync failed",
            /*abortOnFailure=*/false);
      }
      break;
    default:
      LOGE("Unknown message type %" PRIu32 " for host message",
           hostData->messageType);
      break;
  }
}

void Manager::sendCapabilitiesToHost() {
  LOGI("Sending capabilites to host");
  chre_cross_validation_wwan_WwanCapabilities wwanCapabilities =
      makeWwanCapabilitiesMessage(chreWwanGetCapabilities());
  test_shared::sendMessageToHost(
      mHostEndpoint, &wwanCapabilities,
      chre_cross_validation_wwan_WwanCapabilities_fields,
      chre_cross_validation_wwan_MessageType_WWAN_CAPABILITIES);
}

void Manager::handleWwanCellInfoResult(const chreWwanCellInfoResult *event) {
  if (event->errorCode != CHRE_ERROR_NONE) {
    LOGE("chreWwanCellInfoResult received with errorCode: 0x%" PRIu8,
         event->errorCode);
  }

  LOGI("Sending wwan scan results to host");
  chre_cross_validation_wwan_WwanCellInfoResult result =
      toWwanCellInfoResultProto(*event);

  test_shared::sendMessageToHostWithPermissions(
      mHostEndpoint, &result,
      chre_cross_validation_wwan_WwanCellInfoResult_fields,
      chre_cross_validation_wwan_MessageType_WWAN_CELL_INFO_RESULTS,
      NanoappPermissions::CHRE_PERMS_WWAN);
}

chre_cross_validation_wwan_WwanCapabilities
Manager::makeWwanCapabilitiesMessage(uint32_t capabilitiesFromChre) {
  chre_cross_validation_wwan_WwanCapabilities capabilities;
  capabilities.has_wwanCapabilities = true;
  capabilities.wwanCapabilities = capabilitiesFromChre;
  return capabilities;
}

// Converts chreWwanCellTimestampType to the corresponding
// chre_cross_validation_wwan_CellTimestampType.
chre_cross_validation_wwan_CellTimestampType ToCellTimestampType(
    uint8_t cell_timestamp_type) {
  switch (cell_timestamp_type) {
    case CHRE_WWAN_CELL_TIMESTAMP_TYPE_ANTENNA:
      return chre_cross_validation_wwan_CellTimestampType_CELL_TIMESTAMP_TYPE_ANTENNA;
    case CHRE_WWAN_CELL_TIMESTAMP_TYPE_MODEM:
      return chre_cross_validation_wwan_CellTimestampType_CELL_TIMESTAMP_TYPE_MODEM;
    case CHRE_WWAN_CELL_TIMESTAMP_TYPE_OEM_RIL:
      return chre_cross_validation_wwan_CellTimestampType_CELL_TIMESTAMP_TYPE_OEM_RIL;
    case CHRE_WWAN_CELL_TIMESTAMP_TYPE_JAVA_RIL:
      return chre_cross_validation_wwan_CellTimestampType_CELL_TIMESTAMP_TYPE_JAVA_RIL;
    default:
      return chre_cross_validation_wwan_CellTimestampType_CELL_TIMESTAMP_TYPE_UNKNOWN;
  }
}

chre_cross_validation_wwan_WwanCellInfo toWwanCellInfoProto(
    const chreWwanCellInfo &cell_info) {
  chre_cross_validation_wwan_WwanCellInfo cell_info_proto;
  cell_info_proto.has_timestamp_ns = true;
  cell_info_proto.timestamp_ns = cell_info.timeStamp;
  cell_info_proto.has_timestamp_type = true;
  cell_info_proto.timestamp_type = ToCellTimestampType(cell_info.timeStampType);
  cell_info_proto.has_is_registered = true;
  cell_info_proto.is_registered = cell_info.registered;
  cell_info_proto.has_cell_info_type = true;
  cell_info_proto.cell_info_type =
      chre_cross_validation_wwan_WwanCellInfoType_WWAN_CELL_INFO_TYPE_UNKNOWN;
  LOGI("Encoding chreWwanCellInfo to proto");

  switch (cell_info.cellInfoType) {
    case CHRE_WWAN_CELL_INFO_TYPE_GSM:
      LOGI("Encoding GSM cellInfoType to proto");
      cell_info_proto.which_cell_info =
          chre_cross_validation_wwan_WwanCellInfo_gsm_tag;
      cell_info_proto.cell_info_type =
          chre_cross_validation_wwan_WwanCellInfoType_WWAN_CELL_INFO_TYPE_GSM;
      cell_info_proto.cell_info.gsm = {
          .has_cell_identity = true,
          .cell_identity =
              {
                  .has_mcc = true,
                  .mcc = cell_info.CellInfo.gsm.cellIdentityGsm.mcc,
                  .has_mnc = true,
                  .mnc = cell_info.CellInfo.gsm.cellIdentityGsm.mnc,
                  .has_lac = true,
                  .lac = cell_info.CellInfo.gsm.cellIdentityGsm.lac,
                  .has_cid = true,
                  .cid = cell_info.CellInfo.gsm.cellIdentityGsm.cid,
                  .has_arfcn = true,
                  .arfcn = cell_info.CellInfo.gsm.cellIdentityGsm.arfcn,
                  .has_bsic = true,
                  .bsic = cell_info.CellInfo.gsm.cellIdentityGsm.bsic,
              },
          .has_signal_strength = true,
          .signal_strength = {
              .has_signal_strength = true,
              .signal_strength =
                  cell_info.CellInfo.gsm.signalStrengthGsm.signalStrength,
              .has_bit_error_rate = true,
              .bit_error_rate =
                  cell_info.CellInfo.gsm.signalStrengthGsm.bitErrorRate,
              .has_timing_advance = true,
              .timing_advance =
                  cell_info.CellInfo.gsm.signalStrengthGsm.timingAdvance,
          }};
      break;
    case CHRE_WWAN_CELL_INFO_TYPE_WCDMA:
      LOGI("Encoding WCDMA cellInfoType to proto");
      cell_info_proto.which_cell_info =
          chre_cross_validation_wwan_WwanCellInfo_wcdma_tag;
      cell_info_proto.cell_info_type =
          chre_cross_validation_wwan_WwanCellInfoType_WWAN_CELL_INFO_TYPE_WCDMA;
      cell_info_proto.cell_info.wcdma = {
          .has_cell_identity = true,
          .cell_identity =
              {
                  .has_mcc = true,
                  .mcc = cell_info.CellInfo.wcdma.cellIdentityWcdma.mcc,
                  .has_mnc = true,
                  .mnc = cell_info.CellInfo.wcdma.cellIdentityWcdma.mnc,
                  .has_lac = true,
                  .lac = cell_info.CellInfo.wcdma.cellIdentityWcdma.lac,
                  .has_cid = true,
                  .cid = cell_info.CellInfo.wcdma.cellIdentityWcdma.cid,
                  .has_psc = true,
                  .psc = cell_info.CellInfo.wcdma.cellIdentityWcdma.psc,
                  .has_uarfcn = true,
                  .uarfcn = cell_info.CellInfo.wcdma.cellIdentityWcdma.uarfcn,
              },
          .has_signal_strength = true,
          .signal_strength = {
              .has_signal_strength = true,
              .signal_strength =
                  cell_info.CellInfo.wcdma.signalStrengthWcdma.signalStrength,
              .has_bit_error_rate = true,
              .bit_error_rate =
                  cell_info.CellInfo.wcdma.signalStrengthWcdma.bitErrorRate,
          }};
      break;
    case CHRE_WWAN_CELL_INFO_TYPE_CDMA:
      LOGI("Encoding CDMA cellInfoType to proto");
      cell_info_proto.which_cell_info =
          chre_cross_validation_wwan_WwanCellInfo_cdma_tag;
      cell_info_proto.cell_info_type =
          chre_cross_validation_wwan_WwanCellInfoType_WWAN_CELL_INFO_TYPE_CDMA;
      cell_info_proto.cell_info.cdma = {
          .has_cell_identity = true,
          .cell_identity =
              {
                  .has_network_id = true,
                  .network_id =
                      cell_info.CellInfo.cdma.cellIdentityCdma.networkId,
                  .has_system_id = true,
                  .system_id =
                      cell_info.CellInfo.cdma.cellIdentityCdma.systemId,
                  .has_basestation_id = true,
                  .basestation_id =
                      cell_info.CellInfo.cdma.cellIdentityCdma.basestationId,
                  .has_longitude = true,
                  .longitude =
                      cell_info.CellInfo.cdma.cellIdentityCdma.longitude,
                  .has_latitude = true,
                  .latitude = cell_info.CellInfo.cdma.cellIdentityCdma.latitude,
              },
          .has_signal_strength_cdma = true,
          .signal_strength_cdma =
              {
                  .has_dbm = true,
                  .dbm = cell_info.CellInfo.cdma.signalStrengthCdma.dbm,
                  .has_ecio = true,
                  .ecio = cell_info.CellInfo.cdma.signalStrengthCdma.ecio,
              },
          .has_signal_strength_evdo = true,
          .signal_strength_evdo = {
              .has_dbm = true,
              .dbm = cell_info.CellInfo.cdma.signalStrengthEvdo.dbm,
              .has_ecio = true,
              .ecio = cell_info.CellInfo.cdma.signalStrengthEvdo.ecio,
              .has_signal_noise_ratio = true,
              .signal_noise_ratio =
                  cell_info.CellInfo.cdma.signalStrengthEvdo.signalNoiseRatio,
          }};
      break;
    case CHRE_WWAN_CELL_INFO_TYPE_LTE:
      LOGI("Encoding LTE cellInfoType to proto");
      cell_info_proto.which_cell_info =
          chre_cross_validation_wwan_WwanCellInfo_lte_tag;
      cell_info_proto.cell_info_type =
          chre_cross_validation_wwan_WwanCellInfoType_WWAN_CELL_INFO_TYPE_LTE;
      cell_info_proto.cell_info.lte = {
          .has_cell_identity = true,
          .cell_identity =
              {
                  .has_mcc = true,
                  .mcc = cell_info.CellInfo.lte.cellIdentityLte.mcc,
                  .has_mnc = true,
                  .mnc = cell_info.CellInfo.lte.cellIdentityLte.mnc,
                  .has_ci = true,
                  .ci = cell_info.CellInfo.lte.cellIdentityLte.ci,
                  .has_pci = true,
                  .pci = cell_info.CellInfo.lte.cellIdentityLte.pci,
                  .has_tac = true,
                  .tac = cell_info.CellInfo.lte.cellIdentityLte.tac,
                  .has_earfcn = true,
                  .earfcn = cell_info.CellInfo.lte.cellIdentityLte.earfcn,
              },
          .has_signal_strength = true,
          .signal_strength = {
              .has_signal_strength = true,
              .signal_strength =
                  cell_info.CellInfo.lte.signalStrengthLte.signalStrength,
              .has_rsrp = true,
              .rsrp = cell_info.CellInfo.lte.signalStrengthLte.rsrp,
              .has_rsrq = true,
              .rsrq = cell_info.CellInfo.lte.signalStrengthLte.rsrq,
              .has_rssnr = true,
              .rssnr = cell_info.CellInfo.lte.signalStrengthLte.rssnr,
              .has_cqi = true,
              .cqi = cell_info.CellInfo.lte.signalStrengthLte.cqi,
              .has_timing_advance = true,
              .timing_advance =
                  cell_info.CellInfo.lte.signalStrengthLte.timingAdvance,
          }};
      break;
    case CHRE_WWAN_CELL_INFO_TYPE_TD_SCDMA:
      LOGI("Encoding TD_SCDMA cellInfoType to proto");
      cell_info_proto.which_cell_info =
          chre_cross_validation_wwan_WwanCellInfo_tdscdma_tag;
      cell_info_proto.cell_info_type =
          chre_cross_validation_wwan_WwanCellInfoType_WWAN_CELL_INFO_TYPE_TD_SCDMA;
      cell_info_proto.cell_info.tdscdma = {
          .has_cell_identity = true,
          .cell_identity =
              {
                  .has_mcc = true,
                  .mcc = cell_info.CellInfo.tdscdma.cellIdentityTdscdma.mcc,
                  .has_mnc = true,
                  .mnc = cell_info.CellInfo.tdscdma.cellIdentityTdscdma.mnc,
                  .has_lac = true,
                  .lac = cell_info.CellInfo.tdscdma.cellIdentityTdscdma.lac,
                  .has_cid = true,
                  .cid = cell_info.CellInfo.tdscdma.cellIdentityTdscdma.cid,
                  .has_cpid = true,
                  .cpid = cell_info.CellInfo.tdscdma.cellIdentityTdscdma.cpid,
              },
          .has_signal_strength = true,
          .signal_strength = {
              .has_rscp = true,
              .rscp = cell_info.CellInfo.tdscdma.signalStrengthTdscdma.rscp,
          }};
      break;
    case CHRE_WWAN_CELL_INFO_TYPE_NR:
      LOGI("Encoding NR cellInfoType to proto");
      cell_info_proto.which_cell_info =
          chre_cross_validation_wwan_WwanCellInfo_nr_tag;
      cell_info_proto.cell_info_type =
          chre_cross_validation_wwan_WwanCellInfoType_WWAN_CELL_INFO_TYPE_NR;
      cell_info_proto.cell_info.nr = {
          .has_cell_identity = true,
          .cell_identity =
              {
                  .has_mcc = true,
                  .mcc = cell_info.CellInfo.nr.cellIdentityNr.mcc,
                  .has_mnc = true,
                  .mnc = cell_info.CellInfo.nr.cellIdentityNr.mnc,
                  .has_nci = true,
                  .nci = chreWwanUnpackNrNci(
                      &cell_info.CellInfo.nr.cellIdentityNr),
                  .has_pci = true,
                  .pci = cell_info.CellInfo.nr.cellIdentityNr.pci,
                  .has_tac = true,
                  .tac = cell_info.CellInfo.nr.cellIdentityNr.tac,
                  .has_nrarfcn = true,
                  .nrarfcn = cell_info.CellInfo.nr.cellIdentityNr.nrarfcn,
              },
          .has_signal_strength = true,
          .signal_strength = {
              .has_ss_rsrp = true,
              .ss_rsrp = cell_info.CellInfo.nr.signalStrengthNr.ssRsrp,
              .has_ss_rsrq = true,
              .ss_rsrq = cell_info.CellInfo.nr.signalStrengthNr.ssRsrq,
              .has_ss_sinr = true,
              .ss_sinr = cell_info.CellInfo.nr.signalStrengthNr.ssSinr,
              .has_csi_rsrp = true,
              .csi_rsrp = cell_info.CellInfo.nr.signalStrengthNr.csiRsrp,
              .has_csi_rsrq = true,
              .csi_rsrq = cell_info.CellInfo.nr.signalStrengthNr.csiRsrq,
              .has_csi_sinr = true,
              .csi_sinr = cell_info.CellInfo.nr.signalStrengthNr.csiSinr,
          }};
      break;
    default:
      LOGE("Unknown cellInfoType %" PRIu8 " received", cell_info.cellInfoType);
      break;
  }

  return cell_info_proto;
}

// A nanopb callback for encoding the cell infos in a chreWwanCellInfoResult to
// repeated chre_cross_validation_wwan_WwanCellInfo.
bool EncodeWwanCellInfos(pb_ostream_t *stream, const pb_field_t *field,
                         void *const *arg) {
  const chreWwanCellInfoResult *cell_scan =
      static_cast<const chreWwanCellInfoResult *>(*arg);

  LOGI("Encoding %d cell infos to proto", cell_scan->cellInfoCount);
  for (int i = 0; i < cell_scan->cellInfoCount; ++i) {
    if (!pb_encode_tag_for_field(stream, field)) {
      LOGE("Failed to encode Cell Info tag.");
      return false;
    }

    chre_cross_validation_wwan_WwanCellInfo cell_info_proto =
        toWwanCellInfoProto(cell_scan->cells[i]);
    if (!pb_encode_submessage(stream,
                              chre_cross_validation_wwan_WwanCellInfo_fields,
                              &cell_info_proto)) {
      LOGE("Failed to encode Cell Info.");
      return false;
    }
  }

  return true;
}

chre_cross_validation_wwan_WwanCellInfoResult
Manager::toWwanCellInfoResultProto(
    const chreWwanCellInfoResult &cell_info_result) {
  chre_cross_validation_wwan_WwanCellInfoResult cell_info_result_proto;

  // fill in header
  cell_info_result_proto.version = cell_info_result.version;
  cell_info_result_proto.errorCode = cell_info_result.errorCode;

  cell_info_result_proto.cell_info = {
      .funcs = {.encode = EncodeWwanCellInfos},
      .arg = const_cast<chreWwanCellInfoResult *>(&cell_info_result),
  };

  return cell_info_result_proto;
}

}  // namespace cross_validator_wwan

}  // namespace chre
