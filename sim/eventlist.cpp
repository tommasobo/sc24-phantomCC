// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-

#include "eventlist.h"
#include "trigger.h"

EventList::EventList() : _endtime(0), _lasteventtime(0) {}

void EventList::setEndtime(simtime_picosec endtime) { _endtime = endtime; }

bool EventList::doNextEvent() {
    // triggers happen immediately - no time passes; no guarantee that
    // they happen in any particular order (don't assume FIFO or LIFO).
    if (!_pending_triggers.empty()) {
        TriggerTarget *target = _pending_triggers.back();
        _pending_triggers.pop_back();
        target->activate();
        return true;
    }

    if (_pendingsources.empty())
        return false;

    simtime_picosec nexteventtime = _pendingsources.begin()->first;
    EventSource *nextsource = _pendingsources.begin()->second;
    _pendingsources.erase(_pendingsources.begin());
    assert(nexteventtime >= _lasteventtime);

    // nexteventtime = ((nexteventtime + 100000 - 1) / 100000) * 100000;

    GLOBAL_TIME = nexteventtime;
    _lasteventtime = nexteventtime; // set this before calling doNextEvent, so
                                    // that this::now() is accurate
    nextsource->doNextEvent();
    return true;
}

void EventList::sourceIsPending(EventSource &src, simtime_picosec when) {
    /*
      pendingsources_t::iterator i = _pendingsources.begin();
      while (i != _pendingsources.end()) {
      if (i->second == &src)
      abort();
      i++;
      }
    */

    // printf("When %lu vs Now %lu - EndTime %lu\n", when, now(), _endtime);
    //  fflush(stdout);
    assert(when >= now());
    if (_endtime == 0 || when < _endtime)
        _pendingsources.insert(make_pair(when, &src));
}

void EventList::triggerIsPending(TriggerTarget &target) { _pending_triggers.push_back(&target); }

void EventList::cancelPendingSource(EventSource &src) {
    pendingsources_t::iterator i = _pendingsources.begin();
    while (i != _pendingsources.end()) {
        if (i->second == &src) {
            _pendingsources.erase(i);
            return;
        }
        i++;
    }
}

void EventList::reschedulePendingSource(EventSource &src, simtime_picosec when) {
    cancelPendingSource(src);
    sourceIsPending(src, when);
}
