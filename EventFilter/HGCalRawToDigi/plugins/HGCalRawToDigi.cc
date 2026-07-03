#include <memory>
#include <iostream>

#include "FWCore/Framework/interface/Frameworkfwd.h"
#include "FWCore/Framework/interface/stream/EDProducer.h"
#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/ESWatcher.h"
#include "FWCore/ParameterSet/interface/ParameterSet.h"
#include "FWCore/Utilities/interface/StreamID.h"

#include "DataFormats/FEDRawData/interface/RawDataBuffer.h"
#include "DataFormats/HGCalDigi/interface/HGCalElectronicsId.h"
#include "DataFormats/HGCalDigi/interface/HGCalDigiHost.h"
#include "DataFormats/HGCalDigi/interface/HGCalECONDPacketInfoHost.h"
#include "DataFormats/HGCalDigi/interface/HGCalFEDPacketInfoHost.h"
#include "DataFormats/HGCalDigi/interface/HGCalRawDataDefinitions.h"

#include "CondFormats/DataRecord/interface/HGCalElectronicsMappingRcd.h"
#include "CondFormats/HGCalObjects/interface/HGCalMappingModuleIndexer.h"
//#include "CondFormats/HGCalObjects/interface/HGCalMappingCellIndexer.h"
#include "CondFormats/DataRecord/interface/HGCalModuleConfigurationRcd.h"
#include "CondFormats/HGCalObjects/interface/HGCalConfiguration.h"

#include "oneapi/tbb/task_arena.h"
#include "oneapi/tbb.h"

#include "EventFilter/HGCalRawToDigi/interface/HGCalUnpacker.h"
class HGCalRawToDigi : public edm::stream::EDProducer<edm::stream::WatchRuns> {
public:
  explicit HGCalRawToDigi(const edm::ParameterSet&);
  uint16_t callUnpacker(unsigned fedId,
                        const RawFragmentWrapper& fed_data,
                        const HGCalMappingModuleIndexer& moduleIndexer,
                        const HGCalConfiguration& config,
                        hgcaldigi::HGCalDigiHost& digis,
                        hgcaldigi::HGCalFEDPacketInfoHost& fedPacketInfo,
                        hgcaldigi::HGCalECONDPacketInfoHost& econdPacketInfo);
  static void fillDescriptions(edm::ConfigurationDescriptions&);

private:
  void produce(edm::Event&, const edm::EventSetup&) override;
  void beginRun(edm::Run const&, edm::EventSetup const&) override;

  // input tokens
  const edm::EDGetTokenT<RawDataBuffer> fedRawToken_;

  // output tokens
  const edm::EDPutTokenT<hgcaldigi::HGCalDigiHost> digisToken_;
  const edm::EDPutTokenT<hgcaldigi::HGCalECONDPacketInfoHost> econdPacketInfoToken_;
  const edm::EDPutTokenT<hgcaldigi::HGCalFEDPacketInfoHost> fedPacketInfoToken_;

  // TODO @hqucms
  // what else do we want to output?

  // config tokens
  // COMMENT @pfs : Cell indexer is for the moment commented as it may be used in the future for Si calib and SiPM-on-tile cells
  //edm::ESGetToken<HGCalMappingCellIndexer, HGCalElectronicsMappingRcd> cellIndexToken_;
  edm::ESGetToken<HGCalMappingModuleIndexer, HGCalElectronicsMappingRcd> moduleIndexToken_;
  edm::ESGetToken<HGCalConfiguration, HGCalModuleConfigurationRcd> configToken_;

  // TODO @hqucms
  // how to implement this enabled eRx pattern? Can this be taken from the logical mapping?
  // HGCalCondSerializableModuleInfo::ERxBitPatternMap erxEnableBits_;
  // std::map<uint16_t, uint16_t> fed2slink_;

  HGCalUnpacker unpacker_;

  const bool doSerial_;
  bool headersOnly_;
};

HGCalRawToDigi::HGCalRawToDigi(const edm::ParameterSet& iConfig)
    : fedRawToken_(consumes<RawDataBuffer>(iConfig.getParameter<edm::InputTag>("src"))),
      digisToken_(produces<hgcaldigi::HGCalDigiHost>()),
      econdPacketInfoToken_(produces<hgcaldigi::HGCalECONDPacketInfoHost>()),
      fedPacketInfoToken_(produces<hgcaldigi::HGCalFEDPacketInfoHost>()),
      //cellIndexToken_(esConsumes()),
      moduleIndexToken_(esConsumes()),
      configToken_(esConsumes()),
      doSerial_(iConfig.getParameter<bool>("doSerial")),
      headersOnly_(iConfig.getParameter<bool>("headersOnly")) {
  std::cout << "[HGCalRawToDigi DEBUG] constructor doSerial=" << doSerial_
            << " headersOnly=" << headersOnly_ << std::endl;
}

void HGCalRawToDigi::beginRun(edm::Run const& iRun, edm::EventSetup const& iSetup) {
  // TODO @hqucms
  // init unpacker with proper configs
  std::cout << "[HGCalRawToDigi DEBUG] beginRun" << std::endl;
}

