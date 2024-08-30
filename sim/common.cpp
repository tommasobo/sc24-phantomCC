#include <filesystem>
#include <stdint.h>
#include <sys/types.h>

typedef uint64_t simtime_picosec;

// Global
simtime_picosec GLOBAL_TIME = 0; // Global variable for current sim time
bool COLLECT_DATA = true;
uint64_t HOPS = 6;                          // Max Hops Topology
uint64_t INFINITE_BUFFER_SIZE = 1000000000; // Assume infinite buffer space
std::string FOLDER_NAME = "sim/";

// Values for "modern" networking simulations
int PKT_SIZE_MODERN = 4096;       // Bytes
uint64_t LINK_SPEED_MODERN = 400; // Gb/s
uint64_t INTER_LINK_SPEED_MODERN = 100;
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
simtime_picosec TARGET_RTT_LOW = 0; // Low threshold to perform reductions for RTT.
simtime_picosec TARGET_RTT_HIGH = 0; // High threshold to perform QA for RTT.
simtime_picosec BAREMETAL_RTT = 0; // Baremetal latency.
double LCP_ALPHA = 0.5; // Gain for rtt EWMA calculations.
float LCP_ECN_ALPHA = 1.0; // Gain for ECN fraction EWMA calculations.
double LCP_BETA = 0.0833; // Reduction constant for LCP.
uint32_t LCP_DELTA = 1; // Additive increase constant for LCP.
int LCP_K = 1; // The marking threshold for LCP.
uint32_t LCP_FAST_INCREASE_THRESHOLD = 3; // Number of consecutive good epochs to trigger fast increase.
bool LCP_USE_QUICK_ADAPT = true; // Whether to use quick adapt.
bool LCP_USE_PACING = true; // Whether to use pacing.
bool LCP_USE_FAST_INCREASE = true; // Whether to use fast increase.
double LCP_PACING_BONUS = 0.05; // The reduction to perform to pacing to reduce underutilization.
bool LCP_OFF_RTT = false; // Whether to only react to ECN.
float LCP_ECN_FRACTION_THRESHOLD_LOW = 0.1; // The ECN fraction thresholds for LCP.
float LCP_ECN_FRACTION_THRESHOLD_HIGH = 0.5; // The ECN fraction thresholds for LCP.
bool LCP_USE_REGULAR_EWMA = false; // Whether to use regular EWMA.
float LCP_TARGET_RTT_HIGH_FRACTION = 0.9; // Fraction of queueing latency to add to the baremetal RTT.
bool LCP_DO_PER_ACK_INCREASE = false; // Whether to perform per-ack increase for epoch based.
uint32_t LCP_CONSECUTIVE_DECREASES_FOR_QA = 3; // Number of consecutive decreases to trigger QA.