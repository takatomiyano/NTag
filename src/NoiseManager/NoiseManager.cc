#include <TROOT.h>
#include <TF1.h>
#include <TH1F.h>
#include <TFile.h>
#include <TCanvas.h>
#include <TTree.h>
#include <TFitResult.h>
#include <TFitResultPtr.h>
#include <TChain.h>
#include <TRandom3.h>

#include <tqrealroot.h>
#undef MAXPM
#undef MAXPMA

#include <Calculator.hh>
#include <SKIO.hh>
#include "NoiseManager.hh"

NoiseManager::NoiseManager()
: fNoiseTree(0), fNoiseTreeName("data"),
  fNoiseEventLength(1000e3),
  fNoiseStartTime(0), fNoiseEndTime(535e3), fNoiseWindowWidth(535e3),
  fNoiseT0(0),
  fMinHitDensity(10e-3), fMaxHitDensity(50e-3), // hits per nanosecond
  fPMTDeadtime(900),
  fCurrentHitID(0),
  fCurrentEntry(-1), fNEntries(0),
  fPartID(0), fNParts(2),
  fDoRepeat(false),
  fMsg("NoiseManager")
{}

NoiseManager::NoiseManager(TString option, int nInputEvents, float tStart, float tEnd, int seed)
: NoiseManager()
{
    SetNoiseTimeRange(tStart, tEnd);

    // Read dummy (TChain)
    TChain* dummyChain = new TChain(fNoiseTreeName);

    TObjArray* opt = option.Tokenize('/');
    TString base = ((TObjString*)(opt->At(0)))->GetString();
    TString run = "";
    if (option.Contains("/"))
        run = ((TObjString*)(opt->At(1)))->GetString();

    SetSeed(seed);
    TString dummyRunPath = DUMMYDIR + option;
    int nRequiredEvents = nInputEvents / fNParts;

    std::vector<TString> runDirs = GetListOfSubdirectories(DUMMYDIR+base);
    std::vector<TString> fileList;
    if (run != "") fileList = GetListOfFiles(DUMMYDIR + option);

    TString dummyFilePath;

    std::vector<TString> usedFilesList;

    while (fNEntries <= 2*nRequiredEvents) {
        if (run == "") {
            dummyRunPath = PickRandom(runDirs);
            fileList = GetListOfFiles(dummyRunPath, ".root");
        }
        if (!fileList.empty())
            dummyFilePath = PickRandom(fileList);
        AddNoiseFileToChain(dummyChain, dummyFilePath);
    }

    SetNoiseTree(dummyChain);
}

NoiseManager::NoiseManager(TTree* tree)
: NoiseManager()
{
    SetNoiseTree(tree);
}

NoiseManager::~NoiseManager()
{
    if (fNoiseTree) delete fNoiseTree;
}

void NoiseManager::AddNoiseFileToChain(TChain* chain, TString noiseFilePath)
{
    static std::vector<TString> usedFilesList;

    TFile* dummyFile = 0;
    TTree* tree = 0;
    int nAddedEntries = 0;

    dummyFile = TFile::Open(noiseFilePath);
    tree = (TTree*)(dummyFile->Get(fNoiseTreeName));

    if (tree) {
        nAddedEntries = tree->GetEntries(DUMMYCUT);
        dummyFile->Close();
    }

    if (nAddedEntries && std::find(usedFilesList.begin(), usedFilesList.end(), noiseFilePath) == usedFilesList.end()) {
        fMsg.Print(Form("Adding dummy file at ") + noiseFilePath + Form(": %d entries", nAddedEntries));
        chain->Add(noiseFilePath);
        fNEntries += nAddedEntries;
        usedFilesList.push_back(noiseFilePath);
    }
}

