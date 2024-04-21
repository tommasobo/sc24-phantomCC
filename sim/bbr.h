// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

#ifndef BBR_H
#define BBR_H

/*
 * A BBR source and sink
 */
#include "bbr_pacer.h"
#include "config.h"
#include "eventlist.h"
#include "fairpullqueue.h"
// #include "datacenter/logsim-interface.h"
#include "bbrpacket.h"
#include "network.h"
#include "trigger.h"
#include <functional>
#include <list>
#include <map>

class BBRSink;
// class LogSimInterface;

class SentPacketBBR {
  public:
    SentPacketBBR(simtime_picosec t, uint64_t s, bool a, bool n, bool to)
            : timer{t}, seqno{s}, acked{a}, nacked{n}, timedOut{to} {}
    SentPacketBBR(const SentPacketBBR &sp)
            : timer{sp.timer}, seqno{sp.seqno}, acked{sp.acked}, nacked{sp.nacked}, timedOut{sp.timedOut} {}
    simtime_picosec timer;
    uint64_t seqno;
    bool acked;
    bool nacked;
    bool timedOut;
};

enum BBRStatus { STARTUP, DRAIN, PROBE_BW };

class BBRSrc : public PacketSink, public EventSource, public TriggerTarget {
    friend class BBRSink;

  public:
    BBRSrc(BBRLogger *logger, TrafficLogger *pktLogger, EventList &eventList, uint64_t rtt, uint64_t bdp,
           uint64_t queueDrainTime, int hops);
    // BBRSrc(UecLogger *logger, TrafficLogger* pktLogger, EventList& eventList,
    // uint64_t rtt=timeFromUs(5.25), uint64_t bdp=63000);
    ~BBRSrc();

    virtual void doNextEvent() override;

    void receivePacket(Packet &pkt) override;
    const string &nodename() override;

    virtual void connect(Route *routeout, Route *routeback, BBRSink &sink, simtime_picosec startTime);
    void startflow();
    void set_paths(vector<const Route *> *rt);
    void set_paths(uint32_t no_of_paths);
    void map_entropies();

    void set_dst(uint32_t dst) { _dstaddr = dst; }

    // called from a trigger to start the flow.
    virtual void activate() { startflow(); }
    inline flowid_t flowId() const { return _flow.flow_id(); }

    void set_end_trigger(Trigger &trigger);
    // void updateParams();

    inline void set_flowid(flowid_t flow_id) { _flow.set_flowid(flow_id); }
    inline flowid_t flow_id() const { return _flow.flow_id(); }

    void setCwnd(uint64_t cwnd) { _cwnd = cwnd; };
    void setReuse(bool reuse) { _use_good_entropies = reuse; };
    void setIgnoreEcnAck(bool ignore_ecn_ack) { _ignore_ecn_ack = ignore_ecn_ack; };
    void setIgnoreEcnData(bool ignore_ecn_data) { _ignore_ecn_data = ignore_ecn_data; };
    void setNumberEntropies(int num_entropies) { _num_entropies = num_entropies; };
    void setHopCount(int hops) { _hop_count = hops; };
    // void set_flowsize(uint64_t flow_size_in_bytes) { _flow_size = flow_size_in_bytes; }
    void setFlowSize(uint64_t flow_size) { _flow_size = flow_size; }
    void setKeepAcksInTargetRtt(bool keep) { _target_based_received = keep; }
    // void setLgs(LogSimInterface *lgs) { _lgs = lgs; };
    // void setUsingLgs(bool val) { _using_lgs = val; };
    void enableTrimming(bool enable) { _trimming_enabled = enable; };

    uint64_t getCwnd() { return _cwnd; };
    uint64_t getHighestSent() { return _highest_sent; }
    uint32_t getUnacked() { return _unacked; }
    uint32_t getConsecutiveLowRtt() { return _consecutive_low_rtt; }
    uint64_t getLastAcked() { return _last_acked; }
    uint32_t getReceivedSize() { return _received_ecn.size(); }
    uint32_t getRto() { return _rto; }
    bool supportsTrimming() { return _trimming_enabled; }
    std::size_t getEcnInTargetRtt();

