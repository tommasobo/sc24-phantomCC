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
bool LcpSrc::use_pacing = true;
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
    _bytes_until_next_epoch = _cwnd;
    _consecutive_good_epochs = 0;
    _time_of_next_epoch = TARGET_RTT_LOW;
    _time_of_last_qa = 0;
    _time_of_last_epoch = 0;
    saved_acked_bytes = 0;
    _first_qa_measurement = true;
    _did_qa_this_epoch = false;
    _ecn_fraction_ewma = 0;
    _ecn_count_this_window = 0;
    _good_count_this_window = 0;
    _last_cwnd = 0;
    _consecutive_decreases = 0;

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

        // Driving loop.
        file_name = PROJECT_ROOT_PATH / ("sim/output/driving/driving" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileDriving(file_name, std::ios_base::app);

        for (const auto &p : _list_driving_loop) {
            MyFileDriving << p.first << "," << p.second << std::endl;
        }

        MyFileCWD.close();

        // CURRENT RTT EWMA.
        file_name = PROJECT_ROOT_PATH / ("sim/output/current_rtt_ewma/current_rtt_ewma" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileRTTEWMA(file_name, std::ios_base::app);

        for (const auto &p : _list_current_rtt_ewma) {
            MyFileRTTEWMA << p.first << "," << p.second << std::endl;
        }

        // DUAL CONGESTED.
        file_name = PROJECT_ROOT_PATH / ("sim/output/dual_congested/dual_congested" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileDualCongested(file_name, std::ios_base::app);

        for (const auto &p : _list_is_dual_congested) {
            MyFileDualCongested << p << std::endl;
        }

        MyFileDualCongested.close();

        // ECN Congested.
        file_name = PROJECT_ROOT_PATH / ("sim/output/ecn_congested/ecn_congested" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileEcnCongested(file_name, std::ios_base::app);

        for (const auto &p : _list_is_ecn_congested) {
            MyFileEcnCongested << p << std::endl;
        }

        MyFileEcnCongested.close();

        // RTT congested.
        file_name = PROJECT_ROOT_PATH / ("sim/output/rtt_congested/rtt_congested" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileRttCongested(file_name, std::ios_base::app);

        for (const auto &p : _list_is_rtt_congested) {
            MyFileRttCongested << p << std::endl;
        }

        MyFileRttCongested.close();

        // ECN fraction.
        file_name = PROJECT_ROOT_PATH / ("sim/output/ecn_fraction/ecn_fraction" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileEcnFraction(file_name, std::ios_base::app);

        for (const auto &p : _list_ecn_fraction) {
            MyFileEcnFraction << p.first << "," << p.second << std::endl;
        }

        MyFileEcnFraction.close();


        MyFileRTTEWMA.close();

        // TARGET RTT LOW.
        file_name = PROJECT_ROOT_PATH / ("sim/output/target_rtt_low/target_rtt_low" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileTargetRTTLow(file_name, std::ios_base::app);

        for (const auto &p : _list_target_rtt_low) {
            MyFileTargetRTTLow << p.first << "," << p.second << std::endl;
        }

        MyFileTargetRTTLow.close();

        // TARGET RTT HIGH.
        file_name = PROJECT_ROOT_PATH / ("sim/output/target_rtt_high/target_rtt_high" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileTargetRTTHigh(file_name, std::ios_base::app);

        for (const auto &p : _list_target_rtt_high) {
            MyFileTargetRTTHigh << p.first << "," << p.second << std::endl;
        }

        MyFileTargetRTTHigh.close();

        // BAREMETAL RTT.
        file_name = PROJECT_ROOT_PATH / ("sim/output/baremetal_latency/baremetal_latency" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileBaremetalRTT(file_name, std::ios_base::app);

        for (const auto &p : _list_baremetal_latency) {
            MyFileBaremetalRTT << p.first << "," << p.second << std::endl;
        }

        MyFileBaremetalRTT.close();

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
    // cout << "PaceDelayChange: Is it time to update? " << is_time_to_update << " at " << GLOBAL_TIME / 1000 << endl;
    // cout << "PaceDelayChange: Last change was " << eventlist().now() - last_pac_change << " ago at " << GLOBAL_TIME / 1000 << endl;
    if (LCP_USE_PACING && is_time_to_update) {
        pacing_delay = (((double)_mss) / (((double)_cwnd) / (_base_rtt / 1000.0))) * (1.0 - LCP_PACING_BONUS);
        // cout << "Base RTT: " << _base_rtt << " at " << GLOBAL_TIME / 1000 << endl;
        // cout << "PaceDelayChange: Setting the pacing delay to: " << pacing_delay << " (ns) at " << GLOBAL_TIME / 1000
        //      << " with cwnd: " << _cwnd << " and mss: " << _mss << endl;
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
void LcpSrc::updateParams(uint64_t base_rtt_intra, uint64_t base_rtt_inter, uint64_t bdp_intra, uint64_t bdp_inter, uint64_t intra_queuesize, uint64_t inter_queuesize) {
    uint64_t queuesize_bytes = 0;
    if (src_dc != dest_dc) {
        _hop_count = 9;
        _base_rtt = base_rtt_inter;
        _bdp = bdp_inter;
        queuesize_bytes = inter_queuesize;
        cout << "Base RTT (9): " << _base_rtt << endl;
        cout << "    Base rtt inter: " << base_rtt_inter << endl;
    } else {
        _hop_count = 6;
        _base_rtt = base_rtt_intra;
        _bdp = bdp_intra;
        queuesize_bytes = intra_queuesize;
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

    if (starting_cwnd == 1) {
        cout << "Finally setting CWND to: " << _bdp << endl;
        _cwnd = _bdp;
    } else {
        cout << "thestartcwnd " << starting_cwnd << endl;
        _cwnd = starting_cwnd;
    }
    _bytes_until_next_epoch = min((uint64_t)_cwnd, _flow_size);

    if (LCP_DELTA == 1) {
        LCP_DELTA = _bdp * 0.01;
    }
    BAREMETAL_RTT = _base_rtt;
    TARGET_RTT_LOW = BAREMETAL_RTT * 1.05;
    float queue_latency_ns = (float) queuesize_bytes * 8 / (float) LINK_SPEED_MODERN;
    _max_queue_latency = queue_latency_ns;
    TARGET_RTT_HIGH = LCP_TARGET_RTT_HIGH_FRACTION * queue_latency_ns * 1000.0 + BAREMETAL_RTT;
    cout << "TARGET_RTT_HIGH: " << TARGET_RTT_HIGH << endl;
    cout << "    queue_latency_ns: " << queue_latency_ns << endl;
    cout << "    baremetal_rtt: " << BAREMETAL_RTT << endl;
    cout << "    queuesize_bytes: " << queuesize_bytes << endl;
    cout << "    LINK_SPEED_MODERN: " << LINK_SPEED_MODERN << endl;

    assert(TARGET_RTT_HIGH > TARGET_RTT_LOW);

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

    // Write all of this to a file in csv.
    std::string file_name = PROJECT_ROOT_PATH / ("sim/output/params/params" + _name + "_" + std::to_string(tag) + ".txt");
    std::ofstream MyFile(file_name, std::ios_base::app);

    MyFile << "Link speed (Gbps)," << LINK_SPEED_MODERN << std::endl;
    MyFile << "BDP (KB)," << _bdp / 1000 << std::endl;
    MyFile << "Baremetal RTT (us)," << BAREMETAL_RTT / 1000000 << std::endl;
    MyFile << "Target RTT Low (us)," << TARGET_RTT_LOW / 1000000 << std::endl;
    MyFile << "Target RTT High (us)," << TARGET_RTT_HIGH / 1000000 << std::endl;
    MyFile << "MSS (bytes)," << PKT_SIZE_MODERN << std::endl;
    float max_queueing_latency_us = ((float) (queuesize_bytes * 8) / (float) LINK_SPEED_MODERN) / 1000.0;
    MyFile << "Max Queueing Latency (us)," << max_queueing_latency_us << std::endl;
    MyFile << "Starting cwnd (bytes)," << starting_cwnd << std::endl;
    MyFile << "Queue Size (bytes)," << queuesize_bytes << std::endl;
    MyFile << "Delta," << LCP_DELTA << std::endl;
    MyFile << "Beta," << LCP_BETA << std::endl;
    MyFile << "Alpha," << LCP_ALPHA << std::endl;
    MyFile << "Gamma," << LCP_GAMMA << std::endl;
    MyFile << "K," << LCP_K << std::endl;
    MyFile << "Fast Increase Threshold," << LCP_FAST_INCREASE_THRESHOLD << std::endl;
    MyFile << "Use Quick Adapt," << LCP_USE_QUICK_ADAPT << std::endl;
    MyFile << "Use Pacing," << LCP_USE_PACING << std::endl;
    MyFile << "Use Fast Increase," << LCP_USE_FAST_INCREASE << std::endl;
    MyFile << "Pacing Bonus," << LCP_PACING_BONUS << std::endl;
    MyFile << "Use Min RTT," << LCP_USE_MIN_RTT << std::endl;
    MyFile << "Use Aggressive Decrease," << LCP_USE_AGGRESSIVE_DECREASE << std::endl;
    MyFile << "Gemini Queueing Delay Threshold (us)," << LCP_GEMINI_TARGET_QUEUEING_LATENCY / 1000000 << std::endl;
    MyFile << "Gemini Beta," << LCP_GEMINI_BETA << std::endl;
    MyFile << "Gemini H," << LCP_GEMINI_H << std::endl;

    MyFile.close();


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
        _list_acked_bytes.push_back(std::make_pair(eventlist().now() / 1000, _last_acked));
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
    if (saved_acked_bytes > 0 && _did_qa_this_epoch == false) {
        // Update window and ignore count
        cout << "Quick Adapt: " << _name << "_" << tag << " going from " << _cwnd << " to " << saved_acked_bytes << endl;
        _cwnd = saved_acked_bytes * bonus_drop;
        _list_fast_decrease.push_back(
                        std::make_pair(eventlist().now() / 1000, 1));

        check_limits_cwnd();

        _did_qa_this_epoch = true;
    }
}

void LcpSrc::quick_adapt(bool trimmed) {
    if (_first_qa_measurement) {
        _first_qa_measurement = false;
    } else {
        saved_acked_bytes = acked_bytes;
    }

    // cout << "QADEBUG: Acked bytes " << saved_acked_bytes << " time since last qa: " << eventlist().now() - _time_of_last_qa << endl;
    _time_of_last_qa = eventlist().now();

    acked_bytes = 0;
}

void LcpSrc::processNack(UecNack &pkt) {
    _bytes_until_next_epoch -= _mss;
    num_trim++;
    count_trimmed_in_rtt++;
    consecutive_nack++;
    trimmed_last_rtt++;
    consecutive_good_medium = 0;
    acked_bytes += 64;
    saved_trimmed_bytes += 64;

    last_ecn_seen = eventlist().now();
    last_phantom_increase = eventlist().now();

    if (LCP_USE_QUICK_ADAPT) {
        //quick_adapt_drop();
    }

    if (algorithm_type == "tcp") {
        _cwnd /= 2;
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
        // abort(); // not sure if this can ever happen - if it can, remove this
                 // line
        return _crt_path;
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

        // FCT.
        auto fct_file_name = PROJECT_ROOT_PATH / ("sim/output/fct/fct" + _name + "_" + std::to_string(tag) + ".txt");
        std::ofstream MyFileFCT(fct_file_name, std::ios_base::app);

        MyFileFCT << timeAsUs(eventlist().now()) - timeAsUs(_flow_start_time) << std::endl;

        MyFileFCT.close();

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
    uint32_t old_cwnd = _cwnd;
    _cwnd += _mss;
    cout << "CWND change: " << _name + "_" + std::to_string(tag) << " FI from " << old_cwnd << " to " << _cwnd << " maxcwnd: " << _maxcwnd << endl;
    _list_fast_increase_event.push_back(std::make_pair(eventlist().now() / 1000, 1));
}

void LcpSrc::adjust_window(simtime_picosec ts, bool ecn, simtime_picosec rtt, uint32_t ackno) {

    if (algorithm_type == "lcp") {
        _bytes_until_next_epoch -= _mss;

        if (ecn) {
            _ecn_count_this_window++;
        } else {
            _good_count_this_window++;
        }

        if (_current_rtt_ewma == 0) {
            _current_rtt_ewma = rtt;
        }
        
        // Update RTT_EWMA.
        if (LCP_USE_REGULAR_EWMA) {
            _current_rtt_ewma = (simtime_picosec)(_current_rtt_ewma * (1.0 - LCP_ALPHA) + LCP_ALPHA * rtt);
        } else {
            if (rtt >= TARGET_RTT_HIGH) {
                _current_rtt_ewma = rtt;
            } else {
                _current_rtt_ewma = min((simtime_picosec) (_current_rtt_ewma * (1.0 - LCP_ALPHA) + LCP_ALPHA * rtt), rtt);
            }
        }

        if (rtt > TARGET_RTT_LOW) {
            _consecutive_good_epochs = 0;
        }

        if (LCP_DO_PER_ACK_INCREASE && !(ecn || rtt > TARGET_RTT_HIGH)) { 
            // Should increase.
            if (LCP_USE_FAST_INCREASE && _consecutive_good_epochs > LCP_FAST_INCREASE_THRESHOLD) {
                printf("Doing fi\n");
                fast_increase();
            } else {
                // Do a regular increase.
                // Increase the window.
                uint32_t num_acks = _cwnd / _mss;
                uint32_t cwnd_before = _cwnd;
                if (_current_rtt_ewma < TARGET_RTT_LOW) {
                    _cwnd += (uint32_t) LCP_DELTA / num_acks;
                    cout << "    CWND change: " << _name << "_" << tag << " less than all, go from " << cwnd_before << " to " << _cwnd << " num_acks: " << num_acks << " increasing by " << (uint32_t) LCP_DELTA / num_acks << "LCP_DELTA: " << LCP_DELTA << endl;
                }
            }
        }
        // cout << "Bytes until epoch: " << _bytes_until_next_epoch << endl;
        if (_bytes_until_next_epoch <= 0) {
            cout << "Epoch triggereed " << _name << "_" << tag << "with " << _bytes_until_next_epoch << " bytes remaining, time since last epoch: " << eventlist().now() - _time_of_last_epoch << endl;
            cout << " Cwnd before: " << _last_cwnd << " Cwnd after: " << _cwnd << " Good: " << _good_count_this_window << " ECN: " << _ecn_count_this_window << endl;
            _last_cwnd = _cwnd;
            _time_of_last_epoch = eventlist().now();

            quick_adapt(false); // Update QA state.

            // Update ECN state.
            float new_ecn_fraction = (float) _ecn_count_this_window / ((float) _good_count_this_window + (float) _ecn_count_this_window);
            _ecn_fraction_ewma = _ecn_fraction_ewma * (1.0 - LCP_ECN_ALPHA) + new_ecn_fraction * LCP_ECN_ALPHA;

            // Calculate the ECN reduction.
            bool is_ecn_congested = _ecn_fraction_ewma > LCP_ECN_FRACTION_THRESHOLD && LCP_USE_ECN;
            float k = kmin_double * (float) _bdp;
            float F = 4.0 * k / ((float) _bdp + k);
            float ecn_reduction;
            if (is_ecn_congested) {
                ecn_reduction = _ecn_fraction_ewma * F;
            } else {
                ecn_reduction = 0.0;
            }
            
            // Calculate the RTT reduction.
            if (_previous_rtt_ewma == timeFromMs(0)) {
                _previous_rtt_ewma = _current_rtt_ewma;
            }
            bool is_rtt_congested = false;
            float rtt_reduction = 0.0;
            int64_t rtt_change = (int64_t) _current_rtt_ewma - (int64_t) _previous_rtt_ewma;
            double gradient = ((double) rtt_change) / ((double) TARGET_RTT_LOW);
            cout << _name << "_" << tag << "Current RTT: " << _current_rtt_ewma << " Previous RTT: " << _previous_rtt_ewma << " RTT Change: " << rtt_change << " Gradient: " << gradient << endl;
            if (_current_rtt_ewma > TARGET_RTT_HIGH) {
                cout << "Consecutive Decreases: " << _consecutive_decreases << endl;
                if (_consecutive_decreases > LCP_CONSECUTIVE_DECREASES_FOR_QA && LCP_USE_QUICK_ADAPT) {
                    cout << "Tried QA: " << tried_qa << endl;
                    quick_adapt_drop();
                    _did_qa_this_epoch = true;
                    _consecutive_decreases = 0;
                } else {
                    rtt_reduction = 0.05;
                    is_rtt_congested = true;
                    _consecutive_decreases++;
                }
            }
            else if (_current_rtt_ewma > TARGET_RTT_LOW) {
                rtt_reduction = 0.05;
                is_rtt_congested = true;
                _consecutive_decreases++;
            } else {
                _consecutive_decreases = 0;
            }

            if (!_did_qa_this_epoch) { // Only change CWND if we haven't already done so this epoch.
                uint32_t cwnd_before = _cwnd;

                if (is_ecn_congested || is_rtt_congested) { // If congested reduce based on max.
                    float max_reduction = max(ecn_reduction, rtt_reduction);
                    _cwnd *= (1.0 - max_reduction);
                    _consecutive_good_epochs = 0;
                    std::string change_type = "ECNREDUCE";
                    if (rtt_reduction > ecn_reduction) {
                        change_type = "RTTREDUCE";
                    }

                    if (is_rtt_congested && rtt_reduction > ecn_reduction) {
                        _list_is_rtt_congested.push_back(eventlist().now() / 1000);
                    } else {
                        _list_is_ecn_congested.push_back(eventlist().now() / 1000);
                    }
                    uint64_t current_time = eventlist().now() / 1000000;
                    cout << "    CWND change at " << current_time << ": " << change_type << " " << _name << "_" << tag << " congested from " << cwnd_before << " to " <<
                            _cwnd << " max_reduction: " << max_reduction <<
                            " ecn_reduction: " << ecn_reduction << " rtt_reduction: " << rtt_reduction <<
                            " is_ecn_congested: " << is_ecn_congested << " is_rtt_congested: " << is_rtt_congested << " k: " << k << " F: " << F << " ecn_fraction: " << _ecn_fraction_ewma << endl;
                } else { // If not congested increase.
                    _consecutive_good_epochs++;

                    if (!LCP_DO_PER_ACK_INCREASE) {
                        // Increase the window.
                        if (_current_rtt_ewma < TARGET_RTT_LOW) {
                            _cwnd += (uint32_t) LCP_DELTA;
                            
                            cout << "    CWND change: " << nodename() << " less than all, go from " << cwnd_before << " to " << _cwnd << endl;
                        } else {
                            _cwnd += (uint32_t) LCP_DELTA / 10;
                            cout << "    CWND change: " << nodename() << " between with negative gradient go from " << cwnd_before << " to " << _cwnd << " delta: " << LCP_DELTA << endl;
                        }
                    }
                }
            }

            // Reset all state for next time.
            _did_qa_this_epoch = false;
            _good_count_this_window = 0;
            _ecn_count_this_window = 0;
            _previous_rtt_ewma = _current_rtt_ewma;

            if (COLLECT_DATA) {
                _list_current_rtt_ewma.push_back(std::make_pair(eventlist().now() / 1000, _current_rtt_ewma / 1000));
                _list_ecn_fraction.push_back(std::make_pair(eventlist().now() / 1000, _ecn_fraction_ewma));
                _list_target_rtt_low.push_back(std::make_pair(eventlist().now() / 1000, TARGET_RTT_LOW / 1000));
                _list_target_rtt_high.push_back(std::make_pair(eventlist().now() / 1000, TARGET_RTT_HIGH / 1000));
                _list_baremetal_latency.push_back(std::make_pair(eventlist().now() / 1000, BAREMETAL_RTT / 1000));
            }
            check_limits_cwnd();
            _bytes_until_next_epoch = _cwnd;
        }

        check_limits_cwnd();

        // cout << "DEBUGMSGACK: Node: " << _name << "_" << std::to_string(tag) << " Time: " << eventlist().now() / 1000000 << "  ac_ received: " << ackno << endl;
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
    } else if (algorithm_type == "tcp") {
        _cwnd +=  (float) _mss * (float) _mss / (float)_cwnd;
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

        // cout << "DEBUGMSGSENT: Node: " << _name << "_" << std::to_string(tag) << " Time: " << eventlist().now() / 1000000 << "  sent_packet: " << p->seqno() << endl;

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
    // if (_route_strategy != ECMP_FIB && _route_strategy != ECMP_FIB_ECN && _route_strategy != ECMP_FIB2_ECN &&
    //     _route_strategy != REACTIVE_ECN && _route_strategy != ECMP_RANDOM_ECN && _route_strategy != ECMP_RANDOM2_ECN) {
    //     cout << "Set paths uec (path_count) called with wrong route "
    //             "strategy "
    //          << _route_strategy << endl;
    //     abort();
    // }

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
    case ECMP_FIB:
    case ECMP_FIB_ECN:
    case REACTIVE_ECN:
        // shouldn't call this with these strategies
        abort();
    case SINGLE_PATH:
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
    // _bytes_until_next_epoch -= _mss;
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

    // cout << "DEBUGMSGRETRANS: Node: " << _name << "_" << std::to_string(tag) << " Time: " << eventlist().now() / 1000000 << "  retrans_packet: " << p->seqno() << endl;
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
    case SINGLE_PATH:
        ack = UecAck::newpkt(_src->_flow, *_route, seqno, ackno, 0, _srcaddr);
        ack->set_pathid(_path_ids[_crt_path]);
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

    // ack->inf = inRoute;
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
    case NOT_SET:
        abort();
    case SINGLE_PATH:
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
 * LcpRtxTimerScanner *
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


/**********************
 * LcpEpochAgent *
 **********************/  

LcpEpochAgent::LcpEpochAgent(EventList &event_list, LcpSrc *flow)
        : EventSource(event_list, "window_adjuster"), flow(flow)
        {
    // _next_epoch_time = eventlist().now() + TARGET_RTT_LOW;
    cout << "TRTTLOW: " << TARGET_RTT_LOW << endl;
    // eventlist().sourceIsPendingRel(*this, random() % TARGET_RTT_LOW);
    eventlist().sourceIsPendingRel(*this, 0);
}

void LcpEpochAgent::doNextEvent() {
    // if (flow->_highest_sent > 0 && flow->count_total_ack > 0 && flow->_good_count_this_window + flow->_ecn_count_this_window > 0) {
    //     // flow->quick_adapt(false); // Update QA state.

    //     // // Update ECN state.
    //     // float new_ecn_fraction = (float) flow->_ecn_count_this_window / ((float) flow->_good_count_this_window + (float) flow->_ecn_count_this_window);
    //     // flow->_ecn_fraction_ewma = flow->_ecn_fraction_ewma * (1.0 - LCP_ECN_ALPHA) + new_ecn_fraction * LCP_ECN_ALPHA;

    //     // // Calculate the ECN reduction.
    //     // bool is_ecn_congested = flow->_ecn_fraction_ewma > LCP_ECN_FRACTION_THRESHOLD && LCP_USE_ECN;
    //     // float k = flow->kmin_double * (float) flow->_bdp;
    //     // float F = 4.0 * k / ((float) flow->_bdp + k);
    //     // float ecn_reduction;
    //     // if (is_ecn_congested) {
    //     //     ecn_reduction = flow->_ecn_fraction_ewma * F;
    //     // } else {
    //     //     ecn_reduction = 0.0;
    //     // }
        
    //     // // Calculate the RTT reduction.
    //     // if (flow->_previous_rtt_ewma == timeFromMs(0)) {
    //     //     flow->_previous_rtt_ewma = flow->_current_rtt_ewma;
    //     // }
    //     // bool is_rtt_congested = false;
    //     // float rtt_reduction = 0.0;
    //     // int64_t rtt_change = (int64_t) flow->_current_rtt_ewma - (int64_t) flow->_previous_rtt_ewma;
    //     // cout << "Current RTT: " << flow->_current_rtt_ewma << " Previous RTT: " << flow->_previous_rtt_ewma << " RTT Change: " << rtt_change << endl;
    //     // double gradient = ((double) rtt_change) / ((double) TARGET_RTT_LOW);
    //     // if (flow->_current_rtt_ewma > TARGET_RTT_HIGH) {
    //     //     rtt_reduction = 0.5;
    //     //     is_rtt_congested = true;
    //     // } else if (flow->_current_rtt_ewma > TARGET_RTT_LOW && gradient > 0.0) {
    //     //     double gradient_change = min(max(0.0, gradient * LCP_BETA), 1.0);
    //     //     rtt_reduction = gradient_change;
    //     //     is_rtt_congested = true;
    //     // }

    //     // if (!flow->_did_qa_this_epoch) { // Only change CWND if we haven't already done so this epoch.
    //     //     uint32_t cwnd_before = flow->_cwnd;

    //     //     if (is_ecn_congested || is_rtt_congested) { // If congested reduce based on max.
    //     //         float max_reduction = max(ecn_reduction, rtt_reduction);
    //     //         flow->_cwnd *= (1.0 - max_reduction);
    //     //         flow->_consecutive_good_epochs = 0;

    //     //         cout << "    CWND change: " << flow->nodename() << " congested from " << flow->_cwnd << " to " <<
    //     //                 flow->_cwnd * (1.0 - max_reduction) << " max_reduction: " << max_reduction <<
    //     //                 " ecn_reduction: " << ecn_reduction << " rtt_reduction: " << rtt_reduction <<
    //     //                 " is_ecn_congested: " << is_ecn_congested << " is_rtt_congested: " << is_rtt_congested << " k: " << k << " F: " << F << " ecn_fraction: " << flow->_ecn_fraction_ewma << endl;

    //     //         // Log the congestion type.
    //     //         if (is_ecn_congested && is_rtt_congested) {
    //     //             flow->_list_is_dual_congested.push_back(eventlist().now() / 1000);
    //     //         } else if (is_ecn_congested) {
    //     //             flow->_list_is_ecn_congested.push_back(eventlist().now() / 1000);
    //     //         } else if (is_rtt_congested) {
    //     //             flow->_list_is_rtt_congested.push_back(eventlist().now() / 1000);
    //     //         }
    //     //     } else { // If not congested increase.
    //     //         flow->_consecutive_good_epochs++;

    //     //         // Increase the window.
    //     //         if (flow->_current_rtt_ewma < TARGET_RTT_LOW) {
    //     //             flow->_cwnd += (uint32_t) LCP_DELTA;
                    
    //     //             cout << "    CWND change: " << flow->nodename() << " less than all, go from " << cwnd_before << " to " << flow->_cwnd << endl;
    //     //         } else {
    //     //             flow->_cwnd += (uint32_t) LCP_DELTA / 10;
    //     //             cout << "    CWND change: " << flow->nodename() << " between with negative gradient go from " << cwnd_before << " to " << flow->_cwnd << " delta: " << LCP_DELTA << endl;
    //     //         }
    //     //     }
    //     // }

        

    //     // // Reset all state for next time.
    //     // flow->_did_qa_this_epoch = false;
    //     // flow->_good_count_this_window = 0;
    //     // flow->_ecn_count_this_window = 0;
    //     // flow->_previous_rtt_ewma = flow->_current_rtt_ewma;

    //     // if (COLLECT_DATA) {
    //     //     flow->_list_current_rtt_ewma.push_back(std::make_pair(eventlist().now() / 1000, flow->_current_rtt_ewma / 1000));
    //     //     flow->_list_ecn_fraction.push_back(std::make_pair(eventlist().now() / 1000, flow->_ecn_fraction_ewma));
    //     //     flow->_list_target_rtt_low.push_back(std::make_pair(eventlist().now() / 1000, TARGET_RTT_LOW / 1000));
    //     //     flow->_list_target_rtt_high.push_back(std::make_pair(eventlist().now() / 1000, TARGET_RTT_HIGH / 1000));
    //     //     flow->_list_baremetal_latency.push_back(std::make_pair(eventlist().now() / 1000, BAREMETAL_RTT / 1000));
    //     // }
    // }

    // // Then schedule the next epoch as long as we haven't reached the end.
    // if (!flow->_flow_finished) {
    //     // Add some random jitter so the flows don't all align. Make it max 1% of the epoch time.
    //     if (!flow->_current_rtt_ewma == 0) {
    //         eventlist().sourceIsPendingRel(*this, flow->_current_rtt_ewma);
    //     } else {
    //         eventlist().sourceIsPendingRel(*this, TARGET_RTT_LOW);
    //     }
    // }
    (void)0;
}