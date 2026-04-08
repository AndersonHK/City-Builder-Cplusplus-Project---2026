#include "square.h"

void square::Update(long int NlandValue, long int NairPolution, bool NisVacant, short int NzoningType)
{
	landValue.store(NlandValue, memory_order_relaxed);
	airPolution.store(NairPolution, memory_order_relaxed);
	isVacant.store(NisVacant, memory_order_relaxed);
	zoningType.store(NzoningType, memory_order_relaxed);
}
void square::localUpdate()
{
	if (airPolution < 1) {
		airPolution = 1;
	}
	landValue.store(landValue - airPolution -1);
	if (landValue < 1) {
		landValue = 0;
	}
	airPolution.store(airPolution - 1);
}