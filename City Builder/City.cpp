#include "City.h"

#include <random>
#include <sstream>
#include <utility>

namespace {
std::uint32_t SeedForCity(int regionX, int regionY) {
    const std::uint32_t x = static_cast<std::uint32_t>(regionX + 4096);
    const std::uint32_t y = static_cast<std::uint32_t>(regionY + 4096);
    return 2166136261u ^ (x * 16777619u) ^ (y * 374761393u);
}
}

City::City()
    : regionX_(0),
      regionY_(0),
      previewRevision_(0) {
}

City::City(const std::string& name, int regionX, int regionY, int width, int height)
    : name_(name),
      regionX_(regionX),
      regionY_(regionY),
      previewRevision_(0) {
    saveState_.width = width;
    saveState_.height = height;
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
    return saveState_.width;
}

int City::height() const {
    return saveState_.height;
}

const CitySaveState& City::saveState() const {
    return saveState_;
}

const std::vector<std::uint8_t>& City::previewPixels() const {
    return previewPixels_;
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

bool City::hasPreviewPixels() const {
    return previewPixels_.size() == static_cast<std::size_t>(kPreviewWidth) * static_cast<std::size_t>(kPreviewHeight) * 4u;
}

void City::setSaveState(const CitySaveState& saveState) {
    saveState_ = saveState;
    clearPreviewPixels();
}

void City::generateDefaultState(std::uint32_t seed) {
    saveState_ = CitySaveState();
    saveState_.width = kDefaultWidth;
    saveState_.height = kDefaultHeight;
    saveState_.nextLotId = 1;

    std::mt19937 randomEngine(seed);
    std::uniform_int_distribution<int> baseDistribution(0, 327670);
    const std::size_t totalTileCount = static_cast<std::size_t>(saveState_.width) * static_cast<std::size_t>(saveState_.height);
    saveState_.tiles.assign(totalTileCount, Tile());

    std::size_t tileIndex = 0;
    for (; tileIndex < saveState_.tiles.size(); ++tileIndex) {
        saveState_.tiles[tileIndex].landValue = baseDistribution(randomEngine);
        saveState_.tiles[tileIndex].airPollution = baseDistribution(randomEngine);
        saveState_.tiles[tileIndex].isVacant = true;
        saveState_.tiles[tileIndex].zoningType = 0;
    }

    saveState_.lots.clear();
    saveState_.previewLots.clear();
    saveState_.cityParameters.clear();
    saveState_.transport = TransportNetworkSaveState();
    clearPreviewPixels();
}

void City::setPreviewPixels(const std::vector<std::uint8_t>& previewPixels) {
    previewPixels_ = previewPixels;
    ++previewRevision_;
}

void City::clearPreviewPixels() {
    previewPixels_.clear();
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
            city->generateDefaultState(SeedForCity(regionX, regionY));
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
        const std::vector<float>& cityParameters = cities_[cityIndex]->saveState().cityParameters;
        if (regionParameters_.size() < cityParameters.size()) {
            regionParameters_.resize(cityParameters.size(), 0.0f);
        }

        std::size_t parameterIndex = 0;
        for (; parameterIndex < cityParameters.size(); ++parameterIndex) {
            regionParameters_[parameterIndex] += cityParameters[parameterIndex];
        }
    }
}
