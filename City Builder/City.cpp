#include "City.h"

#include <algorithm>
#include <sstream>
#include <utility>

#include "CityParameters.h"

City::City()
    : regionX_(0),
      regionY_(0),
      width_(kDefaultWidth),
      height_(kDefaultHeight),
      cameraX_(centeredCameraCoordinate(kDefaultWidth, kDefaultVisibleTiles)),
      cameraY_(centeredCameraCoordinate(kDefaultHeight, kDefaultVisibleTiles)),
      visibleTiles_(kDefaultVisibleTiles),
      saveStateDirty_(false),
      previewRevision_(0),
      previewBuildInFlight_(false) {
}

City::City(const std::string& name, int regionX, int regionY, int width, int height)
    : name_(name),
      regionX_(regionX),
      regionY_(regionY),
      width_(width),
      height_(height),
      cameraX_(centeredCameraCoordinate(width, kDefaultVisibleTiles)),
      cameraY_(centeredCameraCoordinate(height, kDefaultVisibleTiles)),
      visibleTiles_(kDefaultVisibleTiles),
      saveStateDirty_(false),
      previewRevision_(0),
      previewBuildInFlight_(false) {
}

const std::string& City::name() const {
    return name_;
}

int City::regionX() const {
    return regionX_;
}

int City::regionY() const {
    return regionY_;
}

int City::width() const {
    return width_;
}

int City::height() const {
    return height_;
}

int City::cameraX() const {
    return cameraX_;
}

int City::cameraY() const {
    return cameraY_;
}

int City::visibleTiles() const {
    return visibleTiles_;
}

int City::population() const {
    CityParameterRegistry registry;
    return CalculatePopulationFromCityParameters(cityParameters_, registry);
}

const std::vector<float>& City::cityParameters() const {
    return cityParameters_;
}

bool City::hasSaveState() const {
    return saveState_.get() != 0;
}

bool City::isSaveStateDirty() const {
    return saveStateDirty_;
}

const CitySaveState& City::saveState() const {
    return *saveState_;
}

int City::previewWidth() const {
    return kPreviewWidth;
}

int City::previewHeight() const {
    return kPreviewHeight;
}

std::uint64_t City::previewRevision() const {
    return previewRevision_;
}

bool City::hasPreviewBuildInFlight() const {
    return previewBuildInFlight_;
}

