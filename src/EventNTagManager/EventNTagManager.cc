#include "TFile.h"

#include "skroot.h"
#undef MAXPM
#undef MAXPMA
#undef MAXHWSK
#include "skparmC.h"
#include "sktqC.h"
#include "skheadC.h"
#include "apmringC.h"
#include "apmueC.h"
#include "appatspC.h"
#include "neworkC.h"
#include "nbnkC.h"


#include "SKLibs.hh"
#include "EventNTagManager.hh"
#include "Calculator.hh"

EventNTagManager::EventNTagManager(Verbosity verbose)
: fInputIsSKROOT(false), fUseECut(false), fIsBranchSet(false)
{
    fMsg = Printer("NTagManager", verbose);

    fSettings = Store("Settings");
    fSettings.Initialize("/disk02/usr6/han/phd/utillib/src/EventNTagManager/NTagConfig");
    ApplySettings();

    fEventVariables = Store("Variables");
    fEventCandidates = CandidateCluster("Delayed");
    fEventEarlyCandidates = CandidateCluster("Early");

    std::vector<std::string> featureList = {"NHits", "N50", "N200", "N1300", "ReconCT", "TRMS", "QSum",
                                            "Beta1", "Beta2", "Beta3", "Beta4", "Beta5",
                                            "AngleMean", "AngleSkew", "AngleStdev", "Label",
                                            "DWall", "DWallMeanDir", "ThetaMeanDir", "DWall_n", "prompt_nfit",
                                            "TMVAOutput", "TagIndex", "TagClass"};
    fEventCandidates.RegisterFeatureNames(featureList);

    std::vector<std::string> earlyFeatureList = {"ReconCT", "x", "y", "z", "DWall", "dirx", "diry", "dirz",
                                                 "NHits", "GateType", "Goodness", "Label", "TagIndex", "TagClass"};
    fEventEarlyCandidates.RegisterFeatureNames(earlyFeatureList);

    InitializeTMVA();

    auto handler = new TInterruptHandler(this);
    handler->Add();
}

EventNTagManager::~EventNTagManager() {}

void EventNTagManager::ReadVariables()
{
    // basic information
    fEventVariables.Set("RunNo", skhead_.nrunsk);
    fEventVariables.Set("SubrunNo", skhead_.nsubsk);
    fEventVariables.Set("EventNo", skhead_.nevsk);

    // hit information
    int nhitac; odpc_2nd_s_(&nhitac);
    int trgtype = skhead_.idtgsk & 1<<28 ? tSHE : skhead_.idtgsk & 1<<29 ? tAFT : tELSE;
    float trgOffset = 0; trginfo_(&trgOffset);
    fEventVariables.Set("QISMSK", skq_.qismsk);
    fEventVariables.Set("NHITAC", nhitac);
    fEventVariables.Set("TrgType", trgtype);
    fEventVariables.Set("TrgOffset", trgOffset);

    // reconstructed information
    int bank = 0; aprstbnk_(&bank);
        // evis
        fEventVariables.Set("EVis", apcomene_.apevis);
        // prompt vertex
        fPromptVertex = TVector3(apcommul_.appos);
        
        if (fVertexMode == mAPFIT) {
            fEventVariables.Set("pvx", apcommul_.appos[0]);
            fEventVariables.Set("pvy", apcommul_.appos[1]);
            fEventVariables.Set("pvz", apcommul_.appos[2]);
        }
        else if (fVertexMode == mTRUE) {
            skgetv_();
            fEventVariables.Set("pvx", skvect_.pos[0]);
            fEventVariables.Set("pvy", skvect_.pos[1]);
            fEventVariables.Set("pvz", skvect_.pos[2]);
        }
        else if (fVertexMode == mCUSTOM) {
            float vx, vy, vz;
            bool vxExists = fSettings.Get("vx", vx);
            bool vyExists = fSettings.Get("vy", vy);
            bool vzExists = fSettings.Get("vz", vz);
            
            if (vxExists && vyExists && vzExists) {
                fEventVariables.Set("pvx", vx);
                fEventVariables.Set("pvy", vy);
                fEventVariables.Set("pvz", vz);
            }
            else
                fMsg.Print("Custom prompt vertex not fully specified! Use -vx, -vy, -vz commands to specify the custom prompt vertex.", pERROR);
        }
        
        // dwall
        fEventVariables.Set("DWall", wallsk_(apcommul_.appos));
        // ring
            // nring
            fEventVariables.Set("NRing", apcommul_.apnring);
            fEventVariables.Set("FirstRingType", apcommul_.apip[0]);
            fEventVariables.Set("FirstRingEMom", appatsp2_.apmsamom[0][1]);
            fEventVariables.Set("FirstRingMuMom", appatsp2_.apmsamom[0][2]);

    // mc information
    float posnu[3]; nerdnebk_(posnu);
    fEventVariables.Set("NEUTMode", nework_.modene);
    fEventVariables.Set("NeutrinoType", nework_.ipne[0]);
    fEventVariables.Set("NeutrinoMom", TVector3(nework_.pne[0]).Mag());

    ReadEarlyCandidates();
}

