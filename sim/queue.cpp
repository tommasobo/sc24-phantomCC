// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#include "queue.h"
#include "bbrpacket.h"
#include "ndppacket.h"
#include "queue_lossless.h"
#include "swifttrimmingpacket.h"
#include "uecpacket.h"
#include "uecpacket_drop.h"
#include <math.h>
#include <sstream>

simtime_picosec BaseQueue::_update_period = timeFromUs(0.1);

// base queue is a generic queue that we can log, but doesn't actually store
// anything
BaseQueue::BaseQueue(linkspeed_bps bitrate, EventList &eventlist,
                     QueueLogger *logger)
        : EventSource(eventlist, "Queue"), _logger(logger), _bitrate(bitrate),
          _switch(NULL) {
    _ps_per_byte = (simtime_picosec)((pow(10.0, 12.0) * 8) / _bitrate);
    _window = timeFromUs(5.0);
    _busy = 0;

    _last_update_qs = 0;
    _last_update_utilization = 0;
    _last_qs = 0;
    _last_utilization = 0;
}

void BaseQueue::log_packet_send(simtime_picosec duration) {
    // a packet tranmission has just finished; it lasted from a to b.
    simtime_picosec b = eventlist().now();
    simtime_picosec a = b - duration;
    _busystart.push(a);
    _busyend.push(b);

    _busy += duration;

    simtime_picosec y = _busyend.back();
    while (y < b - _window) {
        simtime_picosec x = _busystart.pop();
        _busyend.pop();

        _busy -= (y - x);

        if (!_busyend.empty())
            y = _busyend.back();
        else
            break;
    }
}

uint16_t BaseQueue::average_utilization() {
    // how much time have we spent being busy in the current measurement window?
    if (_busystart.empty())
        return 0;

    simtime_picosec y = _busyend.back();
    simtime_picosec b = eventlist().now();

    while (y < b - _window) {
        simtime_picosec x = _busystart.pop();
        _busyend.pop();

        _busy -= (y - x);
        assert(_busy >= 0);

        if (!_busyend.empty())
            y = _busyend.back();
        else
            break;
    }
    return (_busy * 100 / _window);
}

uint8_t BaseQueue::quantized_utilization() {
    if (eventlist().now() - _last_update_utilization > _update_period) {
        _last_update_utilization = eventlist().now();

        uint16_t avg = average_utilization();

        // if (avg>=100) avg = 99;

        // quantize utilization to four 25% bands of linerate.
        //_last_utilization = avg / 25;

        if (avg == 0)
            _last_utilization = 0;
        else if (avg < 15)
            _last_utilization = 1;
        else if (avg < 50)
            _last_utilization = 2;
        else
            _last_utilization = 3;
    }
    return _last_utilization;
}

uint64_t BaseQueue::quantized_queuesize() {
    if (eventlist().now() - _last_update_qs > _update_period) {
        _last_update_qs = eventlist().now();

        uint64_t qs = queuesize();
        if (qs < maxsize() * 0.05)
            _last_qs = 0;
        else if (qs < maxsize() * 0.1)
            _last_qs = 1;
        else if (qs < maxsize() * 0.2)
            _last_qs = 2;
        else
            _last_qs = 3;
        //_last_qs = queuesize();

        // cout << "QS " << (uint32_t)_last_qs << " queuesize " << queuesize()
        // << " max " << maxsize() << endl;
    }
    return _last_qs;
}

Queue::Queue(linkspeed_bps bitrate, mem_b maxsize, EventList &eventlist,
             QueueLogger *logger)
        : BaseQueue(bitrate, eventlist, logger), _maxsize(maxsize),
          _num_drops(0) {
    _queuesize = 0;
    stringstream ss;
    ss << "queue(" << bitrate / 1000000 << "Mb/s," << maxsize << "bytes)";
    _nodename = ss.str();
}

void Queue::beginService() {
    /* schedule the next dequeue event */
    assert(!_enqueued.empty());
    eventlist().sourceIsPendingRel(*this, drainTime(_enqueued.back()));
}