    int choose_route();
    inline simtime_picosec pacing_delay_f() const { return pacing_delay; }
    int next_route();

    void set_traffic_logger(TrafficLogger *pktlogger);
    static void set_queue_type(std::string value) { queue_type = value; }
    static void set_alogirthm(std::string value) { algorithm_type = value; }
    static void set_fast_drop(bool value) { use_fast_drop = value; }
    static void set_fast_drop_rtt(int value) { fast_drop_rtt = value; }
    static void set_use_pacing(int value) { use_pacing = value; }
    static void set_pacing_delay(simtime_picosec value) { pacing_delay = value * 1000; }

    static void set_explicit_rtt(int value) { explicit_base_rtt = value; }
    static void set_explicit_bdp(int value) { explicit_bdp = value; }
    static void set_explicit_target_rtt(int value) { explicit_target_rtt = value; }

    static void set_do_jitter(bool value) { do_jitter = value; }
    static void set_do_exponential_gain(bool value) { do_exponential_gain = value; }
    static void set_use_fast_increase(bool value) { use_fast_increase = value; }
    static void set_use_super_fast_increase(bool value) { use_super_fast_increase = value; }
    static void set_gain_value_med_inc(double value) { exp_gain_value_med_inc = value; }
    static void set_jitter_value_med_inc(double value) { jitter_value_med_inc = value; }
    static void set_delay_gain_value_med_inc(double value) { delay_gain_value_med_inc = value; }
    static void set_target_rtt_percentage_over_base(int value) { target_rtt_percentage_over_base = value; }

    static void set_y_gain(double value) { y_gain = value; }
    static void set_x_gain(double value) { x_gain = value; }
    static void set_z_gain(double value) { z_gain = value; }
    static void set_w_gain(double value) { w_gain = value; }
    static void set_os_ratio_stage_1(double value) { ratio_os_stage_1 = value; }
    static void set_once_per_rtt(int value) { once_per_rtt = value; }
    static void set_quickadapt_lossless_rtt(double value) { quickadapt_lossless_rtt = value; }
    static void set_disable_case_3(double value) { disable_case_3 = value; }
    static void set_reaction_delay(int value) { reaction_delay = value; }
    static void set_precision_ts(int value) { precision_ts = value; }
    static void set_disable_case_4(double value) { disable_case_4 = value; }
    static void set_starting_cwnd(double value) { starting_cwnd = value; }
    static void set_bonus_drop(double value) { bonus_drop = value; }
    static void set_interdc_delay(uint64_t delay) { _interdc_delay = delay; }
    static void set_buffer_drop(double value) { buffer_drop = value; }
    static void set_stop_after_quick(bool value) { stop_after_quick = value; }
    static void setRouteStrategy(RouteStrategy strat) { _route_strategy = strat; }

    void set_flow_over_hook(std::function<void(const Packet &)> hook) { f_flow_over_hook = hook; }

    virtual void rtx_timer_hook(simtime_picosec now, simtime_picosec period);
    void pacedSend();
    void updateParams();
    int src_dc = 0;
    int dest_dc = 0;