void EventNTagManager::ReadHits()
{
    fEventHits = PMTHitCluster(sktqz_);
}

void EventNTagManager::ReadParticles()
{
    skgetv_();

    if (fInputIsSKROOT) {
        int lun = 10;

        TreeManager* mgr  = skroot_get_mgr(&lun);
        SecondaryInfo* SECONDARY = mgr->GetSECONDARY();
        mgr->GetEntry();
    
        secndprt_.nscndprt = SECONDARY->nscndprt;
    
        std::copy(std::begin(SECONDARY->iprtscnd), std::end(SECONDARY->iprtscnd), std::begin(secndprt_.iprtscnd));
        std::copy(std::begin(SECONDARY->iprntprt), std::end(SECONDARY->iprntprt), std::begin(secndprt_.iprntprt));
        std::copy(std::begin(SECONDARY->lmecscnd), std::end(SECONDARY->lmecscnd), std::begin(secndprt_.lmecscnd));
        std::copy(std::begin(SECONDARY->tscnd), std::end(SECONDARY->tscnd), std::begin(secndprt_.tscnd));
    
        std::copy(&SECONDARY->vtxscnd[0][0], &SECONDARY->vtxscnd[0][0] + 3*SECMAXRNG, &secndprt_.vtxscnd[0][0]);
        std::copy(&SECONDARY->pscnd[0][0], &SECONDARY->pscnd[0][0] + 3*SECMAXRNG, &secndprt_.pscnd[0][0]);
    }
    else {
        apflscndprt_();
    }

    fEventParticles.ReadCommonBlock(skvect_, secndprt_);

    float geantT0; trginfo_(&geantT0);
    fEventParticles.SetT0(geantT0);

    fEventTaggables.ReadParticleCluster(fEventParticles);
    fEventTaggables.SetPromptVertex(fPromptVertex);
}

void EventNTagManager::ReadEarlyCandidates()
{
    float parentPeakTime;
    int silence = 1;
    muechk_gate_(apcommul_.appos, parentPeakTime, silence);
    int bank = 0; apclrmue_(); apgetmue_(&bank);

    for (int iMuE=0; apmue_.apmuenhit[iMuE]>0; iMuE++) {
        float reconCT = parentPeakTime*1e-3 + apmue_.apmuetime[iMuE];
            Candidate candidate;
            candidate.Set("ReconCT", reconCT);
            candidate.Set("x", apmue_.apmuepos[iMuE][0]);
            candidate.Set("y", apmue_.apmuepos[iMuE][1]);
            candidate.Set("z", apmue_.apmuepos[iMuE][2]);
            candidate.Set("dirx", apmue_.apmuedir[iMuE][0]);
            candidate.Set("diry", apmue_.apmuedir[iMuE][1]);
            candidate.Set("dirz", apmue_.apmuedir[iMuE][2]);
            candidate.Set("DWall", wallsk_(apmue_.apmuepos[iMuE]));
            candidate.Set("NHits", apmue_.apmuenhit[iMuE]);
            candidate.Set("GateType", apmue_.apmuetype[iMuE]);
            candidate.Set("Goodness", apmue_.apmuegood[iMuE]);
            candidate.Set("TagClass", FindTagClass(candidate));
            fEventEarlyCandidates.Append(candidate);
    }

    //fEventEarlyCandidates.Sort();
}

void EventNTagManager::ReadEventFromCommon()
{
    ClearData();

    ReadVariables();
    ReadHits();

    // if MC
    ReadParticles();
}

