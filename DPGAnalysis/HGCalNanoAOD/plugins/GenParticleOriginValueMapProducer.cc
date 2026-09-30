// GenParticleOriginValueMapProducer.cc
//
// Turns DisplacedParticleGunProducerFlatEtaWithLocalPU's own
// "particleOrigin"/"particleBarcode" companion products (see that
// producer's own comments in its constructor, next to the produces<>()
// calls) into edm::ValueMap<int>s keyed to a reco::Candidate collection
// (in practice, nanoHGCML_cff.py's "genParticles"), so NanoAOD's
// genParticleTable can expose them as branches via the standard
// externalVariables/ExtVar mechanism -- exactly the same mechanism this
// package already uses/deletes for genParticleTable's "iso" variable.
//
// WHY A DIRECT INDEX MATCH IS VALID HERE (not edm::Ptr/Ref-based, which
// would be needed against a PRUNED collection): CMSSW's standard
// GenParticleProducer (which builds the "genParticles" collection
// nanoHGCML_cff.py's genParticleTable.src points to) walks the source
// HepMCProduct's GenEvent particle-by-particle with no pruning, filtering,
// or reordering, converting each HepMC::GenParticle to one
// reco::GenParticle in the SAME order it appears in the GenEvent -- i.e.
// ascending HepMC barcode, exactly the order DisplacedParticleGunProducer
// FlatEtaWithLocalPU builds particleOrigin/particleBarcode in (probe(s)
// first at barcodes [1, NParticles], local PU appended after). So
// genParticles[i] corresponds to particleOrigin[i]/particleBarcode[i] by
// plain index, with no matching logic needed at all -- PROVIDED nothing
// between the gun and this producer drops or reorders particles (no
// pruning step in this sample's chain, since it's a bare gun + local-PU
// sample, not a full hard-process event needing GenParticlePruner). The
// produce() method below checks collection sizes match and throws
// cms::Exception rather than silently mismatching if that assumption
// ever stops holding (e.g. if a pruning step is added to this chain
// later) -- treat that exception as a sign this producer needs a real
// edm::Ptr/barcode-based match instead of a direct index one, not as a
// bug to route around.
//
// CONFIG:
//   genParticleOriginTable = cms.EDProducer("GenParticleOriginValueMapProducer",
//       src             = cms.InputTag("genParticles"),
//       particleOrigin  = cms.InputTag("generator", "particleOrigin"),
//       particleBarcode = cms.InputTag("generator", "particleBarcode"),
//   )
// (see the accompanying genParticleOrigin_cff.py for the canonical
// instance, and nanoHGCML_cff.py.patch for wiring it into genParticleTable).

#include <memory>
#include <vector>

#include "DataFormats/Candidate/interface/Candidate.h"
#include "DataFormats/Common/interface/View.h"
#include "DataFormats/Common/interface/ValueMap.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/Utilities/interface/EDGetToken.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "FWCore/Utilities/interface/InputTag.h"

class GenParticleOriginValueMapProducer : public edm::global::EDProducer<> {
public:
  explicit GenParticleOriginValueMapProducer(const edm::ParameterSet& pset)
      : particlesToken_(consumes<edm::View<reco::Candidate>>(pset.getParameter<edm::InputTag>("src"))),
        originToken_(consumes<std::vector<int>>(pset.getParameter<edm::InputTag>("particleOrigin"))),
        barcodeToken_(consumes<std::vector<int>>(pset.getParameter<edm::InputTag>("particleBarcode"))) {
    produces<edm::ValueMap<int>>("isLocalPU");
    produces<edm::ValueMap<int>>("barcode");
  }
  ~GenParticleOriginValueMapProducer() override = default;

  static void fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    desc.add<edm::InputTag>("src", edm::InputTag("genParticles"))
        ->setComment("The reco::Candidate (in practice reco::GenParticle) collection genParticleTable.src "
                     "also points to -- must be the UNPRUNED, order-preserving collection built directly "
                     "from the gun's own HepMCProduct (see this file's header comment for why).");
    desc.add<edm::InputTag>("particleOrigin", edm::InputTag("generator", "particleOrigin"))
        ->setComment("DisplacedParticleGunProducerFlatEtaWithLocalPU's 'particleOrigin' product: 0=probe, "
                     "1=local PU, index-aligned with 'src' (see header comment).");
    desc.add<edm::InputTag>("particleBarcode", edm::InputTag("generator", "particleBarcode"))
        ->setComment("DisplacedParticleGunProducerFlatEtaWithLocalPU's 'particleBarcode' product, same "
                     "index alignment as particleOrigin.");
    descriptions.add("genParticleOriginValueMap", desc);
  }

private:
  void produce(edm::StreamID, edm::Event& event, const edm::EventSetup&) const override {
    edm::Handle<edm::View<reco::Candidate>> particlesHandle;
    event.getByToken(particlesToken_, particlesHandle);
    const auto& origin = event.get(originToken_);
    const auto& barcode = event.get(barcodeToken_);

    if (origin.size() != particlesHandle->size() || barcode.size() != particlesHandle->size()) {
      throw cms::Exception("GenParticleOriginValueMapProducer")
          << "'src' collection has " << particlesHandle->size() << " particle(s), but 'particleOrigin'/"
             "'particleBarcode' have " << origin.size() << "/" << barcode.size()
          << " -- the 1:1, order-preserving correspondence this producer assumes between the gun's own "
             "HepMC particle list and 'src' does not hold for this sample (a pruning/filtering/reordering "
             "step was likely inserted between the gun and 'src'). This needs a real edm::Ptr- or "
             "barcode-based match instead of the direct-index one implemented here -- see this file's "
             "header comment.";
    }

    auto isLocalPUMap = std::make_unique<edm::ValueMap<int>>();
    {
      edm::ValueMap<int>::Filler filler(*isLocalPUMap);
      filler.insert(particlesHandle, origin.begin(), origin.end());
      filler.fill();
    }
    event.put(std::move(isLocalPUMap), "isLocalPU");

    auto barcodeMap = std::make_unique<edm::ValueMap<int>>();
    {
      edm::ValueMap<int>::Filler filler(*barcodeMap);
      filler.insert(particlesHandle, barcode.begin(), barcode.end());
      filler.fill();
    }
    event.put(std::move(barcodeMap), "barcode");
  }

  const edm::EDGetTokenT<edm::View<reco::Candidate>> particlesToken_;
  const edm::EDGetTokenT<std::vector<int>> originToken_;
  const edm::EDGetTokenT<std::vector<int>> barcodeToken_;
};

DEFINE_FWK_MODULE(GenParticleOriginValueMapProducer);