void Queue::completeService() {
    /* dequeue the packet */
    assert(!_enqueued.empty());
    // Packet* pkt = _enqueued.back();
    //_enqueued.pop_back();
    Packet *pkt = _enqueued.pop();
    _queuesize -= pkt->size();
    pkt->flow().logTraffic(*pkt, *this, TrafficLogger::PKT_DEPART);
    if (_logger)
        _logger->logQueue(*this, QueueLogger::PKT_SERVICE, *pkt);

    // used to compute queue utilization
    log_packet_send(drainTime(pkt));

    /* tell the packet to move on to the next pipe */
    pkt->sendOn();

    if (!_enqueued.empty()) {
        /* schedule the next dequeue event */
        beginService();
    }
}

void Queue::doNextEvent() { completeService(); }

void Queue::receivePacket(Packet &pkt) {
    if (_queuesize + pkt.size() > _maxsize) {
        /* if the packet doesn't fit in the queue, drop it */
        if (_logger)
            _logger->logQueue(*this, QueueLogger::PKT_DROP, pkt);

        pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_DROP);
        pkt.free();

        _num_drops++;
        return;
    }
    pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_ARRIVE);

    /* enqueue the packet */
    bool queueWasEmpty = _enqueued.empty();
    //_enqueued.push_front(&pkt);
    Packet *pkt_p = &pkt;
    _enqueued.push(pkt_p);
    _queuesize += pkt.size();
    if (_logger)
        _logger->logQueue(*this, QueueLogger::PKT_ENQUEUE, pkt);

    if (queueWasEmpty) {
        /* schedule the dequeue event */
        assert(_enqueued.size() == 1);
        beginService();
    }
}

mem_b Queue::queuesize() const { return _queuesize; }

simtime_picosec Queue::serviceTime() { return _queuesize * _ps_per_byte; }

PriorityQueue::PriorityQueue(linkspeed_bps bitrate, mem_b maxsize,
                             EventList &eventlist, QueueLogger *logger)
        : HostQueue(bitrate, maxsize, eventlist, logger) {
    _queuesize[Q_LO] = 0;
    _queuesize[Q_MID] = 0;
    _queuesize[Q_HI] = 0;
    _servicing = Q_NONE;
    _state_send = LosslessQueue::READY;
}

PriorityQueue::queue_priority_t PriorityQueue::getPriority(Packet &pkt) {
    queue_priority_t prio = Q_LO;
    switch (pkt.type()) {
    case TCPACK:
    case NDPACK:
    case NDPNACK:
    case NDPPULL:
    case NDPRTS:
    case NDPLITEACK:
    case NDPLITERTS:
    case NDPLITEPULL:
    case ROCEACK:
    case ROCENACK:
        prio = Q_HI;
        break;
    case NDP:
        if (pkt.header_only()) {
            prio = Q_HI;
        } else {
            NdpPacket *np = (NdpPacket *)(&pkt);
            if (np->retransmitted()) {
                prio = Q_MID;
            } else {
                prio = Q_LO;
            }
        }
        break;
    case TCP:
    case IP:
    case NDPLITE:
    case ROCE:
        prio = Q_LO;
        break;
    default:
        abort();
    }
    return prio;
}

simtime_picosec PriorityQueue::serviceTime(Packet &pkt) {
    queue_priority_t prio = getPriority(pkt);
    switch (prio) {
    case Q_LO:
        // cout << "q_lo: " << _queuesize[Q_HI] + _queuesize[Q_MID] +
        // _queuesize[Q_LO] << " ";
        return (_queuesize[Q_HI] + _queuesize[Q_MID] + _queuesize[Q_LO]) *
               _ps_per_byte;
    case Q_MID:
        // cout << "q_mid: " << _queuesize[Q_MID] + _queuesize[Q_LO] << " ";
        return (_queuesize[Q_HI] + _queuesize[Q_MID]) * _ps_per_byte;
    case Q_HI:
        // cout << "q_hi: " << _queuesize[Q_LO] << " ";
        return _queuesize[Q_HI] * _ps_per_byte;
    default:
        abort();
    }
}

