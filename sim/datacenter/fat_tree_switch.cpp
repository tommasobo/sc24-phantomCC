// -*- c-basic-offset: 4; indent-tabs-mode: nil -*-
#include "fat_tree_switch.h"
#include "callback_pipe.h"
#include "fat_tree_topology.h"
#include "queue_lossless.h"
#include "queue_lossless_output.h"
#include "routetable.h"

int FatTreeSwitch::precision_ts = 1;

FatTreeSwitch::FatTreeSwitch(EventList &eventlist, string s, switch_type t, uint32_t id, simtime_picosec delay,
                             FatTreeTopology *ft)
        : Switch(eventlist, s) {
    _id = id;
    _type = t;
    _pipe = new CallbackPipe(delay, eventlist, this);
    _uproutes = NULL;
    _ft = ft;
    _crt_route = 0;
    _hash_salt = random();
    _last_choice = eventlist.now();
    _fib = new RouteTable();
}

void FatTreeSwitch::receivePacket(Packet &pkt) {

    if (pkt.is_bts_pkt) {
        printf("BTS Packet 2\n");
    }

    if (pkt.type() == ETH_PAUSE) {
        EthPausePacket *p = (EthPausePacket *)&pkt;
        // I must be in lossless mode!
        // find the egress queue that should process this, and pass it over for
        // processing.
        printf("Received a Pause Packet\n");
        for (size_t i = 0; i < _ports.size(); i++) {
            LosslessQueue *q = (LosslessQueue *)_ports.at(i);
            if (q->getRemoteEndpoint() && ((Switch *)q->getRemoteEndpoint())->getID() == p->senderID()) {
                q->receivePacket(pkt);
                break;
            }
        }

        return;
    }

    // printf("Node %s - Packet Received\n", nodename().c_str());
    // printf("Node %s - Pkt ID %d - Packet Received at %lu\n", nodename().c_str(), pkt.id(), GLOBAL_TIME/1000);

    // printf("Packet Destination is 3 %d\n", pkt.dst());

    if (_packets.find(&pkt) == _packets.end()) {
        // ingress pipeline processing.

        _packets[&pkt] = true;

        const Route *nh = getNextHop(pkt, NULL);
        // set next hop which is peer switch.
        pkt.set_route(*nh);

        // emulate the switching latency between ingress and packet arriving at
        // the egress queue.
        _pipe->receivePacket(pkt);
    } else {
        _packets.erase(&pkt);

        // egress queue processing.
        // cout << "Switch type " << _type <<  " id " << _id << " pkt dst " <<
        // pkt.dst() << " dir " << pkt.get_direction() << endl;

        pkt.hop_count++;

        // printf("From %d - At %s - Hop %d - Time %lu - Typr %d\n", pkt.from,
        //        nodename().c_str(), pkt.hop_count, GLOBAL_TIME, pkt.type());

        if ((pkt.hop_count == 1) && (pkt.type() == UEC || pkt.type() == NDP || pkt.type() == SWIFTTRIMMING ||
                                     pkt.type() == UEC_DROP || pkt.type() == BBR)) {

            simtime_picosec my_time =
                    (GLOBAL_TIME - (4160 * 8 / LINK_SPEED_MODERN * 1000) - (LINK_DELAY_MODERN * 1000));

            pkt.set_ts(my_time);

            if (COLLECT_DATA) {
                // Sent
                std::string file_name = PROJECT_ROOT_PATH / ("sim/output/sent/s" + std::to_string(pkt.from) + ".txt ");
                std::ofstream MyFile(file_name, std::ios_base::app);

                MyFile << my_time / 1000 << "," << 1 << "," << pkt.id() << std::endl;

                MyFile.close();

                // printf("Pkt %d %d - Time %lu %lu - Size %d\n", pkt.from,
                //        pkt.id(), eventlist().now() / 1000, my_time,
                //        pkt.size());
            }
        }

        pkt.sendOn();
    }
};

void FatTreeSwitch::addHostPort(int addr, int flowid, PacketSink *transport) {
    Route *rt = new Route();
    rt->push_back(_ft->queues_nlp_ns[_ft->HOST_POD_SWITCH(addr)][addr]);
    rt->push_back(_ft->pipes_nlp_ns[_ft->HOST_POD_SWITCH(addr)][addr]);
    rt->push_back(transport);
    _fib->addHostRoute(addr, rt, flowid);
}

uint32_t mhash(uint32_t x) {
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}

