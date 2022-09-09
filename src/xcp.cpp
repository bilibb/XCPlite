/*----------------------------------------------------------------------------
| File:
|   xcp.cpp
|
| Description:
|   C++ wrapper for XCP server
|
| Copyright (c) Vector Informatik GmbH. All rights reserved.
| Licensed under the MIT license. See LICENSE file in the project root for details.
|
 ----------------------------------------------------------------------------*/
 /*
 | Code released into public domain, no attribution required
 */

#include "main.h"
#include "main_cfg.h"
#include "platform.h"
#include "util.h"
#include "xcpLite.h"
#include "xcp.hpp"
#include "xcpServer.h"



// Singleton
Xcp* Xcp::instance = 0;
MUTEX Xcp::mutex;


Xcp* Xcp::getInstance()
{
    if (!instance) {
        mutexInit(&mutex, 0, 1000);
        lock();
        if (!instance) {
            instance = new Xcp();
        }
        unlock();
    }
    return instance;
}


Xcp::Xcp() : useTCP(FALSE), usePTP(FALSE), port(0), addr(), a2lFile(NULL) {

}

Xcp::~Xcp() {}



BOOL Xcp::init(const uint8_t* addr0, uint16_t port0, BOOL useTCP0, BOOL usePTP0, uint16_t segmentSize) {

    addr = addr0;
    port = port0;
    useTCP = useTCP0;
    usePTP = usePTP0;
    a2lFile = NULL;

    // Init network
    if (!socketStartup()) return FALSE;

    // Init clock
    if (!clockInit()) return FALSE;

    // Init and start XCP server
    if (!XcpServerInit(addr, port, useTCP, segmentSize)) return FALSE;

    return TRUE;
}

void Xcp::shutdown() {

    // Stop and shutdown XCP server
    XcpServerShutdown();

    socketCleanup();
}


uint64_t Xcp::getDaqClock() {
    return ::clockGet();
}


BOOL Xcp::onConnect() {

    // if A2L file is not closed yet, finalize it and make it available
    // to be able to offer the file for upload, it has to be finalized here at latest 
#ifdef OPTION_ENABLE_A2L_GEN
    closeA2L();
#endif

    return TRUE;
}

BOOL Xcp::onPrepareDaq() {
    return TRUE;
}

BOOL Xcp::onStartDaq() {
    return TRUE;
}

BOOL Xcp::onStopDaq() {
    return TRUE;
}


BOOL Xcp::status() {
    return XcpServerStatus();
}

BOOL Xcp::connected() {
    return XcpIsConnected();
}

BOOL Xcp::daqRunning() {
    return XcpIsDaqRunning();
}


void Xcp::event(uint16_t event) {
    XcpEvent(event);
}

void Xcp::eventExt(uint16_t event, uint8_t* base) {
    XcpEventExt(event, base);
}

void Xcp::eventAt(uint16_t event, uint64_t clock) {
    XcpEventAt(event, clock);
}


void Xcp::clearEventList() {
    XcpClearEventList();
}

uint16_t Xcp::createEvent(XcpEventDescriptor event) {
    return XcpCreateEvent(event.name, event.cycleTime, event.priority, event.sampleCount, event.size);
}

vector<Xcp::XcpEventDescriptor>* Xcp::getEventList() {

    uint16_t evtCount = 0;
    tXcpEvent* evtList = XcpGetEventList(&evtCount);

    vector<Xcp::XcpEventDescriptor>* l = new vector<Xcp::XcpEventDescriptor>();
    for (int i = 0; i < evtCount; i++) {
        uint64_t ns = evtList[i].timeCycle;
        uint8_t exp = evtList[i].timeUnit;
        while (exp-- > 0) ns *= 10;
        l->push_back(Xcp::XcpEventDescriptor(evtList[i].name, (uint32_t)(ns / 1000), evtList[i].priority, evtList[i].sampleCount, evtList[i].size));
    }
    return l;
}

#ifdef OPTION_ENABLE_A2L_GEN

uint32_t Xcp::getA2lAddr(uint8_t* p) { // Get A2L addr from pointer
    return ApplXcpGetAddr(p); 
}

const char* Xcp::getA2lFileName() {
    return OPTION_A2L_FILE_NAME;
}

A2L* Xcp::createA2L(const char* projectName) {

    if (a2lFile) return a2lFile;
    a2lFile = new A2L(OPTION_A2L_FILE_NAME);
    if (!a2lFile->open(projectName)) return NULL;
#ifdef OPTION_ENABLE_CAL_SEGMENT
    void create_MOD_PAR(uint32_t startAddr, uint32_t size);
#endif
    return a2lFile;
}

void Xcp::closeA2L() {

    if (a2lFile!=NULL) {
        a2lFile->create_XCP_IF_DATA(useTCP, addr, port);
        a2lFile->close();
        delete a2lFile;
        a2lFile = NULL;
    }
}

#endif



XcpObject::XcpObject(const char* instanceName, const char* className, int classSize) {

    this->xcpInstanceName = instanceName;
    this->className = className;
    this->classSize = classSize;

    // Create a XCP extended event for this instance
    // The event number is unique and will be the instance id used in A2L address high word
    xcpInstanceId = Xcp::getInstance()->createEvent(Xcp::XcpEventDescriptor(instanceName, 0, 0, 0, sizeof(*this)));

    // Create this instance in A2L
    printf("Create instance %s of %s\n", instanceName, className);
    A2L* a2l = Xcp::getInstance()->getA2L();
    a2l->setFixedEvent(xcpInstanceId);
    a2l->createDynTypedefInstance(instanceName, className, "");
}


void XcpObject::xcpCreateA2lTypedef() {

    // Create a A2L typedef for this class
    A2L* a2l = Xcp::getInstance()->getA2L();
    a2l->setFixedEvent(xcpInstanceId);
    a2l->createTypedefBegin_(className, classSize, "");
    a2l->createTypedefMeasurementComponent(xcpInstanceId);
    xcpCreateA2lTypedefComponents(a2l);
    a2l->createTypedefEnd();
};


// XCP code instrumentation for measurement and calibration
// Trigger the XCP event (instanceId) associated with this instance 
void XcpObject::xcpEvent() {

    if (this != NULL) {
        Xcp::getInstance()->eventExt(xcpInstanceId, (uint8_t*)this);
    }
}

void XcpObject::xcpEvent(uint8_t* base) {

    if (this != NULL) {
        Xcp::getInstance()->eventExt(xcpInstanceId, base);
    }
}



