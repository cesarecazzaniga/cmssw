// RecoPFJetConstituentTableProducer.cc
//
// Registers a reco-level (non-PAT) instantiation of JetConstituentTableProducer
// for reco::PFJet jets built from a flat reco::PFCandidate collection, such as
// scoutingPFJetRecluster / scoutingFatPFJetRecluster (PhysicsTools/NanoAOD/python/run3scouting_cff.py),
// which are produced directly from scoutingPFCandidate via ak4PFJets.clone(...).
//
// Registered plugin name: "RecoPFJetConstituentTableProducer"
// (deliberately distinct from the existing PAT-level "PatJetConstituentTableProducer"
// to avoid any naming collision or confusion with that plugin's behavior).

#include "PhysicsTools/NanoAOD/plugins/JetConstituentTableProducer.h"

#include "DataFormats/JetReco/interface/PFJet.h"
#include "DataFormats/ParticleFlowCandidate/interface/PFCandidate.h"

typedef JetConstituentTableProducer<reco::PFJet, reco::PFCandidate> RecoPFJetConstituentTableProducer;

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(RecoPFJetConstituentTableProducer);