void PriorityQueue::receivePacket(Packet &pkt) {
    // is this a PAUSE packet?
    // printf("Received2 here\n");
    if (pkt.type() == ETH_PAUSE) {
        EthPausePacket *p = (EthPausePacket *)&pkt;

        if (p->sleepTime() > 0 && _state_send == LosslessQueue::READY) {
            // remote end is telling us to shut up.
            // assert(_state_send == LosslessQueue::READY);
            if (queuesize() > 0)
                // we have a packet in flight
                _state_send = LosslessQueue::PAUSE_RECEIVED;
            else
                _state_send = LosslessQueue::PAUSED;

            // cout << timeAsMs(eventlist().now()) << " " << _name << " PAUSED
            // "<<endl;
        } else if (_state_send != LosslessQueue::READY) {
            // we are allowed to send!
            _state_send = LosslessQueue::READY;
            // cout << timeAsMs(eventlist().now()) << " " << _name << " GO
            // "<<endl;

            // start transmission if we have packets to send!
            if (queuesize() > 0)
                beginService();
        }

        // must send the packets to all sources on the same host!
        for (uint32_t i = 0; i < _senders.size(); i++) {
            EthPausePacket *e =
                    EthPausePacket::newpkt(p->sleepTime(), p->senderID());
            _senders[i]->receivePacket(*e);
        }

        pkt.free();
        return;
    }

    queue_priority_t prio = getPriority(pkt);
    pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_ARRIVE);

    /* enqueue the packet */
    bool queueWasEmpty = false;
    if (queuesize() == 0)
        queueWasEmpty = true;

    _queuesize[prio] += pkt.size();
    _queue[prio].push_front(&pkt);

    if (_logger)
        _logger->logQueue(*this, QueueLogger::PKT_ENQUEUE, pkt);

    if (queueWasEmpty && _state_send == LosslessQueue::READY) {
        /* schedule the dequeue event */
        assert(_queue[Q_LO].size() + _queue[Q_MID].size() +
                       _queue[Q_HI].size() ==
               1);
        beginService();
    }
}

void PriorityQueue::beginService() {
    assert(_state_send == LosslessQueue::READY);

    /* schedule the next dequeue event */
    for (int prio = Q_HI; prio >= Q_LO; --prio) {
        if (_queuesize[prio] > 0) {
            eventlist().sourceIsPendingRel(*this,
                                           drainTime(_queue[prio].back()));
            _servicing = (queue_priority_t)prio;
            return;
        }
    }
}

void PriorityQueue::completeService() {
    /* dequeue the packet */
    // assert(_servicing != Q_NONE);
    // assert(!_queue[_servicing].empty());

    if (_servicing == Q_NONE || _queue[_servicing].empty()) {
        cout << _name << " trying to deque " << _servicing << ", qsize "
             << queuesize() << endl;
    } else {
        Packet *pkt = _queue[_servicing].back();
        _queue[_servicing].pop_back();
        _queuesize[_servicing] -= pkt->size();
        pkt->flow().logTraffic(*pkt, *this, TrafficLogger::PKT_DEPART);
        if (_logger)
            _logger->logQueue(*this, QueueLogger::PKT_SERVICE, *pkt);

        /* tell the packet to move on to the next pipe */
        pkt->sendOn();
    }

    if (_state_send == LosslessQueue::PAUSE_RECEIVED)
        _state_send = LosslessQueue::PAUSED;

    // if (_state_send!=LosslessQueue::READY){
    // cout << eventlist().now() << " queue " << _name << " not ready but
    // sending " << endl;
    // }

    if (queuesize() > 0) {
        if (_state_send == LosslessQueue::READY)
            /* schedule the next dequeue event */
            beginService();
        else {
            // we've received pause or are already paused and will do nothing
            // until the other end unblocks us        }
        }
    } else {
        _servicing = Q_NONE;
    }
}

mem_b PriorityQueue::queuesize() const {
    return _queuesize[Q_LO] + _queuesize[Q_MID] + _queuesize[Q_HI];
}

HostQueue::HostQueue(linkspeed_bps bitrate, mem_b maxsize, EventList &eventlist,
                     QueueLogger *l)
        : Queue(bitrate, maxsize, eventlist, l){};

////////FairPriorityQueue to help with RR at senders with many flows.
FairPriorityQueue::FairPriorityQueue(linkspeed_bps bitrate, mem_b maxsize,
                                     EventList &eventlist, QueueLogger *logger)
        : HostQueue(bitrate, maxsize, eventlist, logger) {
    _queuesize[Q_LO] = 0;
    _queuesize[Q_MID] = 0;
    _queuesize[Q_HI] = 0;
    _servicing = Q_NONE;
    _sending = NULL;
    _state_send = LosslessQueue::READY;
}

