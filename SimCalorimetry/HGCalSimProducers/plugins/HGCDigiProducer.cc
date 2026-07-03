#include "FWCore/Framework/interface/MakerMacros.h"
#include "SimGeneral/MixingModule/interface/DigiAccumulatorMixModFactory.h"
#include "SimCalorimetry/HGCalSimProducers/plugins/HGCDigiProducer.h"
#include "FWCore/AbstractServices/interface/RandomNumberGenerator.h"
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/ServiceRegistry/interface/Service.h"
#include "FWCore/Utilities/interface/StreamID.h"

#include <cstdint>
#include <string>

namespace {
  uint32_t deterministicHGCalDigiSeed(edm::EventID const& eventId, std::string const& digiCollection) {
    uint64_t seed = 0x9e3779b97f4a7c15ULL;
    auto mix = [&seed](uint64_t value) {
      value += 0x9e3779b97f4a7c15ULL;
      value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
      value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
      seed ^= value ^ (value >> 31);
      seed *= 0x9e3779b97f4a7c15ULL;
    };

    mix(eventId.run());
    mix(eventId.luminosityBlock());
    mix(eventId.event());
    for (unsigned char c : digiCollection) {
      mix(c);
    }

    return static_cast<uint32_t>((seed % 900000000ULL) + 1ULL);
  }
}  // namespace

//
HGCDigiProducer::HGCDigiProducer(edm::ParameterSet const& pset,
                                 edm::ProducesCollector producesCollector,
                                 edm::ConsumesCollector& iC)
    : HGCDigiProducer(pset, iC) {
  premixStage1_ = pset.getParameter<bool>("premixStage1");
  if (premixStage1_) {
    producesCollector.produces<PHGCSimAccumulator>(theDigitizer_.digiCollection());
  } else {
    producesCollector.produces<HGCalDigiCollection>(theDigitizer_.digiCollection());
  }
}

HGCDigiProducer::HGCDigiProducer(edm::ParameterSet const& pset, edm::ConsumesCollector& iC)
    : DigiAccumulatorMixMod(), theDigitizer_(pset, iC) {}

//
void HGCDigiProducer::initializeEvent(edm::Event const& event, edm::EventSetup const& es) {
  edm::Service<edm::RandomNumberGenerator> rng;
  randomEngine_ = &rng->getEngine(event.streamID());
  randomEngine_->setSeed(deterministicHGCalDigiSeed(event.id(), theDigitizer_.digiCollection()), 0);
  theDigitizer_.initializeEvent(event, es);
}

//
void HGCDigiProducer::finalizeEvent(edm::Event& event, edm::EventSetup const& es) {
  theDigitizer_.finalizeEvent(event, es, randomEngine_);
  randomEngine_ = nullptr;  // to precent access outside event
}

//
void HGCDigiProducer::accumulate(edm::Event const& event, edm::EventSetup const& es) {
  if (premixStage1_) {
    theDigitizer_.accumulate_forPreMix(event, es, randomEngine_);
  }

  else {
    theDigitizer_.accumulate(event, es, randomEngine_);
  }
}

void HGCDigiProducer::accumulate(PileUpEventPrincipal const& event,
                                 edm::EventSetup const& es,
                                 edm::StreamID const& streamID) {
  if (premixStage1_) {
    theDigitizer_.accumulate_forPreMix(event, es, randomEngine_);
  } else {
    theDigitizer_.accumulate(event, es, randomEngine_);
  }
}

DEFINE_DIGI_ACCUMULATOR(HGCDigiProducer);