void HGCalRawToDigi::produce(edm::Event& iEvent, const edm::EventSetup& iSetup) {
  std::cout << "[HGCalRawToDigi DEBUG] produce start event=" << iEvent.id() << std::endl;
  // retrieve logical mapping
  const auto& moduleIndexer = iSetup.getData(moduleIndexToken_);
  //const auto& cellIndexer = iSetup.getData(cellIndexToken_);
  const auto& config = iSetup.getData(configToken_);

  hgcaldigi::HGCalDigiHost digis(cms::alpakatools::host(), moduleIndexer.maxDataSize());
  hgcaldigi::HGCalECONDPacketInfoHost econdPacketInfo(cms::alpakatools::host(), moduleIndexer.maxModulesCount());
  hgcaldigi::HGCalFEDPacketInfoHost fedPacketInfo(cms::alpakatools::host(), moduleIndexer.fedCount());

  std::cout << "[HGCalRawToDigi DEBUG] mapping maxDataSize=" << moduleIndexer.maxDataSize()
            << " maxModules=" << moduleIndexer.maxModulesCount()
            << " fedCount=" << moduleIndexer.fedCount() << std::endl;

  // retrieve the FED raw data
  const auto& fedBuffer = iEvent.get(fedRawToken_);

  for (int32_t i = 0; i < digis.view().metadata().size(); i++) {
    digis.view()[i].flags() = hgcal::DIGI_FLAG::NotAvailable;
  }

  //serial unpacking calls
  if (doSerial_) {
    std::cout << "[HGCalRawToDigi DEBUG] serial branch" << std::endl;
    for (unsigned fedId = 0; fedId < moduleIndexer.fedCount(); ++fedId) {
      const auto& frs = moduleIndexer.fedReadoutSequences()[fedId];
      if (frs.readoutTypes_.empty()) {
        std::cout << "[HGCalRawToDigi DEBUG] serial skip empty readout fedId=" << fedId << std::endl;
        continue;
      }

      const auto& fed_data = fedBuffer.fragmentData(fedId);
      fedPacketInfo.view()[fedId].FEDPayload() = fed_data.size();
      if (fed_data.size() == 0) {
        std::cout << "[HGCalRawToDigi DEBUG] serial skip zero payload fedId=" << fedId << std::endl;
        continue;
      }
      std::cout << "[HGCalRawToDigi DEBUG] serial unpack fedId=" << fedId
                << " payload=" << fed_data.size() << std::endl;
      fedPacketInfo.view()[fedId].FEDUnpackingFlag() =
          callUnpacker(fedId, fed_data, moduleIndexer, config, digis, fedPacketInfo, econdPacketInfo);
    }
  }
  //parallel unpacking calls
  else {
    std::cout << "[HGCalRawToDigi DEBUG] would use tbb, running sequential debug path" << std::endl;
    for (unsigned fedId = 0; fedId < moduleIndexer.fedCount(); ++fedId) {
      const auto& frs = moduleIndexer.fedReadoutSequences()[fedId];
      if (frs.readoutTypes_.empty()) {
        std::cout << "[HGCalRawToDigi DEBUG] tbb-debug skip empty readout fedId=" << fedId << std::endl;
        continue;
      }
      const auto& fed_data = fedBuffer.fragmentData(fedId);
      fedPacketInfo.view()[fedId].FEDPayload() = fed_data.size();
      if (fed_data.size() == 0) {
        std::cout << "[HGCalRawToDigi DEBUG] tbb-debug skip zero payload fedId=" << fedId << std::endl;
        continue;
      }
      std::cout << "[HGCalRawToDigi DEBUG] tbb-debug unpack fedId=" << fedId
                << " payload=" << fed_data.size() << std::endl;
      fedPacketInfo.view()[fedId].FEDUnpackingFlag() =
          callUnpacker(fedId, fed_data, moduleIndexer, config, digis, fedPacketInfo, econdPacketInfo);
    }
  }

  std::cout << "[HGCalRawToDigi DEBUG] produce end event=" << iEvent.id() << std::endl;

  // put information to the event
  iEvent.emplace(digisToken_, std::move(digis));
  iEvent.emplace(econdPacketInfoToken_, std::move(econdPacketInfo));
  iEvent.emplace(fedPacketInfoToken_, std::move(fedPacketInfo));
}

//
uint16_t HGCalRawToDigi::callUnpacker(unsigned fedId,
                                      const RawFragmentWrapper& fed_data,
                                      const HGCalMappingModuleIndexer& moduleIndexer,
                                      const HGCalConfiguration& config,
                                      hgcaldigi::HGCalDigiHost& digis,
                                      hgcaldigi::HGCalFEDPacketInfoHost& fedPacketInfo,
                                      hgcaldigi::HGCalECONDPacketInfoHost& econdPacketInfo) {
  std::cout << "[HGCalRawToDigi DEBUG] callUnpacker fedId=" << fedId
            << " payload=" << fed_data.size() << std::endl;

  uint16_t status = unpacker_.parseFEDData(
      fedId, fed_data, moduleIndexer, config, digis, fedPacketInfo, econdPacketInfo, headersOnly_);
  return status;
}

// fill descriptions
void HGCalRawToDigi::fillDescriptions(edm::ConfigurationDescriptions& descriptions) {
  edm::ParameterSetDescription desc;
  desc.add<edm::InputTag>("src", edm::InputTag("rawDataCollector"));
  desc.add<bool>("doSerial", true)->setComment("do not attempt to paralleize unpacking of different FEDs");
  desc.add<bool>("headersOnly", false)->setComment("unpack only headers");
  descriptions.add("hgcalDigis", desc);
}

// define this as a plug-in
DEFINE_FWK_MODULE(HGCalRawToDigi);