    Trigger *_end_trigger = 0;
    // should really be private, but loggers want to see:
    uint64_t _highest_sent; // seqno is in bytes
    bool need_quick_adapt = false;
    uint64_t _packets_sent;
    uint64_t _new_packets_sent;
    uint64_t _rtx_packets_sent;
    uint64_t _acks_received;
    uint64_t _nacks_received;
    uint64_t _pulls_received;
    uint64_t _implicit_pulls;
    uint64_t _bounces_received;
    uint32_t _cwnd;
    uint32_t acked_bytes = 0;
    uint32_t good_bytes = 0;
    uint32_t saved_acked_bytes = 0;
    uint32_t saved_good_bytes = 0;
    uint32_t saved_trimmed_bytes = 0;
    uint32_t last_decrease = 0;
    uint32_t drop_amount = 0;
    uint32_t count_total_ecn = 0;
    uint32_t count_total_ack = 0;
    uint64_t _last_acked;
    uint32_t _flight_size;
    uint32_t _dstaddr;
    uint32_t _acked_packets;
    uint64_t _flow_start_time;
    uint64_t _next_check_window;
    uint64_t next_window_end = 0;
    bool update_next_window = true;
    bool _start_timer_window = true;
    bool stop_decrease = false;
    bool fast_drop = false;
    int ignore_for = 0;
    int count_received = 0;
    int count_ecn_in_rtt = 0;
    int count_trimmed_in_rtt = 0;
    uint32_t counter_consecutive_good_bytes = 0;
    bool increasing = false;
    int total_routes;
    int routes_changed = 0;

    int exp_avg_bts = 0;
    int exp_avg_route = 0;
    double alpha_route = 0.0625;
    uint64_t num_trim = 0;
    int current_pkt = 0;
    bool pause_send = false;
    bool paced_packet = false;

    // Custom Parameters
    static std::string queue_type;
    static std::string algorithm_type;
    static int precision_ts;
    static bool use_fast_drop;
    static int fast_drop_rtt;
    bool was_zero_before = false;
    double ideal_x = 0;
    static bool do_jitter;
    static bool do_exponential_gain;
    static bool use_fast_increase;
    static bool use_super_fast_increase;
    static double exp_gain_value_med_inc;
    static double jitter_value_med_inc;
    static double delay_gain_value_med_inc;
    static int target_rtt_percentage_over_base;
    static int ratio_os_stage_1;
    static int once_per_rtt;

    static int explicit_target_rtt;
    static int explicit_base_rtt;
    static int explicit_bdp;

    static double y_gain;
    static double x_gain;
    double initial_x_gain = 0;
    static double z_gain;
    static double w_gain;
    static bool disable_case_3;
    static bool disable_case_4;
    static double quickadapt_lossless_rtt;
    static int reaction_delay;

    static double starting_cwnd;
    static double bonus_drop;
    static double buffer_drop;
    static bool stop_after_quick;
    static RouteStrategy _route_strategy;
    static bool use_pacing;
    static simtime_picosec pacing_delay;
    bool first_quick_adapt = false;

    // BBR Specific Additions
    int delivered_so_far = 0;
    uint32_t current_window_delivery = 0;
    double best_delivery_rate = 0;
    double pacing_gain = 2.89;
    double cwnd_gain = 2.89;
    static uint64_t _interdc_delay;
    int count_pipe_full = 0;
    BBRStatus bbr_status = STARTUP;
    int pacer_d = ((4096 + 64) * 8 / 100);

    double best_bdw_window[10];
    std::vector<double> best_bdw_current_window;
    int bdw_window = 10;
    int m_cycleIndex = 4;
    int m_cycleIndex_bdw = 0;
    simtime_picosec latest_rtt_window_end = 0;
    simtime_picosec latest_phase_window_end = 0;
    double m_fullBandwidth = 0;
    int m_fullBandwidthCount = 0;

    simtime_picosec last_rtt_startup = 0;
    simtime_picosec last_seen_startup = 0;
    bool rtt_passed = true;

  private:
    uint32_t _unacked;
    uint32_t _effcwnd;
    uint16_t _mss;
    uint64_t _flow_size;
    uint64_t _rtt;
    uint64_t _rto;
    uint64_t _rto_margin;
    uint64_t _rtx_timeout;
    uint64_t _maxcwnd;
    uint16_t _crt_path = 0;
    uint32_t target_window;
    // LogSimInterface *_lgs;
    bool _flow_finished = false;
    int count_sent = 0;

