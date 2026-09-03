#include "searchstats.h"

#include <sstream>

SearchStats& SearchStats::operator+=(const SearchStats& other) {
#define ADD_FIELD(field) field += other.field
    ADD_FIELD(rfpCandidates);
    ADD_FIELD(rfpCuts);
    ADD_FIELD(rfpVetoTtQuiet);
    ADD_FIELD(rfpVetoEndgame);
    ADD_FIELD(rfpVetoMate);
    ADD_FIELD(nmpAttempts);
    ADD_FIELD(nmpCuts);
    ADD_FIELD(nmpVerifications);
    ADD_FIELD(nmpVerificationFails);
    ADD_FIELD(lmrReductions);
    ADD_FIELD(lmrFailHighs);
    ADD_FIELD(lmrResearches);
    ADD_FIELD(checkExtensions);
    ADD_FIELD(singularExtensions);
    ADD_FIELD(doubleExtensions);
    ADD_FIELD(tripleExtensions);
    ADD_FIELD(extensionNodes);
    ADD_FIELD(aspirationAttempts);
    ADD_FIELD(aspirationFailLow);
    ADD_FIELD(aspirationFailHigh);
    ADD_FIELD(aspirationResearches);
    ADD_FIELD(aspirationDiscardedNodes);
    ADD_FIELD(probCutAttempts);
    ADD_FIELD(probCutCuts);
    ADD_FIELD(razorCuts);
    ADD_FIELD(futilityCuts);
    ADD_FIELD(lmpTriggers);
    ADD_FIELD(historyCuts);
    ADD_FIELD(seeCaptureCuts);
    ADD_FIELD(seeQuietCuts);
#undef ADD_FIELD

    for (int depth = 0; depth < DepthBuckets; ++depth) {
        rfpCandidatesByDepth[depth] += other.rfpCandidatesByDepth[depth];
        rfpCutsByDepth[depth] += other.rfpCutsByDepth[depth];
    }
    for (int band = 0; band < LmrDepthBands; ++band)
        lmrReductionsByDepth[band] += other.lmrReductionsByDepth[band];
    for (int band = 0; band < LmrReductionBands; ++band)
        lmrReductionsByAmount[band] += other.lmrReductionsByAmount[band];
    return *this;
}

bool SearchStats::invariantsHold() const {
    return rfpCuts <= rfpCandidates
        && nmpCuts <= nmpAttempts
        && nmpVerificationFails <= nmpVerifications
        && lmrFailHighs <= lmrReductions
        && lmrResearches == lmrFailHighs
        && aspirationResearches >= aspirationFailLow + aspirationFailHigh
        && probCutCuts <= probCutAttempts;
}

std::string SearchStats::toUciString() const {
    std::ostringstream out;
    out << "rfp_candidates=" << rfpCandidates
        << " rfp_cuts=" << rfpCuts
        << " rfp_veto_tt=" << rfpVetoTtQuiet
        << " rfp_veto_endgame=" << rfpVetoEndgame
        << " rfp_veto_mate=" << rfpVetoMate
        << " nmp_attempts=" << nmpAttempts
        << " nmp_cuts=" << nmpCuts
        << " nmp_verifications=" << nmpVerifications
        << " nmp_verification_fails=" << nmpVerificationFails
        << " lmr_reductions=" << lmrReductions
        << " lmr_fail_highs=" << lmrFailHighs
        << " lmr_researches=" << lmrResearches
        << " check_extensions=" << checkExtensions
        << " singular_extensions=" << singularExtensions
        << " double_extensions=" << doubleExtensions
        << " triple_extensions=" << tripleExtensions
        << " extension_nodes=" << extensionNodes
        << " aspiration_attempts=" << aspirationAttempts
        << " aspiration_fail_low=" << aspirationFailLow
        << " aspiration_fail_high=" << aspirationFailHigh
        << " aspiration_researches=" << aspirationResearches
        << " aspiration_discarded_nodes=" << aspirationDiscardedNodes
        << " probcut_attempts=" << probCutAttempts
        << " probcut_cuts=" << probCutCuts
        << " razor_cuts=" << razorCuts
        << " futility_cuts=" << futilityCuts
        << " lmp_triggers=" << lmpTriggers
        << " history_cuts=" << historyCuts
        << " see_capture_cuts=" << seeCaptureCuts
        << " see_quiet_cuts=" << seeQuietCuts
        << " rfp_depth=";

    bool first = true;
    for (int depth = 0; depth < DepthBuckets; ++depth) {
        if (rfpCandidatesByDepth[depth] == 0)
            continue;
        if (!first)
            out << ',';
        first = false;
        out << depth << ':' << rfpCutsByDepth[depth]
            << '/' << rfpCandidatesByDepth[depth];
    }
    if (first)
        out << "none";
    out << " lmr_depth="
        << "1-3:" << lmrReductionsByDepth[0]
        << ",4-6:" << lmrReductionsByDepth[1]
        << ",7-10:" << lmrReductionsByDepth[2]
        << ",11+:" << lmrReductionsByDepth[3]
        << " lmr_R="
        << "1:" << lmrReductionsByAmount[0]
        << ",2:" << lmrReductionsByAmount[1]
        << ",3:" << lmrReductionsByAmount[2]
        << ",4+:" << lmrReductionsByAmount[3];
    out << " invariants=" << (invariantsHold() ? "ok" : "FAILED");
    return out.str();
}
