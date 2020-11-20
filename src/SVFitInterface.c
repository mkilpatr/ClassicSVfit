#ifdef DOPYCAPIBIND
#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include "Python.h"
#include "numpy/arrayobject.h"

#include "TLorentzVector.h"
#include "Math/VectorUtil.h"

#include <vector>
#include <iostream>
#include <memory>

#include "SVFit/SVFit/interface/SVFit.h"
#include "SVFit/SVFit/interface/SVFitResults.h"
#include "SVFit/SVFit/interface/SVFitPython.h"
#include "SVFit/SVFit/interface/SVFitUtilities.h"
#include "SVFit/CfgParser/interface/TTException.h"

/// Destructor function for SVFit object cleanup 
static void SVFitInterface_cleanup(PyObject *ptt)
{
    //Get top tagger pointer from capsule 
    SVFit* tt = (SVFit*) PyCapsule_GetPointer(ptt, "SVFit");
    
    if(tt) delete tt;
}

static ttPython::Py_buffer_wrapper<TLorentzVector> createLorentzP4(PyObject* lorentzVector)
{
    if(!lorentzVector || lorentzVector == Py_None) //is invalid pointer
    {
    }
    else if(PyTuple_Check(lorentzVector)) // this is 4-vector components
    {
        PyObject *pPt, *pEta, *pPhi, *pMass;
        int len = 0;
        if (!PyArg_ParseTuple(lorentzVector, "OOOO|i", &pPt, &pEta, &pPhi, &pMass, &len))
        {
        }

        ttPython::Py_buffer_wrapper<Float_t> pt(pPt, len);
        ttPython::Py_buffer_wrapper<Float_t> eta(pEta, len);
        ttPython::Py_buffer_wrapper<Float_t> phi(pPhi, len);
        ttPython::Py_buffer_wrapper<Float_t> m(pMass, len);

        std::vector<TLorentzVector> vec(len);
        for(int i = 0; i < len; ++i)
        {
            vec[i].SetPtEtaPhiM(pt[i], eta[i], phi[i], m[i]);
        }

        return ttPython::Py_buffer_wrapper<TLorentzVector>(std::move(vec));
    }
    else if(TPython::ObjectProxy_Check(lorentzVector)) //this is already vector<TLorentzVector>
    {
        return ttPython::Py_buffer_wrapper<TLorentzVector>(lorentzVector);
    }
    
    //No idea what this is 
    return ttPython::Py_buffer_wrapper<TLorentzVector>(nullptr);
}

static int SVFitInterface_makeGenInfo(
    std::unique_ptr<std::pair<std::vector<TLorentzVector>, std::vector<std::vector<const TLorentzVector*>>>>& genInfo,
    std::vector<ttPython::Py_buffer_wrapper<TLorentzVector>>& tempTLVBuffers,
    PyObject* pArgTuple)
{
    //number of variables which are not stored in supplamental dictionaries
    const unsigned int NTLVVAR = 1;
    int nGenPart;

    PyObject *pGenPart, *pPdgId, *pStatusFlag, *pGenPartIdxMother;
    if (!PyArg_ParseTuple(pArgTuple, "iOOOO", &nGenPart, &pGenPart, &pPdgId, &pStatusFlag, &pGenPartIdxMother))
    {
        return 1;
    }

    //Prepare std::vector<TLorentzVector> for jets and subjets lorentz vectors and subjet linking 
    //Prepare std::vector<TLorentzVector> for jets lorentz vectors
    //reserve space for the vector to stop reallocations during emplacing
    tempTLVBuffers.reserve(NTLVVAR);
    tempTLVBuffers.emplace_back(createLorentzP4(pGenPart));
    auto& genPartLV = tempTLVBuffers.back();

    ttPython::Py_buffer_wrapper<Int_t> pdgId(pPdgId, nGenPart);
    ttPython::Py_buffer_wrapper<Int_t> statusFlag(pStatusFlag, nGenPart);
    ttPython::Py_buffer_wrapper<Int_t> genPartIdxMother(pGenPartIdxMother, nGenPart);

    //Create gen information 
    auto* genInfoPtr = new std::pair<std::vector<TLorentzVector>, std::vector<std::vector<const TLorentzVector*>>>(ttUtility::GetTopdauGenLVecFromNano(genPartLV, pdgId, statusFlag, genPartIdxMother));

    //transfer new ptr to the control of the unique_ptr
    genInfo.reset(genInfoPtr);

    return 0;
}