    bool _rtx_timeout_pending;
    bool _rtx_pending;

    // new CC variables
    uint64_t _target_rtt;
    uint64_t _base_rtt;
    uint64_t _bdp;
    uint64_t _queue_size;
    uint32_t _consecutive_low_rtt;
    uint32_t _consecutive_no_ecn;
    uint64_t _ignore_ecn_until = 0;
    uint64_t previous_window_end = 0;
    bool _target_based_received;
    bool _using_lgs = false;
    int consecutive_nack = 0;
    bool ecn_last_rtt = false;
    int trimmed_last_rtt = 0;
    uint32_t consecutive_good_medium = 0;

    // SentPacketBBRs _sent_packets;
    uint64_t _highest_data_seq;
    uint64_t t_last_decrease = 0;
    int count_skipped = 0;
    uint64_t t_last_fairness = 0;
    uint64_t t_last_clear_byte = 0;
    bool can_decrease = false;
    bool can_clear_byte = false;
    bool can_fairness = false;
    uint64_t avg_rtt;
    int rx_count = 0;
    uint32_t achieved_bdp = 0;
    BBRLogger *_logger;
    BBRSink *_sink;
    bool m_isPipeFilled = false;

    uint16_t _crt_direction;
    vector<int> _path_ids;                 // path IDs to be used for ECMP FIB.
    vector<const Route *> _paths;          // paths in current permutation order
    vector<const Route *> _original_paths; // paths in original permutation
                                           // order
    const Route *_route;
    // order
    vector<int16_t> _path_acks;   // keeps path scores
    vector<int16_t> _path_ecns;   // keeps path scores
    vector<int16_t> _path_nacks;  // keeps path scores
    vector<int16_t> _avoid_ratio; // keeps path scores
    vector<int16_t> _avoid_score; // keeps path scores
    vector<bool> _bad_path;       // keeps path scores

    BBRPacer *generic_pacer = NULL;
    simtime_picosec pacer_start_time = 0;
    PacketFlow _flow;

    string _nodename;
    std::function<void(const Packet &p)> f_flow_over_hook;

    list<std::tuple<simtime_picosec, bool, uint64_t, uint64_t>> _received_ecn; // list of packets received
    vector<SentPacketBBR> _sent_packets;
    unsigned _nack_rtx_pending;
    vector<tuple<simtime_picosec, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t>> _list_rtt;
    vector<pair<simtime_picosec, uint64_t>> _list_cwd;
    vector<pair<simtime_picosec, uint64_t>> _list_unacked;
    vector<pair<simtime_picosec, uint64_t>> _list_acked_bytes;
    vector<pair<simtime_picosec, uint64_t>> _list_ecn_rtt;
    vector<pair<simtime_picosec, uint64_t>> _list_ecn_received;
    vector<pair<simtime_picosec, uint64_t>> _list_trimmed_rtt;
    vector<pair<simtime_picosec, uint64_t>> _list_nack;
    vector<pair<simtime_picosec, uint64_t>> _list_bts;
    vector<pair<simtime_picosec, uint64_t>> _list_fast_increase_event;
    vector<pair<simtime_picosec, uint64_t>> _list_medium_increase_event;
    vector<pair<simtime_picosec, uint64_t>> _list_fast_decrease;
    vector<pair<simtime_picosec, int>> us_to_cs;
    vector<pair<simtime_picosec, int>> ls_to_us;

    vector<const Route *> _good_entropies;
    bool _paced_packet = false;
    bool _use_good_entropies;
    bool _ignore_ecn_ack;
    bool _ignore_ecn_data;
    std::size_t _max_good_entropies;
    std::size_t _next_good_entropy;
    std::vector<int> _entropy_array;
    int _num_entropies = -1;
    int current_entropy = 0;
    bool _enableDistanceBasedRtx;
    bool _trimming_enabled;
    bool _bts_enabled = true;
    int _next_pathid;
    int _hop_count;
    int data_count_idx = 0;
    double current_bw = 5;