void EventNTagManager::ClearData()
{
    fEventVariables.Clear();
    fEventHits.Clear();
    fEventParticles.Clear();
    fEventTaggables.Clear();
    //fEventNCaptures.Clear();
    fEventCandidates.Clear();
    fEventEarlyCandidates.Clear();
}

void EventNTagManager::ApplySettings()
{
    TString inFilePath;
    fSettings.Get("in", inFilePath);
    
    if (inFilePath.EndsWith(".root"))
        fInputIsSKROOT = true;
    else
        fInputIsSKROOT = false;
        
    // vertex mode
    TString vertexMode;
    fSettings.Get("vertex_mode", vertexMode);
    
    if (vertexMode == "APFIT")
        fVertexMode = mAPFIT;
    else if (vertexMode == "TRUE")
        fVertexMode = mTRUE;
    else if (vertexMode == "CUSTOM")
        fVertexMode = mCUSTOM;

    fSettings.Get("T0TH", T0TH);
    fSettings.Get("T0MX", T0MX);
    fSettings.Get("TWIDTH", TWIDTH);
    fSettings.Get("TMINPEAKSEP", TMINPEAKSEP);
    fSettings.Get("TMATCHWINDOW", TMATCHWINDOW);
    fSettings.Get("NHITSTH", NHITSTH);
    fSettings.Get("NHITSMX", NHITSMX);
    fSettings.Get("N200TH", N200TH);
    fSettings.Get("N200MX", N200MX);

    fSettings.Get("INITGRIDWIDTH", INITGRIDWIDTH);
    fSettings.Get("MINGRIDWIDTH", MINGRIDWIDTH);
    fSettings.Get("GRIDSHRINKRATE", GRIDSHRINKRATE);
    fSettings.Get("VTXSRCRANGE", VTXSRCRANGE);
    
    bool useECut_N50 = fSettings.Get("E_N50CUT", E_N50CUT);
    bool useECut_Time = fSettings.Get("E_TIMECUT", E_TIMECUT);
    if (useECut_N50 && useECut_Time)
        fUseECut = true;
    fSettings.Get("N_OUTCUT", N_OUTCUT);
}

void EventNTagManager::ReadArguments(const ArgParser& argParser)
{
    fSettings.ReadArguments(argParser);
}

void EventNTagManager::InitializeTMVA()
{
    std::vector<std::string> featureList = {"AngleMean", "AngleSkew", "AngleStdev",
                                            "Beta1", "Beta2", "Beta3", "Beta4", "Beta5",
                                            "DWall", "DWallMeanDir", "DWall_n", "N200", "NHits", "TRMS",
                                            "ThetaMeanDir", "prompt_nfit"};

    for (auto const& feature: featureList) {
        fFeatureContainer[feature] = 0;
        fTMVAReader.AddVariable(feature, &(fFeatureContainer[feature]));
    }

    fTMVAReader.AddSpectator("Label", &(fCandidateLabel));
    //fTMVAReader.BookMVA("MLP", "/disk02/usr6/han/weights/TMVAClassification_MLP.weights.xml");
    fTMVAReader.BookMVA("MLP", "/disk02/usr6/han/phd/ntag/test_weight.xml");
    //sharedData->eventCandidates.RegisterFeatureName("TMVAOutput");
}

