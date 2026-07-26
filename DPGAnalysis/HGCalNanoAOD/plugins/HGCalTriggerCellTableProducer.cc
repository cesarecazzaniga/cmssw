// HGCalTriggerCellTableProducer
//
// Produces a nanoAOD FlatTable from a BXVector<l1t::HGCalTriggerCell>
// collection (HGCAL L1 trigger primitives), selecting BX=0 only.
//
// BX=0 is the correct (and only needed) index regardless of sample type
// or pileup level -- NOT a gun-sample-specific simplification. BXVector's
// bunch-crossing axis is unrelated to in-time pileup: CMS's convention
// fixes the triggered/central crossing at index 0 always, and any in-time
// PU (same bunch crossing as the hard scatter, at any PU level) is
// already fully contained within BX=0's own trigger cells. BX=+-1, +-2,
// etc. hold trigger primitives from ADJACENT 25ns bunch crossings
// entirely (a different physical effect -- L1 pipeline/latency and
// detector-integration-time leakage between crossings), relevant only if
// specifically studying the trigger's own out-of-time behavior, not for
// reconstructing the event itself. If a future sample's Mixing config
// includes genuine out-of-time PU (neighboring-crossing energy leakage),
// that leakage is already reflected inside BX=0's own trigger cells --
// no need to also read BX!=0 for that.
//
// Unlike HGCALHitPositionTableProducer, this needs NO separate geometry
// service lookup: l1t::HGCalTriggerCell already carries its own
// GlobalPoint position() directly (set upstream by the L1 trigger
// emulation), and inherits pt()/eta()/phi()/energy() from L1Candidate ->
// reco::LeafCandidate, the same standard interface used throughout
// NanoAOD.
//
// Default configuration targets the Concentrator-stage output
// (l1tHGCalConcentratorProducer:HGCalConcentratorProcessorSelection) --
// the post-selection/compression stage that downstream L1 clustering
// actually consumes, i.e. "what the trigger actually saw" -- rather than
// the earlier, rawer VFE stage. Override src in cfi/cff config if the VFE
// stage (l1tHGCalVFEProducer:HGCalVFEProcessorSums) is wanted instead.

#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/InputTag.h"

#include "DataFormats/L1THGCal/interface/HGCalTriggerCell.h"
#include "DataFormats/NanoAOD/interface/FlatTable.h"

#include <vector>
#include <string>
#include <memory>

class HGCalTriggerCellTableProducer : public edm::stream::EDProducer<> {
public:
  explicit HGCalTriggerCellTableProducer(edm::ParameterSet const& params)
      : name_(params.getParameter<std::string>("name")),
        doc_(params.getParameter<std::string>("doc")),
        token_(consumes<l1t::HGCalTriggerCellBxCollection>(params.getParameter<edm::InputTag>("src"))) {
    produces<nanoaod::FlatTable>();
  }

  ~HGCalTriggerCellTableProducer() override {}

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("src", edm::InputTag("l1tHGCalConcentratorProducer", "HGCalConcentratorProcessorSelection"))
        ->setComment("BXVector<l1t::HGCalTriggerCell> input collection");
    desc.add<std::string>("name", "TriggerCell")->setComment("branch name stem in the output tree");
    desc.add<std::string>("doc", "HGCAL L1 trigger cell (BX=0 only)")->setComment("table documentation string");
    descriptions.add("hgcalTriggerCellTable", desc);
  }

  void produce(edm::Event& iEvent, edm::EventSetup const&) override {
    edm::Handle<l1t::HGCalTriggerCellBxCollection> handle;
    iEvent.getByToken(token_, handle);

    // BX=0 is the correct index regardless of PU level -- see file header
    // note for why this isn't a gun-sample-specific simplification.
    unsigned int n = handle.isValid() ? handle->size(0) : 0;

    std::vector<float> pt(n), eta(n), phi(n), energy(n), mipPt(n);
    std::vector<float> x(n), y(n), z(n);
    std::vector<uint32_t> detId(n);
    std::vector<int32_t> subdetId(n);
    std::vector<uint32_t> uncompressedCharge(n), compressedCharge(n);
    std::vector<int32_t> qual(n);

    if (handle.isValid()) {
      unsigned int i = 0;
      for (auto it = handle->begin(0); it != handle->end(0); ++it, ++i) {
        pt[i] = it->pt();
        eta[i] = it->eta();
        phi[i] = it->phi();
        energy[i] = it->energy();
        mipPt[i] = it->mipPt();
        const auto& pos = it->position();
        x[i] = pos.x();
        y[i] = pos.y();
        z[i] = pos.z();
        detId[i] = it->detId();
        subdetId[i] = it->subdetId();
        uncompressedCharge[i] = it->uncompressedCharge();
        compressedCharge[i] = it->compressedCharge();
        qual[i] = it->hwQual();
      }
    }

    auto table = std::make_unique<nanoaod::FlatTable>(n, name_, /*singleton=*/false, /*extension=*/false);
    table->setDoc(doc_);
    table->addColumn<float>("pt", pt, "transverse momentum (GeV)");
    table->addColumn<float>("eta", eta, "pseudorapidity");
    table->addColumn<float>("phi", phi, "azimuthal angle (rad)");
    table->addColumn<float>("energy", energy, "energy (GeV)");
    table->addColumn<float>("mipPt", mipPt, "MIP-equivalent transverse momentum");
    table->addColumn<float>("x", x, "position x (cm)");
    table->addColumn<float>("y", y, "position y (cm)");
    table->addColumn<float>("z", z, "position z (cm)");
    table->addColumn<uint32_t>("detId", detId, "raw DetId");
    table->addColumn<int32_t>("subdetId", subdetId, "subdetector id");
    table->addColumn<uint32_t>("uncompressedCharge", uncompressedCharge, "raw uncompressed charge");
    table->addColumn<uint32_t>("compressedCharge", compressedCharge, "compressed charge");
    table->addColumn<int32_t>("qual", qual, "hardware quality word (hwQual)");

    iEvent.put(std::move(table));
  }

private:
  const std::string name_;
  const std::string doc_;
  const edm::EDGetTokenT<l1t::HGCalTriggerCellBxCollection> token_;
};

DEFINE_FWK_MODULE(HGCalTriggerCellTableProducer);