    vector<pair<simtime_picosec, int>> count_case_1;
    vector<pair<simtime_picosec, int>> count_case_2;
    vector<pair<simtime_picosec, int>> count_case_3;
    vector<pair<simtime_picosec, int>> count_case_4;

    void send_packets();
    void quick_adapt(bool);
    uint64_t get_unacked();
    uint64_t get_unacked_raw();

    void adjust_window(simtime_picosec ts, bool ecn, simtime_picosec rtt);
    uint32_t medium_increase(simtime_picosec);
    void fast_increase();
    void update_bw_model(double);
    void UpdatePacingRate(double);
    bool no_ecn_last_target_rtt();
    bool no_rtt_over_target_last_target_rtt();
    bool ecn_congestion();
    uint32_t InFlight(double gain);
    void drop_old_received();
    void CheckCyclePhase();
    const Route *get_path();
    void mark_received(BBRAck &pkt);
    void add_ack_path(const Route *rt);
    bool IsNextCyclePhase();
    bool RTTPassed();
    bool resend_packet(std::size_t i);
    double GetBestBw();
    void retransmit_packet();
    void processAck(BBRAck &pkt, bool);
    std::size_t get_sent_packet_idx(uint32_t pkt_seqno);

    void update_rtx_time();
    void reduce_cwnd(uint64_t amount);
    void processNack(BBRNack &nack);
    void CheckFullPipe();
    void CheckDrain();
    void simulateTrimEvent(BBRAck &nack);
    void reduce_unacked(uint64_t amount);
    void check_limits_cwnd();
    int ChooseStartingIndex();
    void apply_timeout_penalty();
};

class BBRSink : public PacketSink, public DataReceiver {
    friend class BBRSrc;

  public:
    BBRSink();

    void receivePacket(Packet &pkt) override;
    const string &nodename() override;

    void set_end_trigger(Trigger &trigger);

    uint64_t cumulative_ack() override;
    uint32_t drops() override;
    void connect(BBRSrc &src, const Route *route);
    // inline flowid_t flowId() const { return _flow.flow_id(); }
    void set_paths(uint32_t num_paths);
    void set_src(uint32_t s) { _srcaddr = s; }
    uint32_t from = 0;
    uint32_t to = 0;
    uint32_t tag = 0;
    static void setRouteStrategy(RouteStrategy strat) { _route_strategy = strat; }
    static RouteStrategy _route_strategy;
    Trigger *_end_trigger = 0;
    int pfc_just_seen = -10;

  private:
    BBRAck::seq_t _cumulative_ack;
    uint64_t _packets;
    uint32_t _srcaddr;
    uint32_t _drops;
    int ack_count_idx = 0;
    string _nodename;
    list<BBRAck::seq_t> _received; // list of packets received OOO
    uint16_t _crt_path;
    const Route *_route;
    vector<const Route *> _paths;
    vector<int> _path_ids;                 // path IDs to be used for ECMP FIB.
    vector<const Route *> _original_paths; // paths in original permutation
                                           // order
    BBRSrc *_src;

    void send_ack(simtime_picosec ts, bool marked, BBRAck::seq_t seqno, BBRAck::seq_t ackno, const Route *rt,
                  const Route *inRoute, int path_id, int);
    void send_nack(simtime_picosec ts, bool marked, BBRAck::seq_t seqno, BBRAck::seq_t ackno, const Route *rt, int);
    bool already_received(BBRPacket &pkt);
};

class BBRRtxTimerScanner : public EventSource {
  public:
    BBRRtxTimerScanner(simtime_picosec scanPeriod, EventList &eventlist);
    void doNextEvent();
    void registerBBR(BBRSrc &BBRsrc);

  private:
    simtime_picosec _scanPeriod;
    simtime_picosec _lastScan;
    typedef list<BBRSrc *> BBRs_t;
    BBRs_t _uecs;
};

#endif
