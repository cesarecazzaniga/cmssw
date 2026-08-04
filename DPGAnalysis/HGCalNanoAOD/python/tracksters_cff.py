import FWCore.ParameterSet.Config as cms
from PhysicsTools.NanoAOD.common_cff import Var

# TICL Trackster table -- primarily to expose barycenter (trackster
# position, see DataFormats/HGCalReco/interface/Trackster.h and
# RecoHGCal/TICL/plugins/TrackstersPCA.cc's assignPCAtoTracksters(), which
# computes it as an energy-weighted centroid of the trackster's
# constituent LayerCluster positions -- NOT recomputed here, just read
# directly off the already-persisted RECO-tier Trackster object).
#
# Confirmed empirically (see the standard-vs-ticl_v5 comparison) that
# ticlTrackstersCLUE3DHigh exists with genuine, matching content in BOTH
# the standard and ticl_v5 pipelines -- not a v5-exclusive product -- so
# this table is added to the main, unconditional nanoHGCMLSequence in
# nanoHGCML_cff.py, same as LayerCluster/TriggerCell, not gated behind a
# customize function the way the MergedSimCluster-dependent additions are.
#
# Kept deliberately focused on position + core kinematic/energy fields,
# not a full trackster dump -- ticl::Trackster also exposes per-particle-
# type PID probabilities (id_probabilities(int index), 8 ParticleType
# values -- see the enum in Trackster.h) and seedIndex()/trackIdx(), all
# easy to add later the same way (Var('id_probabilities(0)', 'float', ...)
# etc.) if wanted.
tracksterTable = cms.EDProducer("SimpleTracksterFlatTableProducer",
    src = cms.InputTag("ticlTrackstersCLUE3DHigh"),
    cut = cms.string(""),
    name = cms.string("Trackster"),
    doc  = cms.string("TICL CLUE3DHigh trackster information"),
    singleton = cms.bool(False),  # variable number of tracksters per event
    extension = cms.bool(False),  # this is the main table, not an extension
    variables = cms.PSet(
        # barycenter -- the trackster POSITION (see module docstring above)
        x = Var('barycenter().x()', 'float', precision=14, doc='barycenter x position (cm)'),
        y = Var('barycenter().y()', 'float', precision=14, doc='barycenter y position (cm)'),
        z = Var('barycenter().z()', 'float', precision=14, doc='barycenter z position (cm)'),
        eta = Var('barycenter().eta()', 'float', precision=12, doc='barycenter eta'),
        phi = Var('barycenter().phi()', 'float', precision=12, doc='barycenter phi'),

        raw_energy = Var('raw_energy()', 'float', precision=14,
                          doc='sum of constituent LayerCluster energies (energy-weighting numerator for barycenter)'),
        raw_em_energy = Var('raw_em_energy()', 'float', precision=14,
                             doc='raw_energy restricted to the EM section (|z| <= z_limit_em)'),
        regressed_energy = Var('regressed_energy()', 'float', precision=14,
                                doc='network-regressed energy; 0 if regression disabled for this iteration (see CLUE3DHighStep_cff.py: doRegression=0 for both standard and ticl_v5)'),
        raw_pt = Var('raw_pt()', 'float', precision=14, doc='raw_energy / cosh(barycenter eta)'),

        time = Var('time()', 'float', precision=10, doc='trackster time; -99/-1-type sentinels if not computed, see Trackster.h'),
        timeError = Var('timeError()', 'float', precision=10, doc='uncertainty on time; -1 if not available'),

        nLayerCluster = Var('vertices().size()', 'int', precision=-1,
                             doc='number of constituent LayerClusters (DAG vertices) in this trackster'),
        ticlIteration = Var('ticlIteration()', 'int', precision=-1,
                             doc='TICL iteration index that produced this trackster (see Trackster::IterationIndex enum)'),
    ),
)

tracksterTables = cms.Task(tracksterTable)