uint32_t FatTreeSwitch::adaptive_route_p2c(vector<FibEntry *> *ecmp_set, int8_t (*cmp)(FibEntry *, FibEntry *)) {
    uint32_t choice = 0, min = UINT32_MAX;
    uint32_t start, i = 0;
    static const uint16_t nr_choices = 2;

    do {
        start = random() % ecmp_set->size();

        Route *r = (*ecmp_set)[start]->getEgressPort();
        assert(r && r->size() > 1);
        BaseQueue *q = (BaseQueue *)(r->at(0));
        assert(q);
        if (q->queuesize() < min) {
            choice = start;
            min = q->queuesize();
        }
        i++;
    } while (i < nr_choices);
    return choice;
}

uint32_t FatTreeSwitch::adaptive_route(vector<FibEntry *> *ecmp_set, int8_t (*cmp)(FibEntry *, FibEntry *)) {
    uint32_t choice = 0;

    uint32_t best_choices[256];
    uint32_t best_choices_count = 0;

    FibEntry *min = (*ecmp_set)[choice];
    best_choices[best_choices_count++] = choice;

    for (uint32_t i = 1; i < ecmp_set->size(); i++) {
        int8_t c = cmp(min, (*ecmp_set)[i]);

        if (c < 0) {
            choice = i;
            min = (*ecmp_set)[choice];
            best_choices_count = 0;
            best_choices[best_choices_count++] = choice;
        } else if (c == 0) {
            assert(best_choices_count < 256);
            best_choices[best_choices_count++] = i;
        }
    }

    assert(best_choices_count >= 1);
    choice = best_choices[random() % best_choices_count];
    return choice;
}

uint32_t FatTreeSwitch::replace_worst_choice(vector<FibEntry *> *ecmp_set, int8_t (*cmp)(FibEntry *, FibEntry *),
                                             uint32_t my_choice) {
    uint32_t best_choice = 0;
    uint32_t worst_choice = 0;

    uint32_t best_choices[256];
    uint32_t best_choices_count = 0;

    FibEntry *min = (*ecmp_set)[best_choice];
    FibEntry *max = (*ecmp_set)[worst_choice];
    best_choices[best_choices_count++] = best_choice;

    for (uint32_t i = 1; i < ecmp_set->size(); i++) {
        int8_t c = cmp(min, (*ecmp_set)[i]);

        if (c < 0) {
            best_choice = i;
            min = (*ecmp_set)[best_choice];
            best_choices_count = 0;
            best_choices[best_choices_count++] = best_choice;
        } else if (c == 0) {
            assert(best_choices_count < 256);
            best_choices[best_choices_count++] = i;
        }

        if (cmp(max, (*ecmp_set)[i]) > 0) {
            worst_choice = i;
            max = (*ecmp_set)[worst_choice];
        }
    }

    // might need to play with different alternatives here, compare to worst
    // rather than just to worst index.
    int8_t r = cmp((*ecmp_set)[my_choice], (*ecmp_set)[worst_choice]);
    assert(r >= 0);

    if (r == 0) {
        assert(best_choices_count >= 1);
        return best_choices[random() % best_choices_count];
    } else
        return my_choice;
}

int8_t FatTreeSwitch::compare_pause(FibEntry *left, FibEntry *right) {
    Route *r1 = left->getEgressPort();
    assert(r1 && r1->size() > 1);
    LosslessOutputQueue *q1 = dynamic_cast<LosslessOutputQueue *>(r1->at(0));
    Route *r2 = right->getEgressPort();
    assert(r2 && r2->size() > 1);
    LosslessOutputQueue *q2 = dynamic_cast<LosslessOutputQueue *>(r2->at(0));

    if (!q1->is_paused() && q2->is_paused())
        return 1;
    else if (q1->is_paused() && !q2->is_paused())
        return -1;
    else
        return 0;
}

int8_t FatTreeSwitch::compare_queuesize(FibEntry *left, FibEntry *right) {
    Route *r1 = left->getEgressPort();
    assert(r1 && r1->size() > 1);
    BaseQueue *q1 = dynamic_cast<BaseQueue *>(r1->at(0));
    Route *r2 = right->getEgressPort();
    assert(r2 && r2->size() > 1);
    BaseQueue *q2 = dynamic_cast<BaseQueue *>(r2->at(0));

    if (q1->quantized_queuesize() < q2->quantized_queuesize())
        return 1;
    else if (q1->quantized_queuesize() > q2->quantized_queuesize())
        return -1;
    else
        return 0;
}

