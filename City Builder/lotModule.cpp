#pragma once
#include "lotModule.h"

lotModule::lotModule(int sizeX, int sizeY, int setAirPollutionEmit, int setLandValueEmit)
{
	x = sizeX;
	y = sizeY;
	airPollutionEmit = setAirPollutionEmit;
	landValueEmit = setLandValueEmit;
}