static int SVFitInterface_makeAK4Const(
    std::unique_ptr<ttUtility::ConstAK4Inputs<Float_t, ttPython::Py_buffer_wrapper<Float_t>, ttPython::Py_buffer_wrapper<TLorentzVector>>>& ak4ConstInputs, 
    std::vector<ttPython::Py_buffer_wrapper<TLorentzVector>>& tempTLVBuffers, 
    std::vector<ttPython::Py_buffer_wrapper<Float_t>>& tempFloatBuffers, 
    std::vector<unsigned char>& filterVec, 
    std::unique_ptr<std::pair<std::vector<TLorentzVector>, std::vector<std::vector<const TLorentzVector*>>>>& genInfo,
    PyObject* pArgTuple)
{
    //number of variables which are not stored in supplamental dictionaries
    const unsigned int NTLVVAR = 1;
    const unsigned int NEXTRAVAR = 1;
    int nJet, nElec, nMuon;

    PyObject *pJet, *pJetBtag, *pFloatVarsDict, *pIntVarsDict, *pElectronIdx1 = nullptr, *pMuonIdx1 = nullptr, *pElectron = nullptr, *pElectron_cutBasedBits = nullptr, *pElectron_miniPFRelIso = nullptr, *pMuon = nullptr, *pMuon_id = nullptr, *pMuon_pfRelIso = nullptr;
    if (!PyArg_ParseTuple(pArgTuple, "iOOO!O!|OOiOOOiOOO", &nJet, &pJet, &pJetBtag, &PyDict_Type, &pFloatVarsDict, &PyDict_Type, &pIntVarsDict, &pElectronIdx1, &pMuonIdx1, &nElec, &pElectron, &pElectron_cutBasedBits, &pElectron_miniPFRelIso, &nMuon, &pMuon, &pMuon_id, &pMuon_pfRelIso))
    {
        return 1;
    }

    //reserve space for the vector to stop reallocations during emplacing, need space for float and int "vectors" stored here
    Py_ssize_t floatSize = PyDict_Size(pFloatVarsDict);
    Py_ssize_t intSize = PyDict_Size(pIntVarsDict);
    tempFloatBuffers.reserve(NEXTRAVAR + floatSize + intSize);

    //Prepare std::vector<TLorentzVector> for jets lorentz vectors
    //reserve space for the vector to stop reallocations during emplacing
    tempTLVBuffers.reserve(NTLVVAR);
    tempTLVBuffers.emplace_back(createLorentzP4(pJet));
    auto& jetsLV = tempTLVBuffers.back();

    //lepton matching variables
    ttPython::Py_buffer_wrapper<Int_t> elecIdx1(pElectronIdx1, nJet);
    ttPython::Py_buffer_wrapper<Int_t> muonIdx1(pMuonIdx1, nJet);

    auto elecLV = createLorentzP4(pElectron);
    ttPython::Py_buffer_wrapper<Int_t> elecCutBits(pElectron_cutBasedBits, nElec);
    ttPython::Py_buffer_wrapper<Float_t> elecMiniPFRelIso(pElectron_miniPFRelIso, nElec);

    auto muonLV = createLorentzP4(pMuon);
    ttPython::Py_buffer_wrapper<Bool_t> muonID(pMuon_id, nMuon);
    ttPython::Py_buffer_wrapper<Float_t> muonPFRelIso(pMuon_pfRelIso, nMuon);

    //reserve space for the vector to stop reallocations during emplacing
    filterVec.resize(jetsLV.size(), true);
    for(unsigned int iJet = 0; iJet < jetsLV.size(); ++iJet)
    {
        //check first and last optional parameter ... assume others are here 
        if(pElectronIdx1 && pMuon_pfRelIso)
        {
            bool isLep = false;

            //recalculate electron ID without isolation 
            //VID compressed bitmap (MinPtCut,GsfEleSCEtaMultiRangeCut,GsfEleDEtaInSeedCut,GsfEleDPhiInCut,GsfEleFull5x5SigmaIEtaIEtaCut,GsfEleHadronicOverEMEnergyScaledCut,GsfEleEInverseMinusPInverseCut,GsfEleRelPFIsoScaledCut,GsfEleConversionVetoCut,GsfEleMissingHitsCut), 3 bits per cut
            //this is a 'bit' awful <- that pun is awful, but yes, this should be made a little better 
            if(elecIdx1[iJet] >= 0 && elecIdx1[iJet] < static_cast<int>(elecLV.size()) && elecLV[elecIdx1[iJet]].Pt() > 10.0)
	    {
                //MAsk relIso from the ID so we can apply miniIso
                const int NCUTS = 10;
                const int BITSTRIDE = 3;
                const int BITMASK = 0x7;
                const int ISOBITMASK = 070000000;  //note to the curious, 0 before an integer is octal, so 070000000 = 0xE00000 = 14680064, so this corrosponds to the three pfRelIso bits 
                int cutBits = elecCutBits[elecIdx1[iJet]] | ISOBITMASK; // the | masks the iso cut
                int elecID = 07; // start with the largest 3 bit number
                for(int i = 0; i < NCUTS; ++i)
                {
                    elecID = std::min(elecID, cutBits & BITMASK);
                    cutBits = cutBits >> BITSTRIDE;
                }

                double dR = ROOT::Math::VectorUtil::DeltaR(jetsLV[iJet], elecLV[elecIdx1[iJet]]);

                isLep = isLep || (elecID >= 1 && elecMiniPFRelIso[elecIdx1[iJet]] < 0.10 && dR < 0.2);
            }
            
            if(muonIdx1[iJet] >= 0 && muonIdx1[iJet] < static_cast<int>(muonLV.size()) && muonLV[muonIdx1[iJet]].Pt() > 10.0)
            {
                double dR = ROOT::Math::VectorUtil::DeltaR(jetsLV[iJet], muonLV[muonIdx1[iJet]]);

                //pMuon_id == Py_None is a hack because moun loose ID is not a variable, but instead only loose muons are saved 
                isLep = isLep || ((pMuon_id == Py_None || muonID[muonIdx1[iJet]]) && muonPFRelIso[muonIdx1[iJet]] < 0.2 && dR < 0.2);
            }

            //filter out jet if it is matched to a lepton, or if it has pt < 20 GeV
            filterVec[iJet] = !isLep && jetsLV[iJet].Pt() >= 19.9; //better to underclean just a bit because of nanoAOD rounding
        }
    }

    //prepare b-tag discriminator
    tempFloatBuffers.emplace_back(pJetBtag, nJet);
    auto& jetBTag = tempFloatBuffers.back();

    //Create the AK4 constituent helper
    ak4ConstInputs.reset(new ttUtility::ConstAK4Inputs<Float_t, ttPython::Py_buffer_wrapper<Float_t>, ttPython::Py_buffer_wrapper<TLorentzVector>>(jetsLV, jetBTag));
    ak4ConstInputs->setFilterVector(filterVec);
    if(genInfo) ak4ConstInputs->addGenCollections(genInfo->first, genInfo->second);

    //prepare floating point supplamental "vectors"
    PyObject *key, *value;
    Py_ssize_t pos = 0;

    while(PyDict_Next(pFloatVarsDict, &pos, &key, &value))
    {
        if(PyString_Check(key))
        {
            //Get the ROOT.PyFloatBuffer into a c++ friendly format
            tempFloatBuffers.emplace_back(value, nJet);

            char *vecName = PyString_AsString(key);
            ak4ConstInputs->addSupplamentalVector(vecName, tempFloatBuffers.back());
        }
        else
        {
            //Handle error here
            PyErr_SetString(PyExc_KeyError, "Dictionary keys must be strings for top tagger supplamentary variables.");
            return 1;
        }
    }

    //prepare integer supplamental "vectors" and convert to float vector 
    pos = 0;
    //reserve space for the vector to stop reallocations during emplacing 
    while(PyDict_Next(pIntVarsDict, &pos, &key, &value)) 
    {
        if(PyString_Check(key))
        {
            //Get the ROOT.PyIntBuffer into a c++ friendly format
            ttPython::Py_buffer_wrapper<Int_t> buffer(value, nJet);
            
            //translate the integers to floats
            std::vector<float> tempIntToFloat(buffer.begin(), buffer.end());

            //wrap vector in buffer
            tempFloatBuffers.emplace_back(std::move(tempIntToFloat));
            
            char *vecName = PyString_AsString(key);
            ak4ConstInputs->addSupplamentalVector(vecName, tempFloatBuffers.back());
        }
        else
        {
            //Handle error here
            PyErr_SetString(PyExc_KeyError, "Dictionary keys must be strings for top tagger supplamentary variables.");
            return 1;
        }
    }

    return 0;
}

