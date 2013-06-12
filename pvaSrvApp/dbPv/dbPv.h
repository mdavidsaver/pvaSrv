/**
 * Copyright - See the COPYRIGHT that is included with this distribution.
 * EPICS pvData is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 */
/**
 * @author mrk
 */

#ifndef DBPV_H
#define DBPV_H

#include <dbAccess.h>
#include <dbNotify.h>

#include <pv/channelBase.h>
#include <pv/thread.h>
#include <pv/event.h>

#include "caMonitor.h"
#include "dbPvDebug.h"

namespace epics { namespace pvaSrv { 

class DbUtil;
typedef std::tr1::shared_ptr<DbUtil> DbUtilPtr;

class DbPvProvider;
typedef std::tr1::shared_ptr<DbPvProvider> DbPvProviderPtr;
class DbPv;
class DbPvProcess;
class DbPvGet;
class DbPvPut;
class DbPvMonitor;
class DbPvArray;

typedef struct dbAddr DbAddr;
typedef std::vector<DbAddr> DbAddrArray;

class DbPvProvider :
    public epics::pvAccess::ChannelBaseProvider
{
public:
     POINTER_DEFINITIONS(DbPvProvider);
    static DbPvProviderPtr getDbPvProvider();
    virtual ~DbPvProvider();
    virtual epics::pvAccess::ChannelFind::shared_pointer channelFind(
        epics::pvData::String const &channelName,
        epics::pvAccess::ChannelFindRequester::shared_pointer const & channelFindRequester);
    virtual epics::pvAccess::Channel::shared_pointer createChannel(
        epics::pvData::String const &channelName,
        epics::pvAccess::ChannelRequester::shared_pointer  const &channelRequester,
        short priority,
        epics::pvData::String const &address);
private:
    DbPvProvider();
};

class DbPv :
  public virtual epics::pvAccess::ChannelBase
{
public:
    POINTER_DEFINITIONS(DbPv);
    DbPv(
        epics::pvAccess::ChannelBaseProvider::shared_pointer const & provider,
        epics::pvAccess::ChannelRequester::shared_pointer const & requester,
        epics::pvData::String const & name,
        std::tr1::shared_ptr<DbAddr> addr
        );
    virtual ~DbPv();
    void init();
    virtual void getField(
        epics::pvAccess::GetFieldRequester::shared_pointer const &requester,
        epics::pvData::String const &subField);
    virtual epics::pvAccess::ChannelProcess::shared_pointer createChannelProcess(
        epics::pvAccess::ChannelProcessRequester::shared_pointer const &channelProcessRequester,
        epics::pvData::PVStructurePtr const &pvRequest);
    virtual epics::pvAccess::ChannelGet::shared_pointer createChannelGet(
        epics::pvAccess::ChannelGetRequester::shared_pointer const &channelGetRequester,
        epics::pvData::PVStructurePtr const &pvRequest);
    virtual epics::pvAccess::ChannelPut::shared_pointer createChannelPut(
        epics::pvAccess::ChannelPutRequester::shared_pointer const &channelPutRequester,
        epics::pvData::PVStructurePtr const &pvRequest);
    virtual epics::pvData::Monitor::shared_pointer createMonitor(
        epics::pvData::MonitorRequester::shared_pointer const &monitorRequester,
        epics::pvData::PVStructurePtr const &pvRequest);
    virtual epics::pvAccess::ChannelArray::shared_pointer createChannelArray(
        epics::pvAccess::ChannelArrayRequester::shared_pointer const &channelArrayRequester,
        epics::pvData::PVStructurePtr const &pvRequest);
    virtual void printInfo();
    virtual void printInfo(epics::pvData::StringBuilder out);
private:
    std::tr1::shared_ptr<DbAddr> dbAddr;
    epics::pvData::FieldConstPtr recordField; 
    epics::pvData::PVStructurePtr pvNullStructure;
    epics::pvData::BitSetPtr emptyBitSet;
};

class DbPvProcess :
  public virtual epics::pvAccess::ChannelProcess,
  public std::tr1::enable_shared_from_this<DbPvProcess>
{
public:
    POINTER_DEFINITIONS(DbPvProcess);
    DbPvProcess(
        epics::pvAccess::ChannelBase::shared_pointer const & dbPv,
        epics::pvAccess::ChannelProcessRequester::shared_pointer const & channelProcessRequester,
        DbAddr &dbAddr);
    virtual ~DbPvProcess();
    bool init();
    virtual epics::pvData::String getRequesterName();
    virtual void message(
        epics::pvData::String const &message,
        epics::pvData::MessageType messageType);
    virtual void destroy();
    virtual void process(bool lastRequest);
    virtual void lock();
    virtual void unlock();
private:
    shared_pointer getPtrSelf()
    {
        return shared_from_this();
    }
    static void notifyCallback(struct putNotify *);
    epics::pvAccess::ChannelBase::shared_pointer dbPv;
    epics::pvAccess::ChannelProcessRequester::shared_pointer channelProcessRequester;
    DbAddr &dbAddr;
    epics::pvData::String recordString;
    epics::pvData::String processString;
    epics::pvData::String fieldString;
    epics::pvData::String fieldListString;
    epics::pvData::String valueString;
    std::tr1::shared_ptr<struct putNotify> pNotify;
    std::tr1::shared_ptr<DbAddr> notifyAddr;
    epics::pvData::Event event;
    epics::pvData::Mutex mutex;
    bool beingDestroyed;
};

class DbPvGet :
  public virtual epics::pvAccess::ChannelGet,
  public std::tr1::enable_shared_from_this<DbPvGet>
{
public:
    POINTER_DEFINITIONS(DbPvGet);
    DbPvGet(
        epics::pvAccess::ChannelBase::shared_pointer const & dbPv,
        epics::pvAccess::ChannelGetRequester::shared_pointer const &channelGetRequester,
        DbAddr &dbAddr);
    virtual ~DbPvGet();
    bool init(epics::pvData::PVStructurePtr const & pvRequest);
    virtual epics::pvData::String getRequesterName();
    virtual void message(
        epics::pvData::String const &message,
        epics::pvData::MessageType messageType);
    virtual void destroy();
    virtual void get(bool lastRequest);
    virtual void lock();
    virtual void unlock();
private:
    shared_pointer getPtrSelf()
    {
        return shared_from_this();
    }
    static void notifyCallback(struct putNotify *);
    DbUtilPtr dbUtil;
    epics::pvAccess::ChannelBase::shared_pointer dbPv;
    epics::pvAccess::ChannelGetRequester::shared_pointer channelGetRequester;
    epics::pvData::PVStructurePtr pvStructure;
    epics::pvData::BitSet::shared_pointer bitSet;
    DbAddr &dbAddr;
    bool process;
    bool firstTime;
    int propertyMask;
    std::tr1::shared_ptr<struct putNotify> pNotify;
    std::tr1::shared_ptr<DbAddr> notifyAddr;
    epics::pvData::Event event;
    epics::pvData::Mutex dataMutex;
    epics::pvData::Mutex mutex;
    bool beingDestroyed;
};

class DbPvMultiGet :
  public virtual epics::pvAccess::ChannelGet,
  public std::tr1::enable_shared_from_this<DbPvMultiGet>
{
public:
    POINTER_DEFINITIONS(DbPvMultiGet);
    DbPvMultiGet(
        epics::pvAccess::ChannelBase::shared_pointer const & dbPv,
        epics::pvAccess::ChannelGetRequester::shared_pointer const &channelGetRequester,
        DbAddr &dbAddr);
    virtual ~DbPvMultiGet();
    bool init(epics::pvData::PVStructurePtr const & pvRequest);
    virtual epics::pvData::String getRequesterName();
    virtual void message(
        epics::pvData::String const &message,
        epics::pvData::MessageType messageType);
    virtual void destroy();
    virtual void get(bool lastRequest);
    virtual void lock();
    virtual void unlock();
private:
    shared_pointer getPtrSelf()
    {
        return shared_from_this();
    }
    static void notifyCallback(struct putNotify *);
    DbUtilPtr dbUtil;
    epics::pvAccess::ChannelBase::shared_pointer dbPv;
    epics::pvAccess::ChannelGetRequester::shared_pointer channelGetRequester;
    epics::pvData::PVStructurePtr pvStructure;
    epics::pvData::PVScalarArrayPtr pvScalarArray;
    epics::pvData::BitSet::shared_pointer bitSet;
    DbAddr &dbAddr;
    DbAddrArray dbAddrArray;
    int propertyMask;
    bool process;
    bool firstTime;
    epics::pvData::Mutex dataMutex;
    epics::pvData::Event event;
    epics::pvData::Mutex mutex;
    bool beingDestroyed;
};

class DbPvPut :
  public virtual epics::pvAccess::ChannelPut,
  public std::tr1::enable_shared_from_this<DbPvPut>
{
public:
    POINTER_DEFINITIONS(DbPvPut);
    DbPvPut(
        epics::pvAccess::ChannelBase::shared_pointer const & dbPv,
        epics::pvAccess::ChannelPutRequester::shared_pointer const &channelPutRequester,
        DbAddr &dbAddr);
    virtual ~DbPvPut();
    bool init(epics::pvData::PVStructurePtr const & pvRequest);
    virtual epics::pvData::String getRequesterName();
    virtual void message(
        epics::pvData::String const &message,
        epics::pvData::MessageType messageType);
    virtual void destroy();
    virtual void put(bool lastRequest);
    virtual void get();
    virtual void lock();
    virtual void unlock();
private:
    shared_pointer getPtrSelf()
    {
        return shared_from_this();
    }
    static void notifyCallback(struct putNotify *);
    DbUtilPtr dbUtil;
    epics::pvAccess::ChannelBase::shared_pointer dbPv;
    epics::pvAccess::ChannelPutRequester::shared_pointer channelPutRequester;
    epics::pvData::PVStructurePtr pvStructure;
    epics::pvData::BitSet::shared_pointer bitSet;
    DbAddr &dbAddr;
    int propertyMask;
    bool process;
    bool firstTime;
    std::tr1::shared_ptr<struct putNotify> pNotify;
    std::tr1::shared_ptr<DbAddr> notifyAddr;
    epics::pvData::Mutex dataMutex;
    epics::pvData::Event event;
    epics::pvData::Mutex mutex;
    bool beingDestroyed;
};

class DbPvMultiPut :
  public virtual epics::pvAccess::ChannelPut,
  public std::tr1::enable_shared_from_this<DbPvMultiPut>
{
public:
    POINTER_DEFINITIONS(DbPvMultiPut);
    DbPvMultiPut(
        epics::pvAccess::ChannelBase::shared_pointer const & dbPv,
        epics::pvAccess::ChannelPutRequester::shared_pointer const &channelPutRequester,
        DbAddr &dbAddr);
    virtual ~DbPvMultiPut();
    bool init(epics::pvData::PVStructurePtr const & pvRequest);
    virtual epics::pvData::String getRequesterName();
    virtual void message(
        epics::pvData::String const &message,
        epics::pvData::MessageType messageType);
    virtual void destroy();
    virtual void put(bool lastRequest);
    virtual void get();
    virtual void lock();
    virtual void unlock();
private:
    shared_pointer getPtrSelf()
    {
        return shared_from_this();
    }
    DbUtilPtr dbUtil;
    epics::pvAccess::ChannelBase::shared_pointer dbPv;
    epics::pvAccess::ChannelPutRequester::shared_pointer channelPutRequester;
    epics::pvData::PVStructurePtr pvStructure;
    epics::pvData::PVScalarArrayPtr pvScalarArray;
    epics::pvData::BitSet::shared_pointer bitSet;
    DbAddr &dbAddr;
    DbAddrArray dbAddrArray;
    int propertyMask;
    bool process;
    bool firstTime;
    epics::pvData::Mutex dataMutex;
    epics::pvData::Event event;
    epics::pvData::Mutex mutex;
    bool beingDestroyed;
};

class DbPvMonitor
: public virtual epics::pvData::Monitor,
  public virtual CaMonitorRequester,
  public std::tr1::enable_shared_from_this<DbPvMonitor>
{
public:
    POINTER_DEFINITIONS(DbPvMonitor);
    DbPvMonitor(
        epics::pvAccess::ChannelBase::shared_pointer const & dbPv,
        epics::pvData::MonitorRequester::shared_pointer const & monitorRequester,
        DbAddr &dbAddr
    );
    virtual ~DbPvMonitor();
    bool init(epics::pvData::PVStructurePtr const &  pvRequest);
    virtual epics::pvData::String getRequesterName();
    virtual void message(
        epics::pvData::String const &message,
        epics::pvData::MessageType messageType);
    virtual void destroy();
    virtual epics::pvData::Status start();
    virtual epics::pvData::Status stop();
    virtual epics::pvData::MonitorElementPtr poll();
    virtual void release(
        epics::pvData::MonitorElementPtr const & monitorElement);
    virtual void exceptionCallback(long status,long op);
    virtual void connectionCallback();
    virtual void accessRightsCallback();
    virtual void eventCallback(const char *);
    virtual void lock();
    virtual void unlock();
private:
    shared_pointer getPtrSelf()
    {
        return shared_from_this();
    }
    DbUtilPtr dbUtil;
    epics::pvData::MonitorElementPtr &getFree();
    epics::pvAccess::ChannelBase::shared_pointer dbPv;
    epics::pvData::MonitorRequester::shared_pointer  monitorRequester;
    DbAddr &dbAddr;
    epics::pvData::Event event;
    int propertyMask;
    bool firstTime;
    bool gotEvent;
    CaType caType;
    int queueSize;
    std::tr1::shared_ptr<CaMonitor> caMonitor;
    int numberFree;
    int numberUsed;
    int nextGetFree;
    int nextSetUsed;
    int nextGetUsed;
    int nextReleaseUsed;
    epics::pvData::Mutex mutex;
    bool beingDestroyed;
    bool isStarted;
    epics::pvData::MonitorElementPtrArray elements;
    epics::pvData::MonitorElementPtr currentElement;
    epics::pvData::MonitorElementPtr nextElement;
    epics::pvData::MonitorElementPtr nullElement;
    epics::pvData::MonitorElementPtrArray elementArray;
};

class DbPvArray :
  public epics::pvAccess::ChannelArray,
  public std::tr1::enable_shared_from_this<DbPvArray>
{
public:
    POINTER_DEFINITIONS(DbPvArray);
    DbPvArray(
        epics::pvAccess::ChannelBase::shared_pointer const & dbPv,
        epics::pvAccess::ChannelArrayRequester::shared_pointer const &channelArrayRequester,
        DbAddr &dbAddr);
    virtual ~DbPvArray();
    bool init(epics::pvData::PVStructurePtr const & pvRequest);
    virtual void destroy();
    virtual void putArray(bool lastRequest, int offset, int count);
    virtual void getArray(bool lastRequest, int offset, int count);
    virtual void setLength(bool lastRequest, int length, int capacity);
    virtual void lock();
    virtual void unlock();
private:
    shared_pointer getPtrSelf()
    {
        return shared_from_this();
    }
    epics::pvAccess::ChannelBase::shared_pointer dbPv;
    epics::pvAccess::ChannelArrayRequester::shared_pointer channelArrayRequester;
    epics::pvData::PVScalarArray::shared_pointer pvScalarArray;
    DbAddr &dbAddr;
    epics::pvData::Mutex dataMutex;
    epics::pvData::Mutex mutex;
    bool beingDestroyed;
};

}}

#endif  /* DBPV_H */
