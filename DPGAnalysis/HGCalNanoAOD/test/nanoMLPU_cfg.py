# Auto generated configuration file
# using: 
# Revision: 1.19 
# Source: /local/reps/CMSSW/CMSSW/Configuration/Applications/python/ConfigBuilder.py,v 
# with command line options: step1 --filein file:test.root --fileout testNanoML.root --mc --eventcontent NANOAODSIM --datatier NANOAODSIM --conditions auto:mc --step NANO
import FWCore.ParameterSet.Config as cms
from FWCore.ParameterSet.VarParsing import VarParsing


process = cms.Process('NANO')
options = VarParsing('python')
options.setDefault('outputFile', 'testNanoML.root')
options.register("nThreads", 1, VarParsing.multiplicity.singleton, VarParsing.varType.int,
    "number of threads")
options.register("runPFTruth", 0, VarParsing.multiplicity.singleton, VarParsing.varType.int,
    "Don't run PFTruth (currently not working with pileup)")
#options.register("runPFTruth", 1, VarParsing.multiplicity.singleton, VarParsing.varType.int,
#    "generate PFTruth")
options.parseArguments()

# import of standard configurations
process.load('Configuration.StandardSequences.Services_cff')
process.load('SimGeneral.HepPDTESSource.pythiapdt_cfi')
process.load('FWCore.MessageService.MessageLogger_cfi')
process.load('Configuration.EventContent.EventContent_cff')
process.load('SimGeneral.MixingModule.mixNoPU_cfi')
process.load('Configuration.Geometry.GeometryExtendedRun4D121Reco_cff')
process.load('Configuration.Geometry.GeometryExtendedRun4D121_cff')
process.load('Configuration.StandardSequences.MagneticField_cff')
process.load('DPGAnalysis.HGCalNanoAOD.nanoHGCML_cff')
#added
# Fix for ProductNotFound error with FlatEtaRangeGunProducer
process.tpClusterProducer.pixelSimLinkSrc = cms.InputTag("simSiPixelDigis", "Pixel")
process.tpClusterProducer.phase2OTSimLinkSrc = cms.InputTag("simSiPixelDigis", "Tracker")
#added
process.load('Configuration.StandardSequences.Reconstruction_cff')
process.load('Configuration.StandardSequences.EndOfProcess_cff')
process.load('Configuration.StandardSequences.FrontierConditions_GlobalTag_cff')

# This isn't working with pileup
if not options.runPFTruth:
    process.pfTruth = cms.Sequence()
    process.trackSCAssocTable = cms.Sequence()

process.maxEvents = cms.untracked.PSet(
    input = cms.untracked.int32(-1),
    output = cms.optional.untracked.allowed(cms.int32,cms.PSet)
)
process.options.numberOfThreads=cms.untracked.uint32(options.nThreads)

# Input source
process.source = cms.Source("PoolSource",
    fileNames = cms.untracked.vstring(options.inputFiles),
    secondaryFileNames = cms.untracked.vstring()
)

process.options = cms.untracked.PSet(
#    FailPath = cms.untracked.vstring(),
    IgnoreCompletely = cms.untracked.vstring(),
    Rethrow = cms.untracked.vstring(),
#    SkipEvent = cms.untracked.vstring(),
    allowUnscheduled = cms.obsolete.untracked.bool,
    canDeleteEarly = cms.untracked.vstring(),
    emptyRunLumiMode = cms.obsolete.untracked.string,
    eventSetup = cms.untracked.PSet(
        forceNumberOfConcurrentIOVs = cms.untracked.PSet(
            allowAnyLabel_=cms.required.untracked.uint32
        ),
        numberOfConcurrentIOVs = cms.untracked.uint32(1)
    ),
    fileMode = cms.untracked.string('FULLMERGE'),
    forceEventSetupCacheClearOnNewRun = cms.untracked.bool(False),
    makeTriggerResults = cms.obsolete.untracked.bool,
    numberOfConcurrentLuminosityBlocks = cms.untracked.uint32(1),
    numberOfConcurrentRuns = cms.untracked.uint32(1),
    numberOfStreams = cms.untracked.uint32(0),
    numberOfThreads = cms.untracked.uint32(1),
    printDependencies = cms.untracked.bool(False),
    sizeOfStackForThreadsInKB = cms.optional.untracked.uint32,
    throwIfIllegalParameter = cms.untracked.bool(True),
    wantSummary = cms.untracked.bool(False)
)

