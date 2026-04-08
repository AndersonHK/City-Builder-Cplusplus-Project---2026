#include "LotModule.h"

LotModule::LotModule()
    : width(1),
      height(1),
      airPollutionEmit(0),
      landValueEmit(0) {
}

LotModule::LotModule(int widthInTiles, int heightInTiles, int airPollutionOutput, int landValueOutput)
    : width(widthInTiles),
      height(heightInTiles),
      airPollutionEmit(airPollutionOutput),
      landValueEmit(landValueOutput) {
}
