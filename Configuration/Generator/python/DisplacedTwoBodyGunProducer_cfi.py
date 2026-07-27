import math

import FWCore.ParameterSet.Config as cms


# Displaced two-body (diphoton) gun
# ----------------------------------
#
# Generates TWO particles (default: photons) from a single shared, displaced
# production vertex, with a controlled opening angle between them -- e.g. for
# testing at what angular separation a clustering algorithm stops resolving
# two showers. See DisplacedTwoBodyGunProducer.cc for the full implementation.
#
# Geometry model
# --------------
#
# Identical to DisplacedParticleGunProducer: a shared origin point is sampled
# on the z=0 Origin plane; each daughter's own straight-line trajectory from
# that point must independently cross the Production plane (and Target plane,
# if present) within their configured radial/phi bounds.
#
# Sampling measure
# -----------------
#
# * Shared Origin point: sampled exactly as in DisplacedParticleGunProducer
#   (RadialDistribution controls the radial measure; phi sampled uniformly).
#   Both daughters are produced from this ONE shared point.
#
# * Reference photon (daughter 1) direction: phi sampled uniformly from
#   Momentum.Direction.PhiMin/PhiMax; theta sampled uniformly from
#   Momentum.Direction.ThetaMin/ThetaMax. Unlike DisplacedParticleGunProducer,
#   this producer does NOT analytically solve for the theta sub-intervals that
#   satisfy the Production/Target radial bounds -- it samples theta directly
#   from the full configured window and simply checks whether the resulting
#   trajectory is reachable, retrying the whole configuration (see below) if
#   not. This is simpler but means a badly-mismatched configuration (e.g. a
#   Direction window mostly outside what Production/Target can ever accept)
#   will only be discovered at runtime, via repeated failed attempts, rather
#   than analytically -- pick Direction bounds that comfortably overlap the
#   reachable range, and watch for "Failed to find a valid two-body
#   configuration" exceptions when testing a new parameter set.
#
# * Companion photon (daughter 2) direction: DERIVED, not independently
#   sampled -- an exact 3D rotation of the reference photon's direction by an
#   opening angle drawn uniformly from OpeningAngle.Min/Max, around a
#   uniformly random azimuthal axis. Momentum.Direction bounds do NOT apply to
#   the companion; only its own trajectory reachability is checked.
#
# * Momentum magnitude: if Momentum.EqualEnergy is False (default), each
#   daughter's magnitude is sampled independently from Momentum.Magnitude
#   Min/Max -- use this to test energy-asymmetric configurations. If True,
#   ONE magnitude is sampled and shared by both daughters -- use this if you
#   specifically want the summed diphoton momentum to stay close to the
#   reference photon's direction (equal energies exactly cancel the
#   direction-mismatch term in the vector sum; unequal energies do not).
#
# Retry behavior
# --------------
#
# On any failure (reference trajectory unreachable, companion direction
# rotated into the backward hemisphere, or companion trajectory unreachable),
# the ENTIRE configuration -- shared vertex, reference direction, and opening
# angle/azimuth together -- is resampled from scratch, up to
# MaxSamplingAttempts times.
#
# Parameter constraints (beyond those inherited from the geometry model)
# ------------------------------------------------------------------------
#
# * OpeningAngle bounds must satisfy 0 <= Min < Max < pi.
# * Momentum.Direction.ThetaMax must stay below pi/2 (single-endcap geometry);
#   the companion's derived theta is separately checked to also stay in
#   [0, pi/2) and will trigger a retry if the rotation pushes it outside that
#   range (rather than raising -- an occasional retry from a large opening
#   angle near ThetaMax is expected, not an error).
#
# This example reuses the same Origin/Production/Target geometry as the
# single-photon DisplacedParticleGunProducer example, for direct
# side-by-side comparison. OpeningAngle below is a PLACEholder scan point
# only -- pick the actual range based on HGCAL's angular hit/shower
# separation scale at your target displacement and energy, not this value.

generator = cms.EDProducer(
    "DisplacedTwoBodyGunProducer",
    PGunParameters=cms.PSet(
        PartID=cms.int32(22),

        Momentum=cms.PSet(
            Magnitude=cms.PSet(
                # "pt" or "energy"; bounds are in GeV.
                Variable=cms.string("energy"),
                Min=cms.double(5.0),
                Max=cms.double(100.0),
            ),
            Direction=cms.PSet(
                # Reference photon (daughter 1) only -- see docstring above.
                ThetaMin=cms.double(0.0),
                ThetaMax=cms.double(math.pi / 4.0),
                PhiMin=cms.double(-math.pi),
                PhiMax=cms.double(math.pi),
            ),
            # False: independently sample each daughter's energy (tests
            # energy-asymmetric configurations).
            # True: share one sampled energy between both daughters.
            EqualEnergy=cms.bool(False),
        ),

        # Opening angle between the two daughters, in radians. PLACEHOLDER --
        # tune to your actual clustering-separability scan range.
        OpeningAngle=cms.PSet(
            Min=cms.double(0.01),
            Max=cms.double(0.05),
        ),

        Geometry=cms.PSet(
            # Same geometry block as the single-photon gun example -- see
            # that fragment for the full explanation of each field.
            RadialDistribution=cms.string("uniformArea"),

            Origin=cms.PSet(
                RMin=cms.double(0.0),
                RMax=cms.double(100.0),
                PhiMin=cms.double(-math.pi),
                PhiMax=cms.double(math.pi),
            ),

            Production=cms.PSet(
                Z=cms.double(319.0),
                RMin=cms.double(60.0),
                RMax=cms.double(90.0),
                PhiMin=cms.double(-math.pi),
                PhiMax=cms.double(math.pi),
            ),

            Target=cms.PSet(
                Z=cms.double(362.18),
                RMin=cms.double(75.80),
                RMax=cms.double(120.23),
                PhiMin=cms.double(-math.pi),
                PhiMax=cms.double(math.pi),
            ),
        ),

        # Maximum full-configuration resampling attempts (shared vertex +
        # both daughters' directions together) -- see "Retry behavior" above.
        MaxSamplingAttempts=cms.uint32(10000),
    ),

    Verbosity=cms.untracked.int32(0),
)