#include "CityParameters.h"

#include "CrashLogger.h"

#include <algorithm>
#include <stdexcept>

CityParameterRegistry::CityParameterRegistry()
    : residentsLowWealthId_(-1),
      residentsMediumWealthId_(-1),
      residentsHighWealthId_(-1),
      jobsDirtyIndustryId_(-1),
      jobsLowWealthId_(-1),
      satisfactionLowWealthCommuteId_(-1),
      satisfactionDirtyIndustryStaffingId_(-1) {
    residentsLowWealthId_ = addParameter("residents.low_wealth", CityParameterKind::Driver);
    residentsMediumWealthId_ = addParameter("residents.medium_wealth", CityParameterKind::Driver);
    residentsHighWealthId_ = addParameter("residents.high_wealth", CityParameterKind::Driver);
    residentPopulationParameterIds_.push_back(residentsLowWealthId_);
    residentPopulationParameterIds_.push_back(residentsMediumWealthId_);
    residentPopulationParameterIds_.push_back(residentsHighWealthId_);
    jobsDirtyIndustryId_ = addParameter("jobs.dirty_industry", CityParameterKind::Driver);
    jobsLowWealthId_ = addParameter("jobs.low_wealth", CityParameterKind::Driver);
    satisfactionLowWealthCommuteId_ = addParameter("satisfaction.low_wealth_commute", CityParameterKind::Satisfaction);
    satisfactionDirtyIndustryStaffingId_ = addParameter("satisfaction.dirty_industry_staffing", CityParameterKind::Satisfaction);

    addImpact("jobs.dirty_industry", "jobs.low_wealth", 1.0f);
}

int CityParameterRegistry::parameterId(const std::string& id) const {
    const std::unordered_map<std::string, int>::const_iterator iterator = indexById_.find(id);
    return iterator == indexById_.end() ? -1 : iterator->second;
}

bool CityParameterRegistry::hasParameter(const std::string& id) const {
    return parameterId(id) >= 0;
}

std::size_t CityParameterRegistry::count() const {
    return definitions_.size();
}

const CityParameterDefinition& CityParameterRegistry::definition(int parameterIdValue) const {
    if (parameterIdValue < 0 || parameterIdValue >= static_cast<int>(definitions_.size())) {
        LogError("CityParameterRegistry::definition", "City parameter id is out of range.");
        throw std::out_of_range("City parameter id is out of range.");
    }

    return definitions_[parameterIdValue];
}

int CityParameterRegistry::residentsLowWealthId() const {
    return residentsLowWealthId_;
}

int CityParameterRegistry::residentsMediumWealthId() const {
    return residentsMediumWealthId_;
}

int CityParameterRegistry::residentsHighWealthId() const {
    return residentsHighWealthId_;
}

const std::vector<int>& CityParameterRegistry::residentPopulationParameterIds() const {
    return residentPopulationParameterIds_;
}

int CityParameterRegistry::jobsDirtyIndustryId() const {
    return jobsDirtyIndustryId_;
}

int CityParameterRegistry::jobsLowWealthId() const {
    return jobsLowWealthId_;
}

int CityParameterRegistry::satisfactionLowWealthCommuteId() const {
    return satisfactionLowWealthCommuteId_;
}

int CityParameterRegistry::satisfactionDirtyIndustryStaffingId() const {
    return satisfactionDirtyIndustryStaffingId_;
}

int CityParameterRegistry::addParameter(const std::string& id, CityParameterKind kind) {
    if (id.empty()) {
        LogError("CityParameterRegistry::addParameter", "City parameter id cannot be empty.");
        throw std::runtime_error("City parameter id cannot be empty.");
    }

    if (indexById_.find(id) != indexById_.end()) {
        LogError("CityParameterRegistry::addParameter", "Duplicate city parameter id: " + id);
        throw std::runtime_error("Duplicate city parameter id: " + id);
    }

    CityParameterDefinition definition;
    definition.id = id;
    definition.kind = kind;
    const int idValue = static_cast<int>(definitions_.size());
    definitions_.push_back(definition);
    indexById_[id] = idValue;
    return idValue;
}

void CityParameterRegistry::addImpact(const std::string& sourceId, const std::string& targetId, float multiplier) {
    const int sourceParameterId = parameterId(sourceId);
    const int targetParameterId = parameterId(targetId);
    if (sourceParameterId < 0 || targetParameterId < 0) {
        LogError("CityParameterRegistry::addImpact", "City parameter impact references an unknown parameter.");
        throw std::runtime_error("City parameter impact references an unknown parameter.");
    }

    CityParameterImpact impact;
    impact.targetParameterId = targetParameterId;
    impact.multiplier = multiplier;
    definitions_[sourceParameterId].impacts.push_back(impact);
}

int CalculatePopulationFromCityParameters(const std::vector<float>& cityParameters, const CityParameterRegistry& registry) {
    int population = 0;
    const std::vector<int>& residentParameterIds = registry.residentPopulationParameterIds();
    std::size_t parameterIndex = 0;
    for (; parameterIndex < residentParameterIds.size(); ++parameterIndex) {
        const int residentParameterId = residentParameterIds[parameterIndex];
        if (residentParameterId < 0 || residentParameterId >= static_cast<int>(cityParameters.size())) {
            continue;
        }

        population += std::max(0, static_cast<int>(cityParameters[residentParameterId] + 0.5f));
    }

    return population;
}
