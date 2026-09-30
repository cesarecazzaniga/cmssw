// DisplacedParticleGunProducerFlatEtaWithLocalPU.cc
//
// Extends DisplacedParticleGunProducerFlatEta.cc (attached, unmodified
// probe-generation logic reused almost verbatim -- see below) with a
// SECOND source of particles in the same produce() call / same GenEvent:
// local pileup sampled from a build_pu_sampling_recipe.py ROOT file,
// restricted to a cone of radius LocalPileup.ConeRadius around the
// PROBE'S OWN sampled direction (eta, phi) -- i.e. exactly the
// "reproduce the expected pile-up profile locally around a probe
// particle" gun sketched in that script's docstring and prototyped in
// Python by sample_local_pu_around_probe.py. Every formula below
// (circle-segment cone-bin weighting, uniform-in-area rejection sampling
// in the (eta, phi) plane, the species/pt/displacement draw order, the
// un-smeared-frame vertex convention) is a direct C++ port of that
// prototype; see its module docstring and build_pu_sampling_recipe.py's
// own docstring ("GUN-SIDE RECIPE") for the physics reasoning -- this
// file focuses on the CMSSW-side implementation, not re-deriving it.
//
// WHAT IS REUSED UNCHANGED FROM THE ATTACHED FILE: every helper needed to
// generate the probe itself (MagnitudeParameters, DirectionParameters,
// PlaneParameters, GeometryParameters, ParticleGunParameters,
// sampleEta/sampleRadius/sampleDirection/resolveParticle,
// appendParticleToGenEvent, resolveTimeOfFlight, and all the plane-
// intersection geometry helpers) is copied verbatim into this file's
// anonymous namespace -- C++ anonymous-namespace symbols have internal
// linkage, so they cannot be shared across translation units by #include
// of the .cc file itself; the standard CMSSW way to reuse them is to
// duplicate them into the new plugin's own .cc (a shared private header
// under interface/ would be the alternative if this ever needs to serve
// a third producer, but two is not yet enough to justify one). Nothing
// in that code is touched or reinterpreted; only the class name,
// constructor, fillDescriptions, and produce() differ from the original.
//
// WHAT IS NEW: reading a build_pu_sampling_recipe.py ROOT file once at
// construction time (loadRecipe), and, once per event, computing which
// of the recipe's eta bins the probe's cone overlaps (coneBinWeights,
// the C++ port of sample_local_pu_around_probe.py's cone_bin_weights),
// Poisson-drawing a particle count per overlapping bin, and for each
// particle: a position via uniform-in-area rejection sampling within
// that bin's (eta, phi) strip intersected with the disk (mirroring the
// attached file's own kUniformArea convention for the probe's ORIGIN
// point, just applied to eta/phi here instead of x/y), then species / pt
// / displacement via inverse-CDF draws against the recipe's histograms
// (drawFromHist -- a TH1::GetRandom() equivalent built by hand so every
// random draw in this file goes through the SAME CLHEP engine as the
// probe, which plain TH1::GetRandom()'s reliance on the global gRandom
// would break: CMSSW requires reproducibility tied to the framework's
// RandomNumberGeneratorService, not ROOT's own global generator).
//
// BUILD: this plugin links against ROOT's tree/hist I/O on top of the
// usual HepMC/CLHEP/EDM dependencies -- see the accompanying
// BuildFile.xml (<use name="root"/>).
//
// CONFIG: see the accompanying local_pileup_gun_cfg.py for a worked
// example. The new PSet is "LocalPileup", a sibling of "PGunParameters":
//   LocalPileup = cms.PSet(
//       RecipeFile               = cms.string('pu_sampling_recipe_hgcal.root'),
//       ConeRadius                = cms.double(0.4),   # ΔR = sqrt(Δeta^2+Δphi^2)
//       NPu                       = cms.double(200.),
//       Fluctuate                 = cms.bool(True),
//       MaxConeSamplingAttempts   = cms.uint32(10000),
//   )

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <numbers>
#include <numeric>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <CLHEP/Random/RandFlat.h>
#include <CLHEP/Random/RandPoissonQ.h>
#include <CLHEP/Units/SystemOfUnits.h>

#include "HepMC/GenEvent.h"

#include "TFile.h"
#include "TH1D.h"
#include "TTreeReader.h"
#include "TTreeReaderValue.h"

#include "FWCore/AbstractServices/interface/RandomNumberGenerator.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "FWCore/Utilities/interface/Exception.h"
#include "SimDataFormats/GeneratorProducts/interface/GenEventInfoProduct.h"
#include "SimDataFormats/GeneratorProducts/interface/HepMCProduct.h"
#include "SimGeneral/HepPDTRecord/interface/ParticleDataTable.h"

namespace edm {

  namespace {

    // =========================================================================
    // SECTION 1 -- reused verbatim from DisplacedParticleGunProducerFlatEta.cc.
    // Generates the probe particle exactly as the attached file does. Not
    // one physics/geometry line here has been changed; only the exception
    // category strings were left as "DisplacedParticleGunProducerFlatEta"
    // (not renamed to ...WithLocalPU) so error messages stay greppable
    // against that file's own documentation of the same checks.
    // =========================================================================

    enum class MagnitudeVariable { kEnergy, kPt };
    enum class RadialDistribution { kUniformArea, kUniformRadius };

    MagnitudeVariable readMagnitudeVariable(const std::string& value) {
      if (value == "energy") {
        return MagnitudeVariable::kEnergy;
      }
      if (value == "pt") {
        return MagnitudeVariable::kPt;
      }
      throw cms::Exception("DisplacedParticleGunProducerFlatEta")
          << "Momentum.Magnitude.Variable must be either 'energy' or 'pt', but is '" << value << "'.";
    }

    RadialDistribution readRadialDistribution(const std::string& value) {
      if (value == "uniformArea") {
        return RadialDistribution::kUniformArea;
      }
      if (value == "uniformRadius") {
        return RadialDistribution::kUniformRadius;
      }
      throw cms::Exception("DisplacedParticleGunProducerFlatEta")
          << "Geometry.RadialDistribution must be either 'uniformArea' or 'uniformRadius', but is '" << value << "'.";
    }

    struct MagnitudeParameters {
      explicit MagnitudeParameters(const ParameterSet& pset)
          : variable(readMagnitudeVariable(pset.getParameter<std::string>("Variable"))),
            min(pset.getParameter<double>("Min")),
            max(pset.getParameter<double>("Max")) {
        if (max <= min) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEta") << "Please fix Momentum.Magnitude.Min/Max.";
        }
        if (min <= 0.) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEta") << "Momentum.Magnitude.Min must be positive.";
        }
      }

