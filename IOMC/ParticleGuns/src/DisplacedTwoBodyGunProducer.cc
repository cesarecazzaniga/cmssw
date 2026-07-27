// DisplacedTwoBodyGunProducer
//
// Generates TWO particles (default: photons) from a single shared,
// displaced production vertex, with a controlled opening angle between
// them -- e.g. a neutral LLP decaying to two photons at a displaced
// vertex.
//
// Deliberately implemented as a fully separate, self-contained file
// rather than sharing code with DisplacedParticleGunProducer.cc (whose
// geometry-sampling helpers currently live in an anonymous namespace,
// i.e. not directly reusable across translation units without a header
// refactor) -- this avoids any risk of destabilizing the existing,
// already-validated single-particle producer. The geometry/reachability
// machinery below is copied and adapted from that file; see it for the
// original, single-particle version this is based on.
//
// KEY DESIGN POINT: Momentum.Direction.{ThetaMin,ThetaMax,PhiMin,PhiMax}
// constrains only the REFERENCE particle's direction (particle 1). The
// companion particle's direction (particle 2) is DERIVED by rotating the
// reference direction by a sampled opening angle around a random
// azimuthal axis -- it is not independently re-checked against the same
// theta/phi window, only against geometric reachability (does its own
// resulting trajectory actually hit the configured Production/Target
// caps from the shared vertex). Re-imposing the reference window on a
// derived direction would be physically meaningless.

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <CLHEP/Random/RandFlat.h>
#include <CLHEP/Units/SystemOfUnits.h>

#include "HepMC/GenEvent.h"

#include "FWCore/AbstractServices/interface/RandomNumberGenerator.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/global/EDProducer.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/ParameterSet/interface/ConfigurationDescriptions.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/ParameterSet/interface/ParameterSetDescription.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "SimDataFormats/GeneratorProducts/interface/GenEventInfoProduct.h"
#include "SimDataFormats/GeneratorProducts/interface/HepMCProduct.h"
#include "SimGeneral/HepPDTRecord/interface/ParticleDataTable.h"

namespace edm {

  namespace {

    // ------------------------------------------------------------------
    // Geometry/reachability machinery: copied and adapted from
    // DisplacedParticleGunProducer.cc. Unchanged in behavior from that
    // file except where noted.
    // ------------------------------------------------------------------

    enum class MagnitudeVariable { kEnergy, kPt };
    enum class RadialDistribution { kUniformArea, kUniformRadius };

    MagnitudeVariable readMagnitudeVariable(const std::string& value) {
      if (value == "energy") {
        return MagnitudeVariable::kEnergy;
      }
      if (value == "pt") {
        return MagnitudeVariable::kPt;
      }
      throw cms::Exception("DisplacedTwoBodyGunProducer")
          << "Momentum.Magnitude.Variable must be either 'energy' or 'pt', but is '" << value << "'.";
    }

    RadialDistribution readRadialDistribution(const std::string& value) {
      if (value == "uniformArea") {
        return RadialDistribution::kUniformArea;
      }
      if (value == "uniformRadius") {
        return RadialDistribution::kUniformRadius;
      }
      throw cms::Exception("DisplacedTwoBodyGunProducer")
          << "Geometry.RadialDistribution must be either 'uniformArea' or 'uniformRadius', but is '" << value << "'.";
    }

    struct MagnitudeParameters {
      explicit MagnitudeParameters(const ParameterSet& pset)
          : variable(readMagnitudeVariable(pset.getParameter<std::string>("Variable"))),
            min(pset.getParameter<double>("Min")),
            max(pset.getParameter<double>("Max")) {
        if (max <= min) {
          throw cms::Exception("DisplacedTwoBodyGunProducer") << "Please fix Momentum.Magnitude.Min/Max.";
        }
        if (min <= 0.) {
          throw cms::Exception("DisplacedTwoBodyGunProducer") << "Momentum.Magnitude.Min must be positive.";
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
          throw cms::Exception("DisplacedTwoBodyGunProducer") << "Please fix Momentum.Direction.ThetaMin/ThetaMax.";
        }
        if (phiMax <= phiMin) {
          throw cms::Exception("DisplacedTwoBodyGunProducer") << "Please fix Momentum.Direction.PhiMin/PhiMax.";
        }
        if (thetaMin < 0. || thetaMax >= std::numbers::pi / 2.) {
          throw cms::Exception("DisplacedTwoBodyGunProducer")
              << "Momentum.Direction theta bounds must lie inside [0, pi/2).";
        }
        if (phiMin < -std::numbers::pi || phiMax > std::numbers::pi) {
          throw cms::Exception("DisplacedTwoBodyGunProducer")
              << "Momentum.Direction phi bounds must lie inside [-pi, pi].";
        }
      }