# Production Info
process.configurationMetadata = cms.untracked.PSet(
    annotation = cms.untracked.string('step1 nevts:1'),
    name = cms.untracked.string('Applications'),
    version = cms.untracked.string('$Revision: 1.19 $')
)

# Output definition

process.NANOAODSIMoutput = cms.OutputModule("NanoAODOutputModule",
    compressionAlgorithm = cms.untracked.string('LZMA'),
    compressionLevel = cms.untracked.int32(9),
    dataset = cms.untracked.PSet(
        dataTier = cms.untracked.string('NANOAODSIM'),
        filterName = cms.untracked.string('')
    ),
    fileName = cms.untracked.string(options.outputFile),
    outputCommands = process.NANOAODSIMEventContent.outputCommands
)

process.NANOAODSIMoutput.outputCommands.remove("keep edmTriggerResults_*_*_*")

# Additional output definition

# Other statements
from Configuration.AlCa.GlobalTag import GlobalTag
process.GlobalTag = GlobalTag(process.GlobalTag, 'auto:phase2_realistic_T33_13TeV', '')

# Path and EndPath definitions
process.nanoAOD_step = cms.Path(process.nanoHGCMLSequence)
process.endjob_step = cms.EndPath(process.endOfProcess)
process.NANOAODSIMoutput_step = cms.EndPath(process.NANOAODSIMoutput)

# Schedule definition
process.schedule = cms.Schedule(process.nanoAOD_step,process.endjob_step,process.NANOAODSIMoutput_step)
from PhysicsTools.PatAlgos.tools.helpers import associatePatAlgosToolsTask
associatePatAlgosToolsTask(process)

# customisation of the process.
from DPGAnalysis.HGCalNanoAOD.nanoHGCML_cff import customizeReco,customizeMergedSimClusters
# Uncomment if you didn't schedule SimClusters/CaloParticles
# process = customizeNoMergedCaloTruth(process)

# MERGING DISABLED: customizeMergedSimClusters(process) intentionally NOT
# called -- this is what schedules the SimClusterMerger (hgcSimTruth)
# step in the first place. Needed to work around a crash seen when
# running with real pileup (out-of-bounds SimTrack/SimVertex index --
# see this project's own debugging history), and appropriate here since
# this production is for network evaluation only, not truth-label
# generation, so MergedSimCluster-based truth isn't needed.
#
# IMPORTANT TRADEOFF, not a free workaround: this removes ALL
# MergedSimCluster-based truth info from the output --
# RecHitHGC_MergedSimClusterBestMatchIdx, LayerCluster_
# MergedSimClusterBestMatchIdx, and the collection-level MergedSimCluster
# fields (hasHGCALHit, boundaryEnergy, sumHitEnergy, etc., if
# --include_mergedsimcluster was otherwise being used downstream in
# hgcal_npz.py) are all gone. Raw SimCluster truth
# (RecHitHGC_SimClusterBestMatchIdx / LayerCluster_SimClusterBestMatchIdx)
# is UNAFFECTED and still available -- use --truth_source raw downstream.
# If the network being evaluated was trained with --truth_source merged,
# scoring it against raw truth risks a systematic bias from GEANT
# shower-fragmentation (see this project's own discussion of why
# MergedSimCluster exists at all) -- worth confirming which truth_source
# the model was actually trained with before relying on this for
# quantitative metrics.
process = customizeReco(process)

# customizeReco() ITSELF re-adds mergedSimClusterTables to
# nanoHGCMLRecoSequence independently of customizeMergedSimClusters()
# above (confirmed directly from nanoHGCML_cff.py's own source) -- so
# skipping that one call alone is NOT sufficient. Explicitly remove it
# (and the LayerCluster-level merged-truth table, if present) from the
# final scheduled sequence. hasattr() guards: harmless if either was
# never actually added in this exact CMSSW version/config.
if hasattr(process, 'nanoHGCMLRecoSequence'):
    if hasattr(process, 'mergedSimClusterTables'):
        process.nanoHGCMLRecoSequence.remove(process.mergedSimClusterTables)
    if hasattr(process, 'layerClusterMergedSimClusterTables'):
        process.nanoHGCMLRecoSequence.remove(process.layerClusterMergedSimClusterTables)

# End of customisation functions


# Customisation from command line

# Add early deletion of temporary data products to reduce peak memory need
from Configuration.StandardSequences.earlyDeleteSettings_cff import customiseEarlyDelete
process = customiseEarlyDelete(process)
# End adding early deletion