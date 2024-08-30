// -*- c-basic-offset: 4; tab-width: 8; indent-tabs-mode: t -*-

#ifndef LCP_H
#define LCP_H

/*
 * A UEC source and sink
 */
#include "config.h"
#include "eventlist.h"
#include "fairpullqueue.h"
#include "lcp_pacer.h"
// #include "datacenter/logsim-interface.h"
#include "network.h"
#include "trigger.h"
#include "uecpacket.h"
#include <functional>
#include <list>
#include <map>

class LcpSink;
// class LogSimInterface;

class LcpSentPacket {
  public:
    LcpSentPacket(simtime_picosec t, uint64_t s, bool a, bool n, bool to)
            : timer{t}, seqno{s}, acked{a}, nacked{n}, timedOut{to} {}
    LcpSentPacket(const LcpSentPacket &sp)
            : timer{sp.timer}, seqno{sp.seqno}, acked{sp.acked}, nacked{sp.nacked}, timedOut{sp.timedOut} {}
    simtime_picosec timer;
    uint64_t seqno;
    bool acked;
    bool nacked;
    bool timedOut;
};

class LcpSrc : public PacketSink, public EventSource, public TriggerTarget {
    friend class LcpSink;

  public:
    LcpSrc(UecLogger *logger, TrafficLogger *pktLogger, EventList &eventList, uint64_t rtt, uint64_t bdp,
           uint64_t queueDrainTime, int hops);
    ~LcpSrc();

    virtual void doNextEvent() override;

    void receivePacket(Packet &pkt) override;
    const string &nodename() override;

    virtual void connect(Route *routeout, Route *routeback, LcpSink &sink, simtime_picosec startTime);
    void startflow();
    void set_paths(vector<const Route *> *rt);
    void set_paths(uint32_t no_of_paths);
    void map_entropies();

    void set_dst(uint32_t dst) { _dstaddr = dst; }

    // called from a trigger to start the flow.
    virtual void activate() { startflow(); }

    void set_end_trigger(Trigger &trigger);

    inline void set_flowid(flowid_t flow_id) { _flow.set_flowid(flow_id); }
    inline flowid_t flow_id() const { return _flow.flow_id(); }

    void setCwnd(uint64_t cwnd) { _cwnd = cwnd; };
    void setReuse(bool reuse) { _use_good_entropies = reuse; };
    void setNumberEntropies(int num_entropies) { _num_entropies = num_entropies; };
    void setHopCount(int hops) {
        _hop_count = hops;
        printf("Hop Count is %d\n", hops);
    };
    void setFlowSize(uint64_t flow_size) { _flow_size = flow_size; }

    uint64_t getCwnd() { return _cwnd; };
    uint64_t getHighestSent() { return _highest_sent; }
    uint32_t getUnacked() { return _unacked; }
    uint32_t getConsecutiveLowRtt() { return _consecutive_low_rtt; }
    uint64_t getLastAcked() { return _last_acked; }
    uint32_t getReceivedSize() { return _received_ecn.size(); }
    uint32_t getRto() { return _rto; }

    int choose_route();
    int next_route();

    void set_traffic_logger(TrafficLogger *pktlogger);
    static void set_kmax(double value) { kmax_double = value; }
    static void set_kmin(double value) { kmin_double = value; }
    static void set_queue_type(std::string value) { queue_type = value; }
    static void set_alogirthm(std::string value) { algorithm_type = value; }
    static void set_use_pacing(int value) { use_pacing = value; }
    static void set_pacing_delay(simtime_picosec value) { pacing_delay = value * 1000; }

    void resetQACounting();
    // static void set_os_ratio_stage_1(double value) { ratio_os_stage_1 = value; }
    static void set_frequency(int value) { freq = value; }
    static void set_precision_ts(int value) { precision_ts = value; }
    static void set_starting_cwnd(double value) { starting_cwnd = value; }
    static void set_bonus_drop(double value) { bonus_drop = value; }
    static void set_bts(bool use_b) { use_bts = use_b; }
    static void setRouteStrategy(RouteStrategy strat) { _route_strategy = strat; }