      MagnitudeVariable variable;
      double min;
      double max;
    };

    struct DirectionParameters {
      explicit DirectionParameters(const ParameterSet& pset)
          : thetaMin(pset.getParameter<double>("ThetaMin")),
            thetaMax(pset.getParameter<double>("ThetaMax")),
            phiMin(pset.getParameter<double>("PhiMin")),
            phiMax(pset.getParameter<double>("PhiMax")) {
        if (thetaMax <= thetaMin) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEta")
              << "Please fix Momentum.Direction.ThetaMin/ThetaMax.";
        }
        if (phiMax <= phiMin) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEta") << "Please fix Momentum.Direction.PhiMin/PhiMax.";
        }
        if (thetaMin < 0. || thetaMax >= std::numbers::pi / 2.) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEta")
              << "Momentum.Direction theta bounds must lie inside [0, pi/2).";
        }
        if (phiMin < -std::numbers::pi || phiMax > std::numbers::pi) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEta")
              << "Momentum.Direction phi bounds must lie inside [-pi, pi].";
        }
      }

      double thetaMin;
      double thetaMax;
      double phiMin;
      double phiMax;
    };

    struct MomentumParameters {
      explicit MomentumParameters(const ParameterSet& pset)
          : magnitude(pset.getParameter<ParameterSet>("Magnitude")),
            direction(pset.getParameter<ParameterSet>("Direction")) {}

      MagnitudeParameters magnitude;
      DirectionParameters direction;
    };

    struct PlaneParameters {
      PlaneParameters(const ParameterSet& pset, double planeZ, const char* name)
          : z(planeZ),
            rMin(pset.getParameter<double>("RMin")),
            rMax(pset.getParameter<double>("RMax")),
            phiMin(pset.getParameter<double>("PhiMin")),
            phiMax(pset.getParameter<double>("PhiMax")) {
        if (rMax <= rMin) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEta") << "Please fix Geometry." << name << ".RMin/RMax.";
        }
        if (rMin < 0.) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEta") << "Geometry." << name << ".RMin must be nonnegative.";
        }
        if (phiMax <= phiMin) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEta") << "Please fix Geometry." << name << ".PhiMin/PhiMax.";
        }
        if (phiMin < -std::numbers::pi || phiMax > std::numbers::pi) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEta")
              << "Geometry." << name << " phi bounds must lie inside [-pi, pi].";
        }
      }

      PlaneParameters(const ParameterSet& pset, const char* name)
          : PlaneParameters(pset, pset.getParameter<double>("Z"), name) {}

      double z;
      double rMin;
      double rMax;
      double phiMin;
      double phiMax;
    };

    std::optional<PlaneParameters> readTarget(const ParameterSet& geometry) {
      if (!geometry.existsAs<ParameterSet>("Target")) {
        return std::nullopt;
      }
      return PlaneParameters(geometry.getParameter<ParameterSet>("Target"), "Target");
    }

    struct GeometryParameters {
      explicit GeometryParameters(const ParameterSet& pset)
          : radialDistribution(readRadialDistribution(pset.getParameter<std::string>("RadialDistribution"))),
            origin(pset.getParameter<ParameterSet>("Origin"), 0., "Origin"),
            production(pset.getParameter<ParameterSet>("Production"), "Production"),
            target(readTarget(pset)) {
        if (production.z <= origin.z) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEta") << "Geometry.Production.Z must be greater than zero.";
        }
        if (target && target->z <= production.z) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEta")
              << "Geometry.Target.Z must be greater than Geometry.Production.Z.";
        }
        if (target) {
          const double fraction = production.z / target->z;
          const double largestReachableProductionRadius = (1. - fraction) * origin.rMax + fraction * target->rMax;
          if (production.rMin > largestReachableProductionRadius) {
            throw cms::Exception("DisplacedParticleGunProducerFlatEta")
                << "Geometry.Production radial range is unreachable between the configured Origin and Target caps.";
          }
        }
      }

      RadialDistribution radialDistribution;
      PlaneParameters origin;
      PlaneParameters production;
      std::optional<PlaneParameters> target;
    };

    double getDistanceBetweenIntervals(double firstMin, double firstMax, double secondMin, double secondMax) {
      if (firstMax < secondMin) {
        return secondMin - firstMax;
      }
      if (secondMax < firstMin) {
        return firstMin - secondMax;
      }
      return 0.;
    }

    bool isRadialRangeReachableForDirection(const PlaneParameters& initial,
                                            const PlaneParameters& final,
                                            const DirectionParameters& direction) {
      const double deltaZ = std::abs(final.z - initial.z);
      const double displacementMin = deltaZ * std::tan(direction.thetaMin);
      const double displacementMax = deltaZ * std::tan(direction.thetaMax);
      const double reachableRMin =
          getDistanceBetweenIntervals(initial.rMin, initial.rMax, displacementMin, displacementMax);
      const double reachableRMax = initial.rMax + displacementMax;
      return final.rMax >= reachableRMin && final.rMin <= reachableRMax;
    }

    void validateRadialReachability(const PlaneParameters& initial,
                                    const PlaneParameters& final,
                                    const DirectionParameters& direction,
                                    const char* initialName,
                                    const char* finalName) {
      if (!isRadialRangeReachableForDirection(initial, final, direction)) {
        throw cms::Exception("DisplacedParticleGunProducerFlatEta")
            << "Geometry." << finalName << " radial range is unreachable from Geometry." << initialName
            << " within the theta range.";
      }
    }

    struct ParticleGunParameters {
      explicit ParticleGunParameters(const ParameterSet& pset)
          : partId(pset.getParameter<int>("PartID")),
            nParticles(pset.getParameter<int>("NParticles")),
            momentum(pset.getParameter<ParameterSet>("Momentum")),
            geometry(pset.getParameter<ParameterSet>("Geometry")),
            maxSamplingAttempts(pset.getParameter<unsigned int>("MaxSamplingAttempts")) {
        if (nParticles <= 0) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEta") << "NParticles must be greater than zero.";
        }
        if (maxSamplingAttempts == 0) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEta") << "MaxSamplingAttempts must be greater than zero.";
        }
        if (momentum.magnitude.variable == MagnitudeVariable::kPt && momentum.direction.thetaMin == 0.) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEta")
              << "Momentum.Direction.ThetaMin must be greater than zero when Momentum.Magnitude.Variable is 'pt'.";
        }

        validateRadialReachability(geometry.origin, geometry.production, momentum.direction, "Origin", "Production");
        if (geometry.target) {
          validateRadialReachability(geometry.origin, *geometry.target, momentum.direction, "Origin", "Target");
          validateRadialReachability(geometry.production, *geometry.target, momentum.direction, "Production", "Target");
        }
      }

      int partId;
      int nParticles;
      MomentumParameters momentum;
      GeometryParameters geometry;
      unsigned int maxSamplingAttempts;
    };

    // Top-level probe-gun parameters: the "PGunParameters" PSet plus the
    // untracked "Verbosity" int, exactly as in the attached file.
    struct ProducerParameters {
      explicit ProducerParameters(const ParameterSet& pset)
          : particleGun(pset.getParameter<ParameterSet>("PGunParameters")),
            verbosity(pset.getUntrackedParameter<int>("Verbosity", 0)) {}

      ParticleGunParameters particleGun;
      int verbosity;
    };

    struct Interval {
      double min;
      double max;
    };

    struct Point {
      double x;
      double y;
      double z;
    };

    struct SampledDirection {
      double theta;
      double phi;
      Point productionPoint;
    };

    struct FourMomentum {
      double px;
      double py;
      double pz;
      double energy;
    };

    struct ProductionVertex {
      double x;
      double y;
      double z;
      double time;
    };

    struct ResolvedParticle {
      int pdgId;
      FourMomentum momentum;
      ProductionVertex vertex;
    };

    std::vector<Interval> intersectIntervals(const std::vector<Interval>& first, const std::vector<Interval>& second) {
      std::vector<Interval> result;
      for (const auto& left : first) {
        for (const auto& right : second) {
          const double min = std::max(left.min, right.min);
          const double max = std::min(left.max, right.max);
          if (min < max) {
            result.push_back({min, max});
          }
        }
      }
      return result;
    }

    std::vector<Interval> subtractInterval(const Interval& allowed, const Interval& excluded) {
      std::vector<Interval> result;

      const double leftMax = std::min(allowed.max, excluded.min);
      if (allowed.min < leftMax) {
        result.push_back({allowed.min, leftMax});
      }

      const double rightMin = std::max(allowed.min, excluded.max);
      if (rightMin < allowed.max) {
        result.push_back({rightMin, allowed.max});
      }

      return result;
    }

    std::optional<Interval> getQuadraticRoots(double a, double b, double c) {
      const double discriminant = b * b - 4. * a * c;
      if (discriminant <= 0.) {
        return std::nullopt;
      }

      const double sqrtDiscriminant = std::sqrt(discriminant);
      return Interval{(-b - sqrtDiscriminant) / (2. * a), (-b + sqrtDiscriminant) / (2. * a)};
    }

    std::vector<Interval> getAllowedSlopesForCap(const Point& originPoint,
                                                 double momentumPhi,
                                                 const PlaneParameters& cap) {
      const double deltaZ = cap.z - originPoint.z;
      const double a = deltaZ * deltaZ;
      const double b = 2. * deltaZ * (originPoint.x * std::cos(momentumPhi) + originPoint.y * std::sin(momentumPhi));
      const double c = originPoint.x * originPoint.x + originPoint.y * originPoint.y;

      const auto outerRoots = getQuadraticRoots(a, b, c - cap.rMax * cap.rMax);
      if (!outerRoots) {
        return {};
      }

      const auto innerRoots = getQuadraticRoots(a, b, c - cap.rMin * cap.rMin);
      if (!innerRoots) {
        return {*outerRoots};
      }

      return subtractInterval(*outerRoots, *innerRoots);
    }

    std::vector<Interval> constrainSlopesToCap(const std::vector<Interval>& slopes,
                                               const Point& originPoint,
                                               double momentumPhi,
                                               const PlaneParameters& cap) {
      return intersectIntervals(slopes, getAllowedSlopesForCap(originPoint, momentumPhi, cap));
    }

    double sampleEta(CLHEP::HepRandomEngine* engine, const std::vector<Interval>& slopes) {
      auto thetaToEta = [](double theta) { return -std::log(std::tan(theta / 2.)); };

      std::vector<Interval> etaIntervals;
      etaIntervals.reserve(slopes.size());
      for (const auto& slope : slopes) {
        const double thetaMin = std::atan(slope.min);
        const double thetaMax = std::atan(slope.max);
        etaIntervals.push_back({thetaToEta(thetaMax), thetaToEta(thetaMin)});
      }

      double totalEtaLength = 0.;
      for (const auto& interval : etaIntervals) {
        totalEtaLength += interval.max - interval.min;
      }

      double offset = CLHEP::RandFlat::shoot(engine, 0., totalEtaLength);
      double sampledEta = etaIntervals.back().max;
      for (const auto& interval : etaIntervals) {
        const double length = interval.max - interval.min;
        if (offset <= length) {
          sampledEta = interval.min + offset;
          break;
        }
        offset -= length;
      }

      return 2. * std::atan(std::exp(-sampledEta));
    }

    double sampleRadius(CLHEP::HepRandomEngine* engine, const PlaneParameters& plane, RadialDistribution distribution) {
      if (distribution == RadialDistribution::kUniformArea) {
        return std::sqrt(CLHEP::RandFlat::shoot(engine, plane.rMin * plane.rMin, plane.rMax * plane.rMax));
      }
      return CLHEP::RandFlat::shoot(engine, plane.rMin, plane.rMax);
    }

    Point projectToZ(const Point& sampled, double z, double momentumTheta, double momentumPhi) {
      const double transverseDisplacement = (z - sampled.z) * std::tan(momentumTheta);
      return {sampled.x + transverseDisplacement * std::cos(momentumPhi),
              sampled.y + transverseDisplacement * std::sin(momentumPhi),
              z};
    }

    bool isPhiWithin(double phi, double min, double max) { return phi >= min && phi <= max; }

    bool doesCapContain(const Point& point, const PlaneParameters& cap) {
      const double radius = std::hypot(point.x, point.y);
      return radius >= cap.rMin && radius <= cap.rMax &&
             isPhiWithin(std::atan2(point.y, point.x), cap.phiMin, cap.phiMax);
    }

    double resolveTimeOfFlight(const Point& origin, const Point& production, const FourMomentum& momentum) {
      const double beamSpotToOriginPathLength = std::hypot(origin.x, origin.y, origin.z);

      const double deltaX = production.x - origin.x;
      const double deltaY = production.y - origin.y;
      const double deltaZ = production.z - origin.z;
      const double originToProductionPathLength = std::hypot(deltaX, deltaY, deltaZ);

      const double absoluteMomentum = std::hypot(momentum.px, momentum.py, momentum.pz);
      return (beamSpotToOriginPathLength + originToProductionPathLength * momentum.energy / absoluteMomentum) *
             CLHEP::cm;
    }

    std::optional<SampledDirection> sampleDirection(CLHEP::HepRandomEngine* engine,
                                                    const ParticleGunParameters& parameters,
                                                    const Point& originPoint) {
      const auto& direction = parameters.momentum.direction;
      const auto& geometry = parameters.geometry;

      for (unsigned int attempt = 0; attempt < parameters.maxSamplingAttempts; ++attempt) {
        const double phi = CLHEP::RandFlat::shoot(engine, direction.phiMin, direction.phiMax);
        std::vector<Interval> allowedSlopes{{std::tan(direction.thetaMin), std::tan(direction.thetaMax)}};

        allowedSlopes = constrainSlopesToCap(allowedSlopes, originPoint, phi, geometry.production);
        if (geometry.target) {
          allowedSlopes = constrainSlopesToCap(allowedSlopes, originPoint, phi, *geometry.target);
        }
        if (allowedSlopes.empty()) {
          continue;
        }

        const double theta = sampleEta(engine, allowedSlopes);
        const Point productionPoint = projectToZ(originPoint, geometry.production.z, theta, phi);
        if (!doesCapContain(productionPoint, geometry.production)) {
          continue;
        }

        if (geometry.target) {
          const Point targetPoint = projectToZ(originPoint, geometry.target->z, theta, phi);
          if (!doesCapContain(targetPoint, *geometry.target)) {
            continue;
          }
        }

        return SampledDirection{theta, phi, productionPoint};
      }

      return std::nullopt;
    }

    void validateParticleCompatibility(const ParticleGunParameters& parameters, double mass, double charge) {
      if (parameters.geometry.target && charge != 0.) {
        throw cms::Exception("DisplacedParticleGunProducerFlatEta")
            << "Target constraints assume a straight trajectory and therefore require a neutral particle.";
      }
      const auto& magnitude = parameters.momentum.magnitude;
      if (magnitude.variable == MagnitudeVariable::kEnergy && magnitude.min <= mass) {
        throw cms::Exception("DisplacedParticleGunProducerFlatEta")
            << "Momentum.Magnitude.Min must be greater than the particle mass when Variable is 'energy'. Min="
            << magnitude.min << " GeV, mass=" << mass << " GeV.";
      }
    }

    ResolvedParticle resolveParticle(CLHEP::HepRandomEngine* engine,
                                     const ParticleGunParameters& parameters,
                                     double mass) {
      const auto& magnitude = parameters.momentum.magnitude;
      const auto& geometry = parameters.geometry;
      const auto& origin = geometry.origin;

      const double sampledR = sampleRadius(engine, origin, geometry.radialDistribution);
      const double sampledSpatialPhi = CLHEP::RandFlat::shoot(engine, origin.phiMin, origin.phiMax);
      const Point sampledOriginPoint{
          sampledR * std::cos(sampledSpatialPhi), sampledR * std::sin(sampledSpatialPhi), origin.z};

      const auto sampledDirection = sampleDirection(engine, parameters, sampledOriginPoint);
      if (!sampledDirection) {
        throw cms::Exception("DisplacedParticleGunProducerFlatEta")
            << "Failed to find a direction satisfying all configured caps after MaxSamplingAttempts="
            << parameters.maxSamplingAttempts << ". Fixed sampled point: cap=Origin, R=" << sampledR
            << " cm, phi=" << sampledSpatialPhi << ".";
      }
      const auto [theta, momentumPhi, productionPoint] = *sampledDirection;

      const double sampledMagnitude = CLHEP::RandFlat::shoot(engine, magnitude.min, magnitude.max);
      double pt = 0.;
      double pz = 0.;
      double energy = 0.;
      if (magnitude.variable == MagnitudeVariable::kPt) {
        pt = sampledMagnitude;
        pz = pt / std::tan(theta);
        energy = std::sqrt(pt * pt + pz * pz + mass * mass);
      } else {
        energy = sampledMagnitude;
        const double momentum = std::sqrt(energy * energy - mass * mass);
        pt = momentum * std::sin(theta);
        pz = momentum * std::cos(theta);
      }

      const FourMomentum momentum{pt * std::cos(momentumPhi), pt * std::sin(momentumPhi), pz, energy};

      const double time = resolveTimeOfFlight(sampledOriginPoint, productionPoint, momentum);

      return {parameters.partId, momentum, {productionPoint.x, productionPoint.y, productionPoint.z, time}};
    }

    void appendParticleToGenEvent(HepMC::GenEvent& genEvent,
                                  const ResolvedParticle& particle,
                                  int barcode,
                                  bool verbose) {
      const auto& momentum = particle.momentum;
      const auto& vertex = particle.vertex;

      auto* genVertex = new HepMC::GenVertex(
          HepMC::FourVector(vertex.x * CLHEP::cm, vertex.y * CLHEP::cm, vertex.z * CLHEP::cm, vertex.time));
      auto* genParticle = new HepMC::GenParticle(
          HepMC::FourVector(momentum.px, momentum.py, momentum.pz, momentum.energy), particle.pdgId, 1);
      genParticle->suggest_barcode(barcode);

      genVertex->add_particle_out(genParticle);
      genEvent.add_vertex(genVertex);

      if (verbose) {
        genVertex->print();
        genParticle->print();
      }
    }

    // =========================================================================
    // SECTION 2 -- NEW: recipe loading (build_pu_sampling_recipe.py's ROOT
    // output) and local-pileup sampling within a cone around the probe.
    // =========================================================================

    // Inverse-CDF representation of one histogram (a pt spectrum or a
    // log10(displacement) shape): edges.size() == cumulative.size() + 1
    // when the histogram has any entries at all; cumulative.empty() (with
    // edges possibly still populated, or the whole struct default-
    // constructed) means "this histogram is missing or empty" -- the same
    // "None" sentinel sample_pu_event.py's load_recipe/draw_from_hist use,
    // just spelled as an empty vector instead of a Python None.
    struct Histogram1D {
      std::vector<double> edges;
      std::vector<double> cumulative;  // normalized to end at 1.
    };

    // One row of the recipe's "displacement" TTree -- see
    // build_pu_sampling_recipe.py's docstring for what each field means;
    // histIdx == -1 means "use the all-species fallback histogram".
    struct RecipeDisplacementRow {
      double pLo;
      double pHi;
      long long nParticles;
      double displacedFraction;
      int histIdx;
    };

    // One row of the recipe's "density"/"composition"/pt-spectrum content
    // for a single eta bin.
    struct RecipeEtaBin {
      double etaLo;
      double etaHi;
      double meanMultiplicity;
      std::vector<int> compositionPdgId;
      std::vector<double> compositionCumulative;  // normalized to end at 1.
      Histogram1D ptAggregate;
      std::map<std::string, Histogram1D> ptByGroup;  // present only if the
                                                       // recipe was built
                                                       // with --pt_by_species
    };

    struct RecipeData {
      std::vector<RecipeEtaBin> etaBins;
      std::map<int, std::vector<RecipeDisplacementRow>> displacementRows;  // keyed by pdgId, sorted by pLo
      std::map<int, Histogram1D> displacementHists;                       // keyed by hist_idx
      Histogram1D allDisplacementHist;
      double nEventsNpz;
    };

    // Same 4 species groups + "other" fallback as build_pu_sampling_
    // recipe.py's / sample_pu_event.py's SPECIES_GROUPS / _group_for_pid --
    // must stay in lockstep with those for --pt_by_species histogram names
    // (pt_spectrum_eta{i}_{group}) to resolve correctly.
    std::string groupForPdgId(int pdgId) {
      if (pdgId == 22) {
        return "photon";
      }
      if (std::abs(pdgId) == 11) {
        return "electron";
      }
      if (std::abs(pdgId) == 13) {
        return "muon";
      }
      if (std::abs(pdgId) > 100) {
        return "hadron";
      }
      return "other";
    }

    // GetRandom()-equivalent inverse-CDF draw, built to go through the
    // framework's own CLHEP engine (see file header). Mirrors
    // sample_pu_event.py's draw_from_hist exactly: draw u ~ Uniform(0,1),
    // find the first bin whose cumulative content is >= u
    // (std::lower_bound, matching numpy.searchsorted's default 'left'
    // side), then a uniform position within that bin -- works unchanged
    // for variable-width bins (the pt spectrum's log-spaced tail, or the
    // log10(cm) displacement histograms).
    double drawFromHist(CLHEP::HepRandomEngine* engine, const Histogram1D& hist) {
      const double u = CLHEP::RandFlat::shoot(engine, 0., 1.);
      auto it = std::lower_bound(hist.cumulative.begin(), hist.cumulative.end(), u);
      const size_t idx =
          std::min(static_cast<size_t>(std::distance(hist.cumulative.begin(), it)), hist.edges.size() - 2);
      return CLHEP::RandFlat::shoot(engine, hist.edges[idx], hist.edges[idx + 1]);
    }

    // Reads one TH1D by name and converts it to a Histogram1D. Detaches
    // the histogram from the TFile's directory before taking ownership
    // (SetDirectory(nullptr)) so it can be safely destroyed independently
    // of the TFile's own lifetime/Close(). Returns a default-constructed
    // (empty) Histogram1D if the name doesn't exist in the file -- this is
    // EXPECTED for optional content (e.g. a per-species-group pt
    // histogram when the recipe wasn't built with --pt_by_species), not
    // an error; callers check hist.edges.empty() / hist.cumulative.empty().
    Histogram1D readCumulativeHistogram(TFile& file, const std::string& name) {
      TH1D* raw = nullptr;
      file.GetObject(name.c_str(), raw);
      if (raw == nullptr) {
        return {};
      }
      raw->SetDirectory(nullptr);
      std::unique_ptr<TH1D> hist(raw);

      const int nBins = hist->GetNbinsX();
      Histogram1D result;
      result.edges.reserve(nBins + 1);
      for (int i = 1; i <= nBins; ++i) {
        result.edges.push_back(hist->GetXaxis()->GetBinLowEdge(i));
      }
      result.edges.push_back(hist->GetXaxis()->GetBinUpEdge(nBins));

      double total = 0.;
      std::vector<double> counts(nBins);
      for (int i = 1; i <= nBins; ++i) {
        counts[i - 1] = hist->GetBinContent(i);
        total += counts[i - 1];
      }
      if (total > 0.) {
        result.cumulative.reserve(nBins);
        double running = 0.;
        for (double c : counts) {
          running += c;
          result.cumulative.push_back(running / total);
        }
      }
      return result;
    }

    // Loads everything build_pu_sampling_recipe.py writes: the density/
    // composition/pt-spectrum content per eta bin, and the displacement
    // TTree + its histograms. See that script's docstring for the file
    // layout this must stay in sync with. Called ONCE, from the
    // constructor -- never per event.
    RecipeData loadRecipe(const std::string& path) {
      std::unique_ptr<TFile> file(TFile::Open(path.c_str(), "READ"));
      if (!file || file->IsZombie()) {
        throw cms::Exception("DisplacedParticleGunProducerFlatEtaWithLocalPU")
            << "Could not open LocalPileup.RecipeFile '" << path << "'.";
      }

      RecipeData recipe;

      {
        TTreeReader reader("meta", file.get());
        TTreeReaderValue<Long64_t> nEventsNpz(reader, "n_events_npz");
        if (!reader.Next()) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEtaWithLocalPU")
              << "Recipe file '" << path << "' has an empty or missing 'meta' tree.";
        }
        recipe.nEventsNpz = static_cast<double>(*nEventsNpz);
      }

      std::vector<double> etaLoVec, etaHiVec, meanMultVec;
      {
        TTreeReader reader("density", file.get());
        TTreeReaderValue<Double_t> etaLo(reader, "eta_lo");
        TTreeReaderValue<Double_t> etaHi(reader, "eta_hi");
        TTreeReaderValue<Double_t> meanMult(reader, "mean_multiplicity");
        while (reader.Next()) {
          etaLoVec.push_back(*etaLo);
          etaHiVec.push_back(*etaHi);
          meanMultVec.push_back(*meanMult);
        }
      }
      if (etaLoVec.empty()) {
        throw cms::Exception("DisplacedParticleGunProducerFlatEtaWithLocalPU")
            << "Recipe file '" << path << "' has an empty 'density' tree -- nothing to sample.";
      }
      const int nBins = static_cast<int>(etaLoVec.size());
      recipe.etaBins.resize(nBins);
      for (int b = 0; b < nBins; ++b) {
        recipe.etaBins[b].etaLo = etaLoVec[b];
        recipe.etaBins[b].etaHi = etaHiVec[b];
        recipe.etaBins[b].meanMultiplicity = meanMultVec[b];
      }

      {
        std::vector<std::vector<std::pair<int, double>>> perBin(nBins);
        TTreeReader reader("composition", file.get());
        TTreeReaderValue<Int_t> etaBinIdx(reader, "eta_bin_idx");
        TTreeReaderValue<Int_t> pdgId(reader, "pdgId");
        TTreeReaderValue<Double_t> fraction(reader, "fraction");
        while (reader.Next()) {
          if (*etaBinIdx >= 0 && *etaBinIdx < nBins) {
            perBin[*etaBinIdx].emplace_back(*pdgId, *fraction);
          }
        }
        for (int b = 0; b < nBins; ++b) {
          double running = 0.;
          for (const auto& pidFrac : perBin[b]) {
            running += pidFrac.second;
            recipe.etaBins[b].compositionPdgId.push_back(pidFrac.first);
            recipe.etaBins[b].compositionCumulative.push_back(running);
          }
          if (running > 0.) {
            for (double& c : recipe.etaBins[b].compositionCumulative) {
              c /= running;
            }
          }
        }
      }

      static const std::vector<std::string> kGroups{"photon", "electron", "muon", "hadron", "other"};
      for (int b = 0; b < nBins; ++b) {
        recipe.etaBins[b].ptAggregate = readCumulativeHistogram(*file, "pt_spectrum_eta" + std::to_string(b));
        for (const auto& group : kGroups) {
          Histogram1D h = readCumulativeHistogram(*file, "pt_spectrum_eta" + std::to_string(b) + "_" + group);
          if (!h.edges.empty()) {
            recipe.etaBins[b].ptByGroup.emplace(group, std::move(h));
          }
        }
      }

      {
        TTreeReader reader("displacement", file.get());
        TTreeReaderValue<Int_t> pdgId(reader, "pdgId");
        TTreeReaderValue<Double_t> pLo(reader, "p_lo");
        TTreeReaderValue<Double_t> pHi(reader, "p_hi");
        TTreeReaderValue<Long64_t> nParticles(reader, "n_particles");
        TTreeReaderValue<Double_t> displacedFraction(reader, "displaced_fraction");
        TTreeReaderValue<Int_t> histIdx(reader, "hist_idx");
        while (reader.Next()) {
          recipe.displacementRows[*pdgId].push_back(
              RecipeDisplacementRow{*pLo, *pHi, *nParticles, *displacedFraction, *histIdx});
        }
        for (auto& pidRows : recipe.displacementRows) {
          std::sort(pidRows.second.begin(), pidRows.second.end(),
                    [](const RecipeDisplacementRow& a, const RecipeDisplacementRow& b) { return a.pLo < b.pLo; });
        }
      }

      recipe.allDisplacementHist = readCumulativeHistogram(*file, "displacement_log10cm_all");
      {
        std::set<int> histIndices;
        for (const auto& pidRows : recipe.displacementRows) {
          for (const auto& row : pidRows.second) {
            if (row.histIdx >= 0) {
              histIndices.insert(row.histIdx);
            }
          }
        }
        for (int idx : histIndices) {
          recipe.displacementHists[idx] = readCumulativeHistogram(*file, "displacement_log10cm_" + std::to_string(idx));
        }
      }

      return recipe;
    }

    // Antiderivative of the circle's chord width 2*sqrt(R^2-x^2); see
    // sample_local_pu_around_probe.py's _circle_segment_area for the
    // derivation -- this is the exact same closed-form circular-segment
    // integral, unchanged.
    double circleSegmentAntiderivative(double x, double radius) {
      x = std::clamp(x, -radius, radius);
      return x * std::sqrt(std::max(0., radius * radius - x * x)) +
             radius * radius * std::asin(std::clamp(x / radius, -1., 1.));
    }

    // Area of the intersection between a disk of the given radius
    // (centered at the origin of a SHIFTED coordinate, i.e. callers pass
    // lo/hi already offset by etaProbe) and the strip {lo <= x <= hi}.
    // lo/hi may be +-infinity (open-ended recipe bins): clips to 0
    // outside [-radius, radius] automatically, no special-casing needed.
    double circleSegmentArea(double lo, double hi, double radius) {
      const double hiClipped = std::min(hi, radius);
      const double loClipped = std::max(lo, -radius);
      if (hiClipped <= loClipped) {
        return 0.;
      }
      return circleSegmentAntiderivative(hiClipped, radius) - circleSegmentAntiderivative(loClipped, radius);
    }

    struct ConeBinWeight {
      int binIndex;
      double meanCount;
    };

    // C++ port of sample_local_pu_around_probe.py's cone_bin_weights: for
    // each FINITE recipe eta bin, its expected contribution (Poisson
    // mean, for one nPu-scaled event) to the cone -- areal_density * area
    // of that bin's eta-phi strip inside the disk. Non-finite (open-
    // ended) bins are skipped with a warning if the cone reaches them
    // (their areal density would be formally zero, an infinite-width
    // bin -- see build_pu_sampling_recipe.py's --geometric_cut, strongly
    // recommended for a recipe meant to feed this producer). coverage
    // (fraction of the disk's own area actually covered by some recipe
    // bin) is returned via the output parameter so callers can log it.
    std::vector<ConeBinWeight> coneBinWeights(const std::vector<RecipeEtaBin>& bins,
                                              double nPu,
                                              double etaProbe,
                                              double coneRadius,
                                              double& coverage) {
      std::vector<ConeBinWeight> weights;
      double coveredArea = 0.;
      for (size_t b = 0; b < bins.size(); ++b) {
        const double lo = bins[b].etaLo;
        const double hi = bins[b].etaHi;
        const double areaB = circleSegmentArea(lo - etaProbe, hi - etaProbe, coneRadius);
        if (!std::isfinite(lo) || !std::isfinite(hi)) {
          if (areaB > 0.) {
            edm::LogWarning("DisplacedParticleGunProducerFlatEtaWithLocalPU")
                << "cone overlaps a non-finite recipe eta bin [" << lo << ", " << hi
                << "] -- its local areal density is not defined (infinite bin width); this bin's contribution to "
                   "the cone is SKIPPED, which underestimates the local density there. Build the recipe with "
                   "build_pu_sampling_recipe.py's --geometric_cut and/or use a smaller LocalPileup.ConeRadius / "
                   "keep the probe further from the recipe's own eta acceptance edge to avoid this.";
          }
          continue;
        }
        if (areaB <= 0.) {
          continue;
        }
        coveredArea += areaB;
        const double rho = bins[b].meanMultiplicity / ((hi - lo) * 2. * std::numbers::pi);
        weights.push_back({static_cast<int>(b), nPu * rho * areaB});
      }
      const double diskArea = std::numbers::pi * coneRadius * coneRadius;
      coverage = diskArea > 0. ? coveredArea / diskArea : 0.;
      return weights;
    }

    std::optional<int> drawSpecies(CLHEP::HepRandomEngine* engine, const RecipeEtaBin& bin) {
      if (bin.compositionPdgId.empty()) {
        return std::nullopt;
      }
      const double u = CLHEP::RandFlat::shoot(engine, 0., 1.);
      auto it = std::lower_bound(bin.compositionCumulative.begin(), bin.compositionCumulative.end(), u);
      const size_t idx = std::min(static_cast<size_t>(std::distance(bin.compositionCumulative.begin(), it)),
                                  bin.compositionPdgId.size() - 1);
      return bin.compositionPdgId[idx];
    }

    // Draws a pt for a particle of species pdgId in this eta bin. Uses
    // the species group's own pt histogram if the recipe has one
    // (--pt_by_species) and it isn't empty; otherwise falls back to the
    // bin's aggregated-over-species histogram -- exactly draw_pt's own
    // fallback logic in sample_pu_event.py.
    std::optional<double> drawPt(CLHEP::HepRandomEngine* engine, const RecipeEtaBin& bin, int pdgId) {
      const auto groupIt = bin.ptByGroup.find(groupForPdgId(pdgId));
      if (groupIt != bin.ptByGroup.end() && !groupIt->second.cumulative.empty()) {
        return drawFromHist(engine, groupIt->second);
      }
      if (bin.ptAggregate.cumulative.empty()) {
        return std::nullopt;
      }
      return drawFromHist(engine, bin.ptAggregate);
    }

    // 3D displacement distance [cm] for one particle (0. if not
    // displaced), following exactly build_pu_sampling_recipe.py's GUN-
    // SIDE RECIPE: look up the (species, momentum) row, roll the
    // displaced-fraction Bernoulli draw, and if displaced draw
    // pow(10, drawFromHist(...)) on that row's histogram (or the all-
    // species fallback if hist_idx == -1). A species entirely absent
    // from the recipe's displacement content (e.g. neutrinos, which
    // build_pu_sampling_recipe.py's SPECIES_GROUPS never assigns a
    // displacement row to) is always treated as prompt.
    double drawDisplacement(CLHEP::HepRandomEngine* engine, int pdgId, double p, const RecipeData& recipe) {
      const auto rowsIt = recipe.displacementRows.find(pdgId);
      if (rowsIt == recipe.displacementRows.end() || rowsIt->second.empty()) {
        return 0.;
      }
      const auto& rows = rowsIt->second;
      const RecipeDisplacementRow* row = &rows.back();
      for (const auto& r : rows) {
        if (r.pLo <= p && p < r.pHi) {
          row = &r;
          break;
        }
      }
      if (CLHEP::RandFlat::shoot(engine, 0., 1.) >= row->displacedFraction) {
        return 0.;
      }
      const Histogram1D& hist =
          row->histIdx >= 0 ? recipe.displacementHists.at(row->histIdx) : recipe.allDisplacementHist;
      if (hist.cumulative.empty()) {
        return 0.;
      }
      return std::pow(10., drawFromHist(engine, hist));
    }

    double wrapPhi(double phi) {
      return std::remainder(phi, 2. * std::numbers::pi);
    }

    // Everything needed to configure the local-pileup sampling, parallel
    // to ParticleGunParameters above but for the recipe-driven part.
    struct LocalPileupParameters {
      explicit LocalPileupParameters(const ParameterSet& pset)
          : recipeFile(pset.getParameter<std::string>("RecipeFile")),
            coneRadius(pset.getParameter<double>("ConeRadius")),
            nPu(pset.getParameter<double>("NPu")),
            fluctuate(pset.getParameter<bool>("Fluctuate")),
            maxConeSamplingAttempts(pset.getParameter<unsigned int>("MaxConeSamplingAttempts")) {
        if (coneRadius <= 0.) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEtaWithLocalPU")
              << "LocalPileup.ConeRadius must be positive.";
        }
        if (nPu < 0.) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEtaWithLocalPU") << "LocalPileup.NPu must be nonnegative.";
        }
        if (maxConeSamplingAttempts == 0) {
          throw cms::Exception("DisplacedParticleGunProducerFlatEtaWithLocalPU")
              << "LocalPileup.MaxConeSamplingAttempts must be greater than zero.";
        }
      }

      std::string recipeFile;
      double coneRadius;
      double nPu;
      bool fluctuate;
      unsigned int maxConeSamplingAttempts;
    };

  }  // namespace

  class DisplacedParticleGunProducerFlatEtaWithLocalPU : public edm::global::EDProducer<> {
  public:
    explicit DisplacedParticleGunProducerFlatEtaWithLocalPU(const ParameterSet&);
    ~DisplacedParticleGunProducerFlatEtaWithLocalPU() override = default;

    static void fillDescriptions(ConfigurationDescriptions& descriptions);

  private:
    void produce(edm::StreamID, edm::Event& event, const edm::EventSetup& setup) const override;

    // Samples the local-PU particles for one event's cone (around the
    // given probe direction) and appends them to genEvent, continuing the
    // barcode numbering from nextBarcode. Returns the number of barcodes
    // used (i.e. the number of particles actually appended -- a particle
    // whose species/pt draw failed, see drawSpecies/drawPt's std::nullopt
    // returns, is silently skipped, exactly as sample_one_event.py's
    // Python prototype does with its own `continue`).
    int addLocalPileup(CLHEP::HepRandomEngine* engine,
                        const HepPDT::ParticleDataTable& pdgTable,
                        double etaProbe,
                        double phiProbe,
                        HepMC::GenEvent& genEvent,
                        int nextBarcode) const;

    const ProducerParameters fParameters;
    const LocalPileupParameters fLocalPileupParameters;
    const RecipeData fRecipe;
    const ESGetToken<HepPDT::ParticleDataTable, edm::DefaultRecord> fPDGTableToken;
  };

  DisplacedParticleGunProducerFlatEtaWithLocalPU::DisplacedParticleGunProducerFlatEtaWithLocalPU(
      const ParameterSet& pset)
      : fParameters(pset),
        fLocalPileupParameters(pset.getParameter<ParameterSet>("LocalPileup")),
        fRecipe(loadRecipe(fLocalPileupParameters.recipeFile)),
        fPDGTableToken(esConsumes<>()) {
    Service<RandomNumberGenerator> rng;
    if (!rng.isAvailable()) {
      throw cms::Exception("Configuration")
          << "The RandomNumberProducer module requires the RandomNumberGeneratorService\n"
             "which appears to be absent.  Please add that service to your configuration\n"
             "or remove the modules that require it.";
    }

    edm::LogInfo("DisplacedParticleGunProducerFlatEtaWithLocalPU")
        << "Loaded recipe '" << fLocalPileupParameters.recipeFile << "': " << fRecipe.etaBins.size()
        << " eta bin(s), " << fRecipe.displacementRows.size() << " species with displacement information, "
        << "n_events_npz=" << fRecipe.nEventsNpz << ".";

    produces<HepMCProduct>("unsmeared");
    produces<GenEventInfoProduct>();

    // Companion, index-aligned tagging products -- NOT part of the
    // HepMCProduct/GenEvent itself (HepMC::GenParticle has no spare field
    // to carry this, and repurposing status or genPartIdxMother would
    // either break GEANT4 simulation downstream or fabricate a false
    // mother-daughter relationship -- see the discussion this was added
    // for). "particleOrigin": 0 = probe, 1 = local PU, one entry per
    // particle IN THE SAME ORDER they are added to the GenEvent (i.e.
    // ascending barcode: probe(s) first, local PU appended after -- see
    // produce()). "particleBarcode": that same particle's actual HepMC
    // barcode, redundant with array index here (barcode == index + 1
    // always, since both start at 1 and every particle added gets the
    // next consecutive barcode with none skipped) but kept as an explicit,
    // self-describing value rather than an implicit contract, in case a
    // downstream step ever needs to re-associate by barcode instead of
    // position (e.g. if some later step reorders or filters particles).
    // Consumers (e.g. a small ValueMap producer feeding NanoAOD's
    // genParticleTable as an ExtVariable) are responsible for matching
    // this vector's ordering to whatever GenParticle collection they
    // build from "unsmeared" -- that collection must preserve HepMC's own
    // particle order/barcodes for a direct index (or barcode) match to
    // stay valid.
    produces<std::vector<int>>("particleOrigin");
    produces<std::vector<int>>("particleBarcode");
  }

  void DisplacedParticleGunProducerFlatEtaWithLocalPU::fillDescriptions(ConfigurationDescriptions& descriptions) {
    edm::ParameterSetDescription desc;
    edm::ParameterSetDescription pgun;

    edm::ParameterSetDescription magnitude;
    magnitude.add<std::string>("Variable");
    magnitude.add<double>("Min");
    magnitude.add<double>("Max");

    edm::ParameterSetDescription direction;
    direction.add<double>("ThetaMin");
    direction.add<double>("ThetaMax");
    direction.add<double>("PhiMin");
    direction.add<double>("PhiMax");

    edm::ParameterSetDescription momentum;
    momentum.add<edm::ParameterSetDescription>("Magnitude", magnitude);
    momentum.add<edm::ParameterSetDescription>("Direction", direction);

    edm::ParameterSetDescription origin;
    origin.add<double>("RMin");
    origin.add<double>("RMax");
    origin.add<double>("PhiMin");
    origin.add<double>("PhiMax");

    edm::ParameterSetDescription production;
    production.add<double>("Z");
    production.add<double>("RMin");
    production.add<double>("RMax");
    production.add<double>("PhiMin");
    production.add<double>("PhiMax");

    edm::ParameterSetDescription target;
    target.add<double>("Z");
    target.add<double>("RMin");
    target.add<double>("RMax");
    target.add<double>("PhiMin");
    target.add<double>("PhiMax");

    edm::ParameterSetDescription geometry;
    geometry.add<std::string>("RadialDistribution");
    geometry.add<edm::ParameterSetDescription>("Origin", origin);
    geometry.add<edm::ParameterSetDescription>("Production", production);
    geometry.addOptional<edm::ParameterSetDescription>("Target", target);

    pgun.add<int>("PartID");
    pgun.add<int>("NParticles");
    pgun.add<edm::ParameterSetDescription>("Momentum", momentum);
    pgun.add<edm::ParameterSetDescription>("Geometry", geometry);
    pgun.add<unsigned int>("MaxSamplingAttempts");

    desc.add<edm::ParameterSetDescription>("PGunParameters", pgun);

    edm::ParameterSetDescription localPileup;
    localPileup.add<std::string>("RecipeFile");
    localPileup.add<double>("ConeRadius");
    localPileup.add<double>("NPu");
    localPileup.add<bool>("Fluctuate");
    localPileup.add<unsigned int>("MaxConeSamplingAttempts");
    desc.add<edm::ParameterSetDescription>("LocalPileup", localPileup);

    desc.addUntracked<int>("Verbosity", 0);

    descriptions.add("DisplacedParticleGunProducerFlatEtaWithLocalPU", desc);
  }

  int DisplacedParticleGunProducerFlatEtaWithLocalPU::addLocalPileup(CLHEP::HepRandomEngine* engine,
                                                                      const HepPDT::ParticleDataTable& pdgTable,
                                                                      double etaProbe,
                                                                      double phiProbe,
                                                                      HepMC::GenEvent& genEvent,
                                                                      int nextBarcode) const {
    const auto& lp = fLocalPileupParameters;

    // n_pu_this_event: mirrors sample_local_pu_around_probe.py's
    // sample_local_pu_around_probe -- Fluctuate controls only this ONE
    // overall draw (an event-to-event varying number of "as-if"
    // interactions), never the per-bin Poisson counts below, which are
    // ALWAYS drawn regardless of Fluctuate.
    const double nPuThisEvent =
        lp.fluctuate ? static_cast<double>(CLHEP::RandPoissonQ::shoot(engine, lp.nPu)) : lp.nPu;

    double coverage = 0.;
    const std::vector<ConeBinWeight> binWeights =
        coneBinWeights(fRecipe.etaBins, nPuThisEvent, etaProbe, lp.coneRadius, coverage);
    if (fParameters.verbosity > 0) {
      edm::LogInfo("DisplacedParticleGunProducerFlatEtaWithLocalPU")
          << "Local PU cone around (eta=" << etaProbe << ", phi=" << phiProbe << "), R=" << lp.coneRadius
          << ": recipe coverage of the cone's own area = " << 100. * coverage << "%"
          << (coverage > 0.999 ? "" : " (probe close to the recipe's own eta acceptance edge -- see any warning above)");
    }

    int barcode = nextBarcode;
    for (const auto& bw : binWeights) {
      const RecipeEtaBin& bin = fRecipe.etaBins[bw.binIndex];
      const double boxEtaLo = std::max(bin.etaLo - etaProbe, -lp.coneRadius);
      const double boxEtaHi = std::min(bin.etaHi - etaProbe, lp.coneRadius);

      const int nInBin = static_cast<int>(CLHEP::RandPoissonQ::shoot(engine, bw.meanCount));
      for (int i = 0; i < nInBin; ++i) {
        // Uniform-in-area rejection sampling within (bin strip) ∩ (disk)
        // -- the SAME kUniformArea convention Section 1's sampleRadius
        // uses for the probe's own Origin plane, applied here in
        // (eta, phi) instead of (x, y). See sample_local_pu_around_probe.
        // py's sample_local_pu_around_probe docstring.
        double dEta = 0.5 * (boxEtaLo + boxEtaHi);
        double dPhi = 0.;
        for (unsigned int attempt = 0; attempt < lp.maxConeSamplingAttempts; ++attempt) {
          dEta = CLHEP::RandFlat::shoot(engine, boxEtaLo, boxEtaHi);
          dPhi = CLHEP::RandFlat::shoot(engine, -lp.coneRadius, lp.coneRadius);
          if (dEta * dEta + dPhi * dPhi <= lp.coneRadius * lp.coneRadius) {
            break;
          }
        }
        const double eta = etaProbe + dEta;
        const double phi = wrapPhi(phiProbe + dPhi);

        const auto pdgId = drawSpecies(engine, bin);
        if (!pdgId) {
          continue;
        }
        const auto pt = drawPt(engine, bin, *pdgId);
        if (!pt) {
          continue;
        }

        const HepPDT::ParticleData* particleData = pdgTable.particle(HepPDT::ParticleID(std::abs(*pdgId)));
        if (particleData == nullptr) {
          edm::LogWarning("DisplacedParticleGunProducerFlatEtaWithLocalPU")
              << "pdgId " << *pdgId << " drawn from the recipe is not in the PDG table -- skipping this particle.";
          continue;
        }
        const double mass = particleData->mass().value();

        const double px = *pt * std::cos(phi);
        const double py = *pt * std::sin(phi);
        const double pz = *pt * std::sinh(eta);
        const double p = std::sqrt(px * px + py * py + pz * pz);
        const double energy = std::sqrt(p * p + mass * mass);

        const double d3d = drawDisplacement(engine, *pdgId, p, fRecipe);

        // Un-smeared-frame vertex, anchored at the coordinate ORIGIN
        // (i.e. the true primary-vertex region), exactly
        // build_pu_sampling_recipe.py's GUN-SIDE RECIPE pseudocode:
        // prompt particles at the origin, displaced ones at d*p_hat with
        // ct=d. Deliberately NOT anchored on the probe's own vertex: real
        // pileup originates from independent minimum-bias collisions at
        // the actual primary vertex/beamspot, not at wherever this
        // particular probe happens to be injected (e.g. Production.Z
        // sitting at the HGCAL face for a genuinely displaced decay, or
        // as a deliberate injection point) -- these PU particles need to
        // traverse the same upstream material (tracker, ECAL, B field)
        // as any other pileup particle would, which is exactly what full
        // GEANT4 simulation downstream of this producer does correctly
        // when given an origin-region GEN vertex. VtxSmeared (kept
        // downstream in the sequence, unchanged) then shifts this
        // particle's vertex -- and the probe's own, and every other local
        // PU particle's -- together by one common beamspot draw.
        double vx = 0., vy = 0., vz = 0., ct = 0.;
        if (d3d > 0.) {
          vx = d3d * px / p;
          vy = d3d * py / p;
          vz = d3d * pz / p;
          ct = d3d;
        }

        const ResolvedParticle particle{*pdgId, {px, py, pz, energy}, {vx, vy, vz, ct * CLHEP::cm}};
        appendParticleToGenEvent(genEvent, particle, barcode, fParameters.verbosity > 0);
        ++barcode;
      }
    }
    return barcode - nextBarcode;
  }

  void DisplacedParticleGunProducerFlatEtaWithLocalPU::produce(edm::StreamID,
                                                               edm::Event& event,
                                                               const edm::EventSetup& setup) const {
    if (fParameters.verbosity > 0) {
      LogDebug("DisplacedParticleGunProducerFlatEtaWithLocalPU")
          << " DisplacedParticleGunProducerFlatEtaWithLocalPU : Begin New Event Generation" << std::endl;
    }

    const auto& particleGun = fParameters.particleGun;
    edm::Service<edm::RandomNumberGenerator> rng;
    CLHEP::HepRandomEngine* randomEngine = &rng->getEngine(event.streamID());

    auto const& pdgTable = setup.getData(fPDGTableToken);
    const HepPDT::ParticleData* pData = pdgTable.particle(HepPDT::ParticleID(std::abs(particleGun.partId)));
    if (!pData) {
      throw cms::Exception("DisplacedParticleGunProducerFlatEtaWithLocalPU")
          << "Particle ID " << particleGun.partId << " not found in PDG table";
    }

    const double mass = pData->mass().value();
    validateParticleCompatibility(particleGun, mass, pData->charge());

    HepMC::GenEvent* genEvent = new HepMC::GenEvent();
    genEvent->set_event_number(event.id().event());
    genEvent->set_signal_process_id(20);

    // --- probe particle(s), unchanged from DisplacedParticleGunProducer
    // FlatEta -- see Section 1. The cone below is centered on the FIRST
    // probe particle's own sampled direction; NParticles > 1 is still
    // allowed (e.g. to shoot several independent probes with no shared
    // local PU), but the local-PU cone's geometry model assumes one
    // probe direction, so with NParticles > 1 only particle #1 anchors
    // the cone -- document this in your config if you rely on it. ---
    double etaProbe = 0.;
    double phiProbe = 0.;
    const int nProbeParticles = particleGun.nParticles;
    int barcode = 1;
    for (int particleIndex = 0; particleIndex < particleGun.nParticles; ++particleIndex) {
      const ResolvedParticle particle = resolveParticle(randomEngine, particleGun, mass);
      appendParticleToGenEvent(*genEvent, particle, barcode, fParameters.verbosity > 0);
      if (particleIndex == 0) {
        const auto& mom = particle.momentum;
        const double p = std::hypot(mom.px, mom.py, mom.pz);
        etaProbe = std::atanh(mom.pz / p);  // pz > 0 always: ThetaMax < pi/2 is enforced by DirectionParameters
        phiProbe = std::atan2(mom.py, mom.px);
      }
      ++barcode;
    }

    // --- local pileup, NEW: sampled from the recipe within a cone around
    // the probe's own direction, and appended to the SAME genEvent. Note
    // this is anchored at the origin/primary-vertex region, NOT the
    // probe's own (possibly displaced) vertex -- see addLocalPileup. ---
    const int nLocalPileup = addLocalPileup(randomEngine, pdgTable, etaProbe, phiProbe, *genEvent, barcode);
    if (fParameters.verbosity > 0) {
      edm::LogInfo("DisplacedParticleGunProducerFlatEtaWithLocalPU")
          << "Appended " << nLocalPileup << " local PU particle(s) around the probe.";
    }

    // --- particleOrigin/particleBarcode: see the produces<>() calls in
    // the constructor for what these mean and why they exist. Built
    // purely arithmetically from the barcode ranges above -- no need to
    // touch addLocalPileup's own loop -- since every particle from
    // barcode 1 gets the next consecutive integer with none skipped:
    // barcodes [1, nProbeParticles] are the probe(s), everything from
    // [nProbeParticles+1, nProbeParticles+nLocalPileup] is local PU.
    auto particleOrigin = std::make_unique<std::vector<int>>(nProbeParticles, 0);
    particleOrigin->resize(nProbeParticles + nLocalPileup, 1);
    auto particleBarcode = std::make_unique<std::vector<int>>(nProbeParticles + nLocalPileup);
    std::iota(particleBarcode->begin(), particleBarcode->end(), 1);
    event.put(std::move(particleOrigin), "particleOrigin");
    event.put(std::move(particleBarcode), "particleBarcode");

    if (fParameters.verbosity > 0) {
      genEvent->print();
    }

    auto hepMcProduct = std::make_unique<HepMCProduct>();
    hepMcProduct->addHepMCData(genEvent);
    event.put(std::move(hepMcProduct), "unsmeared");

    auto genEventInfo = std::make_unique<GenEventInfoProduct>(genEvent);
    event.put(std::move(genEventInfo));

    if (fParameters.verbosity > 0) {
      LogDebug("DisplacedParticleGunProducerFlatEtaWithLocalPU")
          << " DisplacedParticleGunProducerFlatEtaWithLocalPU : Event Generation Done. " << std::endl;
    }
  }

}  // namespace edm

#include "FWCore/Framework/interface/MakerMacros.h"
using edm::DisplacedParticleGunProducerFlatEtaWithLocalPU;
DEFINE_FWK_MODULE(DisplacedParticleGunProducerFlatEtaWithLocalPU);