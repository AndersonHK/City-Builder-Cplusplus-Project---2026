#pragma once

class LotModule {
public:
    LotModule();
    LotModule(int widthInTiles, int heightInTiles, int airPollutionOutput, int landValueOutput);

    int width;
    int height;
    int airPollutionEmit;
    int landValueEmit;
};
