from experiment_options import *
from experiment_utils import *

CONFIG = [
    [Alpha(1/16)],
    [Beta(0.05)],
    [OptionSet([FastIncrease(True), QuickAdapt(True)])],
    [QueueSizeRatio(1.0), QueueSizeRatio(0.1)],
    [FastIncreaseThreshold(5)],
    [PacingBonus(0.0)],
    [UseRegularEwma(True)],
    [PerAck(True)],
    [KMin(50)],
    [KMax(70)],
    [
        InterAlgo("lcp"),
        InterAlgo("lcp-per-ack"),
        InterAlgo("bbr"),
        InterAlgo("uec")
    ],
    [ConsecutiveDecreasesForQuickAdapt(0)],
    [ECNAlpha(0.5)],
]

TOPO = "lcp/configs/topos/fat_tree_100Gbps.topo"
FOLDER = "exp_data/incast_experiment" + f"_{get_topo_name(TOPO)}"

TMS = [
    "lcp/configs/tms/simple/2_inter_100MB.cm",
    # "configs/a/4_inter_100MB.cm",
    # "configs/a/16_inter_100MB.cm",
]

run_experiment(CONFIG, TMS, TOPO, FOLDER, os_border=128)