void EventNTagManager::SearchCandidates()
{
    float pmtDeadTime; fSettings.Get("TRBNWIDTH", pmtDeadTime);
    fEventHits.ApplyDeadtime(pmtDeadTime);

    // subtract tof
    SubtractToF();

    int   iHitPrevious    = 0;
    int   NHitsNew        = 0;
    int   NHitsPrevious   = 0;
    int   N200Previous    = 0;
    float t0Previous      = std::numeric_limits<float>::min();

    unsigned long nEventHits = fEventHits.GetSize();

    // Loop over the saved TQ hit array from current event
    for (unsigned int iHit = 0; iHit < nEventHits; iHit++) {

        PMTHitCluster hitsInTWIDTH = fEventHits.Slice(iHit, TWIDTH);

        // If (ToF-subtracted) hit comes earlier than T0TH or later than T0MX, skip:
        float firstHitTime = hitsInTWIDTH[0].t();
        if (firstHitTime < T0TH || firstHitTime > T0MX) continue;

        // Calculate NHitsNew:
        // number of hits within TWIDTH (ns) from the i-th hit
        int NHits_iHit = hitsInTWIDTH.GetSize();

        // Pass only if NHITSTH <= NHits_iHit <= NHITSMX:
        if (NHits_iHit < NHITSTH || NHits_iHit > NHITSMX) continue;

        // We've found a new peak.
        NHitsNew = NHits_iHit;
        float t0New = firstHitTime;

        // Calculate N200
        PMTHitCluster hitsIn200ns = fEventHits.Slice(iHit, TWIDTH/2.-100, TWIDTH/2.+100);
        int N200New = hitsIn200ns.GetSize();

        // If peak t0 diff = t0New - t0Previous > TMINPEAKSEP, save the previous peak.
        // Also check if N200Previous is below N200 cut and if t0Previous is over t0 threshold
        if (t0New - t0Previous > TMINPEAKSEP) {
            if (N200Previous < N200MX && t0Previous > T0TH) {
                Candidate candidate(iHitPrevious);
                FindFeatures(candidate);
                fEventCandidates.Append(candidate);
            }
            // Reset NHitsPrevious,
            // if peaks are separated enough
            NHitsPrevious = 0;
        }

        // If NHits is not greater than previous, skip
        if ( NHitsNew <= NHitsPrevious ) continue;

        iHitPrevious  = iHit;
        t0Previous    = t0New;
        NHitsPrevious = NHitsNew;
        N200Previous  = N200New;
    }

    // Save the last peak
    if (NHitsPrevious >= NHITSTH) {
        Candidate candidate(iHitPrevious);
        FindFeatures(candidate);
        fEventCandidates.Append(candidate);
    }

    // if MC
    if (fUseECut) PruneCandidates();
    MapTaggables();
    
    fEventEarlyCandidates.FillVectorMap();
    fEventCandidates.FillVectorMap();

    FillNTagCommon();

    // TMVA output
    //for (auto& candidate: fEventCandidates)
    //    candidate.Set("TMVAOutput", GetTMVAOutput(candidate));
}

void EventNTagManager::SubtractToF()
{
    fEventHits.SetVertex(fPromptVertex);
}

void EventNTagManager::FindFeatures(Candidate& candidate)
{
    int firstHitID = candidate.HitID();
    //PMTHitCluster hitsIn30ns = fEventHits.Slice(candidate.HitID(), TWIDTH/2. -15, TWIDTH/2. +15);
    //PMTHitCluster hitsIn50ns = fEventHits.Slice(candidate.HitID(), TWIDTH/2. -25, TWIDTH/2. +25);
    auto hitsInTWIDTH = fEventHits.Slice(firstHitID, TWIDTH);
    auto hitsIn50ns   = fEventHits.Slice(firstHitID, TWIDTH/2.- 25, TWIDTH/2.+ 25);
    auto hitsIn200ns  = fEventHits.Slice(firstHitID, TWIDTH/2.-100, TWIDTH/2.+100);
    auto hitsIn1300ns = fEventHits.Slice(firstHitID, TWIDTH/2.-520, TWIDTH/2.+780);

    // TRMS-fit (BONSAI?)
    auto trmsFitVertex = hitsIn50ns.FindTRMSMinimizingVertex(/* TRMS-fit options */
                                                             INITGRIDWIDTH,
                                                             MINGRIDWIDTH,
                                                             GRIDSHRINKRATE,
                                                             VTXSRCRANGE);

    //hitsIn50ns.SetVertex(trmsFitVertex);
    //fEventHits.SetVertex(trmsFitVertex);
    //float tempT = hitsIn50ns[GetMaxNHitsIndex(hitsIn50ns)].t();
    //unsigned int trmsFitFirstHitID = fEventHits.GetIndex(tempT);


    //candidate.SetHitID(trmsFitFirstHitID);

    // Number of hits
    candidate.Set("NHits", hitsInTWIDTH.GetSize());
    candidate.Set("N50",   hitsIn50ns.GetSize());
    candidate.Set("N200",  hitsIn200ns.GetSize());
    candidate.Set("N1300", hitsIn1300ns.GetSize());

    // Time
    float reconCT = hitsInTWIDTH.Find(HitFunc::T, Calc::Mean) * 1e-3;
    candidate.Set("ReconCT", reconCT);
    candidate.Set("TRMS", hitsInTWIDTH.Find(HitFunc::T, Calc::RMS));

    // Charge
    candidate.Set("QSum", hitsInTWIDTH.Find(HitFunc::Q, Calc::Sum));

    // Beta's
    auto beta = hitsInTWIDTH.GetBetaArray();
    candidate.Set("Beta1", beta[1]);
    candidate.Set("Beta2", beta[2]);
    candidate.Set("Beta3", beta[3]);
    candidate.Set("Beta4", beta[4]);
    candidate.Set("Beta5", beta[5]);

    // DWall
    float dWall; fEventVariables.Get("DWall", dWall);
    auto dirVec = hitsInTWIDTH[HitFunc::Dir];
    auto meanDir = GetMean(dirVec).Unit();
    candidate.Set("DWall", dWall);
    candidate.Set("DWallMeanDir", GetDWallInDirection(fPromptVertex, meanDir));

    // Mean angle formed by all hits and the mean hit direction
    std::vector<float> angles;
    for (auto const& dir: dirVec) {
        angles.push_back((180/M_PI)*meanDir.Angle(dir));
    }

    float meanAngleWithMeanDirection = GetMean(angles);
    candidate.Set("ThetaMeanDir", meanAngleWithMeanDirection);

    // Opening angle stats
    auto openingAngleStats = hitsInTWIDTH.GetOpeningAngleStats();
    candidate.Set("AngleMean",  openingAngleStats.mean);
    candidate.Set("AngleStdev", openingAngleStats.stdev);
    candidate.Set("AngleSkew",  openingAngleStats.skewness);

    candidate.Set("DWall_n", GetDWall(trmsFitVertex));
    candidate.Set("prompt_nfit", (fPromptVertex-trmsFitVertex).Mag());

    candidate.Set("TMVAOutput", GetTMVAOutput(candidate));
    candidate.Set("TagClass", FindTagClass(candidate));

    //fEventHits.SetVertex(fPromptVertex);
}

