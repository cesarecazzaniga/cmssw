import FWCore.ParameterSet.Config as cms
from PhysicsTools.NanoAOD.common_cff import P3Vars,Var,ExtVar
from DPGAnalysis.HGCalNanoAOD.hgcRecHits_cff import hgcRecHitsTable
from DPGAnalysis.CaloNanoAOD.simClusters_cff import simClusterTable
from DPGAnalysis.CaloNanoAOD.caloParticles_cff import caloParticleTable
from RecoLocalCalo.HGCalRecProducers.recHitMapProducer_cff import recHitMapProducer
from SimCalorimetry.HGCalSimProducers.hgcHitAssociation_cfi import lcAssocByEnergyScoreProducer, scAssocByEnergyScoreProducer
from SimCalorimetry.HGCalAssociatorProducers.LCToCPAssociation_cfi import layerClusterCaloParticleAssociation
from SimCalorimetry.HGCalAssociatorProducers.LCToSCAssociation_cfi import layerClusterSimClusterAssociation

# NOTE: HGCalLayerClusterProducer computes a per-cluster (time, timeError)
# pair via a robust highest-density estimator over constituent rechit
# times/timeErrors (see RecoLocalCalo/HGCalRecProducers/plugins/
# HGCalLayerClusterProducer.cc), but stores it as a SEPARATE product --
# edm::ValueMap<std::pair<float,float>>, default instance name
# "timeLayerCluster" -- not as a member of the LayerCluster (reco::
# CaloCluster) object itself. It therefore can't be read via an ordinary
# Var("...") string expression the way eta/phi/energy/position are below.
#
# layerClusterTimeSplit below splits that pair-valued ValueMap into two
# plain edm::ValueMap<float> products (time, timeError), which the
# standard NanoAOD externalVariables/ValueMapVariable mechanism DOES
# directly support (confirmed via the FloatExtVar typedef in
# PhysicsTools/NanoAOD/interface/SimpleFlatTableProducer.h) -- avoiding any
# reliance on unverified pair/.first/.second string-expression syntax
# against a composite-typed ValueMap. See PairValueMapSplitterProducer.cc.
#
# IMPORTANT: confirm the actual module instance producing "timeLayerCluster"
# in this workflow before running -- it's the module labeled
# "hgcalMergeLayerClusters" in some workflows and a differently-named
# module (e.g. a per-algo hgcalLayerClusters instance merged downstream) in
# others. Check via:
#   edmDumpEventContent file:step3_RECO_<label>.root | grep -i "timeLayerCluster"
# and update layerClusterTimeSplit.src's module label below if it differs
# from layerClusterTable.src.
layerClusterTimeSplit = cms.EDProducer("PairValueMapSplitterProducer",
    src = cms.InputTag("hgcalMergeLayerClusters", "timeLayerCluster"),
    collection = cms.InputTag("hgcalMergeLayerClusters"),
    firstName = cms.string("time"),
    secondName = cms.string("timeError"),
)

layerClusterTable = cms.EDProducer("SimpleCaloClusterFlatTableProducer",
    src = cms.InputTag("hgcalMergeLayerClusters"),
    cut = cms.string(""),
    name = cms.string("LayerCluster"),
    doc  = cms.string("LayerCluster information"),
    singleton = cms.bool(False), # the number of entries is variable
    extension = cms.bool(False), # this is the main table for the muons
    variables = cms.PSet(
        eta  = Var("eta",  float,precision=12),
        phi = Var("phi", float, precision=12),
        energy = Var("energy", float, precision=14),
        x = Var('position().x()', 'float', precision=14, doc='x position'),
        y = Var('position.y()', 'float', precision=14, doc='y position'),
        z = Var('position.z()', 'float', precision=14, doc='z position'),
        nHits = Var('size', 'int', precision=14, doc='number of rechits'),
        seedDetId = Var('seed().rawId()', 'int', precision=-1, doc='detId of seed hit'),
    ),
    externalVariables = cms.PSet(
        time = ExtVar(cms.InputTag("layerClusterTimeSplit", "time"), float,
                       doc="cluster time from a robust highest-density estimator over constituent "
                           "rechit times (see HGCalLayerClusterProducer::calculateTime); "
                           "-99 if below the estimator's minimum-hit-count threshold"),
        timeError = ExtVar(cms.InputTag("layerClusterTimeSplit", "timeError"), float,
                            doc="uncertainty on LayerCluster_time; -1 if not computed (see LayerCluster_time doc)"),
    ),
)

