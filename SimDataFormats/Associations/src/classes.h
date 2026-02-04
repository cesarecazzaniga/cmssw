#include "DataFormats/Common/interface/Wrapper.h"

// Add includes for your classes here
#include "SimDataFormats/Associations/interface/MuonToTrackingParticleAssociator.h"
#include "SimDataFormats/Associations/interface/TrackToGenParticleAssociator.h"
#include "SimDataFormats/Associations/interface/TrackToTrackingParticleAssociator.h"
#include "SimDataFormats/Associations/interface/VertexAssociation.h"
#include "SimDataFormats/Associations/interface/VertexToTrackingVertexAssociator.h"
#include "SimDataFormats/Associations/interface/LayerClusterToCaloParticleAssociator.h"
#include "SimDataFormats/Associations/interface/LayerClusterToSimClusterAssociator.h"
#include "SimDataFormats/Associations/interface/TrackAssociation.h"
#include "SimDataFormats/Associations/interface/TracksterToSimClusterAssociator.h"
#include "SimDataFormats/Associations/interface/MultiClusterToCaloParticleAssociator.h"
#include "SimDataFormats/Associations/interface/TracksterToSimTracksterAssociator.h"
#include "SimDataFormats/Associations/interface/TracksterToSimTracksterHitLCAssociator.h"
#include "SimDataFormats/Associations/interface/TTTrackTruthPair.h"
#include "SimDataFormats/Associations/interface/LayerClusterToSimTracksterAssociator.h"
#include "SimDataFormats/Associations/interface/MtdRecoClusterToSimLayerClusterAssociationMap.h"
#include "SimDataFormats/Associations/interface/MtdSimLayerClusterToRecoClusterAssociationMap.h"
#include "SimDataFormats/Associations/interface/MtdRecoClusterToSimLayerClusterAssociator.h"
#include "SimDataFormats/Associations/interface/MtdSimLayerClusterToTPAssociator.h"
#include "SimDataFormats/Associations/interface/TTTypes.h"
#include "SimDataFormats/TrackingAnalysis/interface/TrackingParticle.h"
#include "SimDataFormats/TrackingAnalysis/interface/TrackingParticleFwd.h"
#include "SimDataFormats/TrackingHit/interface/PSimHitContainer.h"
#include "SimDataFormats/Associations/interface/TICLAssociationMap.h"
#include "DataFormats/Common/interface/RefProd.h"
#include "DataFormats/Common/interface/Ref.h"

//added
#include "DataFormats/HGCRecHit/interface/HGCRecHit.h"
#include "DataFormats/HGCRecHit/interface/HGCRecHitCollections.h"
#include "DataFormats/CaloRecHit/interface/CaloCluster.h"
#include "SimDataFormats/PFAnalysis/interface/PFTruthParticle.h"

#include "DataFormats/Common/interface/Association.h"
#include "DataFormats/Common/interface/AssociationMap.h"
#include "DataFormats/Common/interface/AssociationMapHelpers.h"
#include "DataFormats/Common/interface/Handle.h"
#include "DataFormats/Common/interface/OneToManyWithQuality.h"
#include "DataFormats/Common/interface/OneToManyWithQualityGeneric.h"
#include "DataFormats/Common/interface/View.h"

#include "SimDataFormats/CaloAnalysis/interface/SimCluster.h"
#include "DataFormats/Common/interface/Wrapper.h"
#include <atomic>
#include <vector>
#include <utility>
#include <functional>

#include "DataFormats/Common/interface/RefToBase.h"

namespace SimDataFormats_Associations {
  struct SimDataFormats_Associations {
    // add 'dummy' Wrapper variable for each class type you put into the Event
    edm::Wrapper<reco::TrackToTrackingParticleAssociator> dummy1;
    edm::Wrapper<reco::TrackToGenParticleAssociator> dummy2;
    edm::Wrapper<reco::MuonToTrackingParticleAssociator> dummy3;

    edm::Wrapper<reco::VertexToTrackingVertexAssociator> dummy4;

    edm::Wrapper<ticl::LayerClusterToCaloParticleAssociator> dummy5;

    edm::Wrapper<ticl::LayerClusterToSimClusterAssociator> dummy6;

    edm::Wrapper<ticl::TracksterToSimClusterAssociator> dummy7;

    edm::Wrapper<hgcal::MultiClusterToCaloParticleAssociator> dummy8;

    edm::Wrapper<ticl::TracksterToSimTracksterAssociator> dummy9;

    edm::Wrapper<ticl::LayerClusterToSimTracksterAssociator> dummy10;

    reco::VertexSimToRecoCollection vstrc;
    reco::VertexSimToRecoCollection::const_iterator vstrci;
    edm::Wrapper<reco::VertexSimToRecoCollection> wvstrc;

    reco::VertexRecoToSimCollection vrtsc;
    reco::VertexRecoToSimCollection::const_iterator vrtsci;
    edm::Wrapper<reco::VertexRecoToSimCollection> wvrtsci;
  };
}  // namespace SimDataFormats_Associations
//added