    void set_flow_over_hook(std::function<void(const Packet &)> hook) { f_flow_over_hook = hook; }

    simtime_picosec targetDelay(uint32_t cwnd);

    virtual void rtx_timer_hook(simtime_picosec now, simtime_picosec period);
    void pacedSend();
    static void set_interdc_delay(uint64_t delay) { _interdc_delay = delay; }
    void updateParams(uint64_t base_rtt_intra, uint64_t base_rtt_inter, uint64_t bdp_intra, uint64_t bdp_inter, uint64_t intra_queuesize, uint64_t inter_queuesize);

    void track_sending_rate();
    void track_ecn_rate();
    void check_limits_cwnd();
    void quick_adapt(bool);

    Trigger *_end_trigger = 0;
    // should really be private, but loggers want to see:
    uint64_t _highest_sent; // seqno is in bytes
    bool need_quick_adapt = false;
    uint64_t _packets_sent;
    uint64_t _new_packets_sent;
    uint64_t _rtx_packets_sent;
    uint64_t _acks_received;
    uint64_t _nacks_received;
    static simtime_picosec _interdc_delay; // The one way delay between datacenters.
    float _cwnd;
    uint32_t acked_bytes = 0;
    uint32_t good_bytes = 0;
    uint32_t saved_acked_bytes = 0; // Stores received bytes for QA.
    simtime_picosec qa_period_time = 0; // Keeps track of the measurement period for received bytes.
    uint32_t saved_trimmed_bytes = 0;
    static int freq; // Modifies the frequencey with which we count QA.
    uint32_t count_total_ecn = 0;
    uint32_t count_total_ack = 0;
    uint64_t _last_acked;
    uint32_t _flight_size;
    uint32_t _dstaddr;
    uint32_t _acked_packets;
    uint64_t _flow_start_time;
    uint64_t _next_check_window;
    uint64_t next_window_end = 0;
    uint64_t qa_window_start = 0;
    bool update_next_window = true;
    bool _start_timer_window = true;
    bool _paced_packet = false;
    bool fast_drop = false;
    int ignore_for = 0;
    int count_received = 0;
    int count_ecn_in_rtt = 0;
    int count_trimmed_in_rtt = 0;
    uint32_t counter_consecutive_good_bytes = 0;
    bool increasing = false;
    int bts_received = 0;
    int tot_pkt_seen_qa = 0;

    int total_routes;
    int routes_changed = 0;
    int current_pkt = 0;
    bool pause_send = false;
    int total_pkt = 0;
    int total_nack = 0;
    uint64_t send_size = 0;

    // Custom Parameters
    static int adjust_packet_counts;
    static std::string queue_type;
    static std::string algorithm_type;
    static int precision_ts;
    static bool use_fast_drop;
    bool was_zero_before = false;
    double ideal_x = 0;
    static bool do_jitter;
    static int jump_to;
    int qa_count = 0;
    static double kmax_double;
    static double gemini_f;
    static bool use_bts;
    static double kmin_double;
    double phantom_size_calc = 0;
    simtime_picosec last_phantom_increase = 0;
    simtime_picosec last_qa_event = 0;
    simtime_picosec next_increase_at = 0;
    int increasing_for = 1;
    int src_dc = 0;
    int dest_dc = 0;
    int num_trim = 0;

    static uint64_t _switch_queue_size;
    simtime_picosec last_adjust_ts;
    vector<int> _good_entropies_list;
    int curr_entropy = 0;

    static double starting_cwnd;
    static double bonus_drop;
    static RouteStrategy _route_strategy;
    static bool use_pacing;
    static simtime_picosec pacing_delay;
    bool first_quick_adapt = false;

