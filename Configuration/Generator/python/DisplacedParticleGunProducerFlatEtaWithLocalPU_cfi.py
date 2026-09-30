"""Worked example for DisplacedParticleGunProducerFlatEtaWithLocalPU.

PGunParameters is IDENTICAL to how you'd already configure
DisplacedParticleGunProducerFlatEta for the probe (copy your existing
block verbatim) -- LocalPileup is the only new PSet. Adjust paths/values
to your actual setup; this fragment assumes it's dropped into an existing
GEN-SIM cmsRun config that already has the RandomNumberGeneratorService,
VtxSmeared, and the rest of the sim sequence set up (see the module
docstring's "THINGS TO KEEP IN MIND" for what stays unchanged there).
"""
import FWCore.ParameterSet.Config as cms

process = cms.Process("GEN")

process.load("SimGeneral.HepPDTESSource.pythiapdt_cfi")

process.generator = cms.EDProducer(
    "DisplacedParticleGunProducerFlatEtaWithLocalPU",
    Verbosity=cms.untracked.int32(0),
    PGunParameters=cms.PSet(
        PartID=cms.int32(22),  # e.g. a displaced photon probe
        NParticles=cms.int32(1),
        Momentum=cms.PSet(
            Magnitude=cms.PSet(
                Variable=cms.string("pt"),
                Min=cms.double(10.0),
                Max=cms.double(100.0),
            ),
            Direction=cms.PSet(
                ThetaMin=cms.double(0.15),   # eta ~ 3.0
                ThetaMax=cms.double(0.30),   # eta ~ 1.5
                PhiMin=cms.double(-3.14159265),
                PhiMax=cms.double(3.14159265),
            ),
        ),
        Geometry=cms.PSet(
            RadialDistribution=cms.string("uniformArea"),
            Origin=cms.PSet(
                RMin=cms.double(0.0),
                RMax=cms.double(0.0),
                PhiMin=cms.double(-3.14159265),
                PhiMax=cms.double(3.14159265),
            ),
            Production=cms.PSet(
                Z=cms.double(320.0),  # cm, e.g. just in front of HGCAL
                RMin=cms.double(0.0),
                RMax=cms.double(0.0),
                PhiMin=cms.double(-3.14159265),
                PhiMax=cms.double(3.14159265),
            ),
            # Target omitted here -- add one if your probe needs a
            # straight-line pointing constraint, exactly as you would for
            # the plain DisplacedParticleGunProducerFlatEta.
        ),
        MaxSamplingAttempts=cms.uint32(10000),
    ),
    LocalPileup=cms.PSet(
        # Output of build_pu_sampling_recipe.py -- STRONGLY recommended to
        # have been built with --geometric_cut (see that script's
        # docstring): this producer needs FINITE eta bins to define a
        # local areal density at all, and skips (with a LogWarning) any
        # part of the cone that falls in a non-finite bin.
        RecipeFile=cms.string("pu_sampling_recipe_hgcal.root"),
        # Disk radius R in the flat (eta, phi) plane (ΔR =
        # sqrt(Δeta^2+Δphi^2) convention) around the probe's own sampled
        # direction to sample local pileup within.
        ConeRadius=cms.double(0.4),
        # Target number of "as-if" PU interactions this cone's rate is
        # scaled to -- same meaning as the Python prototype's --n_pu.
        NPu=cms.double(200.0),
        # If True, the realized number of PU interactions itself
        # fluctuates event-to-event (Poisson around NPu) on top of the
        # per-recipe-bin Poisson particle-count fluctuation, which always
        # happens regardless of this flag.
        Fluctuate=cms.bool(True),
        MaxConeSamplingAttempts=cms.uint32(10000),
    ),
)

process.generation_step = cms.Path(process.generator)