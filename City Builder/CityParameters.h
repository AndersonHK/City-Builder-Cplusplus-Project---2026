#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

enum class CityParameterKind {
    Driver,
    Satisfaction
};

struct CityParameterImpact {
    int targetParameterId;
    float multiplier;

    CityParameterImpact()
        : targetParameterId(-1),
          multiplier(0.0f) {
    }
};

struct CityParameterDefinition {
    std::string id;
    CityParameterKind kind;
    std::vector<CityParameterImpact> impacts;

    CityParameterDefinition()
        : kind(CityParameterKind::Driver) {
    }
};

struct CityParameterContribution {
    int parameterId;
    float amount;

    CityParameterContribution()
        : parameterId(-1),
          amount(0.0f) {
    }
};

class CityParameterRegistry {
public:
    CityParameterRegistry();

    int parameterId(const std::string& id) const;
    bool hasParameter(const std::string& id) const;
    std::size_t count() const;
    const CityParameterDefinition& definition(int parameterId) const;

    int residentsLowWealthId() const;
    int residentsMediumWealthId() const;
    int residentsHighWealthId() const;
    const std::vector<int>& residentPopulationParameterIds() const;
    int jobsDirtyIndustryId() const;
    int jobsLowWealthId() const;
    int satisfactionLowWealthCommuteId() const;
    int satisfactionDirtyIndustryStaffingId() const;

private:
    int addParameter(const std::string& id, CityParameterKind kind);
    void addImpact(const std::string& sourceId, const std::string& targetId, float multiplier);

    std::vector<CityParameterDefinition> definitions_;
    std::unordered_map<std::string, int> indexById_;
    std::vector<int> residentPopulationParameterIds_;
    int residentsLowWealthId_;
    int residentsMediumWealthId_;
    int residentsHighWealthId_;
    int jobsDirtyIndustryId_;
    int jobsLowWealthId_;
    int satisfactionLowWealthCommuteId_;
    int satisfactionDirtyIndustryStaffingId_;
};

int CalculatePopulationFromCityParameters(const std::vector<float>& cityParameters, const CityParameterRegistry& registry);
