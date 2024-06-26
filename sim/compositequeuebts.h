// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#ifndef COMPOSITE_QUEUE_BTS_H
#define COMPOSITE_QUEUE_BTS_H

/*
 * A composite queue that transforms packets into headers when there is no space
 * and services headers with priority.
 */

#define QUEUE_INVALID 0
#define QUEUE_LOW 1
#define QUEUE_HIGH 2

#include "config.h"
#include "eventlist.h"
#include "loggertypes.h"
#include "network.h"
#include "queue.h"
#include <list>

class CompositeQueueBts : public Queue {
  public:
    CompositeQueueBts(linkspeed_bps bitrate, mem_b maxsize,
                      EventList &eventlist, QueueLogger *logger);
    virtual void receivePacket(Packet &pkt);
    virtual void doNextEvent();
    // should really be private, but loggers want to see
    mem_b _queuesize_low, _queuesize_high;
    int num_headers() const { return _num_headers; }
    int num_packets() const { return _num_packets; }
    int num_stripped() const { return _num_stripped; }
    int num_bounced() const { return _num_bounced; }
    int num_acks() const { return _num_acks; }
    int num_nacks() const { return _num_nacks; }
    int num_pulls() const { return _num_pulls; }
    virtual mem_b queuesize() const;
    virtual void setName(const string &name) {
        Logged::setName(name);
        _nodename += name;
    }
    virtual const string &nodename() { return _nodename; }
    void set_ecn_threshold(mem_b ecn_thresh) {
        _ecn_minthresh = ecn_thresh;
        _ecn_maxthresh = ecn_thresh;
    }
    void set_ecn_thresholds(mem_b min_thresh, mem_b max_thresh) {
        _ecn_minthresh = min_thresh;
        _ecn_maxthresh = max_thresh;
    }

    void set_bts_threshold(mem_b bts_triggers_at) {
        _bts_triggering = bts_triggers_at;
    }

    void set_ignore_ecn_data(mem_b bts_ignore_data) {
        _bts_ignore_ecn_data = bts_ignore_data;
    }

    int _num_packets;
    int _num_headers; // only includes data packets stripped to headers, not
                      // acks or nacks
    int _num_acks;
    int _num_nacks;
    int _num_pulls;
    int _num_stripped; // count of packets we stripped
    int _num_bounced;  // count of packets we bounced
    int _current_from;
    int my_id;
    int packets_seen = 0;
    int trimmed_seen = 0;

  protected:
    // Mechanism
    void beginService();    // start serving the item at the head of the queue
    void completeService(); // wrap up serving the item at the head of the queue
    bool decide_ECN();

    int _serv;
    int _ratio_high, _ratio_low, _crt;
    // below minthresh, 0% marking, between minthresh and maxthresh
    // increasing random mark propbability, abve maxthresh, 100%
    // marking.
    mem_b _ecn_minthresh;
    mem_b _ecn_maxthresh;
    mem_b _bts_triggering;
    bool _bts_ignore_ecn_data = true;

    CircularBuffer<Packet *> _enqueued_low;
    CircularBuffer<Packet *> _enqueued_high;
};

#endif
