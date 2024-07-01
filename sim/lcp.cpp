// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-
#include "lcp.h"
#include "ecn.h"
#include "queue.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <math.h>
#include <regex>
#include <stdio.h>
#include <utility>

#define timeInf 0

// Static Parameters
int LcpSrc::jump_to = 0;
double LcpSrc::kmax_double;
bool LcpSrc::use_bts = false;
double LcpSrc::kmin_double;
std::string LcpSrc::queue_type = "composite";
std::string LcpSrc::algorithm_type = "standard_trimming";
bool LcpSrc::use_fast_drop = false;
int LcpSrc::fast_drop_rtt = 1;
bool LcpSrc::use_pacing = false;
simtime_picosec LcpSrc::pacing_delay = 0;
bool LcpSrc::do_jitter = false;
bool LcpSrc::do_exponential_gain = false;
bool LcpSrc::use_fast_increase = false;
uint64_t LcpSrc::_interdc_delay = 0;
bool LcpSrc::use_super_fast_increase = false;
int LcpSrc::target_rtt_percentage_over_base = 50;
bool LcpSrc::stop_after_quick = false;
double LcpSrc::y_gain = 1;
double LcpSrc::x_gain = 0.15;
double LcpSrc::z_gain = 1;
double LcpSrc::w_gain = 1;
double LcpSrc::quickadapt_lossless_rtt = 2.0;
bool LcpSrc::disable_case_4 = false;
bool LcpSrc::disable_case_3 = false;
double LcpSrc::starting_cwnd = 1;
double LcpSrc::bonus_drop = 1;
double LcpSrc::buffer_drop = 1.2;
int LcpSrc::ratio_os_stage_1 = 1;
double LcpSrc::decrease_on_nack = 1;
simtime_picosec LcpSrc::stop_pacing_after_rtt = 0;
int LcpSrc::reaction_delay = 1;
int LcpSrc::precision_ts = 1;
int LcpSrc::once_per_rtt = 0;
uint64_t LcpSrc::explicit_target_rtt = 0;
uint64_t LcpSrc::explicit_base_rtt = 0;
uint64_t LcpSrc::explicit_bdp = 0;
uint64_t LcpSrc::_switch_queue_size = 0;
double LcpSrc::exp_avg_ecn_value = 0.3;
double LcpSrc::exp_avg_rtt_value = 0.3;
double LcpSrc::exp_avg_alpha = 0.125;
bool LcpSrc::use_exp_avg_ecn = false;
bool LcpSrc::use_exp_avg_rtt = false;
int LcpSrc::adjust_packet_counts = 1;
int LcpSrc::freq = 1;

RouteStrategy LcpSrc::_route_strategy = NOT_SET;
RouteStrategy LcpSink::_route_strategy = NOT_SET;

LcpSrc::LcpSrc(UecLogger *logger, TrafficLogger *pktLogger, EventList &eventList, uint64_t rtt, uint64_t bdp,
               uint64_t queueDrainTime, int hops)
        : EventSource(eventList, "lcp"), _logger(logger), _flow(pktLogger) {
    _mss = Packet::data_packet_size();
    _unacked = 0;
    _nodename = "LcpSrc";

    _last_acked = 0;
    _highest_sent = 0;
    _use_good_entropies = false;
    _next_good_entropy = 0;

    _nack_rtx_pending = 0;
    current_ecn_rate = 0;

    // new CC variables
    _hop_count = hops;

    _base_rtt = ((_hop_count * LINK_DELAY_MODERN) + ((PKT_SIZE_MODERN + 64) * 8 / LINK_SPEED_MODERN * _hop_count) +
                 +(_hop_count * LINK_DELAY_MODERN) + (64 * 8 / LINK_SPEED_MODERN * _hop_count)) *
                1000;

    if (precision_ts != 1) {
        _base_rtt = (((_base_rtt + precision_ts - 1) / precision_ts) * precision_ts);
    }

    _target_rtt = _base_rtt * ((target_rtt_percentage_over_base + 1) / 100.0 + 1);

    if (precision_ts != 1) {
        _target_rtt = (((_target_rtt + precision_ts - 1) / precision_ts) * precision_ts);
    }

    _rtt = _base_rtt;
    _rto = rtt + _hop_count * queueDrainTime + (rtt * 900000);
    _rto = _base_rtt * 3;
    _rto_margin = _rtt / 8;
    _rtx_timeout = timeInf;
    _rtx_timeout_pending = false;
    _rtx_pending = false;
    _crt_path = 0;
    _flow_size = _mss * 934;
    _trimming_enabled = true;

    _next_pathid = 1;

    _bdp = (_base_rtt * LINK_SPEED_MODERN / 8) / 1000;
    _queue_size = _bdp; // Temporary
    initial_x_gain = x_gain;
    initial_z_gain = z_gain;

    if (explicit_base_rtt != 0) {
        _base_rtt = explicit_base_rtt;
        _target_rtt = explicit_target_rtt;
        bdp = explicit_bdp * 1;
    }

    // internal_stop_pacing_rtt = 0;

    _maxcwnd = bdp;
    _cwnd = starting_cwnd;
    _consecutive_low_rtt = 0;
    target_window = _cwnd;
    _target_based_received = true;

    /* printf("Link Delay %d - Link Speed %lu - Pkt Size %d - Base RTT %lu - "
           "Target RTT is %lu - BDP %lu - CWND %u - Hops %d - Stop Pacing "
           "%lu\n",
           LINK_DELAY_MODERN, LINK_SPEED_MODERN, PKT_SIZE_MODERN, _base_rtt, _target_rtt, _bdp, _cwnd, _hop_count,
           stop_pacing_after_rtt); */

    _max_good_entropies = 10; // TODO: experimental value
    _enableDistanceBasedRtx = false;
    f_flow_over_hook = nullptr;
    last_pac_change = 0;

    update_pacing_delay();

    if (queue_type == "composite_bts") {
        _bts_enabled = true;
    } else {
        _bts_enabled = false;
    }

    // LCP changes.
    _previous_rtt_ewma = timeFromMs(0);
    _current_rtt_ewma = timeFromMs(0);
    _next_measurement_seq_no = 0;
    _consecutive_good_epochs = 0;
    _time_of_next_epoch = TARGET_RTT_LOW;
    _time_of_last_qa = 0;
    saved_acked_bytes = _bdp / 2;
    _first_qa_measurement = true;

    // LCP gemini.
    _next_window_seq_no = 0;
    _current_rtt_measurement = timeFromMs(0);
}