FairPriorityQueue::queue_priority_t
FairPriorityQueue::getPriority(Packet &pkt) {
    queue_priority_t prio = Q_LO;
    switch (pkt.type()) {
    case TCPACK:
    case UECACK:
    case UECNACK:
    case BBRACK:
    case BBRNACK:
    case UECACK_DROP:
    case UECNACK_DROP:
    case SWIFTTRIMMINGACK:
    case SWIFTTRIMMINGNACK:
    case NDPACK:
    case NDPNACK:
    case NDPPULL:
    case NDPRTS:
    case NDPLITEACK:
    case NDPLITERTS:
    case NDPLITEPULL:
    case SWIFTACK:
    case ROCEACK:
    case ROCENACK:
    case HPCCNACK:
    case HPCCACK:
        prio = Q_HI;
        break;
    case NDP:
        if (pkt.header_only()) {
            prio = Q_HI;
        } else {
            NdpPacket *np = (NdpPacket *)(&pkt);
            if (np->retransmitted()) {
                prio = Q_MID;
            } else {
                prio = Q_LO;
            }
        }
        break;
    case UEC:
        if (pkt.header_only()) {
            prio = Q_HI;
        } else {
            UecPacket *uec = dynamic_cast<UecPacket *>(&pkt);
            if (uec->retransmitted()) {
                prio = Q_LO;
            } else {
                prio = Q_LO;
            }
        }
        break;
    case UEC_DROP:
        if (pkt.header_only()) {
            prio = Q_HI;
        } else {
            UecDropPacket *uec = dynamic_cast<UecDropPacket *>(&pkt);
            if (uec->retransmitted()) {
                prio = Q_MID;
            } else {
                prio = Q_LO;
            }
        }
        break;
    case BBR:
        if (pkt.header_only()) {
            prio = Q_HI;
        } else {
            BBRPacket *uec = dynamic_cast<BBRPacket *>(&pkt);
            if (uec->retransmitted()) {
                prio = Q_MID;
            } else {
                prio = Q_LO;
            }
        }
        break;
    case SWIFTTRIMMING:
        if (pkt.header_only()) {
            prio = Q_HI;
        } else {
            SwiftTrimmingPacket *uec =
                    dynamic_cast<SwiftTrimmingPacket *>(&pkt);
            if (uec->retransmitted()) {
                prio = Q_MID;
            } else {
                prio = Q_LO;
            }
        }
        break;
    case TCP:
    case IP:
    case NDPLITE:
    case SWIFT:
    case ROCE:
    case HPCC:
        prio = Q_LO;
        break;
    default:
        abort();
    }
    return prio;
}

// this is inaccurate!
simtime_picosec FairPriorityQueue::serviceTime(Packet &pkt) {
    queue_priority_t prio = getPriority(pkt);
    switch (prio) {
    case Q_LO:
        // cout << "q_lo: " << _queuesize[Q_HI] + _queuesize[Q_MID] +
        // _queuesize[Q_LO] << " ";
        return (_queuesize[Q_HI] + _queuesize[Q_MID] + _queuesize[Q_LO]) *
               _ps_per_byte;
    case Q_MID:
        // cout << "q_mid: " << _queuesize[Q_MID] + _queuesize[Q_LO] << " ";
        return (_queuesize[Q_HI] + _queuesize[Q_MID]) * _ps_per_byte;
    case Q_HI:
        // cout << "q_hi: " << _queuesize[Q_LO] << " ";
        return _queuesize[Q_HI] * _ps_per_byte;
    default:
        abort();
    }
}

