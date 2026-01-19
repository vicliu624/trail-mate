/**
 * @file      nfc_include.h
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2024  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2024-12-04
 *
 */

#pragma once

#ifdef USING_ST25R3916
#include <ndef_buffer.h>
#include <ndef_class.h>
#include <ndef_message.h>
#include <ndef_poller.h>
#include <ndef_record.h>
#include <ndef_type_wifi.h>
#include <ndef_types.h>
#include <ndef_types_mime.h>
#include <ndef_types_rtd.h>
#include <nfc_utils.h>
#include <rfal_isoDep.h>
#include <rfal_nfc.h>
#include <rfal_nfcDep.h>
#include <rfal_nfca.h>
#include <rfal_nfcb.h>
#include <rfal_nfcf.h>
#include <rfal_nfcv.h>
#include <rfal_rf.h>
#include <rfal_rfst25r3916.h>
#include <rfal_st25tb.h>
#include <rfal_st25xv.h>
#include <rfal_t1t.h>
#include <rfal_t2t.h>
#include <rfal_t4t.h>
#include <st_errno.h>

#endif
