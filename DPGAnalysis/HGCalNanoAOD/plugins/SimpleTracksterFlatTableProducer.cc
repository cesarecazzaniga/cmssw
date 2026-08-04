// SimpleTracksterFlatTableProducer
//
// One-line instantiation of the generic PhysicsTools/NanoAOD
// SimpleFlatTableProducer<T> template for ticl::Trackster -- exactly the
// same pattern already used elsewhere in this codebase for plain,
// dictionary-having classes (see e.g. SimplePFTruthParticleFlatTableProducer
// in DPGAnalysis/PFNanoAOD). No bespoke plugin needed: ticl::Trackster is
// a plain std::vector<Trackster>-collection type (see DataFormats/
// HGCalReco/interface/Trackster.h's `typedef std::vector<Trackster>
// TracksterCollection;`), and its accessors (barycenter(), raw_energy(),
// etc.) are ordinary public member functions reachable via the same
// Var("...") string-expression mechanism already used for LayerCluster
// (e.g. Var('position().x()', ...)) -- ticl::Trackster does not need to
// inherit from reco::Candidate/reco::CaloCluster for this to work, any
// class with a ROOT dictionary (required for any EDM-persisted type
// anyway) is usable this way.

#include "PhysicsTools/NanoAOD/interface/SimpleFlatTableProducer.h"
#include "DataFormats/HGCalReco/interface/Trackster.h"

typedef SimpleFlatTableProducer<ticl::Trackster> SimpleTracksterFlatTableProducer;

#include "FWCore/Framework/interface/MakerMacros.h"
DEFINE_FWK_MODULE(SimpleTracksterFlatTableProducer);