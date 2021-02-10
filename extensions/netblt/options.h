//
// Created by developer on 1/28/21.
//

#ifndef EXTENSIONS_OPTIONS_H
#define EXTENSIONS_OPTIONS_H
namespace ndn {
struct Options {
    // Common options
    time::milliseconds interestLifetime = DEFAULT_INTEREST_LIFETIME;
    int maxRetriesOnTimeoutOrNack = 15;
    bool disableVersionDiscovery = false;
    bool mustBeFresh = false;
    bool isQuiet = false;
    bool isVerbose = true;

    // Fixed pipeline options
    size_t maxPipelineSize = 1;

    // Adaptive pipeline common options
    double initCwnd = 2.0;        ///< initial congestion window size
    double initSsthresh = std::numeric_limits<double>::max(); ///< initial slow start threshold
    time::milliseconds rtoCheckInterval{10}; ///< interval for checking retransmission timer
    bool ignoreCongMarks = false; ///< disable window decrease after receiving congestion mark
    bool disableCwa = false;      ///< disable conservative window adaptation

    // AIMD pipeline options
    double aiStep = 0.2;          ///< AIMD additive increase step (in segments)
    double mdCoef = 0.5;          ///< AIMD multiplicative decrease factor
    bool resetCwndToInit = false; ///< reduce cwnd to initCwnd when loss event occurs

    // Cubic pipeline options
    double cubicBeta = 0.7;       ///< cubic multiplicative decrease factor
    bool enableFastConv = false;  ///< use cubic fast convergence

    //SOS
    uint32_t m_SOSSz = 15000;

    //flow
    double m_burstSz = 10;
    uint32_t m_minBurstSz = 1;
    time::milliseconds m_burstInterval_ms{5};

    //time
    time::milliseconds m_defaultRTT{200};

    uint8_t m_unbalanceCountThreshold = 2;

    bool simpleWindowAdj = true;
};
}
#endif //EXTENSIONS_OPTIONS_H
