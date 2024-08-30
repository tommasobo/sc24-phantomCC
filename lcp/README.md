# This folder contains modifications made for Long Control Protocol (LCP)

# Modifications Made

## LCP Description
- LCP is a scheme that measures RTT and ECN to determine whether to reduce ratef or interdatacetner algorithms
- Relevant Options:
  - -inter-algo [lcp-per-ack, lcp] Sets the interdatacenter to either ack-based (lcp-per-ack) or epoch-based (lcp).
  - -alpha - Sets the gain for EWMA for RTT
  - -ecn_alpha Sets the gain for EWMA for ecn rate.
  - -beta  - Sets the reduction constant under congestion
  - -fast-increase-threshold - Sets how many epochs must be incurred with no congestion for FI
  - -use-regular-ewma - Performs RTT via regular EWMA instead of an alternate version that tries to incorporate extreme values.
  - -per-ack (Epoch-based only) determines whether increases are done per ack or not.
  - -pacing-bonus Sets the factor to reduce pacing delay by.
  - -consec-epochs-qa Sets the number of consecutive epochs with reduction needed before QA can be triggered.
  
## Switch changes
- Added support for different queue sizes for intra and inter
- REmoved ECN from border link

## Host Changes
- Added LCP
- New flags for LCP in common/config
- Main binary for LCP in main_lcp_entry_modern.cpp
  - Added support for running LCP, BBR, or PhantomCC for inter and MPRDMA for intra
- Added a pacer for LCP (same as bbr_pacer)

## Code Environment changes
- New Logging option to send output to specific folder
  - Use the -logging_folder option to point the output to a specific path.

## Scripts/Plotting/Experiments
- 


