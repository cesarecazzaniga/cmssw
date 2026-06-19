#ifndef PhysicsTools_NanoAOD_plugins_JetConstituentTableProducer_h
#define PhysicsTools_NanoAOD_plugins_JetConstituentTableProducer_h

// JetConstituentTableProducer
//
// Templated EDProducer that builds a flat, paired (jet index, candidate index)
// nanoaod::FlatTable describing which entries of a flat "candidates" collection
// are constituents ("daughters") of each jet in a "jets" collection.
//
// This is a from-scratch implementation written for reco::Jet-derived jets
// (e.g. reco::PFJet) and reco::Candidate-derived candidates (e.g. reco::PFCandidate),
// modeled after the general "constituent index table" idea used elsewhere in
// PhysicsTools/NanoAOD for PAT objects (PatJetConstituentTableProducer), but it
// is NOT a verified line-for-line port of that file -- it has been written
// independently against the standard EDM/Candidate APIs and should be reviewed
// against PatJetConstituentTableProducer.cc before merging, in case there are
// behavioral details (e.g. handling of SVs, sorting conventions) worth aligning.
//
// For each jet, the daughters reported by jet.numberOfDaughters()/daughterPtr(i)
// are resolved against the "candidates" collection passed in via the "candidates"
// InputTag using edm::Ptr::key(), i.e. the position of that candidate in the
// *source* collection that was given to "candidates". This requires that the
// jet's constituents are genuinely edm::Ptr/RefToBase-resolvable elements of
// that same candidate collection (true for jets built directly via
// VirtualJetProducer/FastjetJetProducer from a single flat input collection,
// which is the case for scoutingPFJetRecluster / scoutingFatPFJetRecluster
// built from scoutingPFCandidate).

#include <memory>
#include <vector>
#include <string>

#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/EDGetToken.h"
#include "FWCore/Utilities/interface/InputTag.h"
#include "FWCore/Utilities/interface/StreamID.h"
#include "FWCore/Utilities/interface/Exception.h"

#include "DataFormats/Common/interface/View.h"
#include "DataFormats/Candidate/interface/Candidate.h"
#include "DataFormats/NanoAOD/interface/FlatTable.h"

template <typename JetT, typename CandT>
class JetConstituentTableProducer : public edm::global::EDProducer<> {
public:
  using CandCollection = std::vector<CandT>;

  explicit JetConstituentTableProducer(edm::ParameterSet const& params)
      : name_(params.getParameter<std::string>("name")),
        idx_name_(params.getParameter<std::string>("idx_name")),
        doc_(params.getParameter<std::string>("doc")),
        jets_token_(consumes<edm::View<JetT>>(params.getParameter<edm::InputTag>("jets"))),
        candidates_token_(consumes<CandCollection>(params.getParameter<edm::InputTag>("candidates"))) {
    produces<nanoaod::FlatTable>();
  }

  ~JetConstituentTableProducer() override {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("jets")->setComment("input jet collection");
    desc.add<edm::InputTag>("candidates")
        ->setComment("input candidate collection that the jets were clustered from (or a superset of it)");
    desc.add<std::string>("name")->setComment("name of the output nanoaod::FlatTable, e.g. 'FatJetPFCands'");
    desc.add<std::string>("idx_name")->setComment("name of the candidate index branch, e.g. 'pFCandsIdx'");
    desc.add<std::string>("doc")->setComment("documentation string for the output table");
    descriptions.addWithDefaultLabel(desc);
  }

  void produce(edm::StreamID, edm::Event& iEvent, edm::EventSetup const&) const override {
    edm::Handle<edm::View<JetT>> jets;
    iEvent.getByToken(jets_token_, jets);

    edm::Handle<CandCollection> candidates;
    iEvent.getByToken(candidates_token_, candidates);
    // ProductID of the concrete "candidates" collection in the event; used
    // below to verify that a jet daughter actually belongs to it before
    // trusting its key() as a row index into "candidates".
    edm::ProductID const candidatesId = candidates.id();

    std::vector<int> jetIdx;
    std::vector<int> candIdx;

    for (size_t ij = 0; ij < jets->size(); ++ij) {
      const auto& jet = jets->at(ij);
      for (unsigned int id = 0; id < jet.numberOfDaughters(); ++id) {
        reco::CandidatePtr dau = jet.daughterPtr(id);
        if (dau.isNull())
          continue;
        // only keep constituents that actually resolve into the candidates
        // collection that was passed in; this guards against e.g. soft-drop
        // / groomed jet collections sharing some but not all daughters with
        // the nominal candidate collection. If the jet's daughters are not
        // edm::Ptr-comparable to "candidates" (e.g. a genuinely different
        // upstream collection), every entry will be silently skipped here --
        // check the produced table is non-empty when wiring this up.
        if (dau.id() != candidatesId)
          continue;
        jetIdx.push_back(static_cast<int>(ij));
        candIdx.push_back(static_cast<int>(dau.key()));
      }
    }

    auto table = std::make_unique<nanoaod::FlatTable>(jetIdx.size(), name_, false, false);
    table->setDoc(doc_);
    table->template addColumn<int>("jetIdx", jetIdx, "index of the jet in the jet collection");
    table->template addColumn<int>(idx_name_, candIdx, "index of the candidate in the source candidate collection");
    iEvent.put(std::move(table));
  }

private:
  const std::string name_, idx_name_, doc_;
  const edm::EDGetTokenT<edm::View<JetT>> jets_token_;
  const edm::EDGetTokenT<CandCollection> candidates_token_;
};

#endif