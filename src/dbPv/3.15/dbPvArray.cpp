/**
 * Copyright information and license terms for this software can be
 * found in the file LICENSE that is included with the distribution.
 */
/**
 * @author mrk
 */
/* Marty Kraimer 2011.03 */
/* This connects to a DB record and presents the data as a PVStructure
 * It provides access to  value, alarm, display, and control.
 */

#include <cstddef>
#include <cstdlib>
#include <cstddef>
#include <string>
#include <cstdio>
#include <stdexcept>
#include <memory>

#include <cantProceed.h>
#include <epicsStdio.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsThread.h>
#include <epicsExit.h>
#include <dbAccess.h>
#include <dbCommon.h>
#include <recSup.h>
#include <dbBase.h>
#include <dbEvent.h>

#include <pv/noDefaultMethods.h>
#include <pv/pvIntrospect.h>
#include <pv/pvData.h>
#include <pv/pvAccess.h>
#include <pv/standardField.h>
#include <pv/standardPVField.h>
#include <pv/alarm.h>
#include <pv/control.h>
#include <pv/display.h>
#include <pv/timeStamp.h>
#include <pv/pvAlarm.h>
#include <pv/pvControl.h>
#include <pv/pvDisplay.h>
#include <pv/pvEnumerated.h>
#include <pv/pvTimeStamp.h>

#define epicsExportSharedSymbols
#include <epicsExport.h>
#include "dbPv.h"

using namespace epics::pvData;
using namespace epics::pvAccess;
using std::tr1::static_pointer_cast;
using std::cout;
using std::endl;
using std::string;