int8_t FatTreeSwitch::compare_bandwidth(FibEntry *left, FibEntry *right) {
    Route *r1 = left->getEgressPort();
    assert(r1 && r1->size() > 1);
    BaseQueue *q1 = dynamic_cast<BaseQueue *>(r1->at(0));
    Route *r2 = right->getEgressPort();
    assert(r2 && r2->size() > 1);
    BaseQueue *q2 = dynamic_cast<BaseQueue *>(r2->at(0));

    if (q1->quantized_utilization() < q2->quantized_utilization())
        return 1;
    else if (q1->quantized_utilization() > q2->quantized_utilization())
        return -1;
    else
        return 0;
}

int8_t FatTreeSwitch::compare_pqb(FibEntry *left, FibEntry *right) {
    // compare pause, queuesize, bandwidth.
    int8_t p = compare_pause(left, right);

    if (p != 0)
        return p;

    p = compare_queuesize(left, right);

    if (p != 0)
        return p;

    return compare_bandwidth(left, right);
}

int8_t FatTreeSwitch::compare_pq(FibEntry *left, FibEntry *right) {
    // compare pause, queuesize, bandwidth.
    int8_t p = compare_pause(left, right);

    if (p != 0)
        return p;

    return compare_queuesize(left, right);
}

int8_t FatTreeSwitch::compare_qb(FibEntry *left, FibEntry *right) {
    // compare pause, queuesize, bandwidth.
    int8_t p = compare_queuesize(left, right);

    if (p != 0)
        return p;

    return compare_bandwidth(left, right);
}

int8_t FatTreeSwitch::compare_pb(FibEntry *left, FibEntry *right) {
    // compare pause, queuesize, bandwidth.
    int8_t p = compare_pause(left, right);

    if (p != 0)
        return p;

    return compare_bandwidth(left, right);
}

void FatTreeSwitch::permute_paths(vector<FibEntry *> *uproutes) {
    int len = uproutes->size();
    for (int i = 0; i < len; i++) {
        int ix = random() % (len - i);
        FibEntry *tmppath = (*uproutes)[ix];
        (*uproutes)[ix] = (*uproutes)[len - 1 - i];
        (*uproutes)[len - 1 - i] = tmppath;
    }
}

FatTreeSwitch::routing_strategy FatTreeSwitch::_strategy = FatTreeSwitch::NIX;
uint16_t FatTreeSwitch::_ar_fraction = 0;
uint16_t FatTreeSwitch::_ar_sticky = FatTreeSwitch::PER_PACKET;
simtime_picosec FatTreeSwitch::_sticky_delta = timeFromUs((uint32_t)10);
double FatTreeSwitch::_ecn_threshold_fraction = 1.0;
int8_t (*FatTreeSwitch::fn)(FibEntry *, FibEntry *) = &FatTreeSwitch::compare_queuesize;