      double thetaMin;
      double thetaMax;
      double phiMin;
      double phiMax;
    };

    // NEW: opening angle between the two daughters, sampled flat.
    struct OpeningAngleParameters {
      explicit OpeningAngleParameters(const ParameterSet& pset)
          : min(pset.getParameter<double>("Min")), max(pset.getParameter<double>("Max")) {
        if (max <= min) {
          throw cms::Exception("DisplacedTwoBodyGunProducer") << "Please fix OpeningAngle.Min/Max.";
        }
        if (min < 0.) {
          throw cms::Exception("DisplacedTwoBodyGunProducer") << "OpeningAngle.Min must be nonnegative.";
        }
        if (max >= std::numbers::pi) {
          throw cms::Exception("DisplacedTwoBodyGunProducer") << "OpeningAngle.Max must be less than pi.";
        }
      }

      double min;
      double max;
    };

    struct MomentumParameters {
      explicit MomentumParameters(const ParameterSet& pset)
          : magnitude(pset.getParameter<ParameterSet>("Magnitude")),
            direction(pset.getParameter<ParameterSet>("Direction")),
            equalEnergy(pset.getParameter<bool>("EqualEnergy")) {}

      MagnitudeParameters magnitude;
      DirectionParameters direction;
      // If true, both daughters share ONE sampled magnitude (equal
      // energies) -- e.g. for testing the case where the summed diphoton
      // momentum stays close to the reference photon's direction. If
      // false (default), each daughter's magnitude is sampled
      // independently from the same Min/Max range -- for testing
      // clustering behavior under energy-asymmetric configurations. Both
      // modes use the SAME, independently-controlled OpeningAngle --
      // this toggle only affects energy sharing, not the angle.
      bool equalEnergy;
    };

    struct PlaneParameters {
      PlaneParameters(const ParameterSet& pset, double planeZ, const char* name)
          : z(planeZ),
            rMin(pset.getParameter<double>("RMin")),
            rMax(pset.getParameter<double>("RMax")),
            phiMin(pset.getParameter<double>("PhiMin")),
            phiMax(pset.getParameter<double>("PhiMax")) {
        if (rMax <= rMin) {
          throw cms::Exception("DisplacedTwoBodyGunProducer") << "Please fix Geometry." << name << ".RMin/RMax.";
        }
        if (rMin < 0.) {
          throw cms::Exception("DisplacedTwoBodyGunProducer") << "Geometry." << name << ".RMin must be nonnegative.";
        }
        if (phiMax <= phiMin) {
          throw cms::Exception("DisplacedTwoBodyGunProducer") << "Please fix Geometry." << name << ".PhiMin/PhiMax.";
        }
        if (phiMin < -std::numbers::pi || phiMax > std::numbers::pi) {
          throw cms::Exception("DisplacedTwoBodyGunProducer")
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
          throw cms::Exception("DisplacedTwoBodyGunProducer") << "Geometry.Production.Z must be greater than zero.";
        }
        if (target && target->z <= production.z) {
          throw cms::Exception("DisplacedTwoBodyGunProducer")
              << "Geometry.Target.Z must be greater than Geometry.Production.Z.";
        }
      }

      RadialDistribution radialDistribution;
      PlaneParameters origin;
      PlaneParameters production;
      std::optional<PlaneParameters> target;
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