    // LCP.
    simtime_picosec _current_rtt_ewma;    
    float _ecn_fraction_ewma;
    float _ecn_count_this_window;
    float _good_count_this_window;
    uint64_t _next_measurement_seq_no; // Next sequence number to declare end of epoch.
    uint32_t _consecutive_good_epochs; // Counts number of epochs without congestion.
    uint64_t _next_qa_sn; // Next sequence number that must be received before window changes are allowed.
    simtime_picosec _time_of_last_qa; // Time since last QA received_bytes measurement.
    bool _first_qa_measurement; // Flag to indicate if this is the first QA measurement.
    vector<pair<simtime_picosec, uint64_t>> _list_current_rtt_ewma;
    vector<pair<simtime_picosec, uint64_t>> _list_ecn_fraction;
    vector<pair<simtime_picosec, uint64_t>> _list_target_rtt_low;
    vector<pair<simtime_picosec, uint64_t>> _list_target_rtt_high;
    vector<pair<simtime_picosec, uint64_t>> _list_baremetal_latency;
    vector<simtime_picosec> _list_is_rtt_congested;
    vector<simtime_picosec> _list_is_ecn_congested;
    vector<pair<simtime_picosec, uint64_t>> _list_retrans;
    uint32_t _consecutive_decreases;


    uint16_t _mss;
    bool _flow_finished = false;
    uint64_t _bdp;

  private:
    uint32_t _unacked;
    uint32_t _effcwnd;
    uint64_t _flow_size;
    uint64_t _rtt;
    uint64_t _rto;
    uint64_t _rto_margin;
    uint64_t _rtx_timeout;
    uint64_t _maxcwnd;
    uint16_t _crt_path = 0;
    uint32_t target_window;
    // LogSimInterface *_lgs;

    bool _rtx_timeout_pending;
    bool _rtx_pending;

    // new CC variables
    simtime_picosec _base_rtt; // Picoseconds.
    uint64_t _queue_size;
    uint32_t _consecutive_low_rtt;
    uint32_t _consecutive_no_ecn;
    uint64_t last_pac_change = 0;
    uint64_t previous_window_end = 0;
    bool _target_based_received;
    bool _using_lgs = false;
    int consecutive_nack = 0;
    UecLogger *_logger;
    LcpSink *_sink;

    // uint16_t _crt_direction;
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

    LcpSmarttPacer *generic_pacer = NULL;
    simtime_picosec pacer_start_time = 0;
    PacketFlow _flow;

    simtime_picosec tracking_period = 0;
    simtime_picosec qa_period = 0;
    simtime_picosec last_track_ts = 0;
    uint64_t tracking_bytes = 0;

    string _nodename;
    std::function<void(const Packet &p)> f_flow_over_hook;

    list<std::tuple<simtime_picosec, bool, uint64_t, uint64_t>> _received_ecn; // list of packets received
    vector<LcpSentPacket> _sent_packets;
    unsigned _nack_rtx_pending;
    vector<tuple<simtime_picosec, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t>> _list_rtt;
    vector<pair<simtime_picosec, uint64_t>> _list_cwd;
    vector<pair<simtime_picosec, uint64_t>> _list_unacked;
    vector<pair<simtime_picosec, uint64_t>> _list_sent;
    vector<pair<simtime_picosec, uint64_t>> _list_acked_bytes;
    vector<pair<simtime_picosec, uint64_t>> _list_ecn_rtt;
    vector<pair<simtime_picosec, double>> _list_ecn_ewma;
    vector<pair<simtime_picosec, uint64_t>> _list_ecn_received;
    vector<pair<simtime_picosec, uint64_t>> _list_trimmed_rtt;
    vector<pair<simtime_picosec, uint64_t>> _list_nack;
    vector<pair<simtime_picosec, uint64_t>> _list_bts;
    vector<pair<simtime_picosec, uint64_t>> _list_fast_increase_event;
    vector<pair<simtime_picosec, uint64_t>> _list_fast_decrease;