Route *FatTreeSwitch::getNextHop(Packet &pkt, BaseQueue *ingress_port) {
    vector<FibEntry *> *available_hops = _fib->getRoutes(pkt.dst());

    if (available_hops) {
        // implement a form of ECMP hashing; might need to revisit based on
        // measured performance.
        uint32_t ecmp_choice = 0;

        if (available_hops->size() > 1)
            switch (_strategy) {
            case NIX:
                abort();
            case ECMP:
                // printf("Pkt Flow ID %d - Path Id %d\n", pkt.flow_id(),
                //        pkt.pathid());
                ecmp_choice = freeBSDHash(pkt.flow_id(), pkt.pathid(), _hash_salt) % available_hops->size();
                break;
            case ADAPTIVE_ROUTING:
                if (_ar_sticky == FatTreeSwitch::PER_PACKET) {
                    ecmp_choice = adaptive_route(available_hops, fn);
                } else if (_ar_sticky == FatTreeSwitch::PER_FLOWLET) {
                    if (_flowlet_maps.find(pkt.flow_id()) != _flowlet_maps.end()) {
                        FlowletInfo *f = _flowlet_maps[pkt.flow_id()];

                        // only reroute an existing flow if its inter packet
                        // time is larger than _sticky_delta and and 50% chance
                        // happens. and (commented out) if the switch has not
                        // taken any other placement decision that we've not
                        // seen the effects of.
                        if (eventlist().now() - f->_last > _sticky_delta &&
                            /*eventlist().now() - _last_choice > _pipe->delay()
                               + BaseQueue::_update_period  &&*/
                            random() % 2 == 0) {
                            uint32_t new_route = adaptive_route(available_hops, fn);
                            if (fn(available_hops->at(f->_egress), available_hops->at(new_route)) < 0) {
                                f->_egress = new_route;
                                _last_choice = eventlist().now();
                                // cout << "Switch " << _type << ":" << _id << "
                                // choosing new path "<<  f->_egress << " for "
                                // << pkt.flow_id() << " at " <<
                                // timeAsUs(eventlist().now()) << " last is " <<
                                // timeAsUs(f->_last) << endl;
                            }
                        }
                        ecmp_choice = f->_egress;

                        f->_last = eventlist().now();
                    } else {
                        ecmp_choice = adaptive_route(available_hops, fn);
                        // cout << "Switch " << _type << ":" << getID() << "
                        // choosing first path "<<  ecmp_choice << " for " <<
                        // pkt.flow_id() << " at " <<
                        // timeAsUs(eventlist().now()) << endl;
                        _last_choice = eventlist().now();

                        _flowlet_maps[pkt.flow_id()] = new FlowletInfo(ecmp_choice, eventlist().now());
                    }
                }

                break;
            case ECMP_ADAPTIVE:
                ecmp_choice = freeBSDHash(pkt.flow_id(), pkt.pathid(), _hash_salt) % available_hops->size();
                if (random() % 100 < 50)
                    ecmp_choice = replace_worst_choice(available_hops, fn, ecmp_choice);
                break;
            case RR:
                if (_crt_route >= 5 * available_hops->size()) {
                    _crt_route = 0;
                    permute_paths(available_hops);
                }
                ecmp_choice = _crt_route % available_hops->size();
                _crt_route++;
                break;
            case RR_ECMP:
                if (_type == TOR) {
                    if (_crt_route >= 5 * available_hops->size()) {
                        _crt_route = 0;
                        permute_paths(available_hops);
                    }
                    ecmp_choice = _crt_route % available_hops->size();
                    _crt_route++;
                } else
                    ecmp_choice = freeBSDHash(pkt.flow_id(), pkt.pathid(), _hash_salt) % available_hops->size();

                break;
            }

        bool force_routing = false;
        if (force_routing && pkt.size() > 1000 && nodename() == "Switch_LowerPod_0") {
            ecmp_choice = pkt.from;
        }

        FibEntry *e = (*available_hops)[ecmp_choice];

        /*printf("Here2 %d %d@%d - Time %lu - Hops Size %d - IdxID %d - Inc ID "
               "%d - "
               "Direction"
               "%d - EgressPort %d - ECMP Choice %d -"
               "MyId "
               "%d %s\n",
               pkt.size(), pkt.from, pkt.dst(), GLOBAL_TIME / 1000,
               available_hops->size(), pkt.my_idx, pkt.inc_id,
               e->getDirection(), e->getEgressPort()->path_id(), ecmp_choice,
               id, nodename().c_str());
        printf("FROM %d / CHOICE %d / HOPS %d \n", pkt.from, ecmp_choice,
               available_hops->size());
        fflush(stdout);*/
        pkt.set_direction(e->getDirection());

        return e->getEgressPort();
    }

    // no route table entries for this destination. Add them to FIB or fail.
    if (_type == TOR) {
        if (_ft->HOST_POD_SWITCH(pkt.dst()) == _id) {
            // this host is directly connected!
            HostFibEntry *fe = _fib->getHostRoute(pkt.dst(), pkt.flow_id());
            assert(fe);
            // printf("Here %d %d\n", pkt.size(), pkt.dst());
            pkt.set_direction(DOWN);
            return fe->getEgressPort();
        } else {
            // route packet up!
            if (_uproutes)
                _fib->setRoutes(pkt.dst(), _uproutes);
            else {
                uint32_t podid, agg_min, agg_max;

                if (_ft->get_tiers() == 3) {
                    podid = 2 * _id / _ft->getK();
                    agg_min = _ft->MIN_POD_ID(podid);
                    agg_max = _ft->MAX_POD_ID(podid);
                } else {
                    agg_min = 0;
                    agg_max = _ft->getNAGG() - 1;
                }

                for (uint32_t k = agg_min; k <= agg_max; k++) {
                    Route *r = new Route();
                    r->push_back(_ft->queues_nlp_nup[_id][k]);
                    r->push_back(_ft->pipes_nlp_nup[_id][k]);
                    r->push_back(_ft->queues_nlp_nup[_id][k]->getRemoteEndpoint());
                    _fib->addRoute(pkt.dst(), r, 1, UP);

                    /*
                      FatTreeSwitch* next =
                      (FatTreeSwitch*)_ft->queues_nlp_nup[_id][k]->getRemoteEndpoint();
                      assert (next->getType()==AGG && next->getID() == k);
                    */
                }
                _uproutes = _fib->getRoutes(pkt.dst());
                permute_paths(_uproutes);
            }
        }
    } else if (_type == AGG) {
        if (_ft->get_tiers() == 2 || _ft->HOST_POD(pkt.dst()) == 2 * _id / _ft->getK()) {
            // must go down!
            // target NLP id is 2 * pkt.dst()/K
            uint32_t target_tor = _ft->HOST_POD_SWITCH(pkt.dst());
            Route *r = new Route();
            r->push_back(_ft->queues_nup_nlp[_id][target_tor]);
            r->push_back(_ft->pipes_nup_nlp[_id][target_tor]);
            r->push_back(_ft->queues_nup_nlp[_id][target_tor]->getRemoteEndpoint());

            _fib->addRoute(pkt.dst(), r, 1, DOWN);
        } else {
            // go up!
            if (_uproutes)
                _fib->setRoutes(pkt.dst(), _uproutes);
            else {
                uint32_t podpos = _id % (_ft->getK() / 2);

                for (uint32_t l = 0; l < _ft->getK() / 2 / _ft->getOS(); l++) {
                    uint32_t k = (podpos * _ft->getK() / 2 + l) / _ft->getOS();

                    uint32_t next_upper_pod =
                            _ft->MIN_POD_ID(_ft->HOST_POD(pkt.dst())) + 2 * k / (_ft->getK() / _ft->getOS());

                    /*printf("Dest %d - FTK %d - k %d - HOSTPOD %d - %d - ID %d
                    "
                           "- PoDPos %d -------- %d -> %d\n",
                           pkt.dst(), _ft->getK(), k, _ft->HOST_POD(pkt.dst()),
                           pkt.size(), _id, podpos, (_ft->HOST_POD(pkt.dst())),
                           _ft->MIN_POD_ID(_ft->HOST_POD(pkt.dst())));
                    printf("1) ID %d - K %d  2) k %d Next Upper %d\n\n", _id, k,
                           k, next_upper_pod);
                    fflush(stdout);*/

                    if (_ft->queues_nup_nc[_id][k] == NULL || _ft->queues_nc_nup[k][next_upper_pod] == NULL) {
                        // failed link, continue to next one. !
                        cout << "Skipping path with failed link AGG" << _id << "-CORE" << k << "("
                             << (_ft->queues_nup_nc[_id][k] == NULL ? "FAILED)" : " OK)");
                        cout << " CORE" << k << "-AGG" << next_upper_pod << "("
                             << (_ft->queues_nc_nup[k][next_upper_pod] == NULL ? "FAILED)" : "OK)") << endl;
                        continue;
                    }

                    Route *r = new Route();
                    r->push_back(_ft->queues_nup_nc[_id][k]);
                    r->push_back(_ft->pipes_nup_nc[_id][k]);
                    r->push_back(_ft->queues_nup_nc[_id][k]->getRemoteEndpoint());

                    /*
                      FatTreeSwitch* next =
                      (FatTreeSwitch*)_ft->queues_nup_nc[_id][k]->getRemoteEndpoint();
                      assert (next->getType()==CORE && next->getID() == k);
                    */

                    _fib->addRoute(pkt.dst(), r, 1, UP);

                    // cout << "AGG switch " << _id << " adding route to " <<
                    // pkt.dst() << " via CORE " << k << endl;
                }
                //_uproutes = _fib->getRoutes(pkt.dst());
                permute_paths(_fib->getRoutes(pkt.dst()));
            }
        }
    } else if (_type == CORE) {
        uint32_t nup = _ft->MIN_POD_ID(_ft->HOST_POD(pkt.dst())) + 2 * _id / (_ft->getK() / _ft->getOS());
        Route *r = new Route();
        // cout << "CORE switch " << _id << " adding route to " << pkt.dst() <<
        // " via AGG " << nup << endl;

        assert(_ft->queues_nc_nup[_id][nup]);
        r->push_back(_ft->queues_nc_nup[_id][nup]);
        assert(_ft->pipes_nc_nup[_id][nup]);
        r->push_back(_ft->pipes_nc_nup[_id][nup]);

        r->push_back(_ft->queues_nc_nup[_id][nup]->getRemoteEndpoint());
        _fib->addRoute(pkt.dst(), r, 1, DOWN);
    } else {
        cerr << "Route lookup on switch with no proper type: " << _type << endl;
        abort();
    }
    assert(_fib->getRoutes(pkt.dst()));

    // FIB has been filled in; return choice.
    return getNextHop(pkt, ingress_port);
};
