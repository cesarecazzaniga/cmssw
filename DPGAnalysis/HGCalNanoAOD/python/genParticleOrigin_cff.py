# genParticleOrigin_cff.py
#
# Instance of GenParticleOriginValueMapProducer (plugins/
# GenParticleOriginValueMapProducer.cc) for the standard local-PU gun
# chain: reads DisplacedParticleGunProducerFlatEtaWithLocalPU's own
# "particleOrigin"/"particleBarcode" products off the module labeled
# "generator" (same label local_pileup_gun_cfg.py's process.generator
# uses -- change the particleOrigin/particleBarcode InputTags below if
# your GEN-SIM step used a different module label), and the standard
# "genParticles" collection genParticleTable.src already points to (see
# nanoHGCML_cff.py). See GenParticleOriginValueMapProducer.cc's own
# header comment for why the direct index match this producer performs
# is valid for this sample.
#
# IMPORTANT: the GEN-SIM step that ran the gun must have KEPT
# "particleOrigin"/"particleBarcode" in its output file's outputCommands
# (they are not part of any standard curated event content, e.g.
# RAWSIMEventContent, by default) -- see DisplacedParticleGunProducer
# FlatEtaWithLocalPU.cc's own comment on this, next to its produces<>()
# calls, for the exact `keep` lines to add there.

import FWCore.ParameterSet.Config as cms

genParticleOriginTable = cms.EDProducer("GenParticleOriginValueMapProducer",
    src = cms.InputTag("genParticles"),
    particleOrigin = cms.InputTag("generator", "particleOrigin"),
    particleBarcode = cms.InputTag("generator", "particleBarcode"),
)