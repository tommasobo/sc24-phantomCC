#include <filesystem>
#include <stdint.h>
#include <sys/types.h>

typedef uint64_t simtime_picosec;

// Global
simtime_picosec GLOBAL_TIME = 0; // Global variable for current sim time
bool COLLECT_DATA = true;
uint64_t HOPS = 6;                          // Max Hops Topology
uint64_t INFINITE_BUFFER_SIZE = 1000000000; // Assume infinite buffer space

// Values for "modern" networking simulations
int PKT_SIZE_MODERN = 2048;       // Bytes
uint64_t LINK_SPEED_MODERN = 400; // Gb/s
uint64_t SINGLE_PKT_TRASMISSION_TIME_MODERN =
        PKT_SIZE_MODERN * 8 / LINK_SPEED_MODERN;
int LINK_DELAY_MODERN = 400; // ns
uint64_t BASE_RTT_MODERN =
        (HOPS * LINK_DELAY_MODERN) +
        (PKT_SIZE_MODERN * 8 / LINK_SPEED_MODERN * HOPS) +
        (HOPS * LINK_DELAY_MODERN) +
        (40 * 8 / LINK_SPEED_MODERN * HOPS); // Target RTT in ns
// UEC Specific Values
uint64_t TARGET_RTT_MODERN = BASE_RTT_MODERN * 1.25; // Target RTT in ns
uint64_t BDP_MODERN_UEC = BASE_RTT_MODERN * LINK_SPEED_MODERN / 8; // BDP
uint64_t MAX_CWD_MODERN_UEC = BDP_MODERN_UEC * 1.0;                // BDP * 1.0
uint64_t MIN_K_ECN_MODERN =
        BDP_MODERN_UEC * 8 / LINK_SPEED_MODERN * 0.2; // 20% BDP
uint64_t MAX_K_ECN_MODERN =
        BDP_MODERN_UEC * 8 / LINK_SPEED_MODERN * 0.8; // 100% BDP
// NDP values
uint64_t BDP_MODERN_NDP = BASE_RTT_MODERN * LINK_SPEED_MODERN / 8; // BDP
uint64_t MAX_CWD_MODERN_NDP =
        BDP_MODERN_NDP *
        3; // BDP * 3. Based on their choice to use 23 cwd with 9000 pacekts
bool ENABLE_FAST_DROP = false;
bool IGNORE_ECN_DATA_BTS = true;

// Values for "old" networking simulations
int PKT_SIZE_OLD = 9000;      // Bytes
uint64_t LINK_SPEED_OLD = 10; // Gb/s
int SINGLE_PKT_TRASMISSION_TIME_OLD = PKT_SIZE_OLD * 8 / LINK_SPEED_OLD;
int LINK_DELAY_OLD = 1000; // ns
uint64_t BASE_RTT_OLD =
        (HOPS * LINK_DELAY_OLD) + (PKT_SIZE_OLD * 8 / LINK_SPEED_OLD * HOPS) +
        (HOPS * LINK_DELAY_OLD) +
        (40 * 8 / LINK_SPEED_OLD * HOPS) * 1000; // Target RTT in ps
// UEC Specific Values
uint64_t TARGET_RTT_OLD = BASE_RTT_OLD * 1.2;             // Target RTT in ps
uint64_t BDP_OLD_UEC = BASE_RTT_OLD * LINK_SPEED_OLD / 8; // BDP
uint64_t MAX_CWD_OLD_UEC = BDP_OLD_UEC * 1.2;             // BDP * 1.2
uint64_t MIN_K_ECN_OLD = BDP_OLD_UEC * 8 / LINK_SPEED_OLD * 0.2; // 20% BDP
uint64_t MAX_K_ECN_OLD = BDP_OLD_UEC * 8 / LINK_SPEED_OLD * 1;   // 100% BDP
uint64_t BUFFER_SIZE_OLD = 8 * PKT_SIZE_OLD;                     // 8 Pkts
// NDP Specific Values
uint64_t BDP_OLD_NDP = BASE_RTT_OLD * LINK_SPEED_OLD / 8; // BDP
uint64_t MAX_CWD_OLD_NDP = BDP_OLD_NDP * 3;               // BDP * 1.2
std::filesystem::path PROJECT_ROOT_PATH;
// LCP Specific Values
simtime_picosec TARGET_RTT_LOW = 0;
simtime_picosec TARGET_RTT_HIGH = 0;
simtime_picosec BAREMETAL_RTT = 0;
double LCP_ALPHA = 0.5;
double LCP_BETA = 0.8;
double LCP_GAMMA = 0.3;
uint32_t LCP_DELTA = 1;
int LCP_K = 1;
uint32_t LCP_FAST_INCREASE_THRESHOLD = 3;
bool LCP_USE_QUICK_ADAPT = true;
bool LCP_USE_PACING = true;
bool LCP_USE_FAST_INCREASE = true;
double LCP_PACING_BONUS = 0.05;
bool LCP_USE_MIN_RTT = false;
bool LCP_USE_AGGRESSIVE_DECREASE = false;
// LCP-Gemini Specific Values.
simtime_picosec LCP_GEMINI_TARGET_QUEUEING_LATENCY = 0;
double LCP_GEMINI_H = 0.0;
double LCP_GEMINI_BETA = 0.0;