void FairPriorityQueue::receivePacket(Packet &pkt) {
    // is this a PAUSE packet?
    if (pkt.type() == ETH_PAUSE) {
        EthPausePacket *p = (EthPausePacket *)&pkt;

        if (p->sleepTime() > 0) {
            // remote end is telling us to shut up.
            // assert(_state_send == LosslessQueue::READY);
            if (queuesize() > 0)
                // we have a packet in flight
                _state_send = LosslessQueue::PAUSE_RECEIVED;
            else
                _state_send = LosslessQueue::PAUSED;

            // cout << timeAsMs(eventlist().now()) << " FPQ " << _name << "
            // PAUSED "<<endl;
        } else {
            // we are allowed to send!
            _state_send = LosslessQueue::READY;
            // cout << timeAsMs(eventlist().now()) << " FPQ " << _name << " GO
            // "<<endl;

            // start transmission if we have packets to send!
            if (queuesize() > 0)
                beginService();
        }

        // must send the packets to all sources on the same host!
        for (uint32_t i = 0; i < _senders.size(); i++) {
            // cout << "Sending pause" << endl;
            EthPausePacket *e =
                    EthPausePacket::newpkt(p->sleepTime(), p->senderID());
            _senders[i]->receivePacket(*e);
        }

        pkt.free();
        return;
    }

    queue_priority_t prio = getPriority(pkt);
    pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_ARRIVE);

    /* enqueue the packet */
    bool queueWasEmpty = false;
    if (queuesize() == 0)
        queueWasEmpty = true;

    if (queuesize() > _maxsize &&
        queuesize() / 1000000 != (queuesize() + pkt.size()) / 1000000) {
        // pkt.free();
        // cout << "Host Queue size " << queuesize() << endl;
        // return;
    }

    _queuesize[prio] += pkt.size();
    // printf("Pkt6 %d %d - Time %lu %lu - Size %d - Prio %d\n", pkt.from,
    //        pkt.id(), eventlist().now() / 1000, 1, pkt.size(), prio);
    _queue[prio].enqueue(pkt);

    if (_logger)
        _logger->logQueue(*this, QueueLogger::PKT_ENQUEUE, pkt);

    if (queueWasEmpty && _state_send == LosslessQueue::READY) {
        /* schedule the dequeue event */
        // printf("Pkt8 %d %d - Time %lu %lu - Size %d - Prio %d\n", pkt.from,
        //        pkt.id(), eventlist().now() / 1000, 1, pkt.size(), prio);
        beginService();
    }
}

void FairPriorityQueue::beginService() {
    assert(_state_send == LosslessQueue::READY);

    /* schedule the next dequeue event */
    for (int prio = Q_HI; prio >= Q_LO; --prio) {
        if (_queuesize[prio] > 0) {
            _sending = _queue[prio].dequeue();

            assert(_sending != NULL);
            // printf("Pkt7 %d %d - Time %lu %lu - Size %d - Prio %d - Drain "
            //        "%lu\n",
            //        _sending->from, _sending->id(), eventlist().now() / 1000,
            //        1, _sending->size(), prio, drainTime(_sending));
            eventlist().sourceIsPendingRel(*this, drainTime(_sending));
            _servicing = (queue_priority_t)prio;
            return;
        }
    }
}

void FairPriorityQueue::completeService() {
    if (_state_send == LosslessQueue::PAUSED)
        return;

    if (_servicing == Q_NONE || _sending == NULL) {
        cout << _name << " trying to deque " << _servicing << ", qsize "
             << queuesize() << endl;
    } else {
        /* dequeue the packet */
        Packet *pkt = _sending;
        _queuesize[_servicing] -= pkt->size();

        _sending = NULL;

        pkt->flow().logTraffic(*pkt, *this, TrafficLogger::PKT_DEPART);
        if (_logger)
            _logger->logQueue(*this, QueueLogger::PKT_SERVICE, *pkt);

        /* tell the packet to move on to the next pipe */
        // printf("Pkt5 %d %d - Time %lu %lu - Size %d\n", pkt->from, pkt->id(),
        //        eventlist().now() / 1000, 1, pkt->size());
        pkt->sendOn();
    }

    if (_state_send == LosslessQueue::PAUSE_RECEIVED)
        _state_send = LosslessQueue::PAUSED;

    // if (_state_send!=LosslessQueue::READY){
    // cout << eventlist().now() << " queue " << _name << " not ready but
    // sending " << endl;
    // }

    if (queuesize() > 0 && _state_send == LosslessQueue::READY) {
        /* schedule the next dequeue event since we're allowed to send*/
        beginService();
    } else {
        _servicing = Q_NONE;
    }
}

mem_b FairPriorityQueue::queuesize() const {
    return _queuesize[Q_LO] + _queuesize[Q_MID] + _queuesize[Q_HI];
}

template class CircularBuffer<Packet *>;
