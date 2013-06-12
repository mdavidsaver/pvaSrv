/**
 * Copyright - See the COPYRIGHT that is included with this distribution.
 * EPICS pvData is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 */
/**
 * @author mrk
 */
/* Marty Kraimer 2011.03 */
/* This connects to a V3 record and presents the data as a PVStructure
 * It provides access to  value, alarm, display, and control.
 */

#include <cstddef>
#include <cstdlib>
#include <cstddef>
#include <string>
#include <cstdio>
#include <stdexcept>
#include <memory>

#include <dbAccess.h>
#include <dbEvent.h>
#include <dbNotify.h>
#include <dbCommon.h>

#include <pv/pvIntrospect.h>
#include <pv/pvData.h>
#include <pv/pvAccess.h>

#include "dbPv.h"
#include "dbUtil.h"

namespace epics { namespace pvaSrv { 

using namespace epics::pvData;
using namespace epics::pvAccess;

V3ChannelPut::V3ChannelPut(
    ChannelBase::shared_pointer const &v3Channel,
    ChannelPutRequester::shared_pointer const &channelPutRequester,
    DbAddr &dbAddr)
: v3Util(V3Util::getV3Util()),
  v3Channel(v3Channel),
  channelPutRequester(channelPutRequester),
  dbAddr(dbAddr),
  propertyMask(0),
  process(false),
  firstTime(true),
  beingDestroyed(false)
{
    if(V3ChannelDebug::getLevel()>0) printf("V3ChannelPut::V3ChannelPut()\n");
}

V3ChannelPut::~V3ChannelPut()
{
    if(V3ChannelDebug::getLevel()>0) printf("V3ChannelPut::~V3ChannelPut()\n");
}

bool V3ChannelPut::init(PVStructure::shared_pointer const &pvRequest)
{
    propertyMask = v3Util->getProperties(
        channelPutRequester,
        pvRequest,
        dbAddr,
        true);
    if(propertyMask==v3Util->noAccessBit) return false;
    if(propertyMask==v3Util->noModBit) {
        channelPutRequester->message(
             String("field not allowed to be changed"),errorMessage);
        return 0;
    }
    pvStructure = PVStructure::shared_pointer(
        v3Util->createPVStructure(
            channelPutRequester,
            propertyMask,
            dbAddr));
    if(pvStructure.get()==0) return 0;
    if((propertyMask&v3Util->dbPutBit)!=0) {
        if((propertyMask&v3Util->processBit)!=0) {
            channelPutRequester->message(
             String("process determined by dbPutField"),errorMessage);
        }
    } else if((propertyMask&v3Util->processBit)!=0) {
       process = true;
       pNotify.reset(new (struct putNotify)());
       notifyAddr.reset(new DbAddr());
       memcpy(notifyAddr.get(),&dbAddr,sizeof(DbAddr));
       DbAddr *paddr = notifyAddr.get();
       struct dbCommon *precord = paddr->precord;
       char buffer[sizeof(precord->name) + 10];
       strcpy(buffer,precord->name);
       strcat(buffer,".PROC");
       if(dbNameToAddr(buffer,paddr)!=0) {
            throw std::logic_error(String("dbNameToAddr failed"));
       }
       struct putNotify *pn = pNotify.get();
       pn->userCallback = this->notifyCallback;
       pn->paddr = paddr;
       pn->nRequest = 1;
       pn->dbrType = DBR_CHAR;
       pn->usrPvt = this;
    }
    int numFields = pvStructure->getNumberFields();
    bitSet.reset(new BitSet(numFields));
    channelPutRequester->channelPutConnect(
       Status::Ok,
       getPtrSelf(),
       pvStructure,
       bitSet);
    return true;
}

String V3ChannelPut::getRequesterName() {
    return channelPutRequester->getRequesterName();
}

void V3ChannelPut::message(String const &message,MessageType messageType)
{
    channelPutRequester->message(message,messageType);
}

void V3ChannelPut::destroy() {
    if(V3ChannelDebug::getLevel()>0) printf("V3ChannelPut::destroy beingDestroyed %s\n",
         (beingDestroyed ? "true" : "false"));
    {
        Lock xx(mutex);
        if(beingDestroyed) return;
        beingDestroyed = true;
    }
    v3Channel->removeChannelPut(getPtrSelf());
}

void V3ChannelPut::put(bool lastRequest)
{
    if(V3ChannelDebug::getLevel()>0) printf("V3ChannelPut::put()\n");
    Lock lock(dataMutex);
    PVFieldPtr pvField = pvStructure.get()->getPVFields()[0];
    if(propertyMask&v3Util->dbPutBit) {
        Status status = v3Util->putField(
            channelPutRequester,propertyMask,dbAddr,pvField);
        lock.unlock();
        channelPutRequester->putDone(status);
        if(lastRequest) destroy();
        return;
    }
    dbScanLock(dbAddr.precord);
    Status status = v3Util->put(
        channelPutRequester,propertyMask,dbAddr,pvField);
    dbScanUnlock(dbAddr.precord);
    lock.unlock();
    if(process) {
        epicsUInt8 value = 1;
        pNotify.get()->pbuffer = &value;
        dbPutNotify(pNotify.get());
        event.wait();
    }
    channelPutRequester->putDone(status);
    if(lastRequest) destroy();
}

void V3ChannelPut::notifyCallback(struct putNotify *pn)
{
    V3ChannelPut * cput = static_cast<V3ChannelPut *>(pn->usrPvt);
    cput->event.signal();
}

void V3ChannelPut::get()
{
    if(V3ChannelDebug::getLevel()>0) printf("V3ChannelPut::get()\n");
    {
    Lock lock(dataMutex);
    bitSet->clear();
    dbScanLock(dbAddr.precord);
    Status status = v3Util->get(
        channelPutRequester,
        propertyMask,dbAddr,
        pvStructure,
        bitSet,
        0);
    if(firstTime) {
        firstTime = false;
        bitSet->set(pvStructure->getFieldOffset());
    }
    dbScanUnlock(dbAddr.precord);
    }
    channelPutRequester->getDone(Status::Ok);
}

void V3ChannelPut::lock()
{
    dataMutex.lock();
}

void V3ChannelPut::unlock()
{
    dataMutex.unlock();
}

}}