# -----------------------------------------------------------------------
# Truth-matching, RAW SimCluster (unmerged) granularity.
#
# One SimCluster per SimTrack -- the finest truth granularity. A single
# physical shower that GEANT happened to fragment into several SimTracks
# (e.g. a hard bremsstrahlung photon spawning its own secondary track mid-
# shower) will show up as MULTIPLE distinct SimCluster labels across the
# LayerClusters that belong to that one physical shower, even though a
# human (and MergedSimCluster, by design -- see below) would call it one
# object. bestMatchTable=True below only collapses the *per-LayerCluster*
# many-to-many match array down to its single highest-quality entry -- it
# does NOT address this cross-object fragmentation. See
# layerClusterToMergedSimClusterTable below for the fragmentation-free
# alternative.
# -----------------------------------------------------------------------
layerClusterToSimClusterTable = cms.EDProducer("LayerClusterToSimClusterIndexTableProducer",
    cut = layerClusterTable.cut,
    src = layerClusterTable.src,
    objName = layerClusterTable.name,
    branchName = simClusterTable.name, 
    objMap = cms.InputTag("layerClusterSimClusterAssociation"),
    bestMatchTable = cms.untracked.bool(True),
    docString = cms.string("SimCluster ordered by most sim energy shared with LayerCluster")
)
layerClusterToCaloParticleTable = cms.EDProducer("LayerClusterToCaloParticleIndexTableProducer",
    cut = layerClusterTable.cut,
    src = layerClusterTable.src,
    objName = layerClusterTable.name,
    branchName = caloParticleTable.name,
    objMap = cms.InputTag("layerClusterCaloParticleAssociation"),
    docString = cms.string("Index of CaloParticle matched to LayerCluster")
)
hgcRecHitsToLayerClusters = cms.EDProducer("RecHitToLayerClusterAssociationProducer",
    caloRecHits = cms.VInputTag("hgcRecHits"),
    layerClusters = cms.InputTag("hgcalMergeLayerClusters"),
)
hgcRecHitsToLayerClusterTable = cms.EDProducer("HGCRecHitToLayerClusterIndexTableProducer",
    cut = hgcRecHitsTable.cut,
    src = hgcRecHitsTable.src,
    objName = hgcRecHitsTable.name,
    branchName = cms.string("LayerCluster"),
    objMap = cms.InputTag("hgcRecHitsToLayerClusters:hgcRecHitsToLayerCluster"),
    docString = cms.string("LayerCluster assigned largest RecHit fraction")
)

# -----------------------------------------------------------------------
# Truth-matching, MERGED SimCluster granularity -- fragmentation-free
# alternative to layerClusterToSimClusterTable above, matching the same
# truth definition RecHitHGC already trains against
# (RecHitHGC_MergedSimClusterBestMatchIdx).
#
# hgcSimTruth (SimClusterMerger, defined in
# DPGAnalysis/CaloNanoAOD/python/mergedSimClusters_cff.py, scheduled via
# nanoHGCML_cff.py's customizeMergedSimClusters()) merges raw SimClusters
# that trace back to the same originating particle into one MergedSimCluster,
# and produces a fully SimClusterCollection-type-compatible output --
# confirmed by mergedSimClusterTable = simClusterTable.clone(src="hgcSimTruth")
# working unchanged elsewhere in that file.
#
# layerClusterSimClusterAssociation's underlying scoring algorithm
# (LCToSCAssociatorByEnergyScoreImpl, see
# SimCalorimetry/HGCalAssociatorProducers/plugins/
# LCToSCAssociatorByEnergyScoreImpl.cc) takes its SimCluster collection as
# a plain edm::Handle<SimClusterCollection> function argument -- nothing
# about the scoring algorithm itself is hardcoded to raw SimCluster/
# mix:MergedCaloTruth. So this is a pure config clone-and-repoint of the
# OUTER association producer (label_scl), reusing the SAME scoring
# algorithm instance (scAssocByEnergyScoreProducer) unchanged -- no new
# C++, mirroring exactly how hgcRecHitsToMergedSimClusters was built from
# hgcRecHitsToSimClusters in mergedSimClusters_cff.py.
#
# CONDITIONAL: only meaningful if hgcSimTruth is actually scheduled (i.e.
# customizeMergedSimClusters() has been applied to this process) -- kept
# in its own Sequence (layerClusterMergedSimClusterTables) below, NOT added to
# the unconditional layerClusterTables Task, and must be inserted into the
# process by customizeMergedSimClusters() itself (see nanoHGCML_cff.py),
# exactly matching how RecHitHGC's own Merged association
# (hgcRecHitsToMergedSimClusters/hgcRecHitsToMergedSimClusterTable) is
# conditionally bundled inside mergedSimClusterTables rather than the
# unconditional hgcRecHitSimAssociationTask. Adding it to the unconditional
# Task would break any workflow that does NOT apply
# customizeMergedSimClusters() (missing hgcSimTruth product at runtime).
# -----------------------------------------------------------------------
layerClusterMergedSimClusterAssociation = layerClusterSimClusterAssociation.clone(
    label_scl = cms.InputTag("hgcSimTruth")
    # associator (the actual scoring algorithm) intentionally left
    # unchanged -- see docstring above for why no separate clone of
    # scAssocByEnergyScoreProducer is needed.
)
layerClusterToMergedSimClusterTable = cms.EDProducer("LayerClusterToSimClusterIndexTableProducer",
    cut = layerClusterTable.cut,
    src = layerClusterTable.src,
    objName = layerClusterTable.name,
    branchName = cms.string("MergedSimCluster"),
    objMap = cms.InputTag("layerClusterMergedSimClusterAssociation"),
    bestMatchTable = cms.untracked.bool(True),
    docString = cms.string("MergedSimCluster ordered by most sim energy shared with LayerCluster")
)
layerClusterMergedSimClusterTables = cms.Sequence(
    layerClusterMergedSimClusterAssociation+
    layerClusterToMergedSimClusterTable
)

layerClusterTables = cms.Task(layerClusterTimeSplit,
    layerClusterTable,
    recHitMapProducer,
    lcAssocByEnergyScoreProducer,
    scAssocByEnergyScoreProducer,
    layerClusterCaloParticleAssociation,
    layerClusterSimClusterAssociation,
    layerClusterToSimClusterTable,
    layerClusterToCaloParticleTable,
    hgcRecHitsToLayerClusters,
    hgcRecHitsToLayerClusterTable)