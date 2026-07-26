from __future__ import print_function
import FWCore.ParameterSet.Config as cms
from PhysicsTools.NanoAOD.common_cff import *
from PhysicsTools.NanoAOD.genparticles_cff import genParticleTable
from PhysicsTools.NanoAOD.genVertex_cff import *
from DPGAnalysis.HGCalNanoAOD.hgcSimHits_cff import *
from DPGAnalysis.HGCalNanoAOD.hgcSimTracks_cff import *
from DPGAnalysis.HGCalNanoAOD.hgcRecHits_cff import *
from DPGAnalysis.HGCalNanoAOD.hgcRecHitSimAssociations_cff import *
from DPGAnalysis.CaloNanoAOD.simClusters_cff import *
from DPGAnalysis.CaloNanoAOD.mergedSimClusters_cff import *
from DPGAnalysis.CaloNanoAOD.caloParticles_cff import *
from DPGAnalysis.HGCalNanoAOD.layerClusters_cff import *
from DPGAnalysis.TrackNanoAOD.trackSimHits_cff import *
from DPGAnalysis.TrackNanoAOD.trackingParticles_cff import *
from DPGAnalysis.TrackNanoAOD.tracks_cff import *
from DPGAnalysis.PFNanoAOD.pfCands_cff import *
from DPGAnalysis.PFNanoAOD.pfTruth_cff import *

nanoMetadata = cms.EDProducer("UniqueStringProducer",
    strings = cms.PSet(
        tag = cms.string("untagged"),
    )
)

genParticleTable.src = "genParticles"
genParticleTable.variables = cms.PSet(genParticleTable.variables,
    charge = CandVars.charge,
    # Production vertex, needed to compute true vertex displacement for
    # displaced-photon-gun studies. reco::GenParticle inherits vx()/vy()/vz()
    # from reco::Candidate; DisplacedParticleGunProducer genuinely places
    # the HepMC production vertex at the sampled Origin plane (Z fixed at 0
    # by that producer, R sampled per-particle), so this should faithfully
    # recover the real displaced position -- not just repeat some default.
    vx = Var("vx", float, precision=10, doc="production vertex x (cm)"),
    vy = Var("vy", float, precision=10, doc="production vertex y (cm)"),
    vz = Var("vz", float, precision=10, doc="production vertex z (cm)")
    )
# genIso (externalVariables.iso) requires packedGenParticles, which only
# exists after a full MiniAOD packing step. nanoHGCMLSequence runs directly
# off RECO-tier genParticles and never produces packedGenParticles, so this
# variable cannot be filled here. It is also not physically meaningful for a
# single/few-particle gun sample (isolation is a sum over nearby particles
# in a cone, and there's essentially nothing else nearby in these samples).
del genParticleTable.externalVariables.iso

nanoHGCMLSequence = cms.Sequence(nanoMetadata+
                                 hgcRecHits+ #so that modules can use them
    genVertexTable+genVertexT0Table+genParticleTable+
    cms.Sequence(layerClusterTables)+
    simTrackTables+
    hgcSimHitsSequence+
    # TODO: Fix producer and allow adding via configuration
    #trackerSimHitTables+
    simClusterTables+
    trackingParticleTables+
    caloParticleTables
)

#if we turn off the custom cms-pepr functions we need to run this instead
#nanoHGCMLSequence = cms.Sequence(nanoMetadata+
#                                 hgcRecHits+hgcRecHitsTable+hgcRecHitsPositionTable+ # (tried adding tables manually same as gentables) Extension table second #so that modules can use them
#    genVertexTable+genVertexT0Table+genParticleTable+
#    cms.Sequence(layerClusterTables)+
#    simTrackTable+simTrackToSimCluster+simTrackToSimClusterTable+ # simTrackTables+
#    hgcSimHitsSequence+
    # TODO: Fix producer and allow adding via configuration
    #trackerHitsPixee+muonCSCHitsTable+muonCSCHitsPositionTable+#trackerSimHitTables+
#    simClusterTables+    
#    generalTrackTable+trackConversionsTable+trackDisplacedTable+
#    caloParticleTables
#)

#nanoHGCMLRecoSequence = cms.Sequence(
#    nanoHGCMLSequence+
#	cms.Sequence(hgcRecHitsTask)+
#    cms.Sequence(hgcRecHitSimAssociationTask)+
#    cms.Sequence(trackTables)+
#    trackingParticleTables+
#    mergedSimClusterTables+
#    pfCandTable+pfTruth+
#    pfTICLCandTable
#)
#def customizeReco(process):
#    process.nanoHGCMLSequence = nanoHGCMLRecoSequence
#    return process

#Added by Claude: redefine customizeReco process to avoid recursion error
def customizeReco(process):
    # Create a new sequence that contains everything needed
    process.nanoHGCMLRecoSequence = cms.Sequence(
        process.nanoHGCMLSequence+  # Keep the original sequence
        cms.Sequence(process.hgcRecHitsTask)+
        cms.Sequence(process.hgcRecHitSimAssociationTask)+
        cms.Sequence(process.trackTables)+
        process.trackingParticleTables+
        process.mergedSimClusterTables+
        process.pfCandTable+process.pfTruth+
        process.pfTICLCandTable
    )
    # Replace the sequence in the path
    for path in process.paths.values():
        if hasattr(path, "replace"):
            path.replace(process.nanoHGCMLSequence, process.nanoHGCMLRecoSequence)
    
    return process


def customizeNoMergedCaloTruth(process):
    process.nanoHGCMLSequence.remove(simClusterTable)
    process.nanoHGCMLSequence.remove(simClusterToCaloPartTable)
    process.nanoHGCMLSequence.remove(hgcEEHitsToSimClusterTable)
    process.nanoHGCMLSequence.remove(hgcHEfrontHitsToSimClusterTable)
    process.nanoHGCMLSequence.remove(hgcHEbackHitsToSimClusterTable)
    process.nanoHGCMLSequence.remove(simTrackToSimClusterTable)

    process.nanoHGCMLSequence.remove(caloParticleTable)
    return process

def customizeMergedSimClusters(process):
    process.nanoHGCMLSequence.insert(-2, mergedSimClusterTables)
    #process.nanoHGCMLSequence.insert(-1, caloParticleMergedTables)
    process.nanoHGCMLSequence.insert(-1, caloParticleTables)
    return process