// Add deconstructor and save data once we are done.
LcpSrc::~LcpSrc() {
    // If we are collecting specific logs
    printf("Total NACKs: %lu\n", num_trim);
    if (COLLECT_DATA) {
        // RTT
        std::string file_name = PROJECT_ROOT_PATH / ("sim/output/rtt/rtt" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFile(file_name, std::ios_base::app);

        for (const auto &p : _list_rtt) {
            MyFile << get<0>(p) << "," << get<1>(p) << "," << get<2>(p) << "," << get<3>(p) << "," << get<4>(p) << ","
                   << get<5>(p) << std::endl;
        }

        MyFile.close();

        // CWD
        file_name = PROJECT_ROOT_PATH / ("sim/output/cwd/cwd" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileCWD(file_name, std::ios_base::app);

        for (const auto &p : _list_cwd) {
            MyFileCWD << p.first << "," << p.second << std::endl;
        }

        MyFileCWD.close();

        // Unacked
        file_name = PROJECT_ROOT_PATH / ("sim/output/unacked/unacked" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileUnack(file_name, std::ios_base::app);

        for (const auto &p : _list_unacked) {
            MyFileUnack << p.first << "," << p.second << std::endl;
        }

        MyFileUnack.close();

        // NACK
        file_name = PROJECT_ROOT_PATH / ("sim/output/nack/nack" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileNack(file_name, std::ios_base::app);

        for (const auto &p : _list_nack) {
            MyFileNack << p.first << "," << p.second << std::endl;
        }

        MyFileNack.close();

        // BTS
        if (_list_bts.size() > 0) {
            file_name = PROJECT_ROOT_PATH / ("sim/output/bts/bts" + _name + "_" + std::to_string(tag) + ".txt");
            std::ofstream MyFileBTS(file_name, std::ios_base::app);

            for (const auto &p : _list_bts) {
                MyFileBTS << p.first << "," << p.second << std::endl;
            }

            MyFileBTS.close();
        }

        // Acked Bytes
        file_name = PROJECT_ROOT_PATH / ("sim/output/acked/acked" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileAcked(file_name, std::ios_base::app);

        for (const auto &p : _list_acked_bytes) {
            MyFileAcked << p.first << "," << p.second << std::endl;
        }

        MyFileAcked.close();

        // Acked ECN
        file_name = PROJECT_ROOT_PATH / ("sim/output/ecn_rtt/ecn_rtt" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileEcnRTT(file_name, std::ios_base::app);

        for (const auto &p : _list_ecn_rtt) {
            MyFileEcnRTT << p.first << "," << p.second << std::endl;
        }

        MyFileEcnRTT.close();

        // ECN Received
        file_name = PROJECT_ROOT_PATH / ("sim/output/ecn/ecn" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileEcnReceived(file_name, std::ios_base::app);

        for (const auto &p : _list_ecn_received) {
            MyFileEcnReceived << p.first << "," << p.second << std::endl;
        }

        MyFileEcnReceived.close();

        // Acked Trimmed
        file_name =
                PROJECT_ROOT_PATH / ("sim/output/trimmed_rtt/trimmed_rtt" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileTrimmedRTT(file_name, std::ios_base::app);

        for (const auto &p : _list_trimmed_rtt) {
            MyFileTrimmedRTT << p.first << "," << p.second << std::endl;
        }

        MyFileTrimmedRTT.close();

        // Fast Increase
        file_name = PROJECT_ROOT_PATH / ("sim/output/fasti/fasti" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileFastInc(file_name, std::ios_base::app);

        for (const auto &p : _list_fast_increase_event) {
            MyFileFastInc << p.first << "," << p.second << std::endl;
        }

        MyFileFastInc.close();

        // Fast Decrease
        file_name = PROJECT_ROOT_PATH / ("sim/output/fastd/fastd" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileFastDec(file_name, std::ios_base::app);

        for (const auto &p : _list_fast_decrease) {
            MyFileFastDec << p.first << "," << p.second << std::endl;
        }

        MyFileFastDec.close();

        // Medium Increase
        file_name = PROJECT_ROOT_PATH / ("sim/output/mediumi/mediumi" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileMediumInc(file_name, std::ios_base::app);

        for (const auto &p : _list_medium_increase_event) {
            MyFileMediumInc << p.first << "," << p.second << std::endl;
        }

        MyFileMediumInc.close();

        // Case 1
        file_name = PROJECT_ROOT_PATH / ("sim/output/case1/case1" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileCase1(file_name, std::ios_base::app);

        for (const auto &p : count_case_1) {
            MyFileCase1 << p.first << "," << p.second << std::endl;
        }

        MyFileCase1.close();

        // Case 2
        file_name = PROJECT_ROOT_PATH / ("sim/output/case2/case2" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileCase2(file_name, std::ios_base::app);

        for (const auto &p : count_case_2) {
            MyFileCase2 << p.first << "," << p.second << std::endl;
        }

        MyFileCase2.close();

        // Case 3
        file_name = PROJECT_ROOT_PATH / ("sim/output/case3/case3" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileCase3(file_name, std::ios_base::app);

        for (const auto &p : count_case_3) {
            MyFileCase3 << p.first << "," << p.second << std::endl;
        }

        MyFileCase3.close();

        // Case 4
        file_name = PROJECT_ROOT_PATH / ("sim/output/case4/case4" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileCase4(file_name, std::ios_base::app);

        for (const auto &p : count_case_4) {
            MyFileCase4 << p.first << "," << p.second << std::endl;
        }

        MyFileCase4.close();

        // Sending Rate
        file_name = PROJECT_ROOT_PATH /
                    ("sim/output/sending_rate/sending_rate" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileSendingRate(file_name, std::ios_base::app);

        for (const auto &p : list_sending_rate) {
            MyFileSendingRate << p.first << "," << p.second << std::endl;
        }

        MyFileSendingRate.close();

        // ECN RATE
        file_name = PROJECT_ROOT_PATH / ("sim/output/ecn_rate/ecn_rate" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileECNRate(file_name, std::ios_base::app);

        for (const auto &p : list_ecn_rate) {
            MyFileECNRate << p.first << "," << p.second << std::endl;
        }

        MyFileECNRate.close();
    }
}

void LcpSrc::update_pacing_delay() {
    bool is_time_to_update = (last_pac_change == 0) || ((eventlist().now() - last_pac_change) > _base_rtt / 20);
    cout << "PaceDelayChange: Is it time to update? " << is_time_to_update << " at " << GLOBAL_TIME / 1000 << endl;
    cout << "PaceDelayChange: Last change was " << eventlist().now() - last_pac_change << " ago at " << GLOBAL_TIME / 1000 << endl;
    if (LCP_USE_PACING && is_time_to_update) {
        pacing_delay = (((double)_mss) / (((double)_cwnd) / (_base_rtt / 1000.0))) * (1.0 - LCP_PACING_BONUS);
        cout << "PaceDelayChange: Setting the pacing delay to: " << pacing_delay << " at " << GLOBAL_TIME / 1000
             << " with cwnd: " << _cwnd << " and mss: " << _mss << endl;
            pacing_delay *= 1000; // ps
        if (generic_pacer != NULL) {
            generic_pacer->cancel();
            last_pac_change = eventlist().now();
        } else {
            generic_pacer = new LcpSmarttPacer(eventlist(), *this);
            pacer_start_time = eventlist().now();
            last_pac_change = eventlist().now();
        }
    }
}

// Start the flow
void LcpSrc::doNextEvent() { startflow(); }

// Triggers for connection matrixes
void LcpSrc::set_end_trigger(Trigger &end_trigger) { _end_trigger = &end_trigger; }

// Update Network Parameters
void LcpSrc::updateParams(uint64_t switch_latency_ns) {
    if (src_dc != dest_dc) {
        _hop_count = 9;
        _base_rtt = 2 * (_hop_count - 1) * (LINK_DELAY_MODERN + switch_latency_ns)   + // All DCN latencies.
                    2 * (_interdc_delay / 1000 + switch_latency_ns)                  + // InterDC latencies.
                    _hop_count * PKT_SIZE_MODERN * 8 / LINK_SPEED_MODERN             + // Packet transmission delays.
                    _hop_count * 64 * 8 / LINK_SPEED_MODERN;                           // Ack transmission delays. 
                    
        
        // ((((_hop_count - 1) * LINK_DELAY_MODERN) + (_interdc_delay / 1000) * 2) +
        //              ((PKT_SIZE_MODERN + 64) * 8 / LINK_SPEED_MODERN * _hop_count) + +(_hop_count * LINK_DELAY_MODERN) +
        //              (64 * 8 / LINK_SPEED_MODERN * _hop_count)) *
        //             1000;
        cout << "Base RTT (9): " << _base_rtt << endl;
        cout << "    LINK_DELAY_MODERN: " << LINK_DELAY_MODERN << endl;
        cout << "    PKT_SIZE_MODERN: " << PKT_SIZE_MODERN << endl;
        cout << "    LINK_SPEED_MODERN: " << LINK_SPEED_MODERN << endl;
        cout << "    switch_latency_ns: " << switch_latency_ns << endl;
        cout << "    _interdc_delay: " << _interdc_delay << endl;
    } else {
        _hop_count = 6;
        _base_rtt = ((_hop_count * LINK_DELAY_MODERN) + ((PKT_SIZE_MODERN + 64) * 8 / LINK_SPEED_MODERN * _hop_count) +
                     +(_hop_count * LINK_DELAY_MODERN) + (64 * 8 / LINK_SPEED_MODERN * _hop_count)) *
                    1000;
        cout << "Base RTT (6): " << _base_rtt << endl;
    }

    if (precision_ts != 1) {
        _base_rtt = (((_base_rtt + precision_ts - 1) / precision_ts) * precision_ts);
    }

    int time_to_drain_queue = _switch_queue_size * 8 / LINK_SPEED_MODERN * 1000;

    _target_rtt = _base_rtt + time_to_drain_queue * ((target_rtt_percentage_over_base + 1) / 100.0 + 1);

    if (precision_ts != 1) {
        _target_rtt = (((_target_rtt + precision_ts - 1) / precision_ts) * precision_ts);
    }

    _rtt = _base_rtt;
    _rto = _base_rtt * 900000;
    _rto = _base_rtt * 3;
    _rto_margin = _rtt / 8;
    _rtx_timeout = timeInf;
    _rtx_timeout_pending = false;
    _rtx_pending = false;
    _crt_path = 0;
    _trimming_enabled = true;

    _next_pathid = 1;
    next_window_end = eventlist().now();
    last_ecn_seen = eventlist().now();

    _bdp = (_base_rtt * LINK_SPEED_MODERN / 8) / 1000;

        if (LCP_DELTA == 1) {
        LCP_DELTA = _bdp * 0.05;
    }
    BAREMETAL_RTT = _base_rtt * 1000;
    TARGET_RTT_LOW = BAREMETAL_RTT * 1.05;
    TARGET_RTT_HIGH = BAREMETAL_RTT * 1.1;

    LCP_GEMINI_TARGET_QUEUEING_LATENCY = 0.1 * BAREMETAL_RTT;
    LCP_GEMINI_BETA = (double)LCP_GEMINI_TARGET_QUEUEING_LATENCY / ((double) LCP_GEMINI_TARGET_QUEUEING_LATENCY + (double) BAREMETAL_RTT);

    double H = 1.2 * pow(10, -7);
    cout << "Double of H: " << H * (double) _bdp << endl;
    LCP_GEMINI_H = max(min((H * (double) _bdp), 5.0), 0.1) * (double) PKT_SIZE_MODERN;
    if (LCP_GEMINI_H == 0) {
        cout << "H is 0, raw value is: " << H * (double) _bdp << " exiting..." << endl;
        exit(-1);
    }

    cout << "==============================" << endl;
    cout << "Link speed: " << LINK_SPEED_MODERN << " Gbps" << endl;
    cout << "Baremetal RTT: " << BAREMETAL_RTT / 1000000 << " us" << endl;
    cout << "Target RTT Low: " << TARGET_RTT_LOW / 1000000 << " us" << endl;
    cout << "Target RTT High: " << TARGET_RTT_HIGH / 1000000 << " us" << endl;
    cout << "MSS: " << PKT_SIZE_MODERN << " Bytes" << endl;
    cout << "BDP: " << _bdp / 1000 << " KB" << endl;
    cout << "Starting cwnd: " << starting_cwnd << " Bytes" << endl;
    cout << "Queue Size: " << _queue_size << " Bytes" << endl;
    cout << "Delta: " << LCP_DELTA << endl;
    cout << "Beta: " << LCP_BETA << endl;
    cout << "Alpha: " << LCP_ALPHA << endl;
    cout << "Gamma: " << LCP_GAMMA << endl;
    cout << "K: " << LCP_K << endl;
    cout << "Fast Increase Threshold: " << LCP_FAST_INCREASE_THRESHOLD << endl;
    cout << "Use Quick Adapt: " << LCP_USE_QUICK_ADAPT << endl;
    cout << "Use Pacing: " << LCP_USE_PACING << endl;
    cout << "Use Fast Increase: " << LCP_USE_FAST_INCREASE << endl;
    cout << "Pacing Bonus: " << LCP_PACING_BONUS << endl;
    cout << "Use Min RTT: " << LCP_USE_MIN_RTT << endl;
    cout << "Use Aggressive Decrease: " << LCP_USE_AGGRESSIVE_DECREASE << endl;
    cout << "Gemini Queueing Delay Threshold: " << LCP_GEMINI_TARGET_QUEUEING_LATENCY / 1000000 << " us" << endl;
    cout << "Gemini Beta: " << LCP_GEMINI_BETA << endl;
    cout << "Gemini H: " << LCP_GEMINI_H << endl;
    cout << "==============================" << endl;


    // _queue_size = _bdp; // Temporary
    initial_x_gain = x_gain;
    initial_z_gain = z_gain;

    _maxcwnd = _bdp;
    // _cwnd = _bdp;

    _consecutive_low_rtt = 0;
    target_window = _cwnd;
    _target_based_received = true;

    tracking_period = 300000 * 1000;
    if (_hop_count == 6) {
        ecn_rate_period = _base_rtt * 1;
        initial_x_gain /= 1;
        initial_z_gain /= 1;
    } else if (_hop_count == 9) {
        ecn_rate_period = _base_rtt / 5;
        initial_x_gain *= 1;
        initial_z_gain *= 1;
    }

    qa_period = _base_rtt / freq;
    qa_mult = _base_rtt / qa_period;
    x_gain_up = min((_bdp / 100 * initial_x_gain) / _mss, (_switch_queue_size * 5.0 / 100) / _mss);
    /* printf("QA MULT is %d\n", qa_mult); */

    near_base_rtt = _base_rtt * 1.05;

    printf("UPDATING VALUES - Link Delay %d (InterDC %lu) - Link Speed %lu - "
           "Pkt Size %d - "
           "Base RTT %lu - "
           "Target RTT is %lu - Drain Queue %lu - BDP %lu - CWND %u - Queue "
           "Size %lu - Hops %d - Stop Pacing "
           "%lu\n",
           LINK_DELAY_MODERN, _interdc_delay / 1000, LINK_SPEED_MODERN, PKT_SIZE_MODERN, _base_rtt, _target_rtt,
           time_to_drain_queue, _bdp, _cwnd, _switch_queue_size, _hop_count, stop_pacing_after_rtt);
    fflush(stdout);
    _max_good_entropies = 10; // TODO: experimental value
    _enableDistanceBasedRtx = false;
    last_pac_change = 0;

    update_pacing_delay();
}

std::size_t LcpSrc::get_sent_packet_idx(uint32_t pkt_seqno) {
    for (std::size_t i = 0; i < _sent_packets.size(); ++i) {
        if (pkt_seqno == _sent_packets[i].seqno) {
            return i;
        }
    }
    return _sent_packets.size();
}

void LcpSrc::update_rtx_time() {
    _rtx_timeout = timeInf;
    for (const auto &sp : _sent_packets) {
        auto timeout = sp.timer;
        if (!sp.acked && !sp.nacked && !sp.timedOut && (timeout < _rtx_timeout || _rtx_timeout == timeInf)) {
            _rtx_timeout = timeout;
        }
    }
}

void LcpSrc::mark_received(UecAck &pkt) {
    // cummulative ack
    if (pkt.seqno() == 1) {
        while (!_sent_packets.empty() && (_sent_packets[0].seqno <= pkt.ackno() || _sent_packets[0].acked)) {
            _sent_packets.erase(_sent_packets.begin());
        }
        update_rtx_time();
        return;
    }
    if (_sent_packets.empty() || _sent_packets[0].seqno > pkt.ackno()) {
        // duplicate ACK -- since we support OOO, this must be caused by
        // duplicate retransmission
        return;
    }
    auto i = get_sent_packet_idx(pkt.seqno());
    if (i == 0) {
        // this should not happen because of cummulative acks, but
        // shouldn't cause harm either
        do {
            _sent_packets.erase(_sent_packets.begin());
        } while (!_sent_packets.empty() && _sent_packets[0].acked);
    } else {
        assert(i < _sent_packets.size());
        auto timer = _sent_packets[i].timer;
        auto seqno = _sent_packets[i].seqno;
        auto nacked = _sent_packets[i].nacked;
        _sent_packets[i] = LcpSentPacket(timer, seqno, true, false, false);
        if (nacked) {
            --_nack_rtx_pending;
        }
        _last_acked = seqno + _mss - 1;
        if (_enableDistanceBasedRtx) {
            bool trigger = true;
            // TODO: this could be optimized with counters or bitsets,
            // but I'm doing this the simple way to avoid bugs while
            // we don't need the optimizations
            for (std::size_t k = 1; k < _sent_packets.size() / 2; ++k) {
                if (!_sent_packets[k].acked) {
                    trigger = false;
                    break;
                }
            }
            if (trigger) {
                // TODO: what's the proper way to act if this packet was
                // NACK'ed? Not super relevant right now as we are not enabling
                // this feature anyway
                _sent_packets[0].timer = eventlist().now();
                _rtx_timeout_pending = true;
            }
        }
    }
    update_rtx_time();
}

void LcpSrc::add_ack_path(const Route *rt) {
    for (auto &r : _good_entropies) {
        if (r == rt) {
            return;
        }
    }
    if (_good_entropies.size() < _max_good_entropies) {
        _good_entropies.push_back(rt);
    } else {
        _good_entropies[_next_good_entropy] = rt;
        ++_next_good_entropy;
        _next_good_entropy %= _max_good_entropies;
    }
}

void LcpSrc::set_traffic_logger(TrafficLogger *pktlogger) { _flow.set_logger(pktlogger); }

void LcpSrc::reduce_cwnd(uint64_t amount) {
    // if (_cwnd >= amount + _mss) {
    //     _cwnd -= amount * 1;
    // } else {
    //     _cwnd = _mss;
    // }
    (void)0;
}

void LcpSrc::reduce_unacked(uint64_t amount) {
    if (_unacked >= amount) {
        _unacked -= amount;
    } else {
        _unacked = 0;
    }
}

void LcpSrc::check_limits_cwnd() {
    // Upper Limit
    if (_cwnd > _maxcwnd) {
        _cwnd = _maxcwnd;
    }
    // Lower Limit
    if (_cwnd < _mss) {
        _cwnd = _mss;
    }

    update_pacing_delay();
}

void LcpSrc::resetQACounting() {
    if (!need_quick_adapt) {
        next_window_end = eventlist().now() + qa_period;
        bts_received = 0;
        acked_bytes = 0;
        tot_pkt_seen_qa = 0;
        need_quick_adapt = true;
        printf("%d resetted QA at %lu\n", from, eventlist().now() / 1000);
    }
}

void LcpSrc::quick_adapt_drop() {
    // Update window and ignore count
    printf("Before Update Saved CWD is %lu \n", saved_acked_bytes);
    if (send_size <= _bdp) {
        // saved_acked_bytes =
        //         saved_acked_bytes * (_bdp / (double)send_size);
        printf("BDP %lu - Send Size %lu - Ratio %f\n", _bdp, send_size,
                (_bdp / (double)send_size));
    }
    printf("After Update Saved CWD is %lu \n", saved_acked_bytes);
    _cwnd = max((double)(saved_acked_bytes * bonus_drop),
                (double)_mss); // 1.5 is the amount of target_rtt over
                                // base_rtt. Simplified here for this
                                // code share.
    _list_fast_decrease.push_back(
                    std::make_pair(eventlist().now() / 1000, 1));

    check_limits_cwnd();
}

void LcpSrc::quick_adapt(bool trimmed) {

    if (eventlist().now() >= next_window_end) {
        previous_window_end = next_window_end;
        if (_first_qa_measurement) {
            _first_qa_measurement = false;
        } else {
            saved_acked_bytes = acked_bytes;
        }

        cout << "QADEBUG: Acked bytes " << saved_acked_bytes << " time since last qa: " << eventlist().now() - _time_of_last_qa << endl;
        _time_of_last_qa = eventlist().now();

        acked_bytes = 0;
        next_window_end = eventlist().now() + _base_rtt;
    }
}

// void LcpSrc::quick_adapt(bool trimmed) {

//     if (!use_fast_drop) {
//         return;
//     }

//     if (_flow_size < _bdp * 10.0 / 100) {
//         return;
//     }

//     double multiplier_small_flow = 1;
//     if (_flow_size < _bdp) {
//         multiplier_small_flow = _bdp / (double)_flow_size;
//         // acked_bytes = acked_bytes * multiplier_small_flow;
//     }

//     if (eventlist().now() >= next_window_end) {
//         printf("Just updated %d at %lu vs %lu - %lu %lu\n", from, eventlist().now() / 1000, previous_window_end / 1000,
//                acked_bytes, acked_bytes);
//         previous_window_end = next_window_end;
//         saved_acked_bytes = acked_bytes;

//         qa_window_start = eventlist().now();

//         next_window_end = eventlist().now() + qa_period;
//         if (algorithm_type == "intersmartt" && _hop_count > 6) {
//             next_window_end = eventlist().now() + qa_period;
//         }
//         // Enable Fast Drop
//         acked_bytes += 1;
//         double rate_bts = bts_received / (double)tot_pkt_seen_qa * 100;

//         /* printf("Considering QA %d - BTS Rate %f - Cond: %d %d - Acked %d - Window End %lu - Time Passed %lu\n",
//            from, rate_bts, trimmed, need_quick_adapt, acked_bytes, next_window_end / 1000, (eventlist().now() -
//            qa_window_start) / 1000); */
//         bts_received = 0;
//         acked_bytes = 0;
//         tot_pkt_seen_qa = 0;

//         if (rate_bts > 3 && use_bts) {
//             return;
//         }

//         /* printf("From %d considering QA at %lu -- %d %d %lu\n", from, eventlist().now() / 1000, trimmed,
//         need_quick_adapt, previous_window_end / 1000); */

//         if ((need_quick_adapt) && previous_window_end != 0 && eventlist().now() > _flow_start_time + _base_rtt * 1.1) {

//             if (eventlist().now() < ignore_for_time) {
//                 return;
//             }

//             if (eventlist().now() > next_qa) {
//                 next_qa = eventlist().now() + _base_rtt * 3;
//             } else {
//                 need_quick_adapt = false;
//                 return;
//             }

//             // Edge case where we get here receiving a packet a long time after
//             // the last one (>> base_rtt). Should not matter in most cases.
//             /*saved_acked_bytes =
//                     saved_acked_bytes *
//                     ((double)_base_rtt /
//                      (eventlist().now() - previous_window_end + _base_rtt));*/

//             // Update window and ignore count
//             printf("Before Update Saved CWD is %lu \n", saved_acked_bytes);
//             if (send_size <= _bdp) {
//                 // saved_acked_bytes =
//                 //         saved_acked_bytes * (_bdp / (double)send_size);
//                 printf("BDP %lu - Send Size %lu - Ratio %f\n", _bdp, send_size, (_bdp / (double)send_size));
//             }

//             double mult_fact = (saved_acked_bytes / (double)old_sent_bytes_previous_window);

//             /* _cwnd = max((double)(saved_acked_bytes * bonus_drop * mult_fact),
//                         (double)_mss); // 1.5 is the amount of target_rtt over
//             // base_rtt. Simplified here for this
//             // code share. */
//             /* printf("Testing Drop: Saved %d - Sent %lu %lu - %f\n", saved_acked_bytes, sent_bytes_previous_window,
//                    old_sent_bytes_previous_window, old_sent_bytes_previous_window / (double)saved_acked_bytes);
//             _cwnd = max((double)(_cwnd * bonus_drop * mult_fact),
//                         (double)_mss); // 1.5 is the amount of target_rtt over
//                                        // base_rtt. Simplified here for this
//                                        // code share. */

//             double increase_by = 1;
//             if (_flow_size < _bdp) {
//                 increase_by = _bdp / (double)((_flow_size * 1.0) + _switch_queue_size);
//             }

//             printf("Increase By %f - Queue Size %lu - Flow Size %lu\n", increase_by, _switch_queue_size, _flow_size);

//             _cwnd = max((double)(saved_acked_bytes * bonus_drop * increase_by * qa_mult),
//                         (double)_mss); // 1.5 is the amount of target_rtt over
//             last_ecn_seen = eventlist().now() + _base_rtt * 1;
//             printf("After Update Saved CWD is %lu \n", _cwnd);

//             if (eventlist().now() < _base_rtt * 5 && jump_to != 0) {
//                 int coin = rand() % 2;
//                 int extra = rand() % (jump_to / 10);
//                 if (jump_to > 1200000) {
//                     extra /= 100;
//                 } else if (jump_to > 1000000) {
//                     extra /= 3;
//                 }

//                 if (coin % 2 == 0) {
//                     _cwnd = jump_to + extra;
//                 } else {
//                     _cwnd = jump_to - extra / 2;
//                 }
//             }
//             ignore_for = (get_unacked() / (double)_mss);
//             tried_qa = true;
//             if (algorithm_type == "intersmartt" && _hop_count > 6) {
//                 _cwnd = _cwnd = max((double)(saved_acked_bytes * bonus_drop * qa_mult), (double)_mss);
//                 ignore_for = (get_unacked() / (double)_mss) * 1.2;
//             } else if (_hop_count > 6) {
//                 ignore_for = (get_unacked() / (double)_mss) * 2.2;
//             }
//             ignore_for_time = eventlist().now() + _base_rtt * 1;

//             if (_hop_count > 6) {
//                 // ignore_for = 10;
//             }

//             printf("Ignoring for %d packets\n", ignore_for);
//             check_limits_cwnd();

//             // Reset counters, update logs.
//             count_received = 0;
//             need_quick_adapt = false;
//             _list_fast_decrease.push_back(std::make_pair(eventlist().now() / 1000, 1));

//             // Update x_gain after large incasts. We want to limit its effect if
//             // we move to much smaller windows.
//             // x_gain = min(initial_x_gain,
//             //             (_queue_size / 5.0) / (_mss * ((double)_bdp /
//             //             _cwnd)));
//             // printf("New X Gain is %f\n", x_gain);
//             did_qa = true;
//             qa_count++;
//             pause_send = false;
//             last_qa_event = eventlist().now();

//             pacing_delay = (4160 * 8) / ((_cwnd * 8.0) / (_base_rtt / 1000.0));
//             //  pacing_delay -= (4160 * 8 / 80);
//             /* printf("Setting the pacing delay update %d %lu to %lu at %lu\n", _cwnd, (_base_rtt / 1000),
//                pacing_delay, GLOBAL_TIME / 1000); */
//             if (use_pacing && generic_pacer != NULL) {
//                 pacing_delay *= 1000; // ps
//                 generic_pacer->cancel();
//             }

//             // generic_pacer->schedule_send(pacing_delay);
//             last_pac_change = eventlist().now();

//             // Print

//             total_pkt = 0;
//             total_nack = 0;
//             printf("Previous and Current Window %lu %lu\n", previous_window_end / 1000, next_window_end / 1000);
//             printf("Using Fast Drop2 - Flow %d@%d@%d, Ecn %d, CWND %d, "
//                    "Saved "
//                    "Acked %d (dropping to %f - bonus1  %f -> %f and "
//                    "%f) - Multiplier %f "
//                    "Previous "
//                    "Window %lu - Next "
//                    "Window %lu// "
//                    "Time "
//                    "%lu\n",
//                    this->from, this->to, tag, 1, _cwnd, saved_acked_bytes,
//                    max((double)(saved_acked_bytes * bonus_drop), saved_acked_bytes * bonus_drop + _mss), bonus_drop,
//                    (saved_acked_bytes * bonus_drop), multiplier_small_flow, (saved_acked_bytes * bonus_drop + _mss),
//                    previous_window_end / 1000, next_window_end / 1000, eventlist().now() / 1000);
//             printf("Multiplier is %f - Dropping to %f\n", multiplier_small_flow,
//                    multiplier_small_flow * saved_acked_bytes);
//             printf("Sent Bytes %lu - Mult %f\n", sent_bytes_previous_window,
//                    sent_bytes_previous_window / (double)saved_acked_bytes);
//         }
//         old_sent_bytes_previous_window = sent_bytes_previous_window;
//         sent_bytes_previous_window = 0;
//     }
// }

void LcpSrc::processNack(UecNack &pkt) {

    num_trim++;
    count_trimmed_in_rtt++;
    consecutive_nack++;
    trimmed_last_rtt++;
    consecutive_good_medium = 0;
    acked_bytes += 64;
    saved_trimmed_bytes += 64;

    // printf("Just NA CK from %d at %lu - %d\n", from, eventlist().now() / 1000, pkt.is_failed);
    last_ecn_seen = eventlist().now();
    last_phantom_increase = eventlist().now();

    // resetQACounting();

    if (algorithm_type == "intersmartt" || algorithm_type == "intersmartt_test") {
        /* printf("Processing Nack - %f %f\n", current_ecn_rate, previous_ecn_rate); */
        // adjust_window(0, true, 0);
        /* printf("Exit Processing Nack - %f %f\n", current_ecn_rate, previous_ecn_rate); */
    }

    // if (eventlist().now() > ignore_for_time) {
    //     reduce_cwnd(uint64_t(_mss * decrease_on_nack));
    // }

    if (use_fast_drop) {
        if (count_received >= ignore_for) {
            if (eventlist().now() > next_qa) {
                need_quick_adapt = true;
                quick_adapt(true);
            }
            if (generic_pacer != NULL) {
                generic_pacer->cancel();
            }
        }
    }

    check_limits_cwnd();

    _list_cwd.push_back(std::make_pair(eventlist().now() / 1000, _cwnd));
    _consecutive_no_ecn = 0;
    _consecutive_low_rtt = 0;
    _received_ecn.push_back(std::make_tuple(eventlist().now(), true, _mss, _target_rtt + 10000));

    if (!pkt.is_failed) {
        _list_nack.push_back(std::make_pair(eventlist().now() / 1000, 1));
    }

    // mark corresponding packet for retransmission
    auto i = get_sent_packet_idx(pkt.seqno());
    assert(i < _sent_packets.size());

    assert(!_sent_packets[i].acked); // TODO: would it be possible for a packet
                                     // to receive a nack after being acked?
    if (!_sent_packets[i].nacked) {
        // ignore duplicate nacks for the same packet
        _sent_packets[i].nacked = true;
        ++_nack_rtx_pending;
    }

    bool success = resend_packet(i);
    if (!_rtx_pending && !success) {
        _rtx_pending = true;
    }
    send_packets();
}

void LcpSrc::simulateTrimEvent(UecAck &pkt) {

    /* consecutive_good_medium = 0;

    if (count_received >= ignore_for) {
        need_quick_adapt = true;
    }

    // Reduce Window Or Do Fast Drop
    if (use_fast_drop) {
        if (count_received >= ignore_for) {
            quick_adapt(true);
        }
    }

    check_limits_cwnd(); */
}

/* Choose a route for a particular packet */
int LcpSrc::choose_route() {

    switch (_route_strategy) {
    case PULL_BASED: {
        /* this case is basically SCATTER_PERMUTE, but avoiding bad paths. */

        assert(_paths.size() > 0);
        if (_paths.size() == 1) {
            // special case - no choice
            return 0;
        }
        // otherwise we've got a choice
        _crt_path++;
        if (_crt_path == _paths.size()) {
            // permute_paths();
            _crt_path = 0;
        }
        uint32_t path_id = _path_ids[_crt_path];
        _avoid_score[path_id] = _avoid_ratio[path_id];
        int ctr = 0;
        while (_avoid_score[path_id] > 0 /* && ctr < 2*/) {
            printf("as[%d]: %d\n", path_id, _avoid_score[path_id]);
            _avoid_score[path_id]--;
            ctr++;
            // re-choosing path
            cout << "re-choosing path " << path_id << endl;
            _crt_path++;
            if (_crt_path == _paths.size()) {
                // permute_paths();
                _crt_path = 0;
            }
            path_id = _path_ids[_crt_path];
            _avoid_score[path_id] = _avoid_ratio[path_id];
        }
        // cout << "AS: " << _avoid_score[path_id] << " AR: " <<
        // _avoid_ratio[path_id] << endl;
        assert(_avoid_score[path_id] == 0);
        break;
    }
    case SCATTER_RANDOM:
        // ECMP
        assert(_paths.size() > 0);
        _crt_path = random() % _paths.size();
        break;
    case SCATTER_PERMUTE:
    case SCATTER_ECMP:
        // Cycle through a permutation.  Generally gets better load balancing
        // than SCATTER_RANDOM.
        _crt_path++;
        assert(_paths.size() > 0);
        if (_crt_path / 1 == _paths.size()) {
            // permute_paths();
            _crt_path = 0;
        }
        break;
    case ECMP_FIB:
        // Cycle through a permutation.  Generally gets better load balancing
        // than SCATTER_RANDOM.
        _crt_path++;
        if (_crt_path == _paths.size()) {
            // permute_paths();
            _crt_path = 0;
        }
        break;
    case ECMP_RANDOM2_ECN: {

        if (false) {
            uint64_t allpathssizes = _mss * _paths.size();
            if (_highest_sent < max(_maxcwnd, (uint64_t)1)) {
                curr_entropy++;
                _crt_path++;
                if (_crt_path == _paths.size()) {
                    _crt_path = 0;
                }
            } else {
                if (!_good_entropies_list.empty()) {
                    _crt_path = _good_entropies_list.back();
                    //_good_entropies_list.pop_back();
                } else {
                    curr_entropy++;
                    _crt_path = curr_entropy % _paths.size();
                }
            }
            break;
        } else {
            uint64_t allpathssizes = _mss * _paths.size();
            if (_highest_sent < max(_maxcwnd, (uint64_t)1)) {
                /*printf("Trying this for %d // Highest Sent %d - cwnd %d - "
                       "allpathsize %d\n",
                       from, _highest_sent, _maxcwnd, allpathssizes);*/
                _crt_path++;
                // printf("Trying this for %d\n", from);
                if (_crt_path == _paths.size()) {
                    // permute_paths();
                    _crt_path = 0;
                }
            } else {
                if (_next_pathid == -1) {
                    assert(_paths.size() > 0);
                    _crt_path = random() % _paths.size();
                } else {
                    _crt_path = _next_pathid;
                }
            }
            break;
        }
    }
    case ECMP_RANDOM_ECN: {
        _crt_path = from;
        // _crt_path = (random() * 1) % _paths.size();
        break;
    }
    case ECMP_FIB_ECN: {
        // Cycle through a permutation, but use ECN to skip paths
        while (1) {
            _crt_path++;
            if (_crt_path == _paths.size()) {
                // permute_paths();
                _crt_path = 0;
            }
            if (_path_ecns[_path_ids[_crt_path]] > 0) {
                _path_ecns[_path_ids[_crt_path]]--;
            } else {
                // eventually we'll find one that's zero
                break;
            }
        }
        break;
    }
    case SINGLE_PATH:
        abort(); // not sure if this can ever happen - if it can, remove this
                 // line
    case REACTIVE_ECN:
        return _crt_path;
    case NOT_SET:
        abort(); // shouldn't be here at all
    default:
        abort();
        break;
    }

    return _crt_path / 1;
}

int LcpSrc::next_route() {
    // used for reactive ECN.
    // Just move on to the next path blindly
    assert(_route_strategy == REACTIVE_ECN);
    _crt_path++;
    assert(_paths.size() > 0);
    if (_crt_path == _paths.size()) {
        // permute_paths();
        _crt_path = 0;
    }
    return _crt_path;
}

void LcpSrc::processBts(UecPacket *pkt) {
    num_trim++;
    count_trimmed_in_rtt++;
    consecutive_nack++;
    trimmed_last_rtt++;
    consecutive_good_medium = 0;
    acked_bytes += 64;
    saved_trimmed_bytes += 64;

    /* printf("Just NA CK from %d at %lu - %d\n", from, eventlist().now() / 1000, pkt->is_failed); */
    /* printf("BTS1 %d at %lu - %d\n", from, eventlist().now() / 1000, _cwnd); */
    reduce_cwnd(uint64_t(_mss * decrease_on_nack));
    /* printf("BTS2 %d at %lu - %d\n", from, eventlist().now() / 1000, _cwnd); */
    // Reduce Window Or Do Fast Drop
    if (count_received >= ignore_for) {
        if (eventlist().now() > next_qa) {
            need_quick_adapt = true;
            quick_adapt(true);
        }
    }

    last_ecn_seen = eventlist().now();
    last_phantom_increase = eventlist().now();

    _list_nack.push_back(std::make_pair(eventlist().now() / 1000, 1));

    // if (algorithm_type == "intersmartt" || algorithm_type == "intersmartt_test") {
    //     adjust_window(0, true, 0);
    // }
    check_limits_cwnd();

    _list_cwd.push_back(std::make_pair(eventlist().now() / 1000, _cwnd));
    _consecutive_no_ecn = 0;
    _consecutive_low_rtt = 0;
    _received_ecn.push_back(std::make_tuple(eventlist().now(), true, _mss, _target_rtt + 10000));

    // mark corresponding packet for retransmission
    auto i = get_sent_packet_idx(pkt->seqno());
    assert(i < _sent_packets.size());

    assert(!_sent_packets[i].acked); // TODO: would it be possible for a packet
                                     // to receive a nack after being acked?
    if (!_sent_packets[i].nacked) {
        // ignore duplicate nacks for the same packet
        _sent_packets[i].nacked = true;
        ++_nack_rtx_pending;
    }

    bool success = resend_packet(i);
    if (!_rtx_pending && !success) {
        _rtx_pending = true;
    }
    send_packets();
}

void LcpSrc::processAck(UecAck &pkt, bool force_marked) {
    UecAck::seq_t seqno = pkt.ackno();
    simtime_picosec ts = pkt.ts();

    consecutive_nack = 0;
    bool marked = pkt.flags() & ECN_ECHO; // ECN was marked on data packet and echoed on ACK

    if (COLLECT_DATA && marked) {
        std::string file_name = PROJECT_ROOT_PATH / ("sim/output/ecn/ecn" + std::to_string(pkt.from) + "_" +
                                                     std::to_string(pkt.to) + ".txt");
        std::ofstream MyFile(file_name, std::ios_base::app);

        MyFile << eventlist().now() / 1000 << "," << marked << std::endl;

        MyFile.close();
    }

    uint64_t now_time = 0;
    if (precision_ts == 1) {
        now_time = eventlist().now();
    } else {
        now_time = (((eventlist().now() + precision_ts - 1) / precision_ts) * precision_ts);
    }
    uint64_t newRtt = now_time - ts;
    mark_received(pkt);

    update_pacing_delay();

    count_total_ack++;
    if (marked) {
        _list_ecn_received.push_back(std::make_pair(eventlist().now() / 1000, 1));
        count_total_ecn++;
        consecutive_good_medium = 0;
    }

    if (from == 0 && count_total_ack % 10 == 0) {
        printf("Currently at Pkt %d\n", count_total_ack);
    }

    if (!marked) {
        _consecutive_no_ecn += _mss;
        _next_pathid = pkt.pathid_echo;
        _good_entropies_list.push_back(pkt.pathid_echo);
    } else {
        _next_pathid = -1;
        ecn_last_rtt = true;
        _consecutive_no_ecn = 0;
    }

    if (COLLECT_DATA) {
        _received_ecn.push_back(std::make_tuple(eventlist().now(), marked, _mss, newRtt));
        _list_rtt.push_back(std::make_tuple(eventlist().now() / 1000, newRtt / 1000, pkt.seqno(), pkt.ackno(),
                                            _base_rtt / 1000, _target_rtt / 1000));
    }

    if (newRtt > _base_rtt * quickadapt_lossless_rtt && marked && queue_type == "lossless_input") {

        simulateTrimEvent(dynamic_cast<UecAck &>(pkt));
    }

    if (seqno >= _flow_size && _sent_packets.empty() && !_flow_finished) {
        _flow_finished = true;
        if (f_flow_over_hook) {
            f_flow_over_hook(pkt);
        }

        cout << "Flow " << nodename() << " finished at " << timeAsMs(eventlist().now()) << endl;
        cout << "Flow " << nodename() << " completion time is " << timeAsMs(eventlist().now() - _flow_start_time)
             << endl;

        printf("Flow Completion time is %f - Flow Finishing Time %lu - Flow "
               "Start Time %lu - Size Finished Flow %lu - From %d - To %d\n",
               timeAsUs(eventlist().now()) - timeAsUs(_flow_start_time), eventlist().now(), _flow_start_time,
               _flow_size, from, to);

        printf("Flow %d - Total Time %f\n", from, timeAsUs(eventlist().now()) - timeAsUs(_flow_start_time));

        printf("Overall Completion at %lu\n", GLOBAL_TIME);
        if (_end_trigger) {
            _end_trigger->activate();
        }
        return;
    }

    if (seqno > _last_acked || true) { // TODO: new ack, we don't care about
                                       // ordering for now. Check later though
        if (seqno >= _highest_sent) {
            _highest_sent = seqno;
        }

        _last_acked = seqno;

        _list_cwd.push_back(std::make_pair(eventlist().now() / 1000, _cwnd));
        // printf("Window Is %d - From %d To %d\n", _cwnd, from, to);
        current_pkt++;
        // printf("Triggering ADJ\n");
        adjust_window(ts, marked, newRtt, seqno);

        acked_bytes += _mss;
        good_bytes += _mss;

        _effcwnd = _cwnd;
        // printf("Received From %d - Sending More\n", from);
        send_packets();
        return; // TODO: if no further code, this can be removed
    }
}

uint64_t LcpSrc::get_unacked() {
    // return _unacked;
    uint64_t missing = 0;
    for (const auto &sp : _sent_packets) {
        if (!sp.acked && !sp.nacked && !sp.timedOut) {
            missing += _mss;
        }
    }
    return missing;
}

void LcpSrc::receivePacket(Packet &pkt) {
    // every packet received represents one less packet in flight

    if (from == 226 && to == 117) {
        printf("Packet Received from %d to %d at %lu - Type %d\n", from, to, GLOBAL_TIME / 1000, pkt.type());
    }

    if (pkt._queue_full || pkt.bounced() == false) {
        reduce_unacked(_mss);
    } else {
        exit(0);
        printf("Never here\n");
    }
    tot_pkt_seen_qa++;

    // TODO: receive window?
    pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_RCVDESTROY);

    if (_logger) {
        _logger->logUec(*this, UecLogger::UEC_RCV);
    }

    if (pkt.is_bts_pkt) {
        /* printf("Receiving BTS %d %lu\n", from, GLOBAL_TIME / 1000); */
        // total_nack++;
        _next_pathid = -1;
        count_received++;
        bts_received++;
        processBts((UecPacket *)(&pkt));
        pkt.free();

    } else {
        switch (pkt.type()) {
        case UEC:
            // BTS
            if (_bts_enabled) {
                if (pkt.bounced()) {
                    // processBts((UecPacket *)(&pkt));
                    counter_consecutive_good_bytes = 0;
                    increasing = false;
                }
            }
            break;
        case UECACK:
            count_received++;
            total_pkt++;

            processAck(dynamic_cast<UecAck &>(pkt), false);

            pkt.free();
            break;
        case ETH_PAUSE:
            printf("Src received a Pause\n");
            // processPause((const EthPausePacket &)pkt);
            pkt.free();
            return;
        case UECNACK:
            // printf("\nNACK at %lu %d@%d@%d - %d\n", GLOBAL_TIME / 1000, from, to, tag, pkt.is_failed);
            //  fflush(stdout);
            //  total_nack++;
            if (_trimming_enabled) {
                _next_pathid = -1;
                count_received++;
                processNack(dynamic_cast<UecNack &>(pkt));
                pkt.free();
            }
            break;
        default:
            std::cout << "unknown packet receive with type code: " << pkt.type() << "\n";
            return;
        }
    }

    if (get_unacked() < _cwnd && _rtx_timeout_pending) {
        eventlist().sourceIsPendingRel(*this, 1000);
    }
}

void LcpSrc::fast_increase() {
    printf("From %d - Fast Increase at %lu\n", from, GLOBAL_TIME / 1000);
    if (use_fast_drop) {
        if (use_super_fast_increase) {
            if (algorithm_type == "intersmartt") {
                _cwnd += 0.5 * _mss;
            } else {
                _cwnd += 1.2 * _mss;
            }
        } else {
            _cwnd += _mss;
        }

    } else {
        if (use_super_fast_increase) {
            _cwnd += 4 * _mss * (LINK_SPEED_MODERN / 100);
        } else {
            _cwnd += _mss;
        }
    }

    increasing = true;
    _list_fast_increase_event.push_back(std::make_pair(eventlist().now() / 1000, 1));
}

void LcpSrc::adjust_window(simtime_picosec ts, bool ecn, simtime_picosec rtt, uint32_t ackno) {

    if (algorithm_type == "lcp") {
        if (_current_rtt_ewma == timeFromMs(0)) {
            _current_rtt_ewma = rtt;
        } else {
            if (LCP_USE_MIN_RTT) {
                _current_rtt_ewma = min(_current_rtt_ewma, rtt);
            } else {
                _current_rtt_ewma = _current_rtt_ewma * (1.0 - LCP_ALPHA) + LCP_ALPHA * rtt;
            }
        }
        // printf("\t_current_rtt_ewma: %d _previous_rtt_ewma: %d rtt: %d alpha: %f curackno: %lu\n", _current_rtt_ewma, _previous_rtt_ewma, rtt, LCP_ALPHA, ackno);

        if (_current_rtt_ewma > TARGET_RTT_LOW) {
            _consecutive_good_epochs = 0;
        }

        if (LCP_USE_FAST_INCREASE && _consecutive_good_epochs > LCP_FAST_INCREASE_THRESHOLD) {
            printf("Doing fi\n");
            fast_increase();
        }

        // _bytes_receieved_since_last_epoch += _mss;

        if (eventlist().now() >= _time_of_next_epoch) {
            cout << "TimeEpoch time: " << eventlist().now() / 1000000 << "  ack sequence number: " << ackno << " next measurement sequence number: " << _next_measurement_seq_no << " highest sent seq no: " << _highest_sent << " abdul'sfix for measurement: " << _highest_sent + 1 << " cwnd: " << _cwnd << endl;
            // cout << "According to me bytes receieved since last time... " << _bytes_receieved_since_last_epoch << endl;
            quick_adapt(false);
            _time_of_next_epoch = eventlist().now() + TARGET_RTT_LOW;
            // _bytes_receieved_since_last_epoch = 0;
        }

        // Next measurement epoch has begun.
        if (ackno >= _next_measurement_seq_no) {
            cout << "ByteEpoch time: " << eventlist().now() / 1000000 << "  ack sequence number: " << ackno << " next measurement sequence number: " << _next_measurement_seq_no << " highest sent seq no: " << _highest_sent << " abdul'sfix for measurement: " << _highest_sent + 1 << " cwnd: " << _cwnd << endl;
            if (_current_rtt_ewma < TARGET_RTT_LOW) {
                _consecutive_good_epochs++;
            } else {
                _consecutive_good_epochs = 0;
            }

            if (_previous_rtt_ewma == timeFromMs(0)) {
                _previous_rtt_ewma = _current_rtt_ewma;
            }
            int64_t rtt_change = (int64_t) _current_rtt_ewma - (int64_t) _previous_rtt_ewma;
            cout << "Current RTT: " << _current_rtt_ewma << " Previous RTT: " << _previous_rtt_ewma << " RTT Change: " << rtt_change << endl;

            uint32_t cwnd_before = _cwnd;

            // Translate rtt_change into a rate.
            double gradient = ((double) rtt_change) / ((double) TARGET_RTT_LOW);
            cout << "CWND change: " << nodename() << " before: " << cwnd_before << " gradient: " << gradient << " rttchange: " << rtt_change << endl;
            cout << "    _current_rtt_ewma: " << _current_rtt_ewma << ", _target_rtt_low: " << TARGET_RTT_LOW << ", _target_rtt_high: " << TARGET_RTT_HIGH << endl;
            if (_current_rtt_ewma < TARGET_RTT_LOW) {
                _cwnd += (uint32_t)LCP_DELTA;
                cout << "    CWND change: " << nodename() << " less than all, go from " << cwnd_before << " to " << _cwnd << endl;
            } else if (_current_rtt_ewma > 2 * TARGET_RTT_HIGH) {
                if (LCP_USE_QUICK_ADAPT) {
                    quick_adapt_drop();
                } else {
                    double latency_ratio = ((double)TARGET_RTT_HIGH) / ((double) _current_rtt_ewma);
                    double latency_factor = LCP_BETA * (1.0 - latency_ratio);
                    double gradient_factor = min(max(-1.0, gradient), 0.0) * LCP_GAMMA;
                    double total_factor = min(max(-1.0, latency_factor + gradient_factor), 1.0);
                    _cwnd *= (1.0 - total_factor);
                }
                cout << "    CWND change: " << nodename() << " more than 2x target high, go from " << cwnd_before << " to " << _cwnd << endl;
            } else if (_current_rtt_ewma > TARGET_RTT_HIGH && abs(gradient) < 0.01) {
                if (LCP_USE_AGGRESSIVE_DECREASE) {
                    // Target RTT is high and the gradient is near 0. Aggressive decrease.
                    _cwnd *= 0.5;
                    cout << "    CWND change: " << nodename() << " more than target high and gradient 0 go from " << cwnd_before << " to " << _cwnd << endl;
                } else {
                    double latency_ratio = ((double)TARGET_RTT_HIGH) / ((double) _current_rtt_ewma);
                    double latency_factor = LCP_BETA * (1.0 - latency_ratio);
                    double gradient_factor = min(max(-1.0, gradient), 0.0) * LCP_GAMMA;
                    double total_factor = min(max(-1.0, latency_factor + gradient_factor), 1.0);
                    _cwnd *= (1.0 - total_factor);
                    cout << "    CWND change: " << nodename() << " greater than all, go from " << cwnd_before << " to " << _cwnd << " latency factor: " << latency_factor << " gradient factor: " << gradient_factor << " total factor: " << total_factor << endl;
                }
            } 
            else if (_current_rtt_ewma > TARGET_RTT_HIGH) {
                double latency_ratio = ((double)TARGET_RTT_HIGH) / ((double) _current_rtt_ewma);
                double latency_factor = LCP_BETA * (1.0 - latency_ratio);
                double gradient_factor = min(max(-1.0, gradient), 0.0) * LCP_GAMMA;
                double total_factor = min(max(-1.0, latency_factor + gradient_factor), 1.0);
                _cwnd *= (1.0 - total_factor);
                cout << "    CWND change: " << nodename() << " greater than all, go from " << cwnd_before << " to " << _cwnd << " latency factor: " << latency_factor << " gradient factor: " << gradient_factor << " total factor: " << total_factor << endl;
            } else if (gradient <= 0.0) {
                _cwnd += _mss;
                cout << "    CWND change: " << nodename() << " between with negative gradient go from " << cwnd_before << " to " << _cwnd << " delta: " << LCP_DELTA << endl;
            } else {
                double gradient_change = min(max(0.0, gradient * LCP_BETA), 1.0);
                _cwnd *= (1 - gradient_change);
                cout << "    CWND change: " << nodename() << " between with positive gradient go from " << cwnd_before << " to " << _cwnd << " gradient_change: " << gradient_change << endl;
            }
        
            // Reset State.
            _next_measurement_seq_no = _highest_sent + 1;
            _previous_rtt_ewma = _current_rtt_ewma;

            check_limits_cwnd();

            if (COLLECT_DATA) {
                std::string file_name =
                        PROJECT_ROOT_PATH /
                        ("sim/output/current_rtt_ewma/current_rtt_ewma_" + _name + "_" +
                                        std::to_string(tag) + ".txt");
                std::ofstream MyFile(file_name, std::ios_base::app);
                MyFile << eventlist().now() / 1000 << "," << _current_rtt_ewma / 1000 << std::endl;
                MyFile.close();

                file_name = PROJECT_ROOT_PATH / ("sim/output/target_rtt_low/target_rtt_low_" + _name + "_" +
                                        std::to_string(tag) + ".txt");
                std::ofstream MyFile2(file_name, std::ios_base::app);
                MyFile2 << eventlist().now() / 1000 << "," << TARGET_RTT_LOW /1000 << std::endl;
                MyFile2.close();

                file_name = PROJECT_ROOT_PATH / ("sim/output/target_rtt_high/target_rtt_high_" + _name + "_" +
                                        std::to_string(tag) + ".txt");
                std::ofstream MyFile3(file_name, std::ios_base::app);
                MyFile3 << eventlist().now() / 1000 << "," << TARGET_RTT_HIGH / 1000 << std::endl;
                MyFile3.close();

                file_name = PROJECT_ROOT_PATH / ("sim/output/baremetal_latency/baremetal_latency_" + _name + "_" +
                                        std::to_string(tag) + ".txt");
                std::ofstream MyFile4(file_name, std::ios_base::app);
                MyFile4 << eventlist().now() / 1000 << "," << BAREMETAL_RTT / 1000 << std::endl;
                MyFile4.close();
            }

            if (LCP_USE_MIN_RTT) {
                _current_rtt_ewma = timeFromMs(0);
            }
        }
    } else if (algorithm_type == "lcp-gemini") {
        if (_current_rtt_measurement == timeFromMs(0)) {
            _current_rtt_measurement = rtt;
        } else {
            _current_rtt_measurement = min(_current_rtt_measurement, rtt);
        }
        double cwnd_before = _cwnd;

        if (_current_rtt_measurement > BAREMETAL_RTT + LCP_GEMINI_TARGET_QUEUEING_LATENCY) {
            _consecutive_good_epochs = 0;
        }

        // Additive increase.
        if (LCP_USE_FAST_INCREASE && _consecutive_good_epochs > LCP_FAST_INCREASE_THRESHOLD) {
            printf("Doing fi\n");
            fast_increase();
        } {
            _cwnd += (double)LCP_GEMINI_H / ((double)_cwnd / (double)_mss);
        }

        cout << "CWND change: " << nodename() << " AI from " << cwnd_before << " to " << _cwnd << " h: " << LCP_GEMINI_H << " current_rtt_measurement: " << _current_rtt_measurement << " target_rtt: " << BAREMETAL_RTT + LCP_GEMINI_TARGET_QUEUEING_LATENCY << " rtt: " << rtt << endl;

        // Make sure the CWND has increased.
        assert(_cwnd == _maxcwnd || _cwnd > cwnd_before);

        // Window ended, examine RTT.
        if (ackno >= _next_window_seq_no) {
            if (_current_rtt_ewma < TARGET_RTT_LOW) {
                _consecutive_good_epochs++;
            } else {
                _consecutive_good_epochs = 0;
            }


            cout << "ByteEpoch time: " << eventlist().now() / 1000000 << " ack sequence number: " << ackno << " next measurement sequence number: " << _next_window_seq_no << " highest sent seq no: " << _highest_sent << " abdul'sfix for measurement: " << _highest_sent + 1 << " cwnd: " << _cwnd << endl;
            if (_current_rtt_measurement > BAREMETAL_RTT + LCP_GEMINI_TARGET_QUEUEING_LATENCY) {
                _cwnd *= (1.0 - LCP_GEMINI_BETA);
                cout << "CWND change: " << nodename() << " MD from " << cwnd_before << " to " << _cwnd << " h: " << LCP_GEMINI_H << " beta: " << LCP_GEMINI_BETA << endl;
            }
            
            _current_rtt_measurement = timeFromMs(0);
            _next_window_seq_no = _highest_sent + 1;
        }

        if (COLLECT_DATA) {
            std::string file_name =
                    PROJECT_ROOT_PATH /
                    ("sim/output/current_rtt_ewma/current_rtt_ewma_" + _name + "_" +
                                        std::to_string(tag) + ".txt");
            if (_current_rtt_measurement != timeFromMs(0)) {
                std::ofstream MyFile(file_name, std::ios_base::app);
                MyFile << eventlist().now() / 1000 << "," << _current_rtt_measurement / 1000 << std::endl;
                MyFile.close();
            }

            file_name = PROJECT_ROOT_PATH / ("sim/output/target_rtt_high/target_rtt_high_" + _name + "_" +
                                        std::to_string(tag) + ".txt");
            std::ofstream MyFile3(file_name, std::ios_base::app);
            MyFile3 << eventlist().now() / 1000 << "," << (BAREMETAL_RTT + LCP_GEMINI_TARGET_QUEUEING_LATENCY) / 1000 << std::endl;
            MyFile3.close();

            file_name = PROJECT_ROOT_PATH / ("sim/output/baremetal_latency/baremetal_latency_" + _name + "_" +
                                        std::to_string(tag) + ".txt");
            std::ofstream MyFile4(file_name, std::ios_base::app);
            MyFile4 << eventlist().now() / 1000 << "," << BAREMETAL_RTT / 1000 << std::endl;
            MyFile4.close();
        }
    }

    check_limits_cwnd();
}

void LcpSrc::drop_old_received() {
    if (true) {
        if (eventlist().now() > _target_rtt) {
            uint64_t lower_thresh = eventlist().now() - (_target_rtt * 1);
            while (!_received_ecn.empty() && std::get<0>(_received_ecn.front()) < lower_thresh) {
                _received_ecn.pop_front();
            }
        }
    } else {
        while (_received_ecn.size() > 10) {
            _received_ecn.pop_front();
        }
    }
}

bool LcpSrc::no_ecn_last_target_rtt() {
    drop_old_received();
    for (const auto &[ts, ecn, size, rtt] : _received_ecn) {
        if (ecn) {
            return false;
        }
    }
    return true;
}

bool LcpSrc::no_rtt_over_target_last_target_rtt() {
    drop_old_received();
    for (const auto &[ts, ecn, size, rtt] : _received_ecn) {
        if (rtt > _target_rtt) {
            return false;
        }
    }
    return true;
}

std::size_t LcpSrc::getEcnInTargetRtt() {
    drop_old_received();
    std::size_t ecn_count = 0;
    for (const auto &[ts, ecn, size, rtt] : _received_ecn) {
        if (ecn) {
            ++ecn_count;
        }
    }
    return ecn_count;
}

bool LcpSrc::ecn_congestion() {
    if (getEcnInTargetRtt() >= _received_ecn.size() / 2) {
        return true;
    }
    return false;
}

const string &LcpSrc::nodename() { return _nodename; }

void LcpSrc::connect(Route *routeout, Route *routeback, LcpSink &sink, simtime_picosec starttime) {
    if (_route_strategy == SINGLE_PATH || _route_strategy == ECMP_FIB || _route_strategy == ECMP_FIB_ECN ||
        _route_strategy == REACTIVE_ECN || _route_strategy == ECMP_RANDOM2_ECN || _route_strategy == ECMP_RANDOM_ECN) {
        assert(routeout);
        _route = routeout;
    }

    _sink = &sink;
    _flow.set_id(get_id()); // identify the packet flow with the NDP source
                            // that generated it
    _flow._name = _name;
    _sink->connect(*this, routeback);

    /* printf("StartTime %s is %lu\n", _name.c_str(), starttime); */

    eventlist().sourceIsPending(*this, starttime);
}

void LcpSrc::startflow() {
    ideal_x = x_gain;
    _flow_start_time = eventlist().now();

    /* printf("Starting Flow from %d to %d tag %d - RTT %lu - Target %lu - "
           "Time "
           "%lu\n",
           from, to, tag, _base_rtt, _target_rtt, GLOBAL_TIME / 1000); */
    send_packets();
}

const Route *LcpSrc::get_path() {
    if (_use_good_entropies && !_good_entropies.empty()) {
        auto rt = _good_entropies.back();
        _good_entropies.pop_back();
        return rt;
    }

    // Means we want to select a random one out of all paths, the original
    // idea
    if (_num_entropies == -1) {
        _crt_path = random() % _paths.size();
    } else {
        // Else we use our entropy array of a certain size and roud robin it
        _crt_path = _entropy_array[current_entropy];
        current_entropy = current_entropy + 1;
        current_entropy = current_entropy % _num_entropies;
    }

    total_routes = _paths.size();
    return _paths.at(_crt_path);
}

void LcpSrc::map_entropies() {
    for (int i = 0; i < _num_entropies; i++) {
        _entropy_array.push_back(random() % _paths.size());
    }
    printf("Printing my Paths: ");
    for (int i = 0; i < _num_entropies; i++) {
        printf("%d - ", _entropy_array[i]);
    }
    printf("\n");
}

void LcpSrc::pacedSend() {
    _paced_packet = true;
    send_packets();
}

void LcpSrc::send_packets() {

    if (_rtx_pending) {
        retransmit_packet();
    }
    _list_unacked.push_back(std::make_pair(eventlist().now() / 1000, _unacked));
    unsigned c = _cwnd;

    while (get_unacked() + _mss <= c && _highest_sent < _flow_size) {

        /* printf("Sending packet from %d at %lu %d vs %d ~ %d vs %d -- %d %d\n", from, GLOBAL_TIME/1000, get_unacked()
           + _mss, _cwnd, _highest_sent, _flow_size, get_unacked() + _mss <= c, _highest_sent < _flow_size);  */

        // Stop sending
        if (pause_send && stop_after_quick) {
            // printf("Not sending at %lu\n", GLOBAL_TIME / 1000);
            break;
        }

        // Check pacer and set timeout
        if (!_paced_packet && use_pacing) {
            if (generic_pacer != NULL && !generic_pacer->is_pending()) {
                /* printf("scheduling send\n"); */
                generic_pacer->schedule_send(pacing_delay);
                return;
            } else if (generic_pacer != NULL) {
                return;
            }
        }

        uint64_t data_seq = 0;
        UecPacket *p = UecPacket::newpkt(_flow, *_route, _highest_sent + 1, data_seq, _mss, false, _dstaddr);

        p->set_route(*_route);
        int crt = choose_route();
        p->is_bts_pkt = false;

        p->set_pathid(_path_ids[crt]);
        p->from = this->from;
        p->to = this->to;
        p->tag = this->tag;
        p->my_idx = data_count_idx++;

        p->flow().logTraffic(*p, *this, TrafficLogger::PKT_CREATESEND);
        p->set_ts(eventlist().now());

        // send packet
        _highest_sent += _mss;
        _packets_sent += _mss;
        _unacked += _mss;

        // Getting time until packet is really sent
        /* printf("Send on at %lu -- %d %d\n", GLOBAL_TIME / 1000, pause_send, stop_after_quick); */
        PacketSink *sink = p->sendOn();
        track_sending_rate();
        tracking_bytes += _mss;
        HostQueue *q = dynamic_cast<HostQueue *>(sink);
        assert(q);
        uint32_t service_time = q->serviceTime(*p);
        _sent_packets.push_back(LcpSentPacket(eventlist().now() + service_time + _rto, p->seqno(), false, false, false));

        if (generic_pacer != NULL && use_pacing) {
            generic_pacer->just_sent();
            _paced_packet = false;
        }
        if (from == 226 && to == 117) {
            printf("Packet Sent1 from %d to %d at %lu\n", from, to, GLOBAL_TIME / 1000);
        }
        sent_bytes_previous_window += _mss;
        if (_rtx_timeout == timeInf) {
            update_rtx_time();
        }
    }
}

void permute_sequence_lcp(vector<int> &seq) {
    size_t len = seq.size();
    for (uint32_t i = 0; i < len; i++) {
        seq[i] = i;
    }
    for (uint32_t i = 0; i < len; i++) {
        int ix = random() % (len - i);
        int tmpval = seq[ix];
        seq[ix] = seq[len - 1 - i];
        seq[len - 1 - i] = tmpval;
    }
}

void LcpSrc::set_paths(uint32_t no_of_paths) {
    if (_route_strategy != ECMP_FIB && _route_strategy != ECMP_FIB_ECN && _route_strategy != ECMP_FIB2_ECN &&
        _route_strategy != REACTIVE_ECN && _route_strategy != ECMP_RANDOM_ECN && _route_strategy != ECMP_RANDOM2_ECN) {
        cout << "Set paths uec (path_count) called with wrong route "
                "strategy "
             << _route_strategy << endl;
        abort();
    }

    _path_ids.resize(no_of_paths);
    permute_sequence_lcp(_path_ids);

    _paths.resize(no_of_paths);
    _original_paths.resize(no_of_paths);
    _path_acks.resize(no_of_paths);
    _path_ecns.resize(no_of_paths);
    _path_nacks.resize(no_of_paths);
    _bad_path.resize(no_of_paths);
    _avoid_ratio.resize(no_of_paths);
    _avoid_score.resize(no_of_paths);

    _path_ids.resize(no_of_paths);
    // permute_sequence(_path_ids);
    _paths.resize(no_of_paths);
    _path_ecns.resize(no_of_paths);

    for (size_t i = 0; i < no_of_paths; i++) {
        _paths[i] = NULL;
        _original_paths[i] = NULL;
        _path_acks[i] = 0;
        _path_ecns[i] = 0;
        _path_nacks[i] = 0;
        _avoid_ratio[i] = 0;
        _avoid_score[i] = 0;
        _bad_path[i] = false;
        _path_ids[i] = i;
    }
}

void LcpSrc::set_paths(vector<const Route *> *rt_list) {
    uint32_t no_of_paths = rt_list->size();
    switch (_route_strategy) {
    case NOT_SET:
    case SINGLE_PATH:
    case ECMP_FIB:
    case ECMP_FIB_ECN:
    case REACTIVE_ECN:
        // shouldn't call this with these strategies
        abort();
    case SCATTER_PERMUTE:
    case SCATTER_RANDOM:
    case PULL_BASED:
    case SCATTER_ECMP: {
        no_of_paths = min(_num_entropies, (int)no_of_paths);
        _path_ids.resize(no_of_paths);
        _paths.resize(no_of_paths);
        _original_paths.resize(no_of_paths);
        _path_acks.resize(no_of_paths);
        _path_ecns.resize(no_of_paths);
        _path_nacks.resize(no_of_paths);
        _bad_path.resize(no_of_paths);
        _avoid_ratio.resize(no_of_paths);
        _avoid_score.resize(no_of_paths);
#ifdef DEBUG_PATH_STATS
        _path_counts_new.resize(no_of_paths);
        _path_counts_rtx.resize(no_of_paths);
        _path_counts_rto.resize(no_of_paths);
#endif

        // generate a randomize sequence of 0 .. size of rt_list - 1
        vector<int> randseq(rt_list->size());
        if (_route_strategy == SCATTER_ECMP) {
            // randsec may have duplicates, as with ECMP
            // randomize_sequence(randseq);
        } else {
            // randsec will have no duplicates
            // permute_sequence(randseq);
        }

        for (size_t i = 0; i < no_of_paths; i++) {
            // we need to copy the route before adding endpoints, as
            // it may be used in the reverse direction too.
            // Pick a random route from the available ones
            Route *tmp = new Route(*(rt_list->at(randseq[i])), *_sink);
            // Route* tmp = new Route(*(rt_list->at(i)));
            tmp->add_endpoints(this, _sink);
            tmp->set_path_id(i, rt_list->size());
            _paths[i] = tmp;
            _path_ids[i] = i;
            _original_paths[i] = tmp;
#ifdef DEBUG_PATH_STATS
            _path_counts_new[i] = 0;
            _path_counts_rtx[i] = 0;
            _path_counts_rto[i] = 0;
#endif
            _path_acks[i] = 0;
            _path_ecns[i] = 0;
            _path_nacks[i] = 0;
            _avoid_ratio[i] = 0;
            _avoid_score[i] = 0;
            _bad_path[i] = false;
        }
        _crt_path = 0;
        // permute_paths();
        break;
    }
    default: {
        abort();
        break;
    }
    }
}

void LcpSrc::apply_timeout_penalty() {
    if (_trimming_enabled) {
        reduce_cwnd(_mss);
    } else {
        reduce_cwnd(_mss);
        //_cwnd = _mss;
    }
}

void LcpSrc::rtx_timer_hook(simtime_picosec now, simtime_picosec period) { retransmit_packet(); }

void LcpSrc::track_sending_rate() {
    if (eventlist().now() > last_track_ts + tracking_period) {
        double rate = (double)(tracking_bytes * 8.0 / ((eventlist().now() - last_track_ts) / 1000));
        list_sending_rate.push_back(std::make_pair(eventlist().now() / 1000, rate));
        tracking_bytes = 0;
        last_track_ts = eventlist().now();
    }
}

void LcpSrc::track_ecn_rate() {}

bool LcpSrc::resend_packet(std::size_t idx) {

    if (get_unacked() >= _cwnd || (pause_send && stop_after_quick)) {
        // printf("Not sending at %lu\n", GLOBAL_TIME / 1000);
        return false;
    }

    // Check pacer and set timeout
    if (!_paced_packet && use_pacing) {
        if (generic_pacer != NULL && !generic_pacer->is_pending()) {
            generic_pacer->schedule_send(pacing_delay);
            return false;
        } else if (generic_pacer != NULL) {
            return false;
        }
    }

    assert(!_sent_packets[idx].acked);

    // this will cause retransmission not only of the offending
    // packet, but others close to timeout
    _rto_margin = _rtt / 2;

    _unacked += _mss;
    UecPacket *p = UecPacket::newpkt(_flow, *_route, _sent_packets[idx].seqno, 0, _mss, true, _dstaddr);
    p->set_ts(eventlist().now());
    p->is_bts_pkt = false;

    p->set_route(*_route);
    int crt = choose_route();
    p->from = this->from;
    p->to = this->to;
    p->tag = this->tag;

    // printf("Resending to %d\n", this->from);

    p->set_pathid(_path_ids[crt]);

    p->flow().logTraffic(*p, *this, TrafficLogger::PKT_CREATE);
    /* printf("Send on at %lu -- %d %d\n", GLOBAL_TIME / 1000, pause_send, stop_after_quick); */
    PacketSink *sink = p->sendOn();
    track_sending_rate();
    tracking_bytes += _mss;
    HostQueue *q = dynamic_cast<HostQueue *>(sink);
    assert(q);
    uint32_t service_time = q->serviceTime(*p);
    if (_sent_packets[idx].nacked) {
        --_nack_rtx_pending;
        _sent_packets[idx].nacked = false;
    }
    _sent_packets[idx].timer = eventlist().now() + service_time + _rto;
    _sent_packets[idx].timedOut = false;
    update_rtx_time();
    if (generic_pacer != NULL) {
        generic_pacer->just_sent();
        _paced_packet = false;
    }
    if (from == 226 && to == 117) {
        printf("Packet Sent2 from %d to %d at %lu\n", from, to, GLOBAL_TIME / 1000);
    }
    sent_bytes_previous_window += _mss;
    return true;
}

// retransmission for timeout
void LcpSrc::retransmit_packet() {
    _rtx_pending = false;
    for (std::size_t i = 0; i < _sent_packets.size(); ++i) {
        auto &sp = _sent_packets[i];
        if (_rtx_timeout_pending && !sp.acked && !sp.nacked && sp.timer <= eventlist().now() + _rto_margin) {
            _cwnd = _mss;
            sp.timedOut = true;
            reduce_unacked(_mss);
        }
        if (!sp.acked && (sp.timedOut || sp.nacked)) {
            if (!resend_packet(i)) {
                _rtx_pending = true;
            }
        }
    }
    _rtx_timeout_pending = false;
}

/**********
 * LcpSink *
 **********/

LcpSink::LcpSink() : DataReceiver("sink"), _cumulative_ack{0}, _drops{0} { _nodename = "LcpSink"; }

void LcpSink::set_end_trigger(Trigger &end_trigger) { _end_trigger = &end_trigger; }

void LcpSink::send_nack(simtime_picosec ts, bool marked, UecAck::seq_t seqno, UecAck::seq_t ackno, const Route *rt,
                        int path_id, bool is_failed) {

    UecNack *nack = UecNack::newpkt(_src->_flow, *_route, seqno, ackno, 0, _srcaddr);
    nack->is_failed = is_failed;
    nack->from = this->from;
    nack->to = this->to;
    nack->tag = this->tag;

    // printf("Sending NACK at %lu\n", GLOBAL_TIME);
    nack->set_pathid(_path_ids[_crt_path]);
    _crt_path++;
    if (_crt_path == _paths.size()) {
        _crt_path = 0;
    }

    nack->pathid_echo = path_id;
    nack->is_ack = false;
    nack->flow().logTraffic(*nack, *this, TrafficLogger::PKT_CREATESEND);
    nack->set_ts(ts);
    if (marked) {
        nack->set_flags(ECN_ECHO);
    } else {
        nack->set_flags(0);
    }

    nack->sendOn();
}

bool LcpSink::already_received(UecPacket &pkt) {
    UecPacket::seq_t seqno = pkt.seqno();

    if (seqno <= _cumulative_ack) { // TODO: this assumes
                                    // that all data packets
                                    // have the same size
        return true;
    }
    for (auto it = _received.begin(); it != _received.end(); ++it) {
        if (seqno == *it) {
            return true; // packet received OOO
        }
    }
    return false;
}

void LcpSink::receivePacket(Packet &pkt) {
    /* printf("Sink Received %d %d - Entropy %d - %lu - \n", pkt.from, pkt.id(), pkt.pathid(), GLOBAL_TIME / 1000); */
    if (pkt.pfc_just_happened) {
        pfc_just_seen = 1;
    } else {
        pfc_just_seen = 0;
    }

    switch (pkt.type()) {
    case UECACK:
    case UECNACK:
        // bounced, ignore
        pkt.free();
        return;
    case UEC:
        // do what comes after the switch
        if (pkt.bounced()) {
            printf("Bounced at Sink, no sense\n");
        }

        break;
    default:
        std::cout << "unknown packet receive with type code: " << pkt.type() << "\n";
        pkt.free();

        return;
    }
    UecPacket *p = dynamic_cast<UecPacket *>(&pkt);
    UecPacket::seq_t seqno = p->seqno();
    UecPacket::seq_t ackno = p->seqno() + p->data_packet_size() - 1;
    simtime_picosec ts = p->ts();

    bool marked = p->flags() & ECN_CE;

    // TODO: consider different ways to select paths
    auto crt_path = random() % _paths.size();

    // packet was trimmed
    if (pkt.header_only() && pkt._is_trim) {
        send_nack(ts, marked, seqno, ackno, _paths.at(crt_path), pkt.pathid(), pkt.is_failed);
        pkt.flow().logTraffic(pkt, *this, TrafficLogger::PKT_RCVDESTROY);
        p->free();
        // printf("NACKR %d@%d@%d - Time %lu\n", from, to, tag,
        //        GLOBAL_TIME / 1000);
        return;
    }

    int size = p->data_packet_size();
    p->free();

    _packets += size;

    if (seqno == _cumulative_ack + 1) { // next expected seq no
        _cumulative_ack = seqno + size - 1;
        seqno = 1;

        // handling packets received OOO
        while (!_received.empty() && _received.front() == _cumulative_ack + 1) {
            _received.pop_front();
            _cumulative_ack += size; // this assumes that all
                                     // packets have the same size
        }
        ackno = _cumulative_ack;
    } else if (seqno < _cumulative_ack + 1) { // already ack'ed
        // this space intentionally left empty
        seqno = 1;
        ackno = _cumulative_ack;
    } else { // not the next expected sequence number
        // TODO: what to do when a future packet is
        // received?
        if (_received.empty()) {
            _received.push_front(seqno);
            _drops += (1000 + seqno - _cumulative_ack - 1) / 1000; // TODO: figure out what is this
                                                                   // calculating exactly
        } else if (seqno > _received.back()) {
            _received.push_back(seqno);
        } else {
            for (auto it = _received.begin(); it != _received.end(); ++it) {
                if (seqno == *it)
                    break; // bad retransmit
                if (seqno < (*it)) {
                    _received.insert(it, seqno);
                    break;
                }
            }
        }
    }
    // TODO: reverse_route is likely sending the packet
    // through the same exact links, which is not correct in
    // Packet Spray, but there doesn't seem to be a good,
    // quick way of doing that in htsim printf("Ack Sending
    // From %d - %d\n", this->from,
    int32_t path_id = p->pathid();
    /* printf("NORMALACK %d@%d@%d - Time %lu\n", from, to, tag,
           GLOBAL_TIME / 1000); */
    send_ack(ts, marked, seqno, ackno, _paths.at(crt_path), pkt.get_route(), path_id);
}

void LcpSink::send_ack(simtime_picosec ts, bool marked, UecAck::seq_t seqno, UecAck::seq_t ackno, const Route *rt,
                       const Route *inRoute, int path_id) {

    UecAck *ack = 0;

    switch (_route_strategy) {
    case ECMP_FIB:
    case ECMP_FIB_ECN:
    case REACTIVE_ECN:
    case ECMP_RANDOM2_ECN:
    case ECMP_RANDOM_ECN:
        ack = UecAck::newpkt(_src->_flow, *_route, seqno, ackno, 0, _srcaddr);

        ack->set_pathid(_path_ids[_crt_path]);
        _crt_path++;
        if (_crt_path == _paths.size()) {
            _crt_path = 0;
        }
        ack->inc_id++;
        ack->my_idx = ack_count_idx++;

        // set ECN echo only if that is selected strategy
        if (marked) {
            ack->set_flags(ECN_ECHO);
        } else {
            ack->set_flags(0);
        }

        break;
    case NOT_SET:
        abort();
    default:
        break;
    }
    assert(ack);
    ack->pathid_echo = path_id;
    ack->pfc_just_happened = false;
    if (pfc_just_seen == 1) {
        ack->pfc_just_happened = true;
    }

    // ack->inRoute = inRoute;
    ack->is_ack = true;
    ack->flow().logTraffic(*ack, *this, TrafficLogger::PKT_CREATE);
    ack->set_ts(ts);

    // printf("Setting TS to %lu at %lu\n", ts / 1000, GLOBAL_TIME / 1000);
    ack->from = this->from;
    ack->to = this->to;
    ack->tag = this->tag;

    ack->sendOn();
}

const string &LcpSink::nodename() { return _nodename; }

uint64_t LcpSink::cumulative_ack() { return _cumulative_ack; }

uint32_t LcpSink::drops() { return _drops; }

void LcpSink::connect(LcpSrc &src, const Route *route) {
    _src = &src;
    switch (_route_strategy) {
    case SINGLE_PATH:
    case ECMP_FIB:
    case ECMP_FIB_ECN:
    case REACTIVE_ECN:
    case ECMP_RANDOM2_ECN:
    case ECMP_RANDOM_ECN:
        assert(route);
        //("Setting route\n");
        _route = route;
        break;
    default:
        // do nothing we shouldn't be using this route - call
        // set_paths() to set routing information
        _route = NULL;
        break;
    }

    _cumulative_ack = 0;
    _drops = 0;
}

void LcpSink::set_paths(uint32_t no_of_paths) {
    switch (_route_strategy) {
    case SCATTER_PERMUTE:
    case SCATTER_RANDOM:
    case PULL_BASED:
    case SCATTER_ECMP:
    case SINGLE_PATH:
    case NOT_SET:
        abort();

    case ECMP_FIB:
    case ECMP_FIB_ECN:
    case ECMP_RANDOM2_ECN:
    case REACTIVE_ECN:
        assert(_paths.size() == 0);
        _paths.resize(no_of_paths);
        _path_ids.resize(no_of_paths);
        for (unsigned int i = 0; i < no_of_paths; i++) {
            _paths[i] = NULL;
            _path_ids[i] = i;
        }
        _crt_path = 0;
        // permute_paths();
        break;
    case ECMP_RANDOM_ECN:
        assert(_paths.size() == 0);
        _paths.resize(no_of_paths);
        _path_ids.resize(no_of_paths);
        for (unsigned int i = 0; i < no_of_paths; i++) {
            _paths[i] = NULL;
            _path_ids[i] = i;
        }
        _crt_path = 0;
        // permute_paths();
        break;
    default:
        break;
    }
}

/**********************
 * UecRtxTimerScanner *
 **********************/

LcpRtxTimerScanner::LcpRtxTimerScanner(simtime_picosec scanPeriod, EventList &eventlist)
        : EventSource(eventlist, "RtxScanner"), _scanPeriod{scanPeriod} {
    eventlist.sourceIsPendingRel(*this, 0);
}

void LcpRtxTimerScanner::registerLcp(LcpSrc &LcpSrc) { _lcps.push_back(&LcpSrc); }

void LcpRtxTimerScanner::doNextEvent() {
    simtime_picosec now = eventlist().now();
    lcps_t::iterator i;
    for (i = _lcps.begin(); i != _lcps.end(); i++) {
        (*i)->rtx_timer_hook(now, _scanPeriod);
    }
    eventlist().sourceIsPendingRel(*this, _scanPeriod);
}

/* printf("Decreasing by %d (%f %f) at %lu\n", gent_dec_amount,
                           (x_gain_up * 1.0) * _mss * ((double)_mss / _cwnd),
                           (x_gain_up * 2.0) * _mss * ((double)_mss / (_cwnd * ((double)_bdp) / _cwnd)) *
                                   (((double)_cwnd) / _bdp),
                           GLOBAL_TIME / 1000); */