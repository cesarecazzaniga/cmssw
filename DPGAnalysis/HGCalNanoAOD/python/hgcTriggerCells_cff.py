import FWCore.ParameterSet.Config as cms

# HGCAL L1 trigger cells (BX=0 only -- correct at any PU level, not a
# gun-sample-specific simplification; see HGCalTriggerCellTableProducer.cc
# for why in-time pileup and BX indexing are unrelated axes).
#
# Defaults to the Concentrator-stage output
# (l1tHGCalConcentratorProducer:HGCalConcentratorProcessorSelection) --
# the post-selection/compression stage that downstream L1 clustering
# actually consumes, i.e. "what the trigger actually saw" -- rather than
# the earlier, rawer VFE stage. Override src below (commented example) if
# the VFE stage is wanted instead.
hgcTriggerCellTable = cms.EDProducer("HGCalTriggerCellTableProducer",
    src = cms.InputTag("l1tHGCalConcentratorProducer", "HGCalConcentratorProcessorSelection"),
    # src = cms.InputTag("l1tHGCalVFEProducer", "HGCalVFEProcessorSums"),  # earlier VFE stage, if wanted instead
    name = cms.string("TriggerCell"),
    doc = cms.string("HGCAL L1 trigger cell (BX=0 only)"),
)

hgcTriggerCellsSequence = cms.Sequence(hgcTriggerCellTable)