bool City::isPreviewBuildReady() const {
    if (!previewBuildInFlight_) {
        return false;
    }

    return previewBuildFuture_.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

void City::setSaveState(const CitySaveState& saveState, bool isDirty) {
    saveState_.reset(new CitySaveState(saveState));
    saveStateDirty_ = isDirty;
    applySaveStateMetadata(saveState);
    bumpPreviewRevision();
}

void City::setMetadataFromSaveState(const CitySaveState& saveState) {
    applySaveStateMetadata(saveState);
}

void City::setCamera(int cameraX, int cameraY, int visibleTiles) {
    cameraX_ = cameraX;
    cameraY_ = cameraY;
    visibleTiles_ = std::max(1, visibleTiles);
    if (saveState_) {
        saveState_->cameraX = cameraX_;
        saveState_->cameraY = cameraY_;
        saveState_->visibleTiles = visibleTiles_;
        saveStateDirty_ = true;
    }
}

void City::markSaveStateClean() {
    saveStateDirty_ = false;
}

void City::unloadCleanSaveState() {
    if (!saveStateDirty_) {
        saveState_.reset();
    }
}

void City::unloadSaveState() {
    saveState_.reset();
    saveStateDirty_ = false;
}

void City::clearPreview() {
    bumpPreviewRevision();
}

void City::startPreviewBuild(std::future<CitySaveState>&& previewBuildFuture) {
    previewBuildFuture_ = std::move(previewBuildFuture);
    previewBuildInFlight_ = true;
}

CitySaveState City::takePreviewBuildState() {
    CitySaveState saveState = previewBuildFuture_.get();
    previewBuildInFlight_ = false;
    return saveState;
}

std::uint32_t City::seedForRegionCoordinate(int regionX, int regionY) {
    const std::uint32_t x = static_cast<std::uint32_t>(regionX + 4096);
    const std::uint32_t y = static_cast<std::uint32_t>(regionY + 4096);
    return 2166136261u ^ (x * 16777619u) ^ (y * 374761393u);
}

CitySaveState City::createDefaultSaveState(std::uint32_t seed, int width, int height) {
    (void)seed;

    CitySaveState saveState;
    saveState.width = width;
    saveState.height = height;
    saveState.nextLotId = 1;
    saveState.visibleTiles = kDefaultVisibleTiles;
    saveState.simulationTick = 0;
    saveState.cameraX = centeredCameraCoordinate(width, saveState.visibleTiles);
    saveState.cameraY = centeredCameraCoordinate(height, saveState.visibleTiles);

    const std::size_t totalTileCount = static_cast<std::size_t>(saveState.width) * static_cast<std::size_t>(saveState.height);
    saveState.tiles.assign(totalTileCount, Tile());

    std::size_t tileIndex = 0;
    for (; tileIndex < saveState.tiles.size(); ++tileIndex) {
        saveState.tiles[tileIndex].landValue = 0;
        saveState.tiles[tileIndex].airPollution = 0;
        saveState.tiles[tileIndex].isVacant = true;
        saveState.tiles[tileIndex].zoningType = 0;
    }

    saveState.lots.clear();
    saveState.zoningLots.clear();
    saveState.previewLots.clear();
    saveState.cityParameters.clear();
    saveState.transport = TransportNetworkSaveState();
    return saveState;
}

int City::centeredCameraCoordinate(int mapSize, int visibleTiles) {
    return (mapSize - visibleTiles) / 2;
}

void City::applySaveStateMetadata(const CitySaveState& saveState) {
    width_ = saveState.width;
    height_ = saveState.height;
    cameraX_ = saveState.cameraX;
    cameraY_ = saveState.cameraY;
    visibleTiles_ = std::max(1, saveState.visibleTiles);
    cityParameters_ = saveState.cityParameters;
}

void City::bumpPreviewRevision() {
    ++previewRevision_;
}

Region::Region()
    : revision_(0) {
}

void Region::clear() {
    cities_.clear();
    regionParameters_.clear();
    ++revision_;
}

void Region::createDefault() {
    clear();
    int regionY = 0;
    for (; regionY < 3; ++regionY) {
        int regionX = 0;
        for (; regionX < 3; ++regionX) {
            std::ostringstream nameBuilder;
            nameBuilder << "City " << regionX << "," << regionY;
            std::unique_ptr<City> city(new City(nameBuilder.str(), regionX, regionY, City::kDefaultWidth, City::kDefaultHeight));
            cities_.push_back(std::move(city));
        }
    }

    recalculateRegionParameters();
    ++revision_;
}

City* Region::cityAt(int regionX, int regionY) {
    std::size_t cityIndex = 0;
    for (; cityIndex < cities_.size(); ++cityIndex) {
        if (cities_[cityIndex]->regionX() == regionX && cities_[cityIndex]->regionY() == regionY) {
            return cities_[cityIndex].get();
        }
    }

    return 0;
}

const City* Region::cityAt(int regionX, int regionY) const {
    std::size_t cityIndex = 0;
    for (; cityIndex < cities_.size(); ++cityIndex) {
        if (cities_[cityIndex]->regionX() == regionX && cities_[cityIndex]->regionY() == regionY) {
            return cities_[cityIndex].get();
        }
    }

    return 0;
}

std::vector<std::unique_ptr<City> >& Region::cities() {
    return cities_;
}

const std::vector<std::unique_ptr<City> >& Region::cities() const {
    return cities_;
}

const std::vector<float>& Region::regionParameters() const {
    return regionParameters_;
}

int Region::population() const {
    CityParameterRegistry registry;
    return CalculatePopulationFromCityParameters(regionParameters_, registry);
}

std::uint64_t Region::revision() const {
    return revision_;
}

void Region::addCity(std::unique_ptr<City> city) {
    cities_.push_back(std::move(city));
    recalculateRegionParameters();
    ++revision_;
}

void Region::recalculateRegionParameters() {
    regionParameters_.clear();
    std::size_t cityIndex = 0;
    for (; cityIndex < cities_.size(); ++cityIndex) {
        const std::vector<float>& cityParameters = cities_[cityIndex]->cityParameters();
        if (regionParameters_.size() < cityParameters.size()) {
            regionParameters_.resize(cityParameters.size(), 0.0f);
        }

        std::size_t parameterIndex = 0;
        for (; parameterIndex < cityParameters.size(); ++parameterIndex) {
            regionParameters_[parameterIndex] += cityParameters[parameterIndex];
        }
    }
}