static int SVFitInterface_makeAK8Const(
    std::unique_ptr<ttUtility::ConstAK8Inputs<Float_t, ttPython::Py_buffer_wrapper<Float_t>, ttPython::Py_buffer_wrapper<TLorentzVector>>>& ak8ConstInputs, 
    std::vector<ttPython::Py_buffer_wrapper<Float_t>>& tempFloatBuffers, 
    std::vector<ttPython::Py_buffer_wrapper<TLorentzVector>>& tempTLVBuffers, 
    std::vector<std::vector<TLorentzVector>>& vecSubjetsLV, 
    std::unique_ptr<std::pair<std::vector<TLorentzVector>, std::vector<std::vector<const TLorentzVector*>>>>& genInfo,
    PyObject* pArgTuple)
{
    //number of variables
    const unsigned int NFLOATVAR = 3;
    const unsigned int NTLVVAR = 1;
    int nFatJet, nSubJet;

    PyObject *pJet, *pJetSDMass, *pJetTDisc, *pJetWDisc, *pSubjet, *pSubjetIdx1, *pSubjetIdx2;
    if (!PyArg_ParseTuple(pArgTuple, "iOOOOiOOO", &nFatJet, &pJet, &pJetSDMass, &pJetTDisc, &pJetWDisc, &nSubJet, &pSubjet, &pSubjetIdx1, &pSubjetIdx2))
    {
        return 1;
    }

    //reserve space for the vector to stop reallocations during emplacing
    tempTLVBuffers.reserve(NTLVVAR);
    //Prepare std::vector<TLorentzVector> for jets and subjets lorentz vectors and subjet linking 
    tempTLVBuffers.emplace_back(createLorentzP4(pJet));
    auto& jetsLV = tempTLVBuffers.back();

    ttPython::Py_buffer_wrapper<Int_t> subjetIdx1(pSubjetIdx1, nFatJet);
    ttPython::Py_buffer_wrapper<Int_t> subjetIdx2(pSubjetIdx2, nFatJet);

    auto subjetsLV = createLorentzP4(pSubjet);

    //reserve space for the vector to stop reallocations during emplacing
    vecSubjetsLV.resize(jetsLV.size());
    for(unsigned int iJet = 0; iJet < jetsLV.size(); ++iJet)
    {
        //reserve space for the vector to stop reallocations during emplacing
        int idx1 = subjetIdx1[iJet];
        int idx2 = subjetIdx2[iJet];

        if(idx1 >= 0 && idx1 < static_cast<int>(subjetsLV.size())) vecSubjetsLV[iJet].push_back(subjetsLV[idx1]);
        if(idx2 >= 0 && idx2 < static_cast<int>(subjetsLV.size())) vecSubjetsLV[iJet].push_back(subjetsLV[idx2]);
    }

    //Wrap basic floating point vectors
    //reserve space for the vector to stop reallocations during emplacing
    tempFloatBuffers.reserve(NFLOATVAR);

    tempFloatBuffers.emplace_back(pJetSDMass, nFatJet);
    auto& jetSDMass = tempFloatBuffers.back();

    tempFloatBuffers.emplace_back(pJetTDisc, nFatJet);
    auto& jetTopDisc = tempFloatBuffers.back();

    tempFloatBuffers.emplace_back(pJetWDisc, nFatJet);
    auto& jetWDisc = tempFloatBuffers.back();

    //Create the AK8 constituent helper
    ak8ConstInputs.reset(new ttUtility::ConstAK8Inputs<Float_t, ttPython::Py_buffer_wrapper<Float_t>, ttPython::Py_buffer_wrapper<TLorentzVector>>(jetsLV, jetTopDisc, jetWDisc, jetSDMass, vecSubjetsLV));
    if(genInfo) ak8ConstInputs->addGenCollections(genInfo->first, genInfo->second);

    return 0;
}