namespace epics { namespace pvaSrv { 

static PVArray::shared_pointer nullPVArray;
static Status illegalOffsetStatus(
        Status::STATUSTYPE_ERROR,
        "count must be >0"
        );
static Status illegalCountStatus(
        Status::STATUSTYPE_ERROR,
        "count must be >0"
        );
static Status illegalStrideStatus(
        Status::STATUSTYPE_ERROR,
        "stride must be >0"
        );

extern "C" {
typedef long (*get_array_info) (DBADDR *,long *,long *);
typedef long (*put_array_info) (DBADDR *,long );
}

DbPvArray::DbPvArray(
        DbPvPtr const &dbPv,
        ChannelArrayRequester::shared_pointer const &channelArrayRequester)
    : dbPv(dbPv),
      channelArrayRequester(channelArrayRequester),
      beingDestroyed(false)
{
    if (DbPvDebug::getLevel() > 0) printf("dbPvArray::dbPvArray\n");
}

DbPvArray::~DbPvArray()
{
    if (DbPvDebug::getLevel() > 0) printf("dbPvArray::~dbPvArray\n");
}

bool DbPvArray::init(PVStructure::shared_pointer const &pvRequest)
{
    requester_type::shared_pointer req(channelArrayRequester.lock());
    if(!(dbChannelFinalElements(dbPv->getDbChannel())>1)) {
        if(req) req->message("field in db record is not an array",
                                             errorMessage);
        return false;
    }
    ScalarType scalarType(pvBoolean);
    // Note that pvBoolean is not a supported type
    switch (dbChannelFinalFieldType(dbPv->getDbChannel())) {
    case DBF_CHAR:
        scalarType = pvByte; break;
    case DBF_UCHAR:
        scalarType = pvUByte; break;
    case DBF_SHORT:
        scalarType = pvShort; break;
    case DBF_USHORT:
        scalarType = pvUShort; break;
    case DBF_LONG:
        scalarType = pvInt; break;
    case DBF_ULONG:
        scalarType = pvUInt; break;
    case DBF_FLOAT:
        scalarType = pvFloat; break;
    case DBF_DOUBLE:
        scalarType = pvDouble; break;
    case DBF_STRING:
        scalarType = pvString; break;
    default:
        break;
    }
    if (scalarType == pvBoolean) {
        if(req) req->message("unsupported field in DB record", errorMessage);
        return false;
    }
    pvScalarArray = getPVDataCreate()->createPVScalarArray(scalarType);
    pvScalarArray->setCapacity(dbChannelFinalElements(dbPv->getDbChannel()));
    if(req) req->channelArrayConnect(
                Status::Ok,
                getPtrSelf(),
                pvScalarArray->getArray());
    return true;
}

void DbPvArray::destroy() {
    if(DbPvDebug::getLevel()>0) printf("dbPvArray::destroy beingDestroyed %s\n",
                                       (beingDestroyed ? "true" : "false"));
    {
        Lock xx(mutex);
        if(beingDestroyed) return;
        beingDestroyed = true;
    }
}

void DbPvArray::getArray(size_t offset,
                         size_t count,
                         size_t stride)
{
    requester_type::shared_pointer req(channelArrayRequester.lock());
    dbScanLock(dbChannelRecord(dbPv->getDbChannel()));
    long alength = 0;
    long aoffset = 0;
    rset *prset = dbGetRset(&dbPv->getDbChannel()->addr);
    if (prset && prset->get_array_info) {
        get_array_info get_info;
        get_info = (get_array_info)(prset->get_array_info);
        get_info(&dbPv->getDbChannel()->addr, &alength, &aoffset);
        if (aoffset != 0) {
            dbScanUnlock(dbChannelRecord(dbPv->getDbChannel()));
            Status status(Status::STATUSTYPE_ERROR,
                          "array offset not supported");
            if(req) req->getArrayDone(
                        status,
                        getPtrSelf(),
                        nullPVArray);
            return;
        }
    }
    bool ok = false;
    while (true) {
        size_t length = alength;
        // TODO: This looks fishy. length and count are unsigned!
        if (length <= 0) break;
        if (count <= 0) {
            count = -offset + length/stride;
            if (count > 0) ok = true;
            break;
        }
        size_t maxcount = -offset + length/stride;
        if (count > maxcount) count = maxcount;
        ok = true;
        break;
    }
    if (!ok) {
        dbScanUnlock(dbChannelRecord(dbPv->getDbChannel()));
        pvScalarArray->setLength(0);
        if(req) req->getArrayDone(
                    Status::Ok,
                    getPtrSelf(),
                    pvScalarArray);
        return;
    }
    size_t capacity = pvScalarArray->getCapacity();
    {
        Lock lock(dataMutex);
        switch (dbChannelFinalFieldType(dbPv->getDbChannel())) {
        case DBF_CHAR: {
            shared_vector<int8> xxx(capacity);
            int8 *from = static_cast<int8 *>(dbChannelField(dbPv->getDbChannel()));
            for(size_t i= 0;i< count; ++i) xxx[i] = from[i*stride+offset];
            shared_vector<const int8> data(freeze(xxx));
            PVByteArrayPtr pva = static_pointer_cast<PVByteArray>(pvScalarArray);
            pva->replace(data);
            break;
        }
        case DBF_UCHAR: {
            shared_vector<uint8> xxx(count);
            uint8 *from = static_cast<uint8 *>(dbChannelField(dbPv->getDbChannel()));
            for(size_t i= 0;i< count; ++i) xxx[i] = from[i*stride+offset];
            shared_vector<const uint8> data(freeze(xxx));
            PVUByteArrayPtr pva = static_pointer_cast<PVUByteArray>(pvScalarArray);
            pva->replace(data);
            break;
        }
        case DBF_SHORT: {
            shared_vector<int16> xxx(count);
            int16 *from = static_cast<int16 *>(dbChannelField(dbPv->getDbChannel()));
            for(size_t i= 0;i< count; ++i) xxx[i] = from[i*stride+offset];
            shared_vector<const int16> data(freeze(xxx));
            PVShortArrayPtr pva = static_pointer_cast<PVShortArray>(pvScalarArray);
            pva->replace(data);
            break;
        }
        case DBF_USHORT: {
            shared_vector<uint16> xxx(count);
            uint16 *from = static_cast<uint16 *>(dbChannelField(dbPv->getDbChannel()));
            for(size_t i= 0;i< count; ++i) xxx[i] = from[i*stride+offset];
            shared_vector<const uint16> data(freeze(xxx));
            PVUShortArrayPtr pva = static_pointer_cast<PVUShortArray>(pvScalarArray);
            pva->replace(data);
            break;
        }
        case DBF_LONG: {
            shared_vector<int32> xxx(count);
            int32 *from = static_cast<int32 *>(dbChannelField(dbPv->getDbChannel()));
            for(size_t i= 0;i< count; ++i) xxx[i] = from[i*stride+offset];
            shared_vector<const int32> data(freeze(xxx));
            PVIntArrayPtr pva = static_pointer_cast<PVIntArray>(pvScalarArray);
            pva->replace(data);
            break;
        }
        case DBF_ULONG: {
            shared_vector<uint32> xxx(count);
            uint32 *from = static_cast<uint32 *>(dbChannelField(dbPv->getDbChannel()));
            for(size_t i= 0;i< count; ++i) xxx[i] = from[i*stride+offset];
            shared_vector<const uint32> data(freeze(xxx));
            PVUIntArrayPtr pva = static_pointer_cast<PVUIntArray>(pvScalarArray);
            pva->replace(data);
            break;
        }
        case DBF_FLOAT: {
            shared_vector<float> xxx(count);
            float *from = static_cast<float *>(dbChannelField(dbPv->getDbChannel()));
            for(size_t i= 0;i< count; ++i) xxx[i] = from[i*stride+offset];
            shared_vector<const float> data(freeze(xxx));
            PVFloatArrayPtr pva = static_pointer_cast<PVFloatArray>(pvScalarArray);
            pva->replace(data);
            break;
        }
        case DBF_DOUBLE: {
            shared_vector<double> xxx(count);
            double *from = static_cast<double *>(dbChannelField(dbPv->getDbChannel()));
            for(size_t i= 0;i< count; ++i) xxx[i] = from[i*stride+offset];
            shared_vector<const double> data(freeze(xxx));
            PVDoubleArrayPtr pva = static_pointer_cast<PVDoubleArray>(pvScalarArray);
            pva->replace(data);
            break;
        }
        case DBF_STRING: {
            shared_vector<string> xxx(count);
            char *from = static_cast<char *>(dbChannelField(dbPv->getDbChannel()));
            from += offset * dbChannelFinalFieldSize(dbPv->getDbChannel());
            for(size_t i= 0;i< count; ++i) {
                xxx[i] = from;
                from += stride * dbChannelFinalFieldSize(dbPv->getDbChannel());
            }
            shared_vector<const string> data(freeze(xxx));
            PVStringArrayPtr pva = static_pointer_cast<PVStringArray>(pvScalarArray);
            pva->replace(data);
            break;
        }
        }
        pvScalarArray->setLength(offset + count * stride);
    }
    dbScanUnlock(dbChannelRecord(dbPv->getDbChannel()));
    if(req) req->getArrayDone(
                Status::Ok,
                getPtrSelf(),
                pvScalarArray);
}

void DbPvArray::putArray(
        PVArray::shared_pointer const &pvArray,
        size_t offset, size_t count, size_t stride)
{
    requester_type::shared_pointer req(channelArrayRequester.lock());
    dbScanLock(dbChannelRecord(dbPv->getDbChannel()));
    long no_elements = dbChannelFinalElements(dbPv->getDbChannel());
    size_t length = no_elements;
    // TODO: This looks fishy. offset and count are unsigned!
    if((offset + count * stride) > length) {
        count = - offset + (length + stride - 1)/stride;
    }
    if (count <= 0) {
        dbScanUnlock(dbChannelRecord(dbPv->getDbChannel()));
        if(req) req->putArrayDone(
                    Status::Ok,
                    getPtrSelf());
        return;
    }
    long newLength = offset + count*stride;
    rset *prset = dbGetRset(&dbPv->getDbChannel()->addr);
    if(prset && prset->get_array_info) {
        long oldLength = 0;
        long aoffset = 0;
        get_array_info get_info;
        get_info = (get_array_info)(prset->get_array_info);
        get_info(&dbPv->getDbChannel()->addr, &oldLength, &aoffset);
        if (newLength > oldLength) {
            if(prset && prset->put_array_info) {
                put_array_info put_info;
                put_info = (put_array_info)(prset->put_array_info);
                put_info(&dbPv->getDbChannel()->addr, newLength);
            }
        }
    }
    {
        Lock lock(dataMutex);
        switch (dbChannelFinalFieldType(dbPv->getDbChannel())) {
        case DBF_CHAR: {
            PVByteArrayPtr pva = static_pointer_cast<PVByteArray>(pvArray);
            shared_vector<const int8> xxx(pva->view());
            int8 *to = static_cast<int8 *>(dbChannelField(dbPv->getDbChannel()));
            for (size_t i = 0; i < count; i++) to[i*stride + offset] = xxx[i];
            break;
        }
        case DBF_UCHAR: {
            PVUByteArrayPtr pva = static_pointer_cast<PVUByteArray>(pvArray);
            shared_vector<const uint8> xxx(pva->view());
            uint8 *to = static_cast<uint8 *>(dbChannelField(dbPv->getDbChannel()));
            for (size_t i = 0; i < count; i++) to[i*stride + offset] = xxx[i];
            break;
        }
        case DBF_SHORT: {
            PVShortArrayPtr pva = static_pointer_cast<PVShortArray>(pvArray);
            shared_vector<const int16> xxx(pva->view());
            int16 *to = static_cast<int16 *>(dbChannelField(dbPv->getDbChannel()));
            for (size_t i = 0; i < count; i++) to[i*stride + offset] = xxx[i];
            break;
        }
        case DBF_USHORT: {
            PVUShortArrayPtr pva = static_pointer_cast<PVUShortArray>(pvArray);
            shared_vector<const uint16> xxx(pva->view());
            uint16 *to = static_cast<uint16 *>(dbChannelField(dbPv->getDbChannel()));
            for (size_t i = 0; i < count; i++) to[i*stride + offset] = xxx[i];
            break;
        }
        case DBF_LONG: {
            PVIntArrayPtr pva = static_pointer_cast<PVIntArray>(pvArray);
            shared_vector<const int32> xxx(pva->view());
            int32 *to = static_cast<int32 *>(dbChannelField(dbPv->getDbChannel()));
            for (size_t i = 0; i < count; i++) to[i*stride + offset] = xxx[i];
            break;
        }
        case DBF_ULONG: {
            PVUIntArrayPtr pva = static_pointer_cast<PVUIntArray>(pvArray);
            shared_vector<const uint32> xxx(pva->view());
            uint32 *to = static_cast<uint32 *>(dbChannelField(dbPv->getDbChannel()));
            for (size_t i = 0; i < count; i++) to[i*stride + offset] = xxx[i];
            break;
        }
        case DBF_FLOAT: {
            PVFloatArrayPtr pva = static_pointer_cast<PVFloatArray>(pvArray);
            shared_vector<const float> xxx(pva->view());
            float *to = static_cast<float *>(dbChannelField(dbPv->getDbChannel()));
            for (size_t i = 0; i < count; i++) to[i*stride + offset] = xxx[i];
            break;
        }
        case DBF_DOUBLE: {
            PVDoubleArrayPtr pva = static_pointer_cast<PVDoubleArray>(pvArray);
            shared_vector<const double> xxx(pva->view());
            double *to = static_cast<double *>(dbChannelField(dbPv->getDbChannel()));
            for (size_t i = 0; i < count; i++) {
                to[i*stride + offset] = xxx[i];
            }
            break;
        }
        case DBF_STRING: {
            PVStringArrayPtr pva = static_pointer_cast<PVStringArray>(pvArray);
            shared_vector<const string> xxx(pva->view());
            char *to = static_cast<char *>(dbChannelField(dbPv->getDbChannel()));
            to += offset * dbChannelFinalFieldSize(dbPv->getDbChannel());
            for (size_t i = 0; i < count; i += stride) {
                const char *from = xxx[i].c_str();
                long nchar = xxx[i].size();
                if (nchar > dbChannelFinalFieldSize(dbPv->getDbChannel()))
                    nchar = dbChannelFinalFieldSize(dbPv->getDbChannel());
                for (long j = 0; j < nchar; j++) to[j] = from[j];
                to[nchar] = 0;
                to += stride * dbChannelFinalFieldSize(dbPv->getDbChannel());
            }
            break;
        }
        }
    }
    db_post_events(dbChannelRecord(dbPv->getDbChannel()),
                   dbChannelField(dbPv->getDbChannel()),
                   DBE_VALUE | DBE_LOG);
    dbScanUnlock(dbChannelRecord(dbPv->getDbChannel()));
    if(req) req->putArrayDone(Status::Ok,getPtrSelf());
}

void DbPvArray::getLength()
{
    long aoffset = 0;
    long alength = 0;
    dbScanLock(dbChannelRecord(dbPv->getDbChannel()));
    rset *prset = dbGetRset(&dbPv->getDbChannel()->addr);
    if(prset && prset->get_array_info) {
        get_array_info get_info;
        get_info = (get_array_info)(prset->get_array_info);
        get_info(&dbPv->getDbChannel()->addr, &alength, &aoffset);
    }
    dbScanUnlock(dbChannelRecord(dbPv->getDbChannel()));
    requester_type::shared_pointer req(channelArrayRequester.lock());
    if(req) req->getLengthDone(
                Status::Ok,getPtrSelf(),alength);
}

void DbPvArray::setLength(size_t length)
{
    dbScanLock(dbChannelRecord(dbPv->getDbChannel()));
    long no_elements = dbChannelFinalElements(dbPv->getDbChannel());
    if (length > static_cast<size_t>(no_elements)) length = no_elements;
    long alength = length;
    rset *prset = dbGetRset(&dbPv->getDbChannel()->addr);
    if(prset && prset->put_array_info) {
        put_array_info put_info;
        put_info = (put_array_info)(prset->put_array_info);
        put_info(&dbPv->getDbChannel()->addr, alength);
    }
    dbScanUnlock(dbChannelRecord(dbPv->getDbChannel()));
    requester_type::shared_pointer req(channelArrayRequester.lock());
    if(req) req->setLengthDone(Status::Ok,getPtrSelf());
}

void DbPvArray::lock()
{
    dataMutex.lock();
}

void DbPvArray::unlock()
{
    dataMutex.unlock();
}

}}