    vector<const Route *> _good_entropies;
    bool _use_good_entropies;
    std::size_t _max_good_entropies;
    std::size_t _next_good_entropy;
    std::vector<int> _entropy_array;
    int _num_entropies = -1;
    int current_entropy = 0;
    bool _enableDistanceBasedRtx;
    bool _bts_enabled = true;
    uint64_t sent_bytes_previous_window = 0;
    int _next_pathid;
    int _hop_count;
    int data_count_idx = 0;
    simtime_picosec count_rtt = 0;
    simtime_picosec next_qa = 0;

    static double kmax, kmin;

    vector<pair<simtime_picosec, int>> list_ecn_rate;
    vector<pair<simtime_picosec, double>> list_sending_rate;

    void send_packets();
    uint64_t get_unacked();

    void adjust_window(simtime_picosec ts, bool ecn, simtime_picosec rtt, uint32_t ackno);
    void fast_increase();
    const Route *get_path();
    void mark_received(UecAck &pkt);
    void add_ack_path(const Route *rt);
    bool resend_packet(std::size_t i);
    void retransmit_packet();
    void processAck(UecAck &pkt, bool);
    std::size_t get_sent_packet_idx(uint32_t pkt_seqno);

    void update_rtx_time();
    // void reduce_cwnd(uint64_t amount);
    void processNack(UecNack &nack);
    void processBts(UecPacket *nack);
    // void simulateTrimEvent(UecAck &nack);
    void reduce_unacked(uint64_t amount);
    // void apply_timeout_penalty();
    void update_pacing_delay();
    void quick_adapt_drop();
};

class LcpSink : public PacketSink, public DataReceiver {
    friend class LcpSrc;

  public:
    LcpSink();

    void receivePacket(Packet &pkt) override;
    const string &nodename() override;

    void set_end_trigger(Trigger &trigger);

    uint64_t cumulative_ack() override;
    uint32_t drops() override;
    void connect(LcpSrc &src, const Route *route);
    void set_paths(uint32_t num_paths);
    void set_src(uint32_t s) { _srcaddr = s; }
    uint32_t from = -1;
    uint32_t to = -2;
    uint32_t tag = 0;
    static void setRouteStrategy(RouteStrategy strat) { _route_strategy = strat; }
    static RouteStrategy _route_strategy;
    Trigger *_end_trigger = 0;
    int pfc_just_seen = -10;

  private:
    UecAck::seq_t _cumulative_ack;
    uint64_t _packets;
    uint32_t _srcaddr;
    uint32_t _drops;
    int ack_count_idx = 0;
    string _nodename;
    list<UecAck::seq_t> _received; // list of packets received OOO
    uint16_t _crt_path;
    const Route *_route;
    vector<const Route *> _paths;
    vector<int> _path_ids;                 // path IDs to be used for ECMP FIB.
    vector<const Route *> _original_paths; // paths in original permutation
                                           // order
    LcpSrc *_src;
    vector<int> _good_entropies_list;

    void send_ack(simtime_picosec ts, bool marked, UecAck::seq_t seqno, UecAck::seq_t ackno, const Route *rt,
                  const Route *inRoute, int path_id);
    void send_nack(simtime_picosec ts, bool marked, UecAck::seq_t seqno, UecAck::seq_t ackno, const Route *rt, int,
                   bool);
    bool already_received(UecPacket &pkt);
};

class LcpRtxTimerScanner : public EventSource {
  public:
    LcpRtxTimerScanner(simtime_picosec scanPeriod, EventList &eventlist);
    void doNextEvent();
    void registerLcp(LcpSrc &LcpSrc);

  private:
    simtime_picosec _scanPeriod;
    simtime_picosec _lastScan;
    typedef list<LcpSrc *> lcps_t;
    lcps_t _lcps;
};

#endif