static int SVFitInterface_makeResolvedTopConst(
    std::unique_ptr<ttUtility::ConstResolvedCandInputs<Float_t, ttPython::Py_buffer_wrapper<Float_t>, ttPython::Py_buffer_wrapper<Int_t>, ttPython::Py_buffer_wrapper<TLorentzVector>>>& resolvedTopConstInputs, 
    std::vector<ttPython::Py_buffer_wrapper<Float_t>>& tempFloatBuffers, 
    std::vector<ttPython::Py_buffer_wrapper<Int_t>>& tempIntBuffers, 
    std::vector<ttPython::Py_buffer_wrapper<TLorentzVector>>& vecTopCandsLV, 
    PyObject* pArgTuple)
{
    //number of variables
    const unsigned int NFLOATVAR = 1;
    const unsigned int NINTVAR = 3;
    const unsigned int NTLVVAR = 1;
    int nResTopCand;

    PyObject *pTopCand, *pTopCandDisc, *pTopCandIdxJ1, *pTopCandIdxJ2, *pTopCandIdxJ3;
    if (!PyArg_ParseTuple(pArgTuple, "iOOOOO", &nResTopCand, &pTopCand, &pTopCandDisc, &pTopCandIdxJ1, &pTopCandIdxJ2, &pTopCandIdxJ3))
    {
        return 1;
    }

    //Prepare topCand 4-vector
    //reserve space for the vector to stop reallocations during emplacing
    vecTopCandsLV.reserve(NTLVVAR);
    vecTopCandsLV.emplace_back(createLorentzP4(pTopCand));
    auto& topCandsLV = vecTopCandsLV.back();

    //Wrap basic floating point/integer vectors
    //reserve space for the vector to stop reallocations during emplacing
    tempFloatBuffers.reserve(NFLOATVAR);
    tempIntBuffers.reserve(NINTVAR);

    tempFloatBuffers.emplace_back(pTopCandDisc, nResTopCand);
    auto& topCandDisc = tempFloatBuffers.back();

    tempIntBuffers.emplace_back(pTopCandIdxJ1, nResTopCand);
    auto& topCandIdxJ1 = tempIntBuffers.back();

    tempIntBuffers.emplace_back(pTopCandIdxJ2, nResTopCand);
    auto& topCandIdxJ2 = tempIntBuffers.back();

    tempIntBuffers.emplace_back(pTopCandIdxJ3, nResTopCand);
    auto& topCandIdxJ3 = tempIntBuffers.back();

    //Create the AK8 constituent helper
    resolvedTopConstInputs.reset(new ttUtility::ConstResolvedCandInputs<Float_t, ttPython::Py_buffer_wrapper<Float_t>, ttPython::Py_buffer_wrapper<Int_t>, ttPython::Py_buffer_wrapper<TLorentzVector>>(topCandsLV, topCandDisc, topCandIdxJ1, topCandIdxJ2, topCandIdxJ3));

    return 0;
}

template<typename A, typename B, typename C>
static inline std::vector<Constituent> createConstituents(A& ak4Inputs, B& ak8Inputs, C& resTopInputs)
{
    //bit mask for terrible switch below
    unsigned int constituentCreationBitMask = 0;
    if(ak4Inputs)    constituentCreationBitMask |= 0x1;
    if(ak8Inputs)    constituentCreationBitMask |= 0x2;
    if(resTopInputs) constituentCreationBitMask |= 0x4;

    //Create constituents 
    switch(constituentCreationBitMask)
    {
    case 0x1: return ttUtility::packageConstituents(*ak4Inputs);
    case 0x5: return ttUtility::packageConstituents(*ak4Inputs, *resTopInputs);
    case 0x2: return ttUtility::packageConstituents(*ak8Inputs);
    case 0x3: return ttUtility::packageConstituents(*ak4Inputs, *ak8Inputs);
    case 0x7: return ttUtility::packageConstituents(*ak4Inputs, *resTopInputs, *ak8Inputs);
    default:
        THROW_TTEXCEPTION("Illegal constituent combination");
        break;
    }
}


