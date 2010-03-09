//==========================================================================
//  RESULTLISTENER.H - part of
//                     OMNeT++/OMNEST
//            Discrete System Simulation in C++
//
//  Author: Andras Varga
//
//==========================================================================

/*--------------------------------------------------------------*
  Copyright (C) 1992-2008 Andras Varga
  Copyright (C) 2006-2008 OpenSim Ltd.

  This file is distributed WITHOUT ANY WARRANTY. See the file
  `license' for details on this and other legal matters.
*--------------------------------------------------------------*/

#ifndef __ENVIR_RESULTLISTENER_H
#define __ENVIR_RESULTLISTENER_H

#include "envirdefs.h"
#include "clistener.h"

class ResultFilter;
class cComponent;

//XXX maybe we could get rid of the (component,signalID) parameters in all functions...
// - component is needed for: recordScalar()/recordStatistic() calls and for collecting
//   result attributes (extractStatisticAttributes(component, attributes)) in ResultRecorders;
//     *** this is now SOLVED, as ResultRecorder stores the component ptr
//
// - signalID is needed for: getSignalTime(signalID) and getSignalValue(signalID) calls
//   in cObject* signals, in both recorders and filters
//     *** this is ONLY needed in the first listener; then it could convert to fire(simtime_t,double) pair!
//
//  FUNCTIONS:
//        virtual void listenerAdded(ResultFilter *prev);
//        virtual void listenerRemoved(ResultFilter *prev);
//        virtual void finish();
//        virtual void receiveSignal(double d);
//        virtual void receiveSignal(simtime_t, double d);
//  and existing functions would delegate to these.
//
//  problem (?): how to handle filters that accept objects or anything, like count(), byteLength(), etc!!
//  maybe that's not a problem at all; re-think!
//

class ResultFilter;

class ENVIR_API ResultListener : public cIListener
{
    public:
        virtual std::string str() const {return opp_typename(typeid(*this));}

        // simplified API that better supports chaining:
        virtual void receiveSignal(ResultFilter *prev, long l) = 0;
        virtual void receiveSignal(ResultFilter *prev, double d) = 0; // primary
        virtual void receiveSignal(ResultFilter *prev, simtime_t t, double d) = 0; // new; primary
        virtual void receiveSignal(ResultFilter *prev, simtime_t t) = 0;
        virtual void receiveSignal(ResultFilter *prev, const char *s) = 0;
        virtual void receiveSignal(ResultFilter *prev, cObject *obj) = 0;
        virtual void listenerAdded(ResultFilter *prev);
        virtual void listenerRemoved(ResultFilter *prev);
        virtual void finish(ResultFilter *prev) {}

        // original listener API delegates to simplified API:
        virtual void receiveSignal(cComponent *source, simsignal_t signalID, long l);
        virtual void receiveSignal(cComponent *source, simsignal_t signalID, double d);
        virtual void receiveSignal(cComponent *source, simsignal_t signalID, simtime_t t);
        virtual void receiveSignal(cComponent *source, simsignal_t signalID, const char *s);
        virtual void receiveSignal(cComponent *source, simsignal_t signalID, cObject *obj);
        virtual void listenerAdded(cComponent *component, simsignal_t signalID);
        virtual void listenerRemoved(cComponent *component, simsignal_t signalID);
        virtual void finish(cComponent *component, simsignal_t signalID);
};

/**
 * Data structure used when setting up chains of ResultFilters and ResultRecorders
 */
class SignalSource
{
  private:
    ResultFilter *filter;
    cComponent *component;
    simsignal_t signalID;
  public:
    SignalSource(ResultFilter *prevFilter) {filter=prevFilter; component=NULL; signalID=-1;}
    SignalSource(cComponent *comp, simsignal_t sigID) {filter=NULL; component=comp; signalID=sigID;}
    bool isNull() const {return !filter && !component;}
    void subscribe(ResultListener *listener) const;
};

#endif


