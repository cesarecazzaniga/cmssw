#!/usr/bin/env python3
"""
_AlCaPhiSymEcal_Nano_

Scenario supporting proton collision data taking for AlCaPhiSymEcal stream with ALCANANO output

"""

from Configuration.DataProcessing.Impl.pp import pp
from Configuration.Eras.Era_Run3_cff import Run3

class AlCaPhiSymEcal_Nano(pp):
    def __init__(self):
        pp.__init__(self)
        self.skims=['EcalPhiSymByRun']
        self.eras=Run3
        self.recoSeq = ':bunchSpacingProducer+ecalMultiFitUncalibRecHitTask+ecalCalibratedRecHitTask'
        self.promptCustoms = [ 'Calibration/EcalCalibAlgos/EcalPhiSymRecoSequence_cff.customise' ]
    """
    _AlCaPhiSymEcal_Nano_

    Implement configuration building for data processing for proton
    collision data taking for AlCaPhiSymEcal stream with ALCANANO output

    """