extern "C"
{
    static PyObject* SVFitInterface_setup(PyObject *self, PyObject *args)
    {
        //suppress unused parameter warning as self is manditory
        (void)self;

        char *cfgFile, *workingDir = nullptr;

        if (!PyArg_ParseTuple(args, "s|s", &cfgFile, &workingDir)) {
            return NULL;
        }

        //Setup top tagger 
        SVFit *tt = nullptr;

        try
        {
            tt = new SVFit();

            //Check that "new" succeeded
            if(!tt)
            {
                PyErr_NoMemory();
                return NULL;
            }

            //Disable internal print statements on exception 
            tt->setVerbosity(0);

            if(workingDir && strlen(workingDir) > 0)
            {
                tt->setWorkingDirectory(workingDir);
            }
            tt->setCfgFile(cfgFile);
        }
        catch(const TTException& e)
        {
            std::cout << "SVFit exception message: " << e << std::endl;
            PyErr_SetString(PyExc_RuntimeError, ("SVFit exception thrown. TTException message: " + e.getPrintMessage()).c_str());
            return NULL;
        }

        PyObject * ret = PyCapsule_New(tt, "SVFit", SVFitInterface_cleanup);

        return Py_BuildValue("N", ret);
    }

    static PyObject* SVFitInterface_run(PyObject *self, PyObject *args, PyObject *kwargs)
    {
        //suppress unused parameter warning as self is manditory
        (void)self;

        PyObject *ptt, *pAK4Inputs = nullptr, *pAK8Inputs = nullptr, *pResolvedTopCandInputs = nullptr, *pGenInputs = nullptr;
        //PYTHON, LEARN ABOUT CONST!!!!!!!!!!!!!!!!!!
        char kw1[] = "topTagger", kw2[] = "ak4Inputs", kw3[] = "ak8Inputs", kw4[] = "resolvedTopInputs", kw5[] = "genInputs";
        char *keywords[] = {kw1, kw2, kw3, kw4, kw5, NULL};
        if (!PyArg_ParseTupleAndKeywords(args, kwargs, "O!|O!O!O!O!", keywords, &PyCapsule_Type, &ptt, &PyTuple_Type, &pAK4Inputs, &PyTuple_Type, &pAK8Inputs, &PyTuple_Type, &pResolvedTopCandInputs, &PyTuple_Type, &pGenInputs))
        {
            return NULL;
        }

        Py_INCREF(ptt);
        if(pAK4Inputs) Py_INCREF(pAK4Inputs);
        if(pAK8Inputs) Py_INCREF(pAK8Inputs);
        if(pResolvedTopCandInputs) Py_INCREF(pResolvedTopCandInputs);
        if(pGenInputs) Py_INCREF(pGenInputs);

        //prepare gen matching info
        std::unique_ptr<std::pair<std::vector<TLorentzVector>, std::vector<std::vector<const TLorentzVector*>>>> genInfo;
        std::vector<ttPython::Py_buffer_wrapper<TLorentzVector>> tempTLVBuffers;
        if(pGenInputs && SVFitInterface_makeGenInfo(genInfo, tempTLVBuffers, pGenInputs))
        {
            //Status is not 0, there was an error, PyErr_SetString is called in function 
            Py_DECREF(ptt);
            if(pAK4Inputs) Py_DECREF(pAK4Inputs);
            if(pAK8Inputs) Py_DECREF(pAK8Inputs);
            if(pResolvedTopCandInputs) Py_DECREF(pResolvedTopCandInputs);
            if(pGenInputs) Py_DECREF(pGenInputs);

            return NULL;
        }

        //Prepare ak4 jet input constituents 
        std::unique_ptr<ttUtility::ConstAK4Inputs<Float_t, ttPython::Py_buffer_wrapper<Float_t>, ttPython::Py_buffer_wrapper<TLorentzVector>>> ak4ConstInputs;
        std::vector<ttPython::Py_buffer_wrapper<TLorentzVector>> ak4TempTLVBuffers;
        std::vector<ttPython::Py_buffer_wrapper<Float_t>> ak4TempFloatBuffers;
        std::vector<unsigned char> ak4FilterVec;
        if(pAK4Inputs && SVFitInterface_makeAK4Const(ak4ConstInputs, ak4TempTLVBuffers, ak4TempFloatBuffers, ak4FilterVec, genInfo, pAK4Inputs))
        {
            //Status is not 0, there was an error, PyErr_SetString is called in function 
            Py_DECREF(ptt);
            if(pAK4Inputs) Py_DECREF(pAK4Inputs);
            if(pAK8Inputs) Py_DECREF(pAK8Inputs);
            if(pResolvedTopCandInputs) Py_DECREF(pResolvedTopCandInputs);
            if(pGenInputs) Py_DECREF(pGenInputs);

            return NULL;
        }

        //Prepare ak8 jet input constituents 
        std::unique_ptr<ttUtility::ConstAK8Inputs<Float_t, ttPython::Py_buffer_wrapper<Float_t>, ttPython::Py_buffer_wrapper<TLorentzVector>>> ak8ConstInputs;
        std::vector<ttPython::Py_buffer_wrapper<Float_t>> ak8TempFloatBuffers;
        std::vector<ttPython::Py_buffer_wrapper<TLorentzVector>> ak8TempTLVBuffers;
        std::vector<std::vector<TLorentzVector>> ak8SubjetsLV;
        if(pAK8Inputs && SVFitInterface_makeAK8Const(ak8ConstInputs, ak8TempFloatBuffers, ak8TempTLVBuffers, ak8SubjetsLV, genInfo, pAK8Inputs))
        {
            //Status is not 0, there was an error, PyErr_SetString is called in function 
            Py_DECREF(ptt);
            if(pAK4Inputs) Py_DECREF(pAK4Inputs);
            if(pAK8Inputs) Py_DECREF(pAK8Inputs);
            if(pResolvedTopCandInputs) Py_DECREF(pResolvedTopCandInputs);
            if(pGenInputs) Py_DECREF(pGenInputs);

            return NULL;
        }

        std::unique_ptr<ttUtility::ConstResolvedCandInputs<Float_t, ttPython::Py_buffer_wrapper<Float_t>, ttPython::Py_buffer_wrapper<Int_t>, ttPython::Py_buffer_wrapper<TLorentzVector>>> resolvedTopConstInputs;
        std::vector<ttPython::Py_buffer_wrapper<Float_t>> resTopTempFloatBuffers;
        std::vector<ttPython::Py_buffer_wrapper<Int_t>> resTopTempIntBuffers;
        std::vector<ttPython::Py_buffer_wrapper<TLorentzVector>> topCandsLV;
        if(pResolvedTopCandInputs && SVFitInterface_makeResolvedTopConst(resolvedTopConstInputs, resTopTempFloatBuffers, resTopTempIntBuffers, topCandsLV, pResolvedTopCandInputs))
        {
            //Status is not 0, there was an error, PyErr_SetString is called in function 
            Py_DECREF(ptt);
            if(pAK4Inputs) Py_DECREF(pAK4Inputs);
            if(pAK8Inputs) Py_DECREF(pAK8Inputs);
            if(pResolvedTopCandInputs) Py_DECREF(pResolvedTopCandInputs);
            if(pGenInputs) Py_DECREF(pGenInputs);

            return NULL;
        }

        //Get top tagger pointer from capsule 
        SVFit* tt;
        if (!(tt = (SVFit*) PyCapsule_GetPointer(ptt, "SVFit"))) 
        {
            //Handle exception here 
            Py_DECREF(ptt);
            if(pAK4Inputs) Py_DECREF(pAK4Inputs);
            if(pAK8Inputs) Py_DECREF(pAK8Inputs);
            if(pResolvedTopCandInputs) Py_DECREF(pResolvedTopCandInputs);
            if(pGenInputs) Py_DECREF(pGenInputs);

            PyErr_SetString(PyExc_ReferenceError, "SVFit pointer invalid");
            return NULL;
        }

        //Run top tagger
        try
        {
            //create constituent vector 
            const auto constituents = createConstituents(ak4ConstInputs, ak8ConstInputs, resolvedTopConstInputs);

            //Run top tagger 
            tt->runTagger(constituents);
        }
        catch(const TTException& e)
        {
            Py_DECREF(ptt);
            if(pAK4Inputs) Py_DECREF(pAK4Inputs);
            if(pAK8Inputs) Py_DECREF(pAK8Inputs);
            if(pResolvedTopCandInputs) Py_DECREF(pResolvedTopCandInputs);
            if(pGenInputs) Py_DECREF(pGenInputs);

            std::cout << "SVFit exception message: " << e << std::endl;
            PyErr_SetString(PyExc_RuntimeError, ("SVFit exception thrown. TTException message: " + e.getPrintMessage()).c_str());
            return NULL;
        }

        Py_DECREF(ptt);
        if(pAK4Inputs) Py_DECREF(pAK4Inputs);
        if(pAK8Inputs) Py_DECREF(pAK8Inputs);
        if(pResolvedTopCandInputs) Py_DECREF(pResolvedTopCandInputs);
        if(pGenInputs) Py_DECREF(pGenInputs);

        Py_INCREF(Py_None);
        return Py_None;
    }

    static PyObject* SVFitInterface_getResults(PyObject *self, PyObject *args)
    {
        //suppress unused parameter warning as self is manditory
        (void)self;

        PyObject *ptt;
        if (!PyArg_ParseTuple(args, "O!", &PyCapsule_Type, &ptt))
        {
            return NULL;
        }

        Py_INCREF(ptt);

        //Get top tagger pointer from capsule 
        SVFit* tt;
        if (!(tt = (SVFit*) PyCapsule_GetPointer(ptt, "SVFit"))) 
        {
            //Handle exception here 
            Py_DECREF(ptt);

            PyErr_SetString(PyExc_ReferenceError, "SVFit pointer invalid");
            return NULL;
        }

        try
        {
            //Get top tagger results 
            const auto& ttr = tt->getResults();

            //Get tops 
            const auto& tops = ttr.getTops();

            //create numpy arrays for passing top data to python
            const npy_intp NVARSFLOAT = 5;
            const npy_intp NVARSINT = 5;
            const npy_intp NTOPS = static_cast<npy_intp>(tops.size());
    
            npy_intp floatsizearray[] = {NTOPS, NVARSFLOAT};
            PyArrayObject* topArrayFloat = reinterpret_cast<PyArrayObject*>(PyArray_SimpleNew(2, floatsizearray, NPY_FLOAT));

            npy_intp intsizearray[] = {NTOPS, NVARSINT};
            PyArrayObject* topArrayInt = reinterpret_cast<PyArrayObject*>(PyArray_SimpleNew(2, intsizearray, NPY_INT));

            //fill numpy array
            for(unsigned int iTop = 0; iTop < tops.size(); ++iTop)
            {
                *static_cast<npy_float*>(PyArray_GETPTR2(topArrayFloat, iTop, 0)) = tops[iTop]->p().Pt();
                *static_cast<npy_float*>(PyArray_GETPTR2(topArrayFloat, iTop, 1)) = tops[iTop]->p().Eta();
                *static_cast<npy_float*>(PyArray_GETPTR2(topArrayFloat, iTop, 2)) = tops[iTop]->p().Phi();
                *static_cast<npy_float*>(PyArray_GETPTR2(topArrayFloat, iTop, 3)) = tops[iTop]->p().M();
                *static_cast<npy_float*>(PyArray_GETPTR2(topArrayFloat, iTop, 4)) = tops[iTop]->getDiscriminator();

                *static_cast<npy_int*>(PyArray_GETPTR2(topArrayInt, iTop, 0)) = static_cast<int>(tops[iTop]->getType());

                //get constituents vector to retrieve matching index
                const auto& topConstituents = tops[iTop]->getConstituents();
                if(topConstituents.size() > 0) *static_cast<npy_int*>(PyArray_GETPTR2(topArrayInt, iTop, 1)) = static_cast<int>(topConstituents[0]->getIndex());
                else                           *static_cast<npy_int*>(PyArray_GETPTR2(topArrayInt, iTop, 1)) = -1;

                if(topConstituents.size() > 1) *static_cast<npy_int*>(PyArray_GETPTR2(topArrayInt, iTop, 2)) = static_cast<int>(topConstituents[1]->getIndex());
                else                           *static_cast<npy_int*>(PyArray_GETPTR2(topArrayInt, iTop, 2)) = -1;

                if(topConstituents.size() > 2) *static_cast<npy_int*>(PyArray_GETPTR2(topArrayInt, iTop, 3)) = static_cast<int>(topConstituents[2]->getIndex());
                else                           *static_cast<npy_int*>(PyArray_GETPTR2(topArrayInt, iTop, 3)) = -1;

                //get gen matching information 
                *static_cast<npy_int*>(PyArray_GETPTR2(topArrayInt, iTop, 4)) = static_cast<int>(tops[iTop]->getBestGenTopMatch() != nullptr);
            }

            Py_DECREF(ptt);

            return Py_BuildValue("NN", topArrayFloat, topArrayInt);
        }
        catch(const TTException& e)
        {
            Py_DECREF(ptt);

            std::cout << "SVFit exception message: " << e << std::endl;
            PyErr_SetString(PyExc_RuntimeError, ("SVFit exception thrown. TTException message: " + e.getPrintMessage()).c_str());
            return NULL;
        }

    }

    static PyObject* SVFitInterface_getSFSyst(PyObject *self, PyObject *args)
    {
        //suppress unused parameter warning as self is manditory
        (void)self;

        PyObject *ptt;
        if (!PyArg_ParseTuple(args, "O!", &PyCapsule_Type, &ptt))
        {
            return NULL;
        }

        Py_INCREF(ptt);

        //Get top tagger pointer from capsule 
        SVFit* tt;
        if (!(tt = (SVFit*) PyCapsule_GetPointer(ptt, "SVFit"))) 
        {
            //Handle exception here 
            Py_DECREF(ptt);

            PyErr_SetString(PyExc_ReferenceError, "SVFit pointer invalid");
            return NULL;
        }

        try
        {
            //Get top tagger results 
            const auto& ttr = tt->getResults();

            //Get tops 
            const auto& tops = ttr.getTops();
            const npy_intp NTOPS = static_cast<npy_intp>(tops.size());

            //create np array for SF
            npy_intp floatsizearray[] = {NTOPS};
            PyArrayObject* sfArrayFloat = reinterpret_cast<PyArrayObject*>(PyArray_SimpleNew(1, floatsizearray, NPY_FLOAT));

            //create dictionary of np arrays 
            PyObject* pSystList = PyList_New(0);

            //Fill SF and syst data if it is avaliable and tops exist 
            if(NTOPS > 0)
            {
                //fill SF and syst arrays in dictionary 
                for(unsigned int iTop = 0; iTop < tops.size(); ++iTop)
                {
                    //assign scale factor
                    *static_cast<npy_float*>(PyArray_GETPTR1(sfArrayFloat, iTop)) = tops[iTop]->getMCScaleFactor();
                
                    //create dictionary to hold systematics 
                    PyObject* pSystDict = PyDict_New();
                    for(const auto& systPair : tops[iTop]->getAllSystematicUncertainties())
                    {
                        PyObject* pFloat = PyFloat_FromDouble(systPair.second);
                        PyDict_SetItemString(pSystDict, systPair.first.c_str(), pFloat);
                        Py_DECREF(pFloat);
                    }
                    
                    //put the systematic dictionary for this top into the list
                    PyList_Append(pSystList, pSystDict);
                    Py_DECREF(pSystDict);
                }
            }
            
            Py_DECREF(ptt);
            
            return Py_BuildValue("NN", sfArrayFloat, pSystList);
        }
        catch(const TTException& e)
        {
            Py_DECREF(ptt);

            std::cout << "SVFit exception message: " << e << std::endl;
            PyErr_SetString(PyExc_RuntimeError, ("SVFit exception thrown. TTException message: " + e.getPrintMessage()).c_str());
            return NULL;
        }

    }

    static PyObject* SVFitInterface_getCandidates(PyObject *self, PyObject *args)
    {
        //suppress unused parameter warning as self is manditory
        (void)self;

        PyObject *ptt;
        if (!PyArg_ParseTuple(args, "O!", &PyCapsule_Type, &ptt))
        {
            return NULL;
        }

        Py_INCREF(ptt);

        //Get top tagger pointer from capsule 
        SVFit* tt;
        if (!(tt = (SVFit*) PyCapsule_GetPointer(ptt, "SVFit"))) 
        {
            //Handle exception here 
            Py_DECREF(ptt);

            PyErr_SetString(PyExc_ReferenceError, "SVFit pointer invalid");
            return NULL;
        }

        try
        {
            //Get top tagger results 
            const auto& ttr = tt->getResults();

            //Get tops 
            const auto& tops = ttr.getTopCandidates();

            //create numpy arrays for passing top data to python
            const npy_intp NVARSFLOAT = 5;
            const npy_intp NVARSINT = 5;
            const npy_intp NTOPS = static_cast<npy_intp>(tops.size());
    
            npy_intp floatsizearray[] = {NTOPS, NVARSFLOAT};
            PyArrayObject* topArrayFloat = reinterpret_cast<PyArrayObject*>(PyArray_SimpleNew(2, floatsizearray, NPY_FLOAT));

            npy_intp intsizearray[] = {NTOPS, NVARSINT};
            PyArrayObject* topArrayInt = reinterpret_cast<PyArrayObject*>(PyArray_SimpleNew(2, intsizearray, NPY_INT));

            //fill numpy array
            for(unsigned int iTop = 0; iTop < tops.size(); ++iTop)
            {
                *static_cast<npy_float*>(PyArray_GETPTR2(topArrayFloat, iTop, 0)) = tops[iTop].p().Pt();
                *static_cast<npy_float*>(PyArray_GETPTR2(topArrayFloat, iTop, 1)) = tops[iTop].p().Eta();
                *static_cast<npy_float*>(PyArray_GETPTR2(topArrayFloat, iTop, 2)) = tops[iTop].p().Phi();
                *static_cast<npy_float*>(PyArray_GETPTR2(topArrayFloat, iTop, 3)) = tops[iTop].p().M();
                *static_cast<npy_float*>(PyArray_GETPTR2(topArrayFloat, iTop, 4)) = tops[iTop].getDiscriminator();

                *static_cast<npy_int*>(PyArray_GETPTR2(topArrayInt, iTop, 0)) = static_cast<int>(tops[iTop].getType());

                //get constituents vector to retrieve matching index
                const auto& topConstituents = tops[iTop].getConstituents();
                if(topConstituents.size() > 0) *static_cast<npy_int*>(PyArray_GETPTR2(topArrayInt, iTop, 1)) = static_cast<int>(topConstituents[0]->getIndex());
                else                           *static_cast<npy_int*>(PyArray_GETPTR2(topArrayInt, iTop, 1)) = -1;

                if(topConstituents.size() > 1) *static_cast<npy_int*>(PyArray_GETPTR2(topArrayInt, iTop, 2)) = static_cast<int>(topConstituents[1]->getIndex());
                else                           *static_cast<npy_int*>(PyArray_GETPTR2(topArrayInt, iTop, 2)) = -1;

                if(topConstituents.size() > 2) *static_cast<npy_int*>(PyArray_GETPTR2(topArrayInt, iTop, 3)) = static_cast<int>(topConstituents[2]->getIndex());
                else                           *static_cast<npy_int*>(PyArray_GETPTR2(topArrayInt, iTop, 3)) = -1;

                //get gen matching information 
                *static_cast<npy_int*>(PyArray_GETPTR2(topArrayInt, iTop, 4)) = static_cast<int>(tops[iTop].getBestGenTopMatch() != nullptr);
            }

            Py_DECREF(ptt);

            return Py_BuildValue("NN", topArrayFloat, topArrayInt);
        }
        catch(const TTException& e)
        {
            Py_DECREF(ptt);

            std::cout << "SVFit exception message: " << e << std::endl;
            PyErr_SetString(PyExc_RuntimeError, ("SVFit exception thrown. TTException message: " + e.getPrintMessage()).c_str());
            return NULL;
        }

    }

    static PyObject* SVFitInterface_getCandidateSFSyst(PyObject *self, PyObject *args)
    {
        //suppress unused parameter warning as self is manditory
        (void)self;

        PyObject *ptt;
        if (!PyArg_ParseTuple(args, "O!", &PyCapsule_Type, &ptt))
        {
            return NULL;
        }

        Py_INCREF(ptt);

        //Get top tagger pointer from capsule 
        SVFit* tt;
        if (!(tt = (SVFit*) PyCapsule_GetPointer(ptt, "SVFit"))) 
        {
            //Handle exception here 
            Py_DECREF(ptt);

            PyErr_SetString(PyExc_ReferenceError, "SVFit pointer invalid");
            return NULL;
        }

        try
        {
            //Get top tagger results 
            const auto& ttr = tt->getResults();

            //Get tops 
            const auto& tops = ttr.getTopCandidates();
            const npy_intp NTOPS = static_cast<npy_intp>(tops.size());

            //create np array for SF
            npy_intp floatsizearray[] = {NTOPS};
            PyArrayObject* sfArrayFloat = reinterpret_cast<PyArrayObject*>(PyArray_SimpleNew(1, floatsizearray, NPY_FLOAT));

            //create dictionary of np arrays 
            PyObject* pSystList = PyList_New(0);

            //Fill SF and syst data if it is avaliable and tops exist 
            if(NTOPS > 0)
            {
                //fill SF and syst arrays in dictionary 
                for(unsigned int iTop = 0; iTop < tops.size(); ++iTop)
                {
                    //assign scale factor
                    *static_cast<npy_float*>(PyArray_GETPTR1(sfArrayFloat, iTop)) = tops[iTop].getMCScaleFactor();
                
                    //create dictionary to hold systematics 
                    PyObject* pSystDict = PyDict_New();
                    for(const auto& systPair : tops[iTop].getAllSystematicUncertainties())
                    {
                        PyObject* pFloat = PyFloat_FromDouble(systPair.second);
                        PyDict_SetItemString(pSystDict, systPair.first.c_str(), pFloat);
                        Py_DECREF(pFloat);
                    }
                    
                    //put the systematic dictionary for this top into the list
                    PyList_Append(pSystList, pSystDict);
                    Py_DECREF(pSystDict);
                }
            }
            
            Py_DECREF(ptt);
            
            return Py_BuildValue("NN", sfArrayFloat, pSystList);
        }
        catch(const TTException& e)
        {
            Py_DECREF(ptt);

            std::cout << "SVFit exception message: " << e << std::endl;
            PyErr_SetString(PyExc_RuntimeError, ("SVFit exception thrown. TTException message: " + e.getPrintMessage()).c_str());
            return NULL;
        }

    }

    static PyObject* SVFitInterface_test(PyObject *self, PyObject *args)
    {
        //suppress unused parameter warning as self is manditory
        (void)self;

        PyObject *p;
        if (!PyArg_ParseTuple(args, "O", &p))
        {
            return NULL;
        }

        Py_INCREF(p);

        if(TPython::ObjectProxy_Check(p))
        std::cout << Py_TYPE(p)->tp_name << std::endl;

        Py_DECREF(p);

        Py_INCREF(Py_None);
        return Py_None;

    }

    static PyMethodDef SVFitInterfaceMethods[] = {
        {"test",       SVFitInterface_test,            METH_VARARGS,                 "test."},
        {"setup",              SVFitInterface_setup,              METH_VARARGS,                 "Configure Top Tagger."},
        {"run",                (PyCFunction)SVFitInterface_run,   METH_VARARGS | METH_KEYWORDS, "Run Top Tagger."},
        {"getResults",         SVFitInterface_getResults,         METH_VARARGS,                 "Get Top Tagger results."},
        {"getSFSyst",          SVFitInterface_getSFSyst,          METH_VARARGS,                 "Get Top Tagger SF and systematics."},
        {"getCandidates",      SVFitInterface_getCandidates,      METH_VARARGS,                 "Get Top Tagger Candidates."},
        {"getCandidateSFSyst", SVFitInterface_getCandidateSFSyst, METH_VARARGS,                 "Get Top Tagger candidate SF and systematics."},
        {NULL, NULL, 0, NULL}        /* Sentinel */
    };

    PyMODINIT_FUNC
    initSVFitInterface(void)
    {
        (void) Py_InitModule("SVFitInterface", SVFitInterfaceMethods);

        //Setup numpy
        import_array();
    }

}

#endif
