import FWCore.ParameterSet.Config as cms

recoPFJetConstituentTableProducer = cms.EDProducer("RecoPFJetConstituentTableProducer",
    jets = cms.InputTag("ak4PFJets"),
    candidates = cms.InputTag("particleFlow"),
    name = cms.string("JetPFCands"),
    idx_name = cms.string("candIdx"),
    doc = cms.string("Jet to PF candidate constituent index table"),
)

