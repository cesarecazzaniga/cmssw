// PairValueMapSplitterProducer
//
// Splits an edm::ValueMap<std::pair<float,float>> (e.g. HGCalLayerCluster
// Producer's "timeLayerCluster", holding (time, timeError) per layer
// cluster) into two separate, plain edm::ValueMap<float> products.
//
// Motivation: NanoAOD's SimpleFlatTableProducer::externalVariables
// mechanism (ValueMapVariable, see PhysicsTools/NanoAOD/interface/
// SimpleFlatTableProducer.h) has a confirmed, directly-supported path for
// plain scalar-valued ValueMaps (float/double/int/bool -- see the
// FloatExtVar/DoubleExtVar/... typedefs there), evaluated via a simple
// string-expression per entry. There is no existing precedent anywhere in
// this checkout for reading a std::pair-valued ValueMap directly through
// that same mechanism, and guessing at unverified template/string-
// expression syntax for a composite type risks a config that silently
// does the wrong thing or simply fails to compile. Splitting into two
// ordinary ValueMap<float> products up front sidesteps that uncertainty
// entirely -- both outputs use the exact same well-supported scalar path
// as every other externalVariable in this ntuple (e.g. genParticleTable's
// former externalVariables.iso).

#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"

#include "DataFormats/Common/interface/ValueMap.h"
#include "DataFormats/Common/interface/View.h"
#include "DataFormats/CaloRecHit/interface/CaloCluster.h"

#include <utility>
#include <vector>

class PairValueMapSplitterProducer : public edm::global::EDProducer<> {
public:
  explicit PairValueMapSplitterProducer(const edm::ParameterSet& params)
      : srcToken_(consumes<edm::ValueMap<std::pair<float, float>>>(params.getParameter<edm::InputTag>("src"))),
        collectionToken_(
            consumes<edm::View<reco::CaloCluster>>(params.getParameter<edm::InputTag>("collection"))),
        firstName_(params.getParameter<std::string>("firstName")),
        secondName_(params.getParameter<std::string>("secondName")) {
    produces<edm::ValueMap<float>>(firstName_);
    produces<edm::ValueMap<float>>(secondName_);
  }

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("src")->setComment(
        "edm::ValueMap<std::pair<float,float>> input, e.g. "
        "(module, \"timeLayerCluster\")");
    desc.add<edm::InputTag>("collection")->setComment(
        "The same collection the input ValueMap is keyed against "
        "(e.g. hgcalMergeLayerClusters) -- needed to build the output "
        "ValueMaps' RefProd.");
    desc.add<std::string>("firstName", "first")->setComment("instance name for the .first-derived output ValueMap");
    desc.add<std::string>("secondName", "second")
        ->setComment("instance name for the .second-derived output ValueMap");
    descriptions.add("pairValueMapSplitter", desc);
  }

  void produce(edm::StreamID, edm::Event& event, const edm::EventSetup&) const override {
    edm::Handle<edm::ValueMap<std::pair<float, float>>> srcHandle;
    event.getByToken(srcToken_, srcHandle);

    edm::Handle<edm::View<reco::CaloCluster>> collectionHandle;
    event.getByToken(collectionToken_, collectionHandle);

    std::vector<float> firstValues, secondValues;
    firstValues.reserve(collectionHandle->size());
    secondValues.reserve(collectionHandle->size());

    for (size_t i = 0; i < collectionHandle->size(); ++i) {
      edm::Ptr<reco::CaloCluster> ptr = collectionHandle->ptrAt(i);
      if (srcHandle.isValid() && srcHandle->contains(ptr.id())) {
        const auto& pair = (*srcHandle)[ptr];
        firstValues.push_back(pair.first);
        secondValues.push_back(pair.second);
      } else {
        // Missing entry (e.g. cluster below the timing estimator's minimum
        // hit-count threshold, hitsTime_ in HGCalLayerClusterProducer) --
        // fall back to a clearly-invalid sentinel rather than skipping the
        // row, so the output ValueMap stays aligned index-for-index with
        // the LayerCluster collection/table.
        firstValues.push_back(-99.f);
        secondValues.push_back(-1.f);
      }
    }

    auto firstMap = std::make_unique<edm::ValueMap<float>>();
    edm::ValueMap<float>::Filler firstFiller(*firstMap);
    firstFiller.insert(collectionHandle, firstValues.begin(), firstValues.end());
    firstFiller.fill();
    event.put(std::move(firstMap), firstName_);

    auto secondMap = std::make_unique<edm::ValueMap<float>>();
    edm::ValueMap<float>::Filler secondFiller(*secondMap);
    secondFiller.insert(collectionHandle, secondValues.begin(), secondValues.end());
    secondFiller.fill();
    event.put(std::move(secondMap), secondName_);
  }

private:
  const edm::EDGetTokenT<edm::ValueMap<std::pair<float, float>>> srcToken_;
  const edm::EDGetTokenT<edm::View<reco::CaloCluster>> collectionToken_;
  const std::string firstName_;
  const std::string secondName_;
};

DEFINE_FWK_MODULE(PairValueMapSplitterProducer);