    // NOTE: the interval-intersection/quadratic-root machinery from
    // DisplacedParticleGunProducer.cc (used there to sample theta given
    // BOTH Production and Target cap constraints simultaneously) is not
    // reused here. That machinery matters when you need to sample a
    // direction that is constrained by two caps at once. Here, each
    // daughter's direction is either sampled freely (particle 1, within
    // its own theta/phi window) or fully DERIVED via rotation (particle
    // 2) -- in both cases we only need to check whether one already-
    // determined direction's resulting trajectory hits the caps, not
    // solve for which directions would. That's a much simpler
    // "compute and check" rather than "solve for the allowed range".

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

    bool isTrajectoryReachable(const Point& originPoint, double theta, double phi, const GeometryParameters& geometry) {
      const Point productionPoint = projectToZ(originPoint, geometry.production.z, theta, phi);
      if (!doesCapContain(productionPoint, geometry.production)) {
        return false;
      }
      if (geometry.target) {
        const Point targetPoint = projectToZ(originPoint, geometry.target->z, theta, phi);
        if (!doesCapContain(targetPoint, *geometry.target)) {
          return false;
        }
      }
      return true;
    }

    double sampleRadius(CLHEP::HepRandomEngine* engine, const PlaneParameters& plane, RadialDistribution distribution) {
      if (distribution == RadialDistribution::kUniformArea) {
        return std::sqrt(CLHEP::RandFlat::shoot(engine, plane.rMin * plane.rMin, plane.rMax * plane.rMax));
      }
      return CLHEP::RandFlat::shoot(engine, plane.rMin, plane.rMax);
    }

    /**
     * Same physical model as DisplacedParticleGunProducer's
     * resolveTimeOfFlight(): straight-line flight at c from the beam spot
     * to the shared vertex, then straight-line flight at beta=|p|/E from
     * the shared vertex to each daughter's own production point. Only
     * valid for uncharged particles (same restriction as the original).
     */
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

    // ------------------------------------------------------------------
    // NEW: two-body-specific logic.
    // ------------------------------------------------------------------

    struct ParticleGunParameters {
      explicit ParticleGunParameters(const ParameterSet& pset)
          : partId(pset.getParameter<int>("PartID")),
            momentum(pset.getParameter<ParameterSet>("Momentum")),
            openingAngle(pset.getParameter<ParameterSet>("OpeningAngle")),
            geometry(pset.getParameter<ParameterSet>("Geometry")),
            maxSamplingAttempts(pset.getParameter<unsigned int>("MaxSamplingAttempts")) {
        if (maxSamplingAttempts == 0) {
          throw cms::Exception("DisplacedTwoBodyGunProducer") << "MaxSamplingAttempts must be greater than zero.";
        }
        if (momentum.magnitude.variable == MagnitudeVariable::kPt && momentum.direction.thetaMin == 0.) {
          throw cms::Exception("DisplacedTwoBodyGunProducer")
              << "Momentum.Direction.ThetaMin must be greater than zero when Momentum.Magnitude.Variable is 'pt'.";
        }
      }

      int partId;
      MomentumParameters momentum;
      OpeningAngleParameters openingAngle;
      GeometryParameters geometry;
      unsigned int maxSamplingAttempts;
    };

    struct ProducerParameters {
      explicit ProducerParameters(const ParameterSet& pset)
          : particleGun(pset.getParameter<ParameterSet>("PGunParameters")),
            verbosity(pset.getUntrackedParameter<int>("Verbosity", 0)) {}

      ParticleGunParameters particleGun;
      int verbosity;
    };

    // Unit direction vector for a given (theta, phi), CMS convention
    // (theta from +z axis).
    struct UnitVector {
      double x, y, z;
    };

    UnitVector directionToUnitVector(double theta, double phi) {
      return {std::sin(theta) * std::cos(phi), std::sin(theta) * std::sin(phi), std::cos(theta)};
    }

    void unitVectorToDirection(const UnitVector& v, double& theta, double& phi) {
      theta = std::acos(std::clamp(v.z, -1., 1.));
      phi = std::atan2(v.y, v.x);
    }