float EventNTagManager::GetTMVAOutput(Candidate& candidate)
{
    // get features from candidate and fill feature container
    for (auto const& pair: fFeatureContainer) {
        float value = candidate[pair.first];
        fFeatureContainer[pair.first] = value;
    }

    // get spectator
    //fCandidateLabel = candidate["Label"];

    return fTMVAReader.EvaluateMVA("MLP");
}

void EventNTagManager::MapTaggables()
{
    MapCandidateClusters(fEventEarlyCandidates);
    MapCandidateClusters(fEventCandidates);
}

void EventNTagManager::ResetTaggableMapping()
{
    // taggables
    for (auto& taggable: fEventTaggables) {
        taggable.SetCandidateIndex("Early", -1);
        taggable.SetCandidateIndex("Delayed", -1);
        taggable.SetTaggedType(typeMissed);
    }
    
    // muechk candidates
    for (auto& candidate: fEventEarlyCandidates) {
        candidate.Set("TagIndex", -1);
        candidate.Set("Label", lNoise);
        candidate.Set("TagClass", FindTagClass(candidate));
    }
    
    // ntag candidates
    for (auto& candidate: fEventCandidates) {
        candidate.Set("TagIndex", -1);
        candidate.Set("Label", lNoise);
        candidate.Set("TagClass", FindTagClass(candidate));
    }
}