void NoiseManager::SetNoiseTree(TTree* tree)
{
    fNoiseTree = tree;
    fHeader = 0;
    fTQReal = 0;
    fNoiseTree->SetBranchAddress("HEADER", &fHeader);
    fNoiseTree->SetBranchAddress("TQREAL", &fTQReal);
    fNEntries = fNoiseTree->GetEntries();

    tree->Draw(Form("TQREAL.nhits>>hNHits(500, %f, %f)", MINDUMMYHITS, MAXDUMMYHITS), DUMMYCUT, "", 1000);
    TH1F* hNHits = (TH1F*)gROOT->Get("hNHits");
    TF1* gausFunc = new TF1("gaus", "gaus", MINDUMMYHITS, MAXDUMMYHITS);
    gausFunc->SetParLimits(0, 0, 1000);
    gausFunc->SetParLimits(1, MINDUMMYHITS, MAXDUMMYHITS);
    gausFunc->SetParLimits(2, 0, (MAXDUMMYHITS-MINDUMMYHITS)/2.);
    TFitResultPtr gausFit = hNHits->Fit(gausFunc, "S", "goff");
    const double* fitParams = gausFit->GetParams();
    double fitMean = fitParams[1]; double fitSigma = fitParams[2];

    float leftEdge = fitMean - 3 * fitSigma;
    float rightEdge = fitMean + 3 * fitSigma;

    fMinHitDensity = leftEdge / 1000.;
    fMaxHitDensity = rightEdge / 1000.;

    fMsg.Print(Form("Noise hits per entry: %3.2f +- %3.2f hits", fitMean, fitSigma));
    fMsg.Print(Form("3-sigma hit density range: (%3.1f , %3.1f) hits/us", fMinHitDensity, fMaxHitDensity));
    fMsg.Print(Form("Total entries: %d", fNEntries));
    fMsg.Print(Form("Total dummy trigger entries: %d", fNoiseTree->GetEntries(DUMMYCUT)));
}

void NoiseManager::SetNoiseTimeRange(float startTime, float endTime)
{
    fNoiseStartTime = startTime;
    fNoiseEndTime = endTime;
    fNoiseWindowWidth = endTime - startTime;
    fNParts = (int)(fNoiseEventLength / fNoiseWindowWidth);
}

void NoiseManager::GetNextNoiseEvent()
{
    fPartID = 0; fCurrentHitID = 0;

    fCurrentEntry++; fNoiseEventHits.Clear();
    if (fCurrentEntry < fNEntries) {
        fNoiseTree->GetEntry(fCurrentEntry);
        if (fHeader->idtgsk != mT2KDummy && fHeader->idtgsk != mRandomWide)
            GetNextNoiseEvent();
        else
            SetNoiseEventHits();
    }
    else {
        fMsg.Print("Noise tree reached its end!", pWARNING);
        if (fCurrentEntry == fNEntries && fDoRepeat) {
            // start from beginning
            fMsg.Print("Repetition allowed: going back to the first entry in noise tree...", pWARNING);
            fCurrentEntry = 0;
            fNoiseTree->GetEntry(fCurrentEntry);
        }
        else {
            // error
            fMsg.Print("Repetition disallowed. To allow, use NoiseManager::SetRepeat(true). Aborting program...", pERROR);
        }
    }
}

void NoiseManager::SetNoiseEventHits()
{
    fT = fTQReal->T;
    fQ = fTQReal->Q;
    fI = fTQReal->cables;
    unsigned int nRawHits = fT.size();

    for (unsigned int j=0; j<=nRawHits; j++)
        fNoiseEventHits.Append({fT[j], fQ[j], fI[j]&0x0000FFFF, 2/*in-gate flag*/});

    fNoiseEventHits.Sort();
    int nHits = fNoiseEventHits.GetSize();
    fNoiseEventLength = fNoiseEventHits[nHits-1].t() - fNoiseEventHits[0].t();
    float rawHitDensity = nRawHits / 1000.;
    fNParts = (int)(fNoiseEventLength / fNoiseWindowWidth);

    if (fNParts <= 0 || rawHitDensity < fMinHitDensity || rawHitDensity > fMaxHitDensity) {
        fMsg.Print(Form("Skipping inappropriate entry with fNParts = %d, rawHitDensity = %3.2f hits/us\n", fNParts, rawHitDensity), pWARNING, false);
        GetNextNoiseEvent();
    }

    fNoiseT0 = fT[0] + (fNoiseEventLength - fNParts*fNoiseWindowWidth)/2.;
}

void NoiseManager::AddNoise(PMTHitCluster* signalHits)
{
    if (fCurrentEntry == -1 || fPartID == fNParts) {
        GetNextNoiseEvent();
        fMsg.Print(Form("Current noise entry: %d", fCurrentEntry));
    }

    float partStartTime = fNoiseT0 + fPartID * fNoiseWindowWidth;
    float partEndTime = partStartTime + fNoiseWindowWidth;

    while (fNoiseEventHits[fCurrentHitID].t() < partStartTime) fCurrentHitID++;

    while (fNoiseEventHits[fCurrentHitID].t() < partEndTime) {
        PMTHit hit = fNoiseEventHits[fCurrentHitID];
        hit += (fNoiseStartTime - partStartTime);
        signalHits->Append(hit);
        fCurrentHitID++;
    }

    signalHits->ApplyDeadtime(fPMTDeadtime);
    signalHits->Sort();
    fPartID++;
}