    /**
     * Rotates unit vector v1 by angle alpha away from itself, choosing the
     * azimuthal orientation of the rotation uniformly at random around v1
     * (i.e. the companion direction is uniformly distributed on the cone
     * of half-angle alpha around v1). This is an exact 3D rotation (valid
     * for any alpha < pi), not a small-angle (dEta, dPhi) approximation.
     *
     * Construction (standard Rodrigues-style approach): build an
     * orthonormal basis (perp1, perp2) spanning the plane perpendicular to
     * v1, pick a uniformly random azimuth psi in that plane, and combine:
     *   v2 = cos(alpha)*v1 + sin(alpha)*(cos(psi)*perp1 + sin(psi)*perp2)
     */
    UnitVector rotateByOpeningAngle(CLHEP::HepRandomEngine* engine, const UnitVector& v1, double alpha) {
      // Pick a reference axis not (nearly) parallel to v1, to avoid a
      // degenerate cross product.
      UnitVector reference = (std::abs(v1.z) < 0.9) ? UnitVector{0., 0., 1.} : UnitVector{1., 0., 0.};

      // perp1 = normalize(v1 x reference)
      double px = v1.y * reference.z - v1.z * reference.y;
      double py = v1.z * reference.x - v1.x * reference.z;
      double pz = v1.x * reference.y - v1.y * reference.x;
      const double pNorm = std::hypot(px, py, pz);
      px /= pNorm;
      py /= pNorm;
      pz /= pNorm;
      const UnitVector perp1{px, py, pz};

      // perp2 = v1 x perp1 (already unit length, since v1 and perp1 are
      // orthonormal)
      const UnitVector perp2{v1.y * perp1.z - v1.z * perp1.y,
                             v1.z * perp1.x - v1.x * perp1.z,
                             v1.x * perp1.y - v1.y * perp1.x};

      const double psi = CLHEP::RandFlat::shoot(engine, -std::numbers::pi, std::numbers::pi);
      const double cosA = std::cos(alpha);
      const double sinA = std::sin(alpha);
      const double cosPsi = std::cos(psi);
      const double sinPsi = std::sin(psi);

      return {cosA * v1.x + sinA * (cosPsi * perp1.x + sinPsi * perp2.x),
              cosA * v1.y + sinA * (cosPsi * perp1.y + sinPsi * perp2.y),
              cosA * v1.z + sinA * (cosPsi * perp1.z + sinPsi * perp2.z)};
    }

    FourMomentum computeMomentum(const MagnitudeParameters& magnitude, double theta, double phi, double sampledMagnitude,
                                 double mass) {
      double pt, pz, energy;
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
      return {pt * std::cos(phi), pt * std::sin(phi), pz, energy};
    }

    void validateParticleCompatibility(const ParticleGunParameters& parameters, double mass, double charge) {
      if (parameters.geometry.target && charge != 0.) {
        throw cms::Exception("DisplacedTwoBodyGunProducer")
            << "Target constraints assume a straight trajectory and therefore require a neutral particle.";
      }
      const auto& magnitude = parameters.momentum.magnitude;
      if (magnitude.variable == MagnitudeVariable::kEnergy && magnitude.min <= mass) {
        throw cms::Exception("DisplacedTwoBodyGunProducer")
            << "Momentum.Magnitude.Min must be greater than the particle mass when Variable is 'energy'. Min="
            << magnitude.min << " GeV, mass=" << mass << " GeV.";
      }
    }

    // Samples the shared vertex, a reference direction for particle 1,
    // and a rotated companion direction for particle 2, retrying (up to
    // maxSamplingAttempts) the WHOLE configuration -- vertex, reference
    // direction, and opening angle/azimuth together -- until both
    // daughters' trajectories are simultaneously reachable. Returns
    // std::nullopt if no valid configuration was found.
    std::optional<std::pair<ResolvedParticle, ResolvedParticle>> resolveTwoBodyParticles(
        CLHEP::HepRandomEngine* engine, const ParticleGunParameters& parameters, double mass) {
      const auto& geometry = parameters.geometry;
      const auto& direction = parameters.momentum.direction;
      const auto& magnitude = parameters.momentum.magnitude;
      const auto& origin = geometry.origin;

      for (unsigned int attempt = 0; attempt < parameters.maxSamplingAttempts; ++attempt) {
        // Shared production vertex.
        const double sampledR = sampleRadius(engine, origin, geometry.radialDistribution);
        const double sampledSpatialPhi = CLHEP::RandFlat::shoot(engine, origin.phiMin, origin.phiMax);
        const Point sharedOriginPoint{
            sampledR * std::cos(sampledSpatialPhi), sampledR * std::sin(sampledSpatialPhi), origin.z};

        // Particle 1: reference direction, sampled freely within its own window.
        const double theta1 = CLHEP::RandFlat::shoot(engine, direction.thetaMin, direction.thetaMax);
        const double phi1 = CLHEP::RandFlat::shoot(engine, direction.phiMin, direction.phiMax);
        if (!isTrajectoryReachable(sharedOriginPoint, theta1, phi1, geometry)) {
          continue;
        }

        // Particle 2: direction DERIVED by rotating particle 1's direction
        // by a sampled opening angle -- see rotateByOpeningAngle()'s
        // docstring. Only reachability is checked here, NOT the
        // Momentum.Direction window (see file header note).
        const double alpha =
            CLHEP::RandFlat::shoot(engine, parameters.openingAngle.min, parameters.openingAngle.max);
        const UnitVector v1 = directionToUnitVector(theta1, phi1);
        const UnitVector v2 = rotateByOpeningAngle(engine, v1, alpha);
        double theta2, phi2;
        unitVectorToDirection(v2, theta2, phi2);
        if (theta2 < 0. || theta2 >= std::numbers::pi / 2.) {
          // Companion rotated into the backward hemisphere or beyond --
          // not reachable in this single-endcap geometry; retry the
          // whole configuration.
          continue;
        }
        if (!isTrajectoryReachable(sharedOriginPoint, theta2, phi2, geometry)) {
          continue;
        }

        // Both daughters' trajectories are valid -- resolve full kinematics.
        // EqualEnergy toggle (see MomentumParameters' docstring) only
        // affects this magnitude-sampling step; the opening angle itself
        // (already fixed above) is controlled independently either way.
        const double magnitude1 = CLHEP::RandFlat::shoot(engine, magnitude.min, magnitude.max);
        const double magnitude2 =
            parameters.momentum.equalEnergy ? magnitude1 : CLHEP::RandFlat::shoot(engine, magnitude.min, magnitude.max);

        const FourMomentum momentum1 = computeMomentum(magnitude, theta1, phi1, magnitude1, mass);
        const FourMomentum momentum2 = computeMomentum(magnitude, theta2, phi2, magnitude2, mass);

        const Point productionPoint1 = projectToZ(sharedOriginPoint, geometry.production.z, theta1, phi1);
        const Point productionPoint2 = projectToZ(sharedOriginPoint, geometry.production.z, theta2, phi2);

        const double time1 = resolveTimeOfFlight(sharedOriginPoint, productionPoint1, momentum1);
        const double time2 = resolveTimeOfFlight(sharedOriginPoint, productionPoint2, momentum2);

        ResolvedParticle particle1{
            parameters.partId, momentum1, {productionPoint1.x, productionPoint1.y, productionPoint1.z, time1}};
        ResolvedParticle particle2{
            parameters.partId, momentum2, {productionPoint2.x, productionPoint2.y, productionPoint2.z, time2}};

        return std::make_pair(particle1, particle2);
      }

      return std::nullopt;
    }

  }  // namespace

  class DisplacedTwoBodyGunProducer : public edm::global::EDProducer<> {
  public:
    explicit DisplacedTwoBodyGunProducer(const ParameterSet&);
    ~DisplacedTwoBodyGunProducer() override = default;

    static void fillDescriptions(ConfigurationDescriptions& descriptions);

  private:
    void produce(edm::StreamID, edm::Event& event, const edm::EventSetup& setup) const override;

    const ProducerParameters fParameters;
    const ESGetToken<HepPDT::ParticleDataTable, edm::DefaultRecord> fPDGTableToken;
  };

  DisplacedTwoBodyGunProducer::DisplacedTwoBodyGunProducer(const ParameterSet& pset)
      : fParameters(pset), fPDGTableToken(esConsumes<>()) {
    Service<RandomNumberGenerator> rng;
    if (!rng.isAvailable()) {
      throw cms::Exception("Configuration")
          << "The RandomNumberProducer module requires the RandomNumberGeneratorService\n"
             "which appears to be absent.  Please add that service to your configuration\n"
             "or remove the modules that require it.";
    }

    produces<HepMCProduct>("unsmeared");
    produces<GenEventInfoProduct>();
  }

  void DisplacedTwoBodyGunProducer::fillDescriptions(ConfigurationDescriptions& descriptions) {
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
    momentum.add<bool>("EqualEnergy", false);

    edm::ParameterSetDescription openingAngle;
    openingAngle.add<double>("Min");
    openingAngle.add<double>("Max");

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
    pgun.add<edm::ParameterSetDescription>("Momentum", momentum);
    pgun.add<edm::ParameterSetDescription>("OpeningAngle", openingAngle);
    pgun.add<edm::ParameterSetDescription>("Geometry", geometry);
    pgun.add<unsigned int>("MaxSamplingAttempts");

    desc.add<edm::ParameterSetDescription>("PGunParameters", pgun);

    desc.addUntracked<int>("Verbosity", 0);

    descriptions.add("DisplacedTwoBodyGunProducer", desc);
  }

  void DisplacedTwoBodyGunProducer::produce(edm::StreamID, edm::Event& event, const edm::EventSetup& setup) const {
    if (fParameters.verbosity > 0) {
      LogDebug("DisplacedTwoBodyGunProducer")
          << " DisplacedTwoBodyGunProducer : Begin New Event Generation" << std::endl;
    }

    const auto& particleGun = fParameters.particleGun;
    edm::Service<edm::RandomNumberGenerator> rng;
    CLHEP::HepRandomEngine* randomEngine = &rng->getEngine(event.streamID());

    auto const& pdgTable = setup.getData(fPDGTableToken);
    const HepPDT::ParticleData* pData = pdgTable.particle(HepPDT::ParticleID(std::abs(particleGun.partId)));
    if (!pData) {
      throw cms::Exception("DisplacedTwoBodyGunProducer")
          << "Particle ID " << particleGun.partId << " not found in PDG table";
    }

    const double mass = pData->mass().value();
    validateParticleCompatibility(particleGun, mass, pData->charge());

    const auto resolved = resolveTwoBodyParticles(randomEngine, particleGun, mass);
    if (!resolved) {
      throw cms::Exception("DisplacedTwoBodyGunProducer")
          << "Failed to find a valid two-body configuration (shared vertex + both daughters' trajectories "
             "reachable) after MaxSamplingAttempts="
          << particleGun.maxSamplingAttempts << ".";
    }
    const auto& [particle1, particle2] = *resolved;

    HepMC::GenEvent* genEvent = new HepMC::GenEvent();
    genEvent->set_event_number(event.id().event());
    genEvent->set_signal_process_id(20);

    appendParticleToGenEvent(*genEvent, particle1, 1, fParameters.verbosity > 0);
    appendParticleToGenEvent(*genEvent, particle2, 2, fParameters.verbosity > 0);

    if (fParameters.verbosity > 0) {
      genEvent->print();
    }

    auto hepMcProduct = std::make_unique<HepMCProduct>();
    hepMcProduct->addHepMCData(genEvent);
    event.put(std::move(hepMcProduct), "unsmeared");

    auto genEventInfo = std::make_unique<GenEventInfoProduct>(genEvent);
    event.put(std::move(genEventInfo));

    if (fParameters.verbosity > 0) {
      LogDebug("DisplacedTwoBodyGunProducer")
          << " DisplacedTwoBodyGunProducer : Event Generation Done. " << std::endl;
    }
  }

}  // namespace edm

#include "FWCore/Framework/interface/MakerMacros.h"
using edm::DisplacedTwoBodyGunProducer;
DEFINE_FWK_MODULE(DisplacedTwoBodyGunProducer);