void EventNTagManager::MapCandidateClusters(CandidateCluster& candidateCluster)
{
    std::string key = candidateCluster.GetName();

    for (unsigned int iCandidate=0; iCandidate<candidateCluster.GetSize(); iCandidate++) {

        auto& candidate = candidateCluster[iCandidate];
        candidate.Set("TagIndex", -1);

        // default label: noise
        TrueLabel label = lNoise;
        bool hasMatchingN = false;
        bool hasMatchingE = false;

        std::vector<int> taggableIndexList;
        std::vector<float> matchTimeList;

        taggableIndexList.clear();
        matchTimeList.clear();
        for (unsigned int iTaggable=0; iTaggable<fEventTaggables.GetSize(); iTaggable++) {
            auto& taggable = fEventTaggables[iTaggable];
            float tDiff = fabs(taggable.Time() - candidate["ReconCT"]*1e3);
            if (tDiff < TMATCHWINDOW) {
                matchTimeList.push_back(tDiff);
                taggableIndexList.push_back(iTaggable);
            }
        }

        if (!matchTimeList.empty()) {

            // closest taggable is matched to the given candidate
            int iMinMatchTimeCapture = taggableIndexList[GetMinIndex(matchTimeList)];
            auto& taggable = fEventTaggables[iMinMatchTimeCapture];

            // candidate label determined by taggable type
            if (taggable.Type() == typeN) {
                hasMatchingN = true;
                if (taggable.Energy() > 6.) label = lnGd;
                else                        label = lnH;
            }
            else if (taggable.Type() == typeE) {
                hasMatchingE = true;
                label = lDecayE;
            }

            // set candidate tagindex as the index of the closest taggable
            candidate.Set("TagIndex", iMinMatchTimeCapture);

            // set two taggable candidate indices:
            // one from early (muechk) and another from delayed (ntag) candidates
            // if the taggable has no previously saved candidate index
            if (taggable.GetCandidateIndex(key) == -1) {
                SetTaggedType(taggable, candidate);
                taggable.SetCandidateIndex(key, iCandidate);
            }
            // if the taggable has previously saved candidate index,
            // save the candidate with more hits
            else {
                auto& givenCandidate = fEventCandidates[iCandidate];
                auto& savedCandidate = fEventCandidates[taggable.GetCandidateIndex(key)];
                int givenNHits = givenCandidate["NHits"];
                int savedNHits = savedCandidate["NHits"];
                if (givenNHits > savedNHits) {
                    SetTaggedType(taggable, candidate);
                    taggable.SetCandidateIndex(key, iCandidate);
                    savedCandidate.Set("Label", lRemnant);
                }
                else {
                    label = lRemnant;
                }
            }
        }

        if (hasMatchingE && hasMatchingN)
            label = lUndefined;

        candidate.Set("Label", label);
    }
}

void EventNTagManager::PruneCandidates()
{
    // n-like early candidates
    //for (int iCandidate=0; iCandidate<fEventEarlyCandidates.GetSize(); iCandidate++) {
    //    auto& candidate = fEventEarlyCandidates[iCandidate];
    //    if (candidate["NHits"] < 50) {
    //        
    //    }

    std::vector<int> duplicateCandidateList;
    for (unsigned int iCandidate=0; iCandidate<fEventCandidates.GetSize(); iCandidate++) {
        auto& candidate = fEventCandidates[iCandidate];
        for (auto& early: fEventEarlyCandidates) {
            if (early["TagClass"] == typeE &&
                fabs(early["ReconCT"] - candidate["ReconCT"])*1e3 < 2*TMATCHWINDOW) {
                duplicateCandidateList.push_back(iCandidate); break;
            }
        }
    }

    for (auto& duplicateIndex: duplicateCandidateList)
        fEventCandidates.Erase(duplicateIndex);
}

int EventNTagManager::GetMaxNHitsIndex(PMTHitCluster& hitCluster)
{
    int maxID = 0;
    int maxNHits = 0;

    for (unsigned int i=0; i<hitCluster.GetSize(); i++) {

        int nHits = hitCluster.Slice(i, TWIDTH).GetSize();
        if (nHits > maxNHits) {
            maxID = i;
            maxNHits = nHits;
        }
    }

    return maxID;
}

int EventNTagManager::FindTagClass(const Candidate& candidate)
{
    int tagClass = 0;
    int n50 = candidate.Get("N50", -1);
    float reconCT = candidate.Get("ReconCT");
    float tmvaOut = candidate.Get("TMVAOutput");

    // simple cuts mode for e/n separation
    if (fUseECut) {
        if (reconCT < T0TH*1e-3)                        tagClass = typeE;      // e: muechk && before ntag
        else if (n50 > E_N50CUT && reconCT < E_TIMECUT) tagClass = typeE;      // e: ntag && elike
        else if (tmvaOut > N_OUTCUT)                    tagClass = typeN;      // n: ntag && !e-like && n-like 
        else                                            tagClass = typeMissed; // otherwise noise
    } 
    // naive tagging mode without e/n separation
    else {
        if (n50 < 0)                 tagClass = typeE; // e: muechk
        else if (tmvaOut > N_OUTCUT) tagClass = typeN; // n: ntag && out cut
        else                         tagClass = typeMissed; // otherwise noise
    }
        
    return tagClass;
}

void EventNTagManager::SetTaggedType(Taggable& taggable, Candidate& candidate)
{
    TaggableType tagClass = static_cast<TaggableType>((int)(candidate.Get("TagClass", -1)+0.5f));
    TaggableType tagType = taggable.TaggedType();
    // if candidate tag class undefined
    if (tagClass < 0)
        candidate.Set("TagClass", FindTagClass(candidate));
    // if candidate tag class defined
    else if (tagType != typeMissed && tagType != tagClass)
        taggable.SetTaggedType(typeEN);
    else
        taggable.SetTaggedType(tagClass);
}

void EventNTagManager::DumpEvent()
{
    fEventVariables.Print();
    fEventParticles.DumpAllElements();
    fEventTaggables.DumpAllElements();
    fEventEarlyCandidates.DumpAllElements({"ReconCT", "NHits", "DWall", "Goodness", 
                                           "Label", "TagIndex", "TagClass"});
    fEventCandidates.DumpAllElements({"ReconCT",
                                      "NHits",
                                      "DWall_n",
                                      "ThetaMeanDir",
                                      "TMVAOutput",
                                      "Label",
                                      "TagIndex",
                                      "TagClass"});
}

void EventNTagManager::FillTrees()
{
    // set branch address for the first event
    if (!fIsBranchSet) {

        // make branches
        fSettings.MakeBranches();
        fEventVariables.MakeBranches();
        fEventParticles.MakeBranches();
        fEventTaggables.MakeBranches();
        fEventEarlyCandidates.MakeBranches();
        fEventCandidates.MakeBranches();

        // settings should be filled only once
        fSettings.FillTree();
        fIsBranchSet = true;
    }

    // fill trees
    fEventVariables.FillTree();
    fEventParticles.FillTree();
    fEventTaggables.FillTree();
    fEventEarlyCandidates.FillTree();
    fEventCandidates.FillTree();
}

void EventNTagManager::WriteTrees(bool doCloseFile)
{
    fSettings.WriteTree();
    fEventVariables.WriteTree();
    fEventParticles.WriteTree();
    fEventTaggables.WriteTree();
    fEventEarlyCandidates.WriteTree();
    fEventCandidates.WriteTree();
    if (doCloseFile) fEventCandidates.GetTree()->GetCurrentFile()->Close();
}

void EventNTagManager::FillNTagCommon()
{
    int nTrueE = 0, nTaggedE = 0, nTrueN = 0, nTaggedN = 0;
    
    // count true
    for (auto const& taggable: fEventTaggables) {
        if      (taggable.Type() == typeE) nTrueE++;
        else if (taggable.Type() == typeN) nTrueN++;
    }

    // muechk: count tagged
    for (auto const& candidate: fEventEarlyCandidates) {
        if (candidate["TagClass"] == typeE)
            nTaggedE++;
        else if (candidate["TagClass"] == typeN)
            nTaggedN++;
    }
    
    // ntag: count tagged
    int i = 0;
    for (auto const& candidate: fEventCandidates) {
        int label = candidate["Label"];
        int isTaggedOrNot = 0;
    
        if (candidate["TagClass"] == typeE) {
            nTaggedE++;
        }
        else if (candidate["TagClass"] == typeN) {
            isTaggedOrNot = 1;
            nTaggedN++;
        }
        
        // fill ntag bank: candidates
        ntag_.ntime[i] = candidate["ReconCT"] * 1e3;
        ntag_.mctruth_neutron[i] = ntag_.np > MAXNP ? -1 : (label == lnH || label == lnGd ? 1 : 0);
        ntag_.goodness[i] = candidate["TMVAOutput"];
        ntag_.tag[i] = isTaggedOrNot;
        i++;
    }

    // fill ntag bank: event variables
    ntag_.np = fEventCandidates.GetSize();
    ntag_.trgtype = nTrueE; // temporarily save nTrueE in ntag_.trgtype for now
    ntag_.n200m = nTaggedE; // temporarily save nTaggedE ntag_.n200m for now
    ntag_.mctruth_nn = nTrueN;
    ntag_.nn = nTaggedN;

    // set event variables
    fEventVariables.Set("NTrueE", nTrueE);
    fEventVariables.Set("NTaggedE", nTaggedE);
    fEventVariables.Set("NTrueN", nTrueN);
    fEventVariables.Set("NTaggedN